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

// ===== 選択時のページ数（view 0..WIFI_MAX_SCROLL）=====
// 1ページに表示できるのは row1..row3 の3項目（ただし view0 は row1 をバーに使うので2項目）
constexpr int WIFI_MAX_SCROLL = 4; // 0..4（必要なら増やす）
static inline int wifiMaxScrollLine() { return WIFI_MAX_SCROLL; }

static inline const char* wlStatusShort(wl_status_t st) {
  if (st == WL_CONNECTED) return "OK";
  if (st == WL_NO_SSID_AVAIL) return "NOAP";
  if (st == WL_CONNECT_FAILED) return "FAIL";
  if (st == WL_DISCONNECTED) return "DISC";
  return "BUSY";
}

static inline String srvShortStr() {
  uint32_t t = millis();
  if (t - lastOkMs < 500)  return "OK";
  if (t - lastOkMs < 2000) return "..";
  return "NG";
}

// RSSI(-95..-50くらい想定) -> 20列バー（■/□）
// 0=枠,1=ベタ を使う（uiInitCharsで作ってる前提）
static inline void drawRssiBar20(int row, int rssi) {
  int v = clampi(rssi, -95, -50);
  int filled = (v + 95) * 20 / 45; // 0..20
  filled = clampi(filled, 0, 20);

  lcd.setCursor(0, row);
  for (int i = 0; i < 20; i++) {
    lcd.write((uint8_t)((i < filled) ? 1 : 0));
  }
}

// 5バー（未選択用）
static inline void drawRssiBar5(int col, int row, int level) {
  lcd.setCursor(col, row);
  for (int i = 0; i < 5; i++) lcd.write((uint8_t)((i < level) ? 1 : 0));
}

// 1行1項目を作る（20桁に収める：長いものは切る）
static inline String itemLine(const String& key, const String& val) {
  String s = key + ":" + val;
  if ((int)s.length() > 20) s = s.substring(0, 20);
  return s;
}

// scrollLine によって表示内容を切り替える
// selected=false: リアルタイム項目のみ（バーは5キャラ）
// selected=true : 全情報を1行1項目で順次表示（スクロールあり）
//   view0 の row1 は 20列バー（要求通り）。以降は row1..row3 を本文に使う。
static inline void renderWifiPage(uint8_t rev, DriveMode drv, int scrollLine, bool selected) {

  wl_status_t st = (wl_status_t)WiFi.status();
  const char* stStr = wlStatusShort(st);
  String srv = srvShortStr();

  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -999;
  int bars5 = wifiBarsLevel();

  // ============
  // 未選択：リアルタイムだけ（5キャラバー）
  // ============
  if (!selected) {
    // row0: WiFi状態 + 5バー + Srv状態（OKを省略しないで明示）
    // 例: "WiFi:OK  ### Srv:OK"
    lcd.setCursor(0, 0);
    lcd.print("WiFi:");
    lcd.print(stStr);
    lcd.print(" ");
    drawRssiBar5(8, 0, bars5); // col8 から 5バー
    lcd.setCursor(14, 0);
    lcd.print("Srv:");
    lcd.print(srv);

    // row1: RSSI数値 + RTT
    char r1[21];
    snprintf(r1, sizeof(r1), "RSSI:%4d RTT:%03lu", rssi, (unsigned long)lastRttMs);
    lcdPrintFixed(0, 1, String(r1));

    // row2: HTTP code + ok/ng
    char r2[21];
    snprintf(r2, sizeof(r2), "C:%d ok:%lu ng:%lu",
             lastHttpCode, (unsigned long)okCount, (unsigned long)ngCount);
    lcdPrintFixed(0, 2, String(r2));

    // row3: 最終OKからの経過
    char r3[21];
    snprintf(r3, sizeof(r3), "lastOK:%5lums", (unsigned long)(millis() - lastOkMs));
    lcdPrintFixed(0, 3, String(r3));
    return;
  }

  // ============
  // 選択：全情報（1行1項目、入り切らなければページ増）
  // ============
  scrollLine = clampi(scrollLine, 0, WIFI_MAX_SCROLL);

  // row0: ページヘッダ
  {
    char h0[21];
    snprintf(h0, sizeof(h0), "WIFI INFO %d/%d", scrollLine, WIFI_MAX_SCROLL);
    lcdPrintFixed(0, 0, String(h0));
  }

  // ここから「項目」を並べる
  // view0 は row1=20列バーなので、row2,row3 に2項目
  // view1.. は row1..row3 に3項目
  if (scrollLine == 0) {
    // row1: 20列RSSIバー
    drawRssiBar20(1, rssi);

    // row2,row3: 項目
    lcdPrintFixed(0, 2, itemLine("WiFi", String(stStr)));
    lcdPrintFixed(0, 3, itemLine("Srv",  srv));
    return;
  }

  // 共通で使う値
  String ip   = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString()     : String("0.0.0.0");
  String gw   = (WiFi.status() == WL_CONNECTED) ? WiFi.gatewayIP().toString()   : String("0.0.0.0");
  String sn   = (WiFi.status() == WL_CONNECTED) ? WiFi.subnetMask().toString()  : String("0.0.0.0");
  String ssid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID()                   : String("-");
  String bssid= (WiFi.status() == WL_CONNECTED) ? WiFi.BSSIDstr()               : String("-");
  String mac  = WiFi.macAddress();

  // dnsIP が無い環境なら、ここだけコメントアウトしてOK（その場合は DNS を別項目に差し替え推奨）
  IPAddress dns1 = WiFi.dnsIP(0);
  IPAddress dns2 = WiFi.dnsIP(1);

  // viewごとに3項目ずつ
  // view1: IP系
  if (scrollLine == 1) {
    lcdPrintFixed(0, 1, itemLine("IP",   ip));
    lcdPrintFixed(0, 2, itemLine("GW",   gw));
    lcdPrintFixed(0, 3, itemLine("SN",   sn));
    return;
  }

  // view2: 無線リンク系
  if (scrollLine == 2) {
    lcdPrintFixed(0, 1, itemLine("RSSI", String(rssi)));
    lcdPrintFixed(0, 2, itemLine("Bars", String(bars5))); // 数値だけ（バーはview0のみ要求）
    lcdPrintFixed(0, 3, itemLine("RTTms", String((unsigned long)lastRttMs)));
    return;
  }

  // view3: ID系
  if (scrollLine == 3) {
    lcdPrintFixed(0, 1, itemLine("SSID", ssid));
    // BSSIDは長いので分割（先頭のみ）
    lcdPrintFixed(0, 2, itemLine("BSSID", bssid));
    lcdPrintFixed(0, 3, itemLine("MAC",  mac));
    return;
  }

  // view4: DNS/HTTP統計
  {
    // DNSは2行使うので、HTTPはコードだけにする（ページ増やしたいなら WIFI_MAX_SCROLL を増やして分割）
    lcdPrintFixed(0, 1, itemLine("DNS1", dns1.toString()));
    lcdPrintFixed(0, 2, itemLine("DNS2", dns2.toString()));
    lcdPrintFixed(0, 3, itemLine("HTTP", String("C=") + String(lastHttpCode)));
    return;
  }
}