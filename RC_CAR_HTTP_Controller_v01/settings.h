#pragma once
#include <Arduino.h>

// =========================
// User settings
// =========================

constexpr bool THR_INVERT = false;
constexpr bool STR_INVERT = false;

static const char* WIFI_SSID = "RC_CAR";
static const char* WIFI_PASS = "12345678";
static const char* CAR_API   = "http://192.168.4.1/api";

constexpr int SEND_HZ_FAST = 30;
constexpr int LCD_HZ = 10;

constexpr uint32_t SW_DEBOUNCE_MS = 180;

// Encoder
constexpr uint32_t ENC_DEBOUNCE_US = 600;
constexpr int ENC_VIEW_LIMIT = 999;

// WiFi reconnect (non-blocking)
constexpr uint32_t WIFI_RETRY_MS = 2000;

// HTTP timeout
constexpr int HTTP_TIMEOUT_MS = 50;

// =========================
// LCD
// =========================
constexpr uint8_t LCD_ADDR = 0x3F;
constexpr int LCD_COLS = 20;
constexpr int LCD_ROWS = 4;

// =========================
// Pins
// =========================
constexpr int PIN_THR = 32; // ADC1
constexpr int PIN_STR = 33; // ADC1

constexpr int PIN_ENC_A  = 19;
constexpr int PIN_ENC_B  = 18;
constexpr int PIN_ENC_SW = 17; // page switch

// switches（INPUT_PULLUP：ON=LOW）
constexpr int PIN_SW_G = 16; // G: gear (ON=REV)
constexpr int PIN_SW_L = 27; // L: 
constexpr int PIN_SW_X = 23; // X: 4WD (ON=4WD)

// UI (encoder page switch / scroll)
constexpr int PAGE_STEP_COUNTS = 4; // 1ページ/1スクロールに必要なencカウント
constexpr uint32_t PAGE_DEBOUNCE_MS = 120;// ページ切替の最短間隔（暴れ防止）
constexpr uint32_t UI_DEBOUNCE_MS = 120;   // ← これを追加
constexpr uint32_t ENC_SW_DEBOUNCE_MS = 180;

// =========================
// Drive mode
// =========================
enum DriveMode : uint8_t { DRV_RWD = 0, DRV_FWD = 1, DRV_4WD = 2 };

// =========================
// Pages
// =========================
enum Page : uint8_t { PAGE_WIFI=0, PAGE_THR=1, PAGE_STR=2, PAGE_ENC=3, PAGE_RESP=4 };
constexpr uint8_t PAGE_COUNT = 5;