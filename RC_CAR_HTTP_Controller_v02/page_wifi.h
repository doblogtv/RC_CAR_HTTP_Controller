#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "ui_common.h"

// stats（.ino側のグローバルを参照）
extern uint32_t lastOkMs;
extern uint32_t lastRttMs;
extern int lastHttpCode;
extern uint32_t okCount;
extern uint32_t ngCount;

// 選択時の内部ビュー数（0..max）
constexpr int WIFI_SCROLL_MAX = 6;

// 選択状態かどうかでスクロール上限を変える
static inline int wifiMaxScrollLine(bool selected) {
  return selected ? WIFI_SCROLL_MAX : 0;
}

static inline String srvShortStr() {
  uint32_t t = millis();
  if (t - lastOkMs < 500) return "OK";
  if (t - lastOkMs < 2000) return "..";
  return "NG";
}

static inline const char* wlStatusShort(wl_status_t st) {
  if (st == WL_CONNECTED) return "OK";
  if (st == WL_NO_SSID_AVAIL) return "NOAP";
  if (st == WL_CONNECT_FAILED) return "FAIL";
  if (st == WL_DISCONNECTED) return "DISC";
  return "BUSY";
}

// selected=false（表示時）: リアルタイム項目のみ（省略しない範囲で固定）
// selected=true（選択時）: ジャンル別に全情報、scrollLineで切替
static inline void renderWifiPage(uint8_t rev, DriveMode drv, int scrollLine, bool selected) {
  wl_status_t st = (wl_status_t)WiFi.status();
  const char* stStr = wlStatusShort(st);
  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -999;

  const char* g = rev ? "R" : "F";
  const char* d = (drv == DRV_4WD) ? "4WD" : "RWD";

  if (!selected) {
    // ---- 未選択（表示時）：リアルタイムだけ ----
    // row0: WIFI: [5bars] OK(省略なし)
    lcd.setCursor(0, 0);
    lcd.print("WIFI:");
    int bars5 = wifiBarsLevel5();
    for (int i=0;i<5;i++) lcd.write((uint8_t)((i < bars5) ? 1 : 0));
    lcd.print(" ");
    lcdPrintFixed(11, 0, "OK:" + srvShortStr()); // "OK:OK" など

    char r1[21];
    snprintf(r1, sizeof(r1), "ST:%-4s RSSI:%4d", stStr, rssi);
    lcdPrintFixed(0, 1, String(r1));

    String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("0.0.0.0");
    lcdPrintFixed(0, 2, "IP:" + ip);

    char r3[21];
    snprintf(r3, sizeof(r3), "G:%s D:%s RTT:%3lu", g, d, (unsigned long)lastRttMs);
    lcdPrintFixed(0, 3, String(r3));
    return;
  }

  // ---- 選択時（詳細）：scrollLineでジャンル別 ----
  scrollLine = clampi(scrollLine, 0, WIFI_SCROLL_MAX);

  // row0: View0だけ“20セルバー”、それ以外は空
  if (scrollLine == 0) {
    // 20セルバー（このページだけ）
    int lv20 = wifiBarsLevel20();
    lcd.setCursor(0, 0);
    for (int i=0;i<20;i++) lcd.write((uint8_t)((i < lv20) ? 1 : 0));
  } else {
    lcdPrintFixed(0, 0, ""); // 空白
  }

  if (scrollLine == 0) {
    // 状態
    char r1[21];
    snprintf(r1, sizeof(r1), "ST:%-4s RSSI:%4d", stStr, rssi);
    lcdPrintFixed(0, 1, String(r1));
    lcdPrintFixed(0, 2, "OK:" + srvShortStr());
    char r3[21];
    snprintf(r3, sizeof(r3), "G:%s D:%s C:%d", g, d, lastHttpCode);
    lcdPrintFixed(0, 3, String(r3));
  }
  else if (scrollLine == 1) {
    String ssid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("-");
    lcdPrintFixed(0, 1, "SSID:" + ssid);
    String bssid = (WiFi.status() == WL_CONNECTED) ? WiFi.BSSIDstr() : String("-");
    lcdPrintFixed(0, 2, "BSSID:" + bssid);
    lcdPrintFixed(0, 3, "MODE:STA");
  }
  else if (scrollLine == 2) {
    String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("0.0.0.0");
    lcdPrintFixed(0, 1, "IP:" + ip);
    String gw = (WiFi.status() == WL_CONNECTED) ? WiFi.gatewayIP().toString() : String("0.0.0.0");
    lcdPrintFixed(0, 2, "GW:" + gw);
    String sn = (WiFi.status() == WL_CONNECTED) ? WiFi.subnetMask().toString() : String("0.0.0.0");
    lcdPrintFixed(0, 3, "SN:" + sn);
  }
  else if (scrollLine == 3) {
    String mac = WiFi.macAddress();
    lcdPrintFixed(0, 1, "MAC:" + mac);
    // dnsIPは環境差が出る可能性があるので、無理に使わず表示固定
    lcdPrintFixed(0, 2, "DNS: (skip)");
    lcdPrintFixed(0, 3, "CH: (skip)");
  }
  else if (scrollLine == 4) {
    char r1[21];
    snprintf(r1, sizeof(r1), "HTTP ok:%6lu", (unsigned long)okCount);
    lcdPrintFixed(0, 1, String(r1));
    char r2[21];
    snprintf(r2, sizeof(r2), "HTTP ng:%6lu", (unsigned long)ngCount);
    lcdPrintFixed(0, 2, String(r2));
    uint32_t dt = millis() - lastOkMs;
    char r3[21];
    snprintf(r3, sizeof(r3), "lastOK:%5lums", (unsigned long)dt);
    lcdPrintFixed(0, 3, String(r3));
  }
  else if (scrollLine == 5) {
    char r1[21];
    snprintf(r1, sizeof(r1), "RTT:%4lums", (unsigned long)lastRttMs);
    lcdPrintFixed(0, 1, String(r1));
    char r2[21];
    snprintf(r2, sizeof(r2), "CODE:%d", lastHttpCode);
    lcdPrintFixed(0, 2, String(r2));
    lcdPrintFixed(0, 3, "API:" + String(CAR_API).substring(0, 15));
  }
  else {
    // 6: help
    lcdPrintFixed(0, 1, "ROT:scroll");
    lcdPrintFixed(0, 2, "SW:confirm step");
    lcdPrintFixed(0, 3, "HOLD:exit edit");
  }
}