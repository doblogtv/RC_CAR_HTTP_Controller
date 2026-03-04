#pragma once
#include <Arduino.h>
#include "ui_common.h"

// scrollLine: 0,1,2...（1ステップ=20文字=1行ぶん進む）
static inline int respMaxScrollLine(const String& s) {
  int len = (int)s.length();
  if (len <= 40) return 0;                 // 2行(40文字)に収まるならスクロール不要
  int maxLine = (len - 1) / 20;            // 20文字単位の最終行
  // 2行表示なので、開始行が maxLine まで行けると下段が欠けるが、それでもOK（空欄になる）
  return maxLine;
}

static inline void renderRespPage(int lastHttpCode, const String& lastResp, int scrollLine) {
  lcdPrintFixed(0, 0, "RESP (server body) ");

  String s = lastResp;
  s.replace("\r", " ");
  s.replace("\n", " ");
  int len = (int)s.length();

  char r1[21];
  snprintf(r1, sizeof(r1), "C:%d len:%3d L:%2d", lastHttpCode, len, scrollLine);
  lcdPrintFixed(0, 1, String(r1));

  // 2行(40文字)のウィンドウを 20文字単位でスクロール
  int start = scrollLine * 20;
  if (start < 0) start = 0;
  if (start > len) start = len;

  String l2 = (len > start) ? s.substring(start, min(start + 20, len)) : String("");
  String l3 = (len > start + 20) ? s.substring(start + 20, min(start + 40, len)) : String("");

  lcdPrintFixed(0, 2, l2);
  lcdPrintFixed(0, 3, l3);
}