#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include "settings.h"

extern LiquidCrystal_I2C lcd;

// clamp
static inline int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// INPUT_PULLUPなので ON=LOW
static inline bool swOn(int pin) { return (digitalRead(pin) == LOW); }

// G/X -> rev / drv（Lは将来ブレーキ予定なので無視）
static inline uint8_t readRev_G() { return swOn(PIN_SW_G) ? 1 : 0; }

static inline DriveMode readDrive_LX() {
  if (swOn(PIN_SW_X)) return DRV_4WD; // X優先
  return DRV_RWD;
}

// LCD fixed print
static inline void lcdPrintFixed(int col, int row, const String& s) {
  lcd.setCursor(col, row);
  String out = s;
  if ((int)out.length() > LCD_COLS - col) out = out.substring(0, LCD_COLS - col);
  lcd.print(out);
  int pad = (LCD_COLS - col) - out.length();
  for (int i = 0; i < pad; i++) lcd.print(' ');
}

// custom chars init (wifi + cursor)
static inline void uiInitChars() {
  // 0=枠, 1=ベタ（WiFiバー兼 チェックボックス）
  uint8_t chEmpty[8] = { 0b11111,0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b11111 };
  uint8_t chFull[8]  = { 0b11111,0b11111,0b11111,0b11111,0b11111,0b11111,0b11111,0b11111 };
  lcd.createChar(0, chEmpty);
  lcd.createChar(1, chFull);

  // 2..6 cursor, 7 midline
  uint8_t cur0[8] = {0b10000,0b10000,0b10000,0b10000,0b11111,0b10000,0b10000,0b10000};
  uint8_t cur1[8] = {0b01000,0b01000,0b01000,0b01000,0b11111,0b01000,0b01000,0b01000};
  uint8_t cur2[8] = {0b00100,0b00100,0b00100,0b00100,0b11111,0b00100,0b00100,0b00100};
  uint8_t cur3[8] = {0b00010,0b00010,0b00010,0b00010,0b11111,0b00010,0b00010,0b00010};
  uint8_t cur4[8] = {0b00001,0b00001,0b00001,0b00001,0b11111,0b00001,0b00001,0b00001};
  uint8_t mid[8]  = {0,0,0,0,0b11111,0,0,0};

  lcd.createChar(2, cur0);
  lcd.createChar(3, cur1);
  lcd.createChar(4, cur2);
  lcd.createChar(5, cur3);
  lcd.createChar(6, cur4);
  lcd.createChar(7, mid);
}

// 20x4 の1行カーソル描画（u8=0..255）
static inline void drawCursor20_u8(int row, uint8_t v255) {
  constexpr int CELLS = 20;
  constexpr int DOTS  = CELLS * 5;
  int dotPos = ((int)v255 * (DOTS - 1) + 255/2) / 255;
  dotPos = clampi(dotPos, 0, DOTS - 1);

  int cell = dotPos / 5;
  int sub  = dotPos % 5;

  lcd.setCursor(0, row);
  for (int c = 0; c < CELLS; c++) {
    if (c == cell) lcd.write((uint8_t)(2 + sub));
    else           lcd.write((uint8_t)7);
  }
}

// WiFi bars (0..5) 旧UI用
static inline int wifiBarsLevel5() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  int rssi = WiFi.RSSI();
  if (rssi >= -50) return 5;
  if (rssi >= -60) return 4;
  if (rssi >= -70) return 3;
  if (rssi >= -80) return 2;
  if (rssi >= -90) return 1;
  return 0;
}

// 20セルWiFiバー用（0..20）
static inline int wifiBarsLevel20() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  int rssi = WiFi.RSSI();
  int lv = (rssi + 90) * 20 / 40; // -90..-50 -> 0..20
  return clampi(lv, 0, 20);
}

// =========================
// Calibration structs / mapping
// =========================
struct CalibCfg {
  bool inv;
  uint16_t minRaw; // 0..4095
  uint16_t maxRaw; // 0..4095
};

static inline void sanitizeCalib(CalibCfg& c) {
  if (c.minRaw > 4095) c.minRaw = 4095;
  if (c.maxRaw > 4095) c.maxRaw = 4095;
  if (c.maxRaw < 1) c.maxRaw = 1;
  if (c.minRaw >= c.maxRaw) {
    // 最低1幅確保
    uint16_t mn = (uint16_t)min((int)c.minRaw, 4094);
    c.minRaw = mn;
    c.maxRaw = (uint16_t)min(4095, (int)mn + 1);
  }
}

// raw(0..4095) -> u8(0..255) with calib
static inline uint8_t mapRawToU8_Calib(int raw, const CalibCfg& c0) {
  CalibCfg c = c0;
  sanitizeCalib(c);

  raw = clampi(raw, 0, 4095);
  if (c.inv) raw = 4095 - raw;

  raw = clampi(raw, (int)c.minRaw, (int)c.maxRaw);

  int span = (int)c.maxRaw - (int)c.minRaw;
  if (span < 1) span = 1;

  int u = ((raw - (int)c.minRaw) * 255 + span/2) / span;
  u = clampi(u, 0, 255);
  return (uint8_t)u;
}

// =========================
// Edit state machine
// =========================
enum EditTarget : uint8_t { EDIT_THR=0, EDIT_STR=1 };
enum EditStep   : uint8_t { EDIT_MENU=0, EDIT_DIR=1, EDIT_MIN=2, EDIT_MAX=3 };

struct EditCtx {
  EditTarget target;
  EditStep step;
  int sel; // 0/1
};

static inline void editReset(EditCtx& e, EditTarget t) {
  e.target = t;
  e.step = EDIT_MENU;
  e.sel = 0;
}

// checkbox using wifi chars
static inline void lcdBox(bool on) {
  lcd.write((uint8_t)(on ? 1 : 0)); // ■ or □
}

// =========================
// Calib edit renderer (common)
// =========================
static inline void renderCalibEdit(const char* label, const CalibCfg& cfg, const EditCtx& e, int rawNow) {
  rawNow = clampi(rawNow, 0, 4095);

  // header
  char h[21];
  snprintf(h, sizeof(h), "%s CALIB", label);
  lcdPrintFixed(0, 0, String(h));

  if (e.step == EDIT_MENU) {
    // row1/row2: menu items
    lcd.setCursor(0, 1);
    lcdBox(e.sel == 0); lcd.print(" Setting");
    lcdPrintFixed(10, 1, ""); // clear rest

    lcd.setCursor(0, 2);
    lcdBox(e.sel == 1); lcd.print(" Back");
    lcdPrintFixed(10, 2, "");

    // row3 summary
    char s[21];
    snprintf(s, sizeof(s), "inv:%c mn:%4u mx:%4u",
             cfg.inv ? 'Y' : 'N', (unsigned)cfg.minRaw, (unsigned)cfg.maxRaw);
    lcdPrintFixed(0, 3, String(s));
    return;
  }

  if (e.step == EDIT_DIR) {
    lcd.setCursor(0, 1);
    lcdBox(e.sel == 0); lcd.print(" Normal");
    lcdPrintFixed(10, 1, "");

    lcd.setCursor(0, 2);
    lcdBox(e.sel == 1); lcd.print(" Invert");
    lcdPrintFixed(10, 2, "");

    lcdPrintFixed(0, 3, "SW:OK  HOLD:MENU   ");
    return;
  }

  if (e.step == EDIT_MIN) {
    lcdPrintFixed(0, 1, "SET MIN (VR->raw)  ");
    char r2[21];
    snprintf(r2, sizeof(r2), "raw:%4d  min:%4u", rawNow, (unsigned)cfg.minRaw);
    lcdPrintFixed(0, 2, String(r2));

    // graph 0..4095 -> 0..255
    uint8_t u = (uint8_t)((rawNow * 255 + 2047) / 4095);
    drawCursor20_u8(3, u);
    return;
  }

  // EDIT_MAX
  lcdPrintFixed(0, 1, "SET MAX (min..4095)");
  char r2[21];
  snprintf(r2, sizeof(r2), "raw:%4d  max:%4u", rawNow, (unsigned)cfg.maxRaw);
  lcdPrintFixed(0, 2, String(r2));

  int mn = (int)cfg.minRaw;
  int span = 4095 - mn;
  if (span < 1) span = 1;
  int rr = clampi(rawNow, mn, 4095);
  uint8_t u = (uint8_t)(((rr - mn) * 255 + span/2) / span);
  drawCursor20_u8(3, u);
}