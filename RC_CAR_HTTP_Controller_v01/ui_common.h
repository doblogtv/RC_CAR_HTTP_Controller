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

// G/L/X -> rev / drv
static inline uint8_t readRev_G() { return swOn(PIN_SW_G) ? 1 : 0; }

static inline DriveMode readDrive_LX() {
  if (swOn(PIN_SW_X)) return DRV_4WD; // X優先
  //if (swOn(PIN_SW_L)) return DRV_FWD; // L=前輪駆動
  return DRV_RWD;                     // 後輪駆動
}

// raw(0..4095)->u8(0..255) 直結（invertは反転だけ）
static inline uint8_t mapRawToU8(int raw, bool invertFlag) {
  raw = clampi(raw, 0, 4095);
  if (invertFlag) raw = 4095 - raw;
  int u = (raw * 255 + (4095 / 2)) / 4095; // round
  u = clampi(u, 0, 255);
  return (uint8_t)u;
}

// THR u8(0..255) -> speed(0..255)（中心128からの距離）
static inline uint8_t thrU8ToSpeed(uint8_t thr_u) {
  const int center = 128;
  int d = abs((int)thr_u - center); // 0..128
  int sp = (d * 255 + 128/2) / 128;
  return (uint8_t)clampi(sp, 0, 255);
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

// custom chars init (wifi + graph)
static inline void uiInitChars() {
  // 0=枠, 1=ベタ
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

// WiFi bars (0..5)
static inline int wifiBarsLevel() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  int rssi = WiFi.RSSI();
  if (rssi >= -50) return 5;
  if (rssi >= -60) return 4;
  if (rssi >= -70) return 3;
  if (rssi >= -80) return 2;
  if (rssi >= -90) return 1;
  return 0;
}