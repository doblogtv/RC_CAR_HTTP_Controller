#pragma once
#include <Arduino.h>
#include "ui_common.h"

// STR: row3 をグラフに（THRと同じ20セルカーソル）
// row2 に send / dlt / http code をまとめて表示
static inline void renderStrPage(int str_raw, uint8_t str_u, uint8_t rev, DriveMode drv,
                                 uint8_t lastSentStr, int16_t lastDeltaStr, int lastHttpCode) {
  lcdPrintFixed(0, 0, "STR raw/u -> send   ");

  char r1[21];
  // raw/u と G/D を軽く表示
  const char* g = rev ? "R" : "F";
  const char* d = (drv == DRV_4WD) ? "4" : (drv == DRV_FWD) ? "F" : "R";
  snprintf(r1, sizeof(r1), "raw:%4d u:%3u G:%sD:%s", str_raw, (unsigned)str_u, g, d);
  lcdPrintFixed(0, 1, String(r1));

  char r2[21];
  // 20桁に収まる形： s:### d:+#### C:###
  snprintf(r2, sizeof(r2), "s:%3u d:%+04d C:%3d",
           (unsigned)lastSentStr, (int)lastDeltaStr, (int)lastHttpCode);
  lcdPrintFixed(0, 2, String(r2));

  // ★グラフ（THRと同じ）
  drawCursor20_u8(3, lastSentStr);   // 0..255 をそのまま 20セルにマッピング
}