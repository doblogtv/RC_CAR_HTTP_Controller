#pragma once
#include <Arduino.h>
#include "ui_common.h"

static inline void renderThrPage(int thr_raw, uint8_t thr_u, uint8_t thr_send, uint8_t rev, DriveMode drv) {
  lcdPrintFixed(0, 0, "THR raw/u -> send  ");
  char r1[21];
  snprintf(r1, sizeof(r1), "raw:%4d u:%3u", thr_raw, (unsigned)thr_u);
  lcdPrintFixed(0, 1, String(r1));

  const char* g = rev ? "REV" : "FWD";
  const char* d = (drv == DRV_4WD) ? "4WD" : "RWD";

  char r2[21];
  snprintf(r2, sizeof(r2), "G:%s D:%s s:%3u", g, d, (unsigned)thr_send);
  lcdPrintFixed(0, 2, String(r2));

  drawCursor20_u8(3, thr_send);
}