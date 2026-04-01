#pragma once

// ── WiFi ──────────────────────────────────────────────────────────────
#define WIFI_SSID "Arkham"
#define WIFI_PASS "montevideo"

// ── Time ──────────────────────────────────────────────────────────────
#define NTP_SERVER            "pool.ntp.org"
#define TIMEZONE_OFFSET_HOURS (-3)  // UTC offset in hours (-3 for Uruguay)
#define DAYLIGHT_SAVING_HOURS (0)   // Uruguay does not observe DST

// ── Calendar ──────────────────────────────────────────────────────────
#define CALENDAR_ID         "primary"   // "primary" or a specific calendar ID
#define MAX_EVENTS          11          // max events to fetch and display
#define PAST_WINDOW_MINUTES 20          // show events that started up to N min ago

// ── Refresh ───────────────────────────────────────────────────────────
#define REFRESH_INTERVAL_MS (5UL * 60UL * 1000UL)  // 5 minutes
