#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "ui_common.h"

// =====================================================
// PAGE_ENC を「操作中モニタ画面」に変更
//
// row0: WiFi感度グラフ（20セル横一列）
// row1: Steering グラフ（20セルカーソル）
// row2: Throttle  グラフ（20セルカーソル）
// row3: Gear / Drive 表示
// =====================================================

// RSSI を 0..20 セルにマップ（接続なしは 0）
static inline int wifiLevel20() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  int rssi = WiFi.RSSI(); // dBm
  // おおざっぱな実用域：-90(弱) .. -50(強)
  // -90以下→0、-50以上→20
  int lv = (rssi + 90) * 20 / 40; // (rssi - (-90)) / 40 * 20
  return clampi(lv, 0, 20);
}

// row に WiFiバー（20セル）描画：char1=ベタ char0=枠 を使用
static inline void drawWifiBar20(int row) {
  int lv = wifiLevel20();
  lcd.setCursor(0, row);
  for (int i = 0; i < 20; i++) {
    lcd.write((uint8_t)((i < lv) ? 1 : 0));
  }
}

// モニタ画面（旧 renderEncPage の置き換え）
// thr_send / str_send は「送信値」（0..255）を入れる想定
static inline void renderEncPage(uint8_t rev, DriveMode drv, uint8_t thr_send, uint8_t str_send) {
  // --- row0: WiFi bar ---
  drawWifiBar20(0);

  // --- row1: Steering ---
  drawCursor20_u8(1, str_send);

  // --- row2: Throttle ---
  drawCursor20_u8(2, thr_send);

  // --- row3: Gear / Drive ---
  const char* g = (rev != 0) ? "R" : "F";
  const char* d = (drv == DRV_4WD) ? "4WD" : "RWD";

  // 20桁内に収める（例： "G:F  D:RWD        "）
  char r3[21];
  snprintf(r3, sizeof(r3), "G:%s  D:%s", g, d);
  lcdPrintFixed(0, 3, String(r3));
}