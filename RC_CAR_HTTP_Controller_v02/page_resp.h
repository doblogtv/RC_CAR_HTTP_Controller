#pragma once
#include <Arduino.h>
#include "ui_common.h"

static inline int respMaxScrollLine(const String& s) {
  // 20文字=1行として必要行数-1を返す
  int len = (int)s.length();
  if (len <= 0) return 0;
  int lines = (len + 20 - 1) / 20;
  if (lines < 1) lines = 1;
  return lines - 1;
}

static inline void renderRespPage(int lastHttpCode, const String& lastResp, int scrollLine) {
  lcdPrintFixed(0, 0, "RESP (body)        ");

  int len = (int)lastResp.length();
  char r1[21];
  snprintf(r1, sizeof(r1), "C:%d len:%3d ln:%2d", lastHttpCode, len, scrollLine);
  lcdPrintFixed(0, 1, String(r1));

  String s = lastResp;
  s.replace("\r", " ");
  s.replace("\n", " ");

  int base = scrollLine * 20;
  String l2 = (base < len) ? s.substring(base, min(base + 20, len)) : String("");
  String l3 = (base + 20 < len) ? s.substring(base + 20, min(base + 40, len)) : String("");

  lcdPrintFixed(0, 2, l2);
  lcdPrintFixed(0, 3, l3);
}