#pragma once

// ── WiFi ──────────────────────────────────────────────────────────────
#define WIFI_SSID "Arkham"
#define WIFI_PASS "montevideo"

// ── Time ──────────────────────────────────────────────────────────────
#define NTP_SERVER            "pool.ntp.org"
#define TIMEZONE_OFFSET_HOURS (-8)  // UTC offset in hours (-8 for US Pacific)
#define DAYLIGHT_SAVING_HOURS (1)   // 1 during DST (Mar-Nov), 0 otherwise

// ── Calendar ──────────────────────────────────────────────────────────
#define CALENDAR_ID         "primary"   // "primary" or a specific calendar ID
#define MAX_EVENTS          11          // max events to fetch and display
#define PAST_WINDOW_MINUTES 20          // show events that started up to N min ago
#define LOOKAHEAD_HOURS     4           // show events up to N hours ahead

// ── Buttons ──────────────────────────────────────────────────────────
#define BTN_HOME  2   // force refresh
#define BTN_EXIT  1   // (reserved)
#define BTN_UP    6   // reset to overview
#define BTN_DOWN  4   // scroll to next meeting in detail view
#define BTN_OK    5   // (reserved)
#define BUTTON_DEBOUNCE_MS 250

// ── Display ───────────────────────────────────────────────────────────
#define DISPLAY_ROTATION 0  // 0 = USB at bottom, 180 = USB at top

// ── Refresh ───────────────────────────────────────────────────────────
#define REFRESH_INTERVAL_MS (5UL * 60UL * 1000UL)  // 5 minutes
