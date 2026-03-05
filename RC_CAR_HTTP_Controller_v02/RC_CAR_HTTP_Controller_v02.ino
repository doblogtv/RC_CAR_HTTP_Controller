#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

#include "settings.h"
#include "ui_common.h"
#include "page_wifi.h"
#include "page_thr.h"
#include "page_str.h"
#include "page_enc.h"
#include "page_resp.h"

// =========================
// LCD 実体（1回だけ定義）
// =========================
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// =========================
// Pages
// =========================
Page page = PAGE_WIFI;

// =========================
// UI mode
// =========================
enum UiMode : uint8_t { UI_PAGE_SWITCH = 0, UI_PAGE_SCROLL = 1, UI_EDIT = 2 };
UiMode uiMode = UI_PAGE_SWITCH;

// スクロール位置（WIFI/RESP用）
int scrollLine = 0;

// UI用のenc基準（差分計算用）
static long uiEncBase = 0;
static uint32_t uiLastMs = 0;

// =========================
// Encoder SW state
// =========================
static uint8_t encSwPrev = HIGH;
static uint32_t encSwLastMs = 0;

// 長押し判定
static bool encSwDown = false;
static uint32_t encSwDownMs = 0;
static bool encSwLongFired = false;

// =========================
// Encoder ISR
// =========================
volatile long g_enc = 0;
volatile int  g_dir = 0;
volatile uint8_t g_prevAB = 0;
volatile uint32_t g_lastISR_us = 0;

static const int8_t ENC_TABLE[16] = {
  0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
  0, +1, -1,  0
};

void IRAM_ATTR encISR() {
  uint32_t now = micros();
  if (now - g_lastISR_us < ENC_DEBOUNCE_US) return;
  g_lastISR_us = now;

  uint8_t a = digitalRead(PIN_ENC_A) ? 1 : 0;
  uint8_t b = digitalRead(PIN_ENC_B) ? 1 : 0;
  uint8_t ab = (a << 1) | b;

  uint8_t idx = ((g_prevAB & 0x3) << 2) | (ab & 0x3);
  int8_t step = ENC_TABLE[idx];
  if (step != 0) {
    g_enc += step;
    g_dir = (step > 0) ? +1 : -1;
  }
  g_prevAB = ab;
}

// =========================
// WiFi/HTTP stats（page_wifiがextern参照）
// =========================
uint32_t seq = 0;
uint32_t okCount = 0;
uint32_t ngCount = 0;
int lastHttpCode = 0;
uint32_t lastRttMs = 0;
uint32_t lastOkMs = 0;

String lastResp = "";
uint32_t lastRespMs = 0;

uint32_t wifiLastTryMs = 0;
bool wifiBeginIssued = false;

// STR送信値の追跡（LCD表示用）
uint8_t lastSentStr = 128;
int16_t lastDeltaStr = 0;

// =========================
// Calib configs (Preferences)
// =========================
Preferences prefs;

CalibCfg cfgThr; // invert/min/max
CalibCfg cfgStr;

// 編集状態
EditCtx edit; // 現在編集中（THR/STR共通）
static inline bool isEditingThr() { return (uiMode == UI_EDIT && edit.target == EDIT_THR); }
static inline bool isEditingStr() { return (uiMode == UI_EDIT && edit.target == EDIT_STR); }

// THR curve (editable)
uint8_t thrDeadzoneU8 = THR_DEADZONE_U8;
uint8_t thrStartU8    = THR_START_U8;

// ---------- Preferences ----------
static inline void loadCalibFromNVS() {
  thrDeadzoneU8 = (uint8_t)prefs.getUChar(KEY_THR_DZ, THR_DEADZONE_U8);
  thrStartU8    = (uint8_t)prefs.getUChar(KEY_THR_ST, THR_START_U8);
  
  prefs.begin(PREF_NS, true);
  cfgThr.inv    = prefs.getBool(KEY_THR_INV, false);
  cfgThr.minRaw = (uint16_t)prefs.getUShort(KEY_THR_MIN, 0);
  cfgThr.maxRaw = (uint16_t)prefs.getUShort(KEY_THR_MAX, 4095);

  cfgStr.inv    = prefs.getBool(KEY_STR_INV, false);
  cfgStr.minRaw = (uint16_t)prefs.getUShort(KEY_STR_MIN, 0);
  cfgStr.maxRaw = (uint16_t)prefs.getUShort(KEY_STR_MAX, 4095);
  prefs.end();

  sanitizeCalib(cfgThr);
  sanitizeCalib(cfgStr);
}

static inline void saveCalibToNVS() {
  
  sanitizeCalib(cfgThr);
  sanitizeCalib(cfgStr);

  prefs.begin(PREF_NS, false);
  prefs.putBool(KEY_THR_INV, cfgThr.inv);
  prefs.putUShort(KEY_THR_MIN, cfgThr.minRaw);
  prefs.putUShort(KEY_THR_MAX, cfgThr.maxRaw);

  prefs.putBool(KEY_STR_INV, cfgStr.inv);
  prefs.putUShort(KEY_STR_MIN, cfgStr.minRaw);
  prefs.putUShort(KEY_STR_MAX, cfgStr.maxRaw);
  
  prefs.putUChar(KEY_THR_DZ, thrDeadzoneU8);
  prefs.putUChar(KEY_THR_ST, thrStartU8);
  
  prefs.end();
}

// ---------- WiFi non-blocking ----------
static inline void wifiTick() {
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    wifiBeginIssued = false;
    return;
  }
  if (!wifiBeginIssued || (now - wifiLastTryMs >= WIFI_RETRY_MS)) {
    wifiLastTryMs = now;
    wifiBeginIssued = true;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

// ---------- HTTP ----------
static inline bool sendControlHTTP(uint8_t thr_u8, uint8_t rev, uint8_t str_u8, uint8_t drv, uint32_t seq_) {
  if (WiFi.status() != WL_CONNECTED) {
    lastHttpCode = -1;
    lastResp = "";
    return false;
  }

  HTTPClient http;
  String url = String(CAR_API)
             + "?thr=" + String((int)thr_u8)
             + "&rev=" + String((int)rev)
             + "&str=" + String((int)str_u8)
             + "&drv=" + String((int)drv)
             + "&seq=" + String(seq_);

  http.setTimeout(HTTP_TIMEOUT_MS);

  uint32_t t0 = millis();
  http.begin(url);
  int code = http.GET();

  String body = "";
  if (code > 0) {
    body = http.getString();
    if ((int)body.length() > 256) body = body.substring(0, 256);
  }

  uint32_t t1 = millis();
  http.end();

  lastHttpCode = code;
  lastRttMs = (t1 - t0);
  lastResp = body;
  lastRespMs = millis();

  if (code == 200) {
    okCount++;
    lastOkMs = millis();
    return true;
  } else {
    ngCount++;
    return false;
  }
}

// =========================
// Edit helpers
// =========================
static inline void editEnter(EditTarget tgt) {
  uiMode = UI_EDIT;
  editReset(edit, tgt);
  uiEncBase = g_enc;
  scrollLine = 0;
  lcd.clear();
}

static inline void editExitToScroll() {
  uiMode = UI_PAGE_SCROLL;
  scrollLine = 0;
  uiEncBase = g_enc;
  lcd.clear();
}

static inline void editHandleRotate(long encNow) {
  long diff = encNow - uiEncBase;
  if (millis() - uiLastMs < UI_DEBOUNCE_MS) return;

  if (edit.step == EDIT_MENU || edit.step == EDIT_DIR) {
    if (diff >= PAGE_STEP_COUNTS) {
      uiEncBase += PAGE_STEP_COUNTS;
      edit.sel = (edit.sel + 1) % 2;
      uiLastMs = millis();
      lcd.clear();
    } else if (diff <= -PAGE_STEP_COUNTS) {
      uiEncBase -= PAGE_STEP_COUNTS;
      edit.sel = (edit.sel + 1) % 2;
      uiLastMs = millis();
      lcd.clear();
    }
  } else {
    // MIN/MAX はVRノブで決めるので回転は無視
    if (diff >= PAGE_STEP_COUNTS) uiEncBase += PAGE_STEP_COUNTS;
    else if (diff <= -PAGE_STEP_COUNTS) uiEncBase -= PAGE_STEP_COUNTS;
  }
}

static inline CalibCfg& editCfgRef() {
  return (edit.target == EDIT_THR) ? cfgThr : cfgStr;
}

static inline int editRawNow(int thr_raw, int str_raw) {
  return (edit.target == EDIT_THR) ? thr_raw : str_raw;
}

static inline void editShortPressConfirm(int thr_raw, int str_raw) {
  CalibCfg& cfg = editCfgRef();
  int rawNow = editRawNow(thr_raw, str_raw);
  rawNow = clampi(rawNow, 0, 4095);

  if (edit.step == EDIT_MENU) {
    // sel=0: Setting / sel=1: Back
    if (edit.sel == 0) {
      edit.step = EDIT_DIR;
      edit.sel = cfg.inv ? 1 : 0; // 0 normal / 1 invert
      lcd.clear();
    } else {
      // back
      editExitToScroll();
    }
    return;
  }

  if (edit.step == EDIT_DIR) {
    cfg.inv = (edit.sel == 1);
    edit.step = EDIT_MIN;
    lcd.clear();
    return;
  }

  if (edit.step == EDIT_MIN) {
    cfg.minRaw = (uint16_t)rawNow;
    // 最小が4095にならないように（操作不能回避）
    if (cfg.minRaw >= 4095) cfg.minRaw = 4094;
    sanitizeCalib(cfg);
    edit.step = EDIT_MAX;
    lcd.clear();
    return;
  }

  // EDIT_MAX
  cfg.maxRaw = (uint16_t)rawNow;
  sanitizeCalib(cfg);

  // max <= min の場合は最小限補正（1だけ広げる）
  if (cfg.maxRaw <= cfg.minRaw) {
    cfg.maxRaw = (uint16_t)min(4095, (int)cfg.minRaw + 1);
  }

  saveCalibToNVS();

  // メニューへ戻る
  edit.step = EDIT_MENU;
  edit.sel = 0;
  lcd.clear();
}

static inline void editLongPressAction() {
  // 長押し：メニューへ戻る（メニューなら編集終了）
  if (edit.step != EDIT_MENU) {
    edit.step = EDIT_MENU;
    edit.sel = 0;
    lcd.clear();
  } else {
    editExitToScroll();
  }
}

// =========================
// Setup/Loop
// =========================
void setup() {
  Serial.begin(115200);
  delay(150);

  analogReadResolution(12);
  pinMode(PIN_THR, INPUT);
  pinMode(PIN_STR, INPUT);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);

  pinMode(PIN_SW_G, INPUT_PULLUP);
  pinMode(PIN_SW_L, INPUT_PULLUP);
  pinMode(PIN_SW_X, INPUT_PULLUP);

  // init prev AB
  uint8_t a = digitalRead(PIN_ENC_A) ? 1 : 0;
  uint8_t b = digitalRead(PIN_ENC_B) ? 1 : 0;
  g_prevAB = (a << 1) | b;

  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  uiInitChars();

  // Load calib
  loadCalibFromNVS();

  // WiFi begin (non-blocking)
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiBeginIssued = true;
  wifiLastTryMs = millis();

  // encoder base
  delay(50);
  noInterrupts();
  uiEncBase = g_enc;
  interrupts();

  lcd.clear();
  lcdPrintFixed(0, 0, "RC HTTP CTRL v03   ");
  lcdPrintFixed(0, 1, "ROT:page  SW:sel   ");
  lcdPrintFixed(0, 2, "SW:toggle  HOLD:ED ");
  lcdPrintFixed(0, 3, "SEND=" + String(SEND_HZ_FAST) + " LCD=" + String(LCD_HZ));
  delay(900);
  lcd.clear();
}

void loop() {
  uint32_t now = millis();

  // --- Encoder snapshot（1回だけ）---
  long enc;
  int dir;
  noInterrupts();
  enc = g_enc;
  dir = g_dir;
  interrupts();

  // --- Read VR ---
  int thr_raw = analogRead(PIN_THR);
  int str_raw = analogRead(PIN_STR);

  // --- Switch meanings ---
  uint8_t rev = readRev_G();
  DriveMode drv = readDrive_LX();

  // --- SW handling (short/long) ---
  uint8_t encSw = digitalRead(PIN_ENC_SW);

  // press start
  if (!encSwDown && encSwPrev == HIGH && encSw == LOW && (now - encSwLastMs) > ENC_SW_DEBOUNCE_MS) {
    encSwLastMs = now;
    encSwDown = true;
    encSwDownMs = now;
    encSwLongFired = false;
  }

  // long press fire
  if (encSwDown && !encSwLongFired && encSw == LOW) {
    if (now - encSwDownMs >= ENC_SW_LONG_MS) {
      encSwLongFired = true;

      if (uiMode == UI_EDIT) {
        editLongPressAction();
      } else if (uiMode == UI_PAGE_SCROLL && (page == PAGE_THR || page == PAGE_STR)) {
        // enter edit from THR/STR selected
        editEnter((page == PAGE_THR) ? EDIT_THR : EDIT_STR);
      } else {
        // long press elsewhere: no-op
      }
    }
  }

  // release -> short press
  if (encSwDown && encSwPrev == LOW && encSw == HIGH) {
    encSwDown = false;

    if (!encSwLongFired) {
      // short press
      if (uiMode == UI_EDIT) {
        editShortPressConfirm(thr_raw, str_raw);
      } else {
        // toggle select/cancel
        if (uiMode == UI_PAGE_SWITCH) {
          uiMode = UI_PAGE_SCROLL;
          scrollLine = 0;
          uiEncBase = enc;
          lcd.clear();
        } else if (uiMode == UI_PAGE_SCROLL) {
          uiMode = UI_PAGE_SWITCH;
          scrollLine = 0;
          uiEncBase = enc;
          lcd.clear();
        }
      }
    }
  }
  encSwPrev = encSw;

  // --- Rotary: page switch / scroll / edit ---
  if (uiMode == UI_EDIT) {
    editHandleRotate(enc);
  } else {
    long diff = enc - uiEncBase;
    if (now - uiLastMs >= UI_DEBOUNCE_MS) {
      if (uiMode == UI_PAGE_SWITCH) {
        if (diff >= PAGE_STEP_COUNTS) {
          uiEncBase += PAGE_STEP_COUNTS;
          page = (Page)((page + 1) % PAGE_COUNT);
          scrollLine = 0;
          lcd.clear();
          uiLastMs = now;
        } else if (diff <= -PAGE_STEP_COUNTS) {
          uiEncBase -= PAGE_STEP_COUNTS;
          page = (Page)((page + PAGE_COUNT - 1) % PAGE_COUNT);
          scrollLine = 0;
          lcd.clear();
          uiLastMs = now;
        }
      } else {
        // UI_PAGE_SCROLL
        if (diff >= PAGE_STEP_COUNTS) {
          uiEncBase += PAGE_STEP_COUNTS;
          scrollLine++;
          uiLastMs = now;
        } else if (diff <= -PAGE_STEP_COUNTS) {
          uiEncBase -= PAGE_STEP_COUNTS;
          scrollLine--;
          uiLastMs = now;
        }

        int maxScroll = 0;
        if (page == PAGE_RESP) {
          maxScroll = respMaxScrollLine(lastResp);
        } else if (page == PAGE_WIFI) {
          maxScroll = wifiMaxScrollLine((uiMode == UI_PAGE_SCROLL));
        }
        scrollLine = clampi(scrollLine, 0, maxScroll);
      }
    }
  }

  // --- WiFi tick (non-blocking) ---
  wifiTick();

  // --- Apply calib mapping ---
  uint8_t thr_u = mapRawToU8_Calib(thr_raw, cfgThr);
  uint8_t str_u = mapRawToU8_Calib(str_raw, cfgStr);

  // thr: apply curve (deadzone + start offset)
  uint8_t thr_curved = applyThrCurve(thr_u);

  // ★送信値：編集中は thr=0（安全）
  uint8_t thr_send = (uiMode == UI_EDIT) ? 0 : thr_curved;
  uint8_t str_send = str_u;

  // --- Send (HTTP) ---
  static uint32_t lastSend = 0;
  const uint32_t sendIntervalMs = 1000 / SEND_HZ_FAST;
  if (now - lastSend >= sendIntervalMs) {
    lastSend = now;
    seq++;

    lastDeltaStr = (int)str_send - (int)lastSentStr;
    lastSentStr = str_send;

    sendControlHTTP(thr_send, rev, str_send, (uint8_t)drv, seq);
  }

  // --- LCD update ---
  static uint32_t lastLcd = 0;
  const uint32_t lcdIntervalMs = 1000 / LCD_HZ;
  if (now - lastLcd < lcdIntervalMs) return;
  lastLcd = now;

  // render
  if (uiMode == UI_EDIT) {
    // 編集対象に応じて描画
    CalibCfg& cfg = editCfgRef();
    int rawNow = editRawNow(thr_raw, str_raw);
    if (edit.target == EDIT_THR) {
      renderCalibEdit("THR", cfg, edit, rawNow);
    } else {
      renderCalibEdit("STR", cfg, edit, rawNow);
    }
    return;
  }

  switch (page) {
    case PAGE_WIFI:
      renderWifiPage(rev, drv, scrollLine, (uiMode == UI_PAGE_SCROLL));
      break;

    case PAGE_THR:
      renderThrPage(thr_raw, thr_u, thr_send, rev, drv);
      break;

    case PAGE_STR:
      renderStrPage(str_raw, str_u, rev, drv, lastSentStr, lastDeltaStr, lastHttpCode);
      break;

    case PAGE_ENC:
      // 操作モニタ（WiFi bar / steer / motor / gear+drive）
      renderEncPage(rev, drv, thr_send, str_send);
      break;

    default:
      renderRespPage(lastHttpCode, lastResp, scrollLine);
      break;
  }
}
