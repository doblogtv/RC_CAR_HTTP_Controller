#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "ui_common.h"

// row0: WiFi感度バー（20セル）
// row1: STR（20セルカーソル）
// row2: THR（20セルカーソル）
// row3: Gear / Drive
static inline void drawWifiBar20_row0() {
  int lv = wifiBarsLevel20();
  lcd.setCursor(0, 0);
  for (int i = 0; i < 20; i++) {
    lcd.write((uint8_t)((i < lv) ? 1 : 0));
  }
}

static inline void renderEncPage(uint8_t rev, DriveMode drv, uint8_t thr_send, uint8_t str_send) {
  drawWifiBar20_row0();
  drawCursor20_u8(1, str_send);
  drawCursor20_u8(2, thr_send);

  const char* g = (rev != 0) ? "R" : "F";
  const char* d = (drv == DRV_4WD) ? "4WD" : "RWD";

  char r3[21];
  snprintf(r3, sizeof(r3), "G:%s  D:%s", g, d);
  lcdPrintFixed(0, 3, String(r3));
}