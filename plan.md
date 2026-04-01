# E-ink Calendar Display — Project Plan

## Hardware
- **Board:** CrowPanel ESP32-S3 (8MB Flash, 8MB PSRAM, 240MHz)
- **Display:** 5.79" e-ink, 272×792 BW, SSD1683×2 driver, SPI
- **Orientation:** Landscape (792×272), Rotation=180 in firmware
- **Pins:** SCK=12, MOSI=11, RES=47, DC=46, CS=45, BUSY=48, Power=GPIO7
- **Buttons:** HOME=IO2, EXIT=IO1, Rotary Up=IO6, Down=IO4, OK=IO5

---

## Versions

### v1 — MVP: Static Calendar Display ← current
**Goal:** Show current time + today's meetings (from 20 min ago to EOD), auto-refresh every 5 min.

**Stack:**
- Arduino (ESP32-S3)
- Google Calendar API v3 (OAuth2 with refresh token)
- NTP for time sync
- ArduinoJson 7.x for JSON parsing

**Layout (792×272 landscape):**
```
x=0                    x=192                                       x=792
 ┌──────────────────────┬──────────────────────────────────────────┐ y=0
 │  10:45               │  10:30-11:00  Team Standup  [NOW]        │
 │  AM                  │  11:00-12:00  1:1 with Manager           │
 │  Tue 31 Mar          │  14:00-15:00  Sprint Planning            │
 │                      │  15:30-16:00  Code Review                │
 │  upd 10:45           │                                          │
 └──────────────────────┴──────────────────────────────────────────┘ y=272
```
- Clock: size-48 font (48×24px per char)
- Date: size-16
- Events: size-16, up to 11 visible rows
- Current event: inverted (black background, white text)

**Files:**
```
firmware/calendar_display/
├── calendar_display.ino   ← main sketch
├── config.h               ← WiFi, NTP, timezone (edit this)
├── secrets.h              ← Google OAuth tokens (gitignored, you fill in)
├── secrets.h.example      ← template
├── EPD.cpp / EPD.h        ← display driver
├── EPD_Init.cpp / .h
├── EPDfont.h
└── spi.cpp / spi.h

tools/
└── get_token.py           ← one-time OAuth2 helper (run on your Mac)
```

**Setup steps:**
1. Install Arduino board: `esp32` by Espressif 3.x
2. Install library: `ArduinoJson` 7.x
3. Board settings: ESP32S3 Dev Module, 8MB Flash, OPI PSRAM
4. Run `pip install google-auth-oauthlib && python3 tools/get_token.py client_secret.json`
5. Paste tokens into `firmware/calendar_display/secrets.h`
6. Edit `config.h` with your WiFi + timezone
7. Flash via USB-C

---

### v2 — Button Navigation
**Goal:** Browse through meetings using hardware buttons without triggering a full display refresh each time.

**New behavior:**
- Rotary Down (IO4) / Up (IO6): scroll through event list
- HOME (IO2): jump to current/next event
- EXIT (IO1): return to overview
- Use partial refresh for smooth scrolling

**Changes from v1:**
- Add interrupt-driven button handling
- Track `selectedEvent` index
- Render a "detail view" for selected event (larger text, full title)
- Use `EPD_PartUpdate()` for button-triggered refreshes

---

### v3 — AI Agent Enrichment
**Goal:** Fetch contextual info about each meeting from an AI agent running on the user's computer.

**Architecture:**
- Agent runs on Mac (Python + Claude API)
- ESP32 polls a local HTTP endpoint (e.g., `http://192.168.x.x:8080/meeting-context`)
- Request: `{ "title": "Sprint Planning", "attendees": [...] }`
- Response: `{ "context": "3rd sprint of Q2, focus on auth migration" }` (≤200 chars)
- Display shows context below event title in detail view (v2 prerequisite)

**Agent capabilities:**
- Search Slack for recent discussion about the meeting
- Check Notion/Confluence for related docs
- Summarize recent email threads
- Return a short, display-friendly string

---

## Google Calendar OAuth2 Setup

1. Go to [console.cloud.google.com](https://console.cloud.google.com)
2. Create project → Enable **Google Calendar API**
3. Credentials → Create OAuth 2.0 Client ID → **Desktop app**
4. Download `client_secret.json`
5. Run: `python3 tools/get_token.py client_secret.json`
6. Browser opens → authorize → terminal prints your secrets
7. Paste into `firmware/calendar_display/secrets.h`
