# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

Firmware for a **CrowPanel ESP32-S3 5.79" e-ink display** that shows a Google Calendar agenda. Written in Arduino C++. No host-side build toolchain — compiled and flashed via arduino-cli.

## Flashing / Development

**arduino-cli** (preferred — no IDE needed):

```bash
# Compile
arduino-cli compile -b esp32:esp32:esp32s3 \
  --board-options "FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,UploadSpeed=921600" \
  firmware/calendar_display/calendar_display.ino

# Flash
arduino-cli upload -b esp32:esp32:esp32s3 \
  --board-options "FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,UploadSpeed=921600" \
  -p /dev/cu.wchusbserial2110 \
  firmware/calendar_display/calendar_display.ino
```

Port may vary — check with `ls /dev/cu.wch*`. Required libraries: **ArduinoJson 7.x**, **PubSubClient 2.8**. Board package: **esp32 by Espressif 3.x**. Monitor serial at 115200 baud.

## First-time secrets setup

```bash
pip install google-auth-oauthlib
python3 tools/get_token.py client_secret.json
cp firmware/calendar_display/secrets.h.example firmware/calendar_display/secrets.h
# paste token output into secrets.h
```

After generating the token, verify **Google Calendar API is enabled** in Google Cloud Console → APIs & Services → Enabled APIs. `secrets.h` is gitignored. Never commit it.

## Configuration

All tuneable constants live in `config.h`:
- WiFi SSID/password
- Timezone: `TIMEZONE_OFFSET_HOURS` (-8), `DAYLIGHT_SAVING_HOURS` (1)
- Window: `PAST_WINDOW_MINUTES` (0), `LOOKAHEAD_HOURS` (4)
- `MAX_EVENTS` (11), `REFRESH_INTERVAL_MS` (1 min)
- Button GPIO pins: `BTN_HOME` (force refresh), `BTN_UP` (back to overview), `BTN_DOWN` (scroll detail)
- Alert thresholds: `ALERT_WARN_MINUTES` (5), `ALERT_URGENT_MINUTES` (1)
- `DISPLAY_ROTATION` (0 = USB at bottom)
- MQTT: `MQTT_BROKER` (broker IP), `MQTT_PORT` (1883), `MQTT_TOPIC` (`eink/push`), `MQTT_CLIENT_ID`, `NOTIF_TIMEOUT_MS` (30s auto-dismiss)

## Architecture

All firmware logic is in a single sketch `calendar_display.ino`. The display driver (`EPD*.cpp/h`, `spi.*`) is vendor-supplied and should not be modified.

**Data flow:**
1. `setup()` — connect WiFi → sync NTP → fetch data → render
2. `loop()` — poll buttons every 30 ms; auto-refresh every 1 min; check alerts and meeting mode every minute

**Key functions:**
- `getAccessToken()` — exchanges refresh token for short-lived access token via `oauth2.googleapis.com`
- `fetchCalendarList()` — fetches all accessible calendar IDs from `/calendarList` API
- `fetchEvents()` — fetches timed events from primary calendar (all-day events skipped entirely)
- `fetchOOO()` — scans all calendars for today's OOO/PTO all-day events; skips system/import calendars; extracts person name from event title
- `refreshData()` — calls all three fetch functions in sequence
- `renderOverview()` — clock left panel + next meeting card (48px) + 2 upcoming events (16px)
- `renderMeeting()` — full-screen black meeting-in-progress view with progress bar
- `renderDetail()` — 2-event detail view with attendees and description
- `checkAlerts()` — blink screen at T-5min and T-1min before meetings
- `checkMeetingMode()` — switches between white/black background when entering/leaving a meeting
- `showFlatText()` — helper: strips newlines, renders multi-line truncated text with "..."
- `connectMQTT()` — connects to Mosquitto broker and subscribes to `eink/push` topic (non-blocking)
- `onMqttMessage()` — MQTT callback: parses JSON payload, sets `notifPending = true`
- `renderNotification()` — shows push notification: `[SOURCE]` label, title (24px), body (16px, up to 3 lines), dismiss hint
- `renderDone()` — static "YOU'RE DONE FOR TODAY" screen shown when no events remain after noon

**Display coordinate system:** Physical display is 272×792, mounted landscape. `Rotation=0` (USB at bottom) → logical canvas 792×272px. Left panel (clock): x=0–192. Right panel (events): x=200–792.

**Color scheme:**
- `FG_COLOR` / `BG_COLOR` — macros that flip BLACK/WHITE when `inMeetingMode` is true
- During a meeting: black background, white text
- Otherwise: white background, black text

**Overview right panel layout:**
```
y=6:   label — "NEXT UP - in X min" / "NOW" / "!! N MIN !!"  (16px)
y=26:  meeting title                                          (48px)
y=82:  time range "HH:MM - HH:MM"                           (24px)
y=112: attendees                                              (16px, if any)
y=~:   description (up to 2 lines, newlines stripped)         (16px, if any)
───:   separator line (dynamic y, based on content)
       event[1] HH:MM-HH:MM title                            (16px)
       event[2] HH:MM-HH:MM title                            (16px)
```

**Left panel layout:**
```
y=6:   time (12h format)      (48px)
y=62:  AM/PM                  (24px)
y=94:  Day DD Mon             (24px)
y=130: OOO: (label)           (12px, only when someone is OOO)
y=146: name per line          (12px, up to 3 names)
y=255: upd HH:MM              (12px)
```

**OOO detection logic:**
- Scans all calendars except `@import.calendar.google.com`, `#holiday@`, `#contacts@`
- Matches `eventType == "outOfOffice"` OR title contains "OOO" or "PTO" (case-insensitive)
- Extracts person name from title: everything before the OOO/PTO keyword, trimmed of dashes/spaces
- e.g. `"Diego OOO"` → `"Diego"`, `"Lucas T - OOO"` → `"Lucas T"`

**Alert system:**
- `checkAlerts()` tracks which meetings have been alerted (by startMins, reset at midnight)
- At T-5min: blink screen 3× alternating black/white, show meeting info centered
- At T-1min: blink screen 5× — same but with "!! 1 MIN !!" label
- `ALERT_SCREEN_MS` (8000ms): how long the alert info screen stays visible

**Overview right panel bottom:**
- Density bar at y=258: 11 hourly blocks (8am–6pm), filled=busy, outlined=free. Uses existing `events[]` array.

**Three views + notification mode:**
- **Overview** — clock left, next meeting card + 2 upcoming events right + density bar
- **Detail** — 2 events at a time, large text, attendees, description; BTN_DOWN enters/scrolls, BTN_UP exits
- **Done** — static screen when `eventCount == 0` and past noon; re-renders if new events appear
- **Notification** (`inNotifMode`) — full-screen push notification; any button or 30s timeout dismisses

**Push notification flow:**
1. Mosquitto broker runs locally via Docker (`tools/mosquitto/docker-compose.yml`)
2. `tools/push.py "message"` publishes JSON `{title, body, source}` to `eink/push`
3. ESP32 receives via PubSubClient, sets `notifPending`, renders notification screen

**Font sizes available:** 12, 16, 24, 48 px (bitmap fonts in `EPDfont.h`). Each character is `size` px tall and `size/2` px wide.

## Git workflow

- After every `git push`, ask the user if they want to create a tag
- If yes: `git log <last-tag>..HEAD --oneline` → write changelog → `git tag -a vX.Y -m "..."` → `git push origin vX.Y`

## Roadmap

- **v1.5 (current):** MQTT push notifications, meeting density bar, done screen, WiFi backoff
- **v2:** partial refresh for smoother button scrolling
- **v3:** AI agent on Mac serves meeting context over local HTTP; ESP32 polls and displays summary per event
- **Future:** Slack/Gmail daemon → MQTT pipeline, OOO improvements
