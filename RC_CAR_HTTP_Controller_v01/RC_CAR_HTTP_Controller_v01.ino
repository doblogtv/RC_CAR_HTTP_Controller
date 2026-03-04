#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

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
// UI mode: page switch / page scroll
// =========================
enum UiMode : uint8_t { UI_PAGE_SWITCH = 0, UI_PAGE_SCROLL = 1 };
UiMode uiMode = UI_PAGE_SWITCH;

// スクロール位置（主にRESP用：1=20文字=1行ぶん）
int scrollLine = 0;

// UI用のenc基準（差分計算用）
static long uiEncBase = 0;
static uint32_t uiLastMs = 0;

// ENC_SW（選択）
static uint8_t encSwPrev = HIGH;
static uint32_t encSwLastMs = 0;

// =========================
// Encoder ISR
// =========================
volatile long g_enc = 0;
volatile int  g_dir = 0;
volatile uint8_t g_prevAB = 0;
volatile uint32_t g_lastISR_us = 0;

long enc_zero = 0;

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
    if ((int)body.length() > 256) body = body.substring(0, 256); // RESPスクロールに少し余裕
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
  uiInitChars(); // wifi chars + graph chars

  // WiFi begin (non-blocking)
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiBeginIssued = true;
  wifiLastTryMs = millis();

  // encoder center at boot
  delay(50);
  noInterrupts();
  enc_zero = g_enc;
  uiEncBase = g_enc;   // UI基準も合わせる
  interrupts();

  lcd.clear();
  lcdPrintFixed(0, 0, "RC HTTP CTRL v01   ");
  lcdPrintFixed(0, 1, "ROT:page  SW:select");
  lcdPrintFixed(0, 2, "SEL:scroll L:cancel");
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

// --- ENC_SW：選択/解除（編集画面はページ側で消費する想定）---
uint8_t encSw = digitalRead(PIN_ENC_SW);
if (encSwPrev == HIGH && encSw == LOW && (now - encSwLastMs) > ENC_SW_DEBOUNCE_MS) {
  encSwLastMs = now;

  // ★ 編集画面でENC_SWを使う場合は true にして “ここで抜けない”
  // （今は編集未実装なので false 固定でOK）
  bool consumedByEditor = false;

  if (!consumedByEditor) {
    if (uiMode == UI_PAGE_SWITCH) {
      // 選択に入る（スクロール/詳細表示モード）
      uiMode = UI_PAGE_SCROLL;
      scrollLine = 0;
      uiEncBase = enc;
      lcd.clear();
    } else {
      // 選択から抜ける（ページ切替モードへ戻る）
      uiMode = UI_PAGE_SWITCH;
      scrollLine = 0;
      uiEncBase = enc;
      lcd.clear();
    }
  }
}
encSwPrev = encSw;


  // --- Rotary：ページ切替 or スクロール ---
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
      } else if (page == PAGE_WIFI && uiMode == UI_PAGE_SCROLL) {
        maxScroll = wifiMaxScrollLine();
      }
      scrollLine = clampi(scrollLine, 0, maxScroll);
    }
  }

  // --- WiFi tick (non-blocking) ---
  wifiTick();

  // --- Read VR ---
  int thr_raw = analogRead(PIN_THR);
  int str_raw = analogRead(PIN_STR);

  // raw -> u8（直結）
  uint8_t thr_u = mapRawToU8(thr_raw, THR_INVERT);
  uint8_t str_u = mapRawToU8(str_raw, STR_INVERT);

  // --- Switch meanings ---
  uint8_t rev = readRev_G();
  DriveMode drv = readDrive_LX();

  // ★送信値：THRもSTRも補正無し（raw->u8そのまま）
  uint8_t thr_send = thr_u;
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
  switch (page) {
    case PAGE_WIFI:
      renderWifiPage(rev, drv, scrollLine, (uiMode == UI_PAGE_SCROLL));
      break;

    case PAGE_THR:
      // THR画面は送信値をグラフ表示（thr_send）
      renderThrPage(thr_raw, thr_u, thr_send, rev, drv);
      break;

    case PAGE_STR:
      renderStrPage(str_raw, str_u, rev, drv, lastSentStr, lastDeltaStr, lastHttpCode);
      break;

    case PAGE_ENC:
      renderEncPage(rev, drv, thr_send, str_send);
      break;

    default:
      renderRespPage(lastHttpCode, lastResp, scrollLine); // ★スクロール対応RESP
      break;
  }
}