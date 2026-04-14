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
#define PAST_WINDOW_MINUTES 0           // show events that started up to N min ago
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

// ── Alerts ────────────────────────────────────────────────────────────
#define ALERT_WARN_MINUTES   5   // first blink warning N minutes before meeting
#define ALERT_URGENT_MINUTES 1   // urgent blink warning N minutes before meeting
#define ALERT_WARN_BLINKS    3   // number of blinks for the 5-min warning
#define ALERT_URGENT_BLINKS  5   // number of blinks for the 1-min warning
#define ALERT_SCREEN_MS   8000   // how long to show the alert info screen (ms)

// ── Refresh ───────────────────────────────────────────────────────────
#define REFRESH_INTERVAL_MS (1UL * 60UL * 1000UL)  // 1 minute

// ── MQTT push notifications ───────────────────────────────────────────
#define MQTT_BROKER      "192.168.1.100"   // change to broker IP
#define MQTT_PORT        1883
#define MQTT_TOPIC       "eink/push"
#define MQTT_CLIENT_ID   "eink-display"
#define NOTIF_TIMEOUT_MS 30000             // auto-dismiss after 30s
