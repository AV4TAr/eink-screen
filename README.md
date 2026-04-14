# E-ink Calendar Display

A Google Calendar agenda display for the **CrowPanel ESP32 5.79" E-paper HMI** (272×792, BW).

Shows current time, upcoming meetings, attendees, descriptions — and who on your team is OOO today. Refreshes every minute.

---

## Hardware

| Part | Detail |
|------|--------|
| Board | [CrowPanel ESP32-S3 5.79" E-Paper HMI](https://www.elecrow.com/crowpanel-esp32-5-79-e-paper-hmi-display-with-272-792-resolution-black-white-color-driven-by-spi-interface.html) |
| Display | 5.79" e-ink, 272×792 BW, SSD1683×2 |
| Interface | SPI |

---

## Display Layout

```
 ┌──────────────────────┬──────────────────────────────────────────────┐
 │  10:45               │  NEXT UP  -  in 4 min                        │
 │  AM                  │                                              │
 │  Tue 13 Apr          │  Team Standup                                │
 │                      │  10:30 - 11:00                               │
 │  OOO:                │  Alice, Bob, Carol                           │
 │  Lucas T             │  Weekly sync to discuss priorities and...    │
 │  Juan Leon           │ ─────────────────────────────────────────── │
 │                      │  11:00-12:00  1:1 with Manager               │
 │  upd 10:45           │  14:00-15:00  Sprint Planning                │
 └──────────────────────┴──────────────────────────────────────────────┘
```

**Left panel:** clock, AM/PM, date. OOO section appears only when someone is out.

**Right panel — Next Up card:** meeting title at 48px, time, attendee list, 2-line description snippet.

**Right panel — upcoming:** next 2 meetings at 16px.

**During a meeting:** full black screen with white text, title at 48px, block progress bar showing elapsed time in 10-min chunks.

**Alerts:** screen blinks at T-5min and T-1min before each meeting.

**Detail view:** press BTN_DOWN to see 2 meetings at a time with full attendee list and description.

---

## Quick Start

### 1. Install CH340 driver (macOS only)

The board uses a CH340 USB-UART chip. macOS needs a driver:

1. Download: [CH341SER_MAC from WCH](https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html)
2. Install `CH34xVCPDriver.pkg`
3. **System Settings → Privacy & Security → Allow** the extension
4. **Restart Mac**
5. Board will appear as `/dev/cu.wchusbserial...`

### 2. Arduino setup

- Install board: **esp32 by Espressif** 3.x via Board Manager
- Install library: **ArduinoJson** 7.x via Library Manager
- Board settings: `ESP32S3 Dev Module`, Flash `8MB`, PSRAM `OPI PSRAM`, Upload Speed `921600`

Or use **arduino-cli**:
```bash
arduino-cli compile -b esp32:esp32:esp32s3 \
  --board-options "FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,UploadSpeed=921600" \
  firmware/calendar_display/calendar_display.ino

arduino-cli upload -b esp32:esp32:esp32s3 \
  --board-options "FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,UploadSpeed=921600" \
  -p /dev/cu.wchusbserial2110 \
  firmware/calendar_display/calendar_display.ino
```

### 3. Google Calendar OAuth2 (one-time)

```bash
pip install google-auth-oauthlib
python3 tools/get_token.py client_secret.json
```

> Get `client_secret.json` from [Google Cloud Console](https://console.cloud.google.com):
> New project → **Enable Google Calendar API** → Credentials → OAuth 2.0 Client ID → Desktop app → Download JSON

The script opens your browser, you authorize, and it prints your secrets.

### 4. Configure

```bash
cp firmware/calendar_display/secrets.h.example firmware/calendar_display/secrets.h
# paste the output from get_token.py into secrets.h
```

Edit `firmware/calendar_display/config.h`:
```cpp
#define WIFI_SSID "your_network"
#define WIFI_PASS "your_password"
#define TIMEZONE_OFFSET_HOURS (-8)   // UTC offset (e.g. -8 for US Pacific)
#define DAYLIGHT_SAVING_HOURS (1)    // 1 if DST is active, 0 otherwise
```

### 5. Flash

Upload via Arduino IDE or arduino-cli (see step 2).

---

## Buttons

| Button | GPIO | Action |
|--------|------|--------|
| BTN_HOME | 2 | Force refresh |
| BTN_UP | 6 | Return to overview |
| BTN_DOWN | 4 | Enter / scroll detail view |

---

## Project Structure

```
firmware/
└── calendar_display/
    ├── calendar_display.ino   ← main sketch
    ├── config.h               ← all tuneable settings
    ├── secrets.h              ← Google OAuth tokens (gitignored)
    ├── secrets.h.example      ← template
    └── EPD*.cpp/h, spi.*      ← vendor display driver (do not modify)

tools/
└── get_token.py               ← one-time OAuth2 token helper
```

---

## Roadmap

| Version | Feature |
|---------|---------|
| **v1.2** (current) | Overview redesign, attendees + description on cards, OOO left panel, meeting alerts |
| v2 | Partial refresh for smoother button navigation |
| v3 | AI agent on Mac serves meeting context over local HTTP; device polls and shows per-meeting summary |
