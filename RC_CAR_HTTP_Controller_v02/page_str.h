#pragma once
#include <Arduino.h>
#include "ui_common.h"

// STR: row3 をグラフに（20セルカーソル）
static inline void renderStrPage(int str_raw, uint8_t str_u, uint8_t rev, DriveMode drv,
                                 uint8_t lastSentStr, int16_t lastDeltaStr, int lastHttpCode) {
  lcdPrintFixed(0, 0, "STR raw/u -> send   ");

  char r1[21];
  const char* g = rev ? "R" : "F";
  const char* d = (drv == DRV_4WD) ? "4WD" : "RWD";
  snprintf(r1, sizeof(r1), "raw:%4d u:%3u G:%s", str_raw, (unsigned)str_u, g);
  lcdPrintFixed(0, 1, String(r1));

  char r2[21];
  snprintf(r2, sizeof(r2), "D:%s s:%3u d:%+04d",
           d, (unsigned)lastSentStr, (int)lastDeltaStr);
  lcdPrintFixed(0, 2, String(r2));

  drawCursor20_u8(3, lastSentStr);
}