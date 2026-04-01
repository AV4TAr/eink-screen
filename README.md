# E-ink Calendar Display

A Google Calendar agenda display for the **CrowPanel ESP32 5.79" E-paper HMI** (272×792, BW).

Shows current time and today's meetings — from 20 minutes ago through end of day — refreshing every 5 minutes.

---

## Hardware

| Part | Detail |
|------|--------|
| Board | CrowPanel ESP32-S3 (8MB Flash, 8MB PSRAM) |
| Display | 5.79" e-ink, 272×792 BW, SSD1683×2 |
| Interface | SPI |

---

## Quick Start

### 1. Install CH340 driver (macOS only, required)

The board uses a CH340 USB-UART chip. macOS needs a driver:

1. Download: [CH341SER_MAC from WCH](https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html)
2. Install `CH34xVCPDriver.pkg`
3. **System Settings → Privacy & Security → Allow** the extension
4. **Restart Mac**
5. Board will appear as `/dev/cu.wchusbserial...`

### 2. Arduino IDE setup

- Install board: **esp32 by Espressif** 3.x via Board Manager
- Install library: **ArduinoJson** 7.x via Library Manager
- Board settings:
  - Board: `ESP32S3 Dev Module`
  - Flash Size: `8MB`
  - PSRAM: `OPI PSRAM`
  - Upload Speed: `921600`

### 2. Google Calendar OAuth2 (one-time)

```bash
pip install google-auth-oauthlib
python3 tools/get_token.py client_secret.json
```

> Get `client_secret.json` from [Google Cloud Console](https://console.cloud.google.com):
> New project → Enable Calendar API → Credentials → OAuth 2.0 Client ID → Desktop app → Download JSON

The script opens your browser, you authorize, and it prints your secrets.

### 3. Configure

Copy and fill in the secrets file:
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

### 4. Flash

Open `firmware/calendar_display/calendar_display.ino` in Arduino IDE and upload via USB-C.

---

## Display Layout

```
 ┌──────────────────────┬──────────────────────────────────────────┐
 │  10:45               │  10:30-11:00  Team Standup  [NOW]        │
 │  AM                  │  11:00-12:00  1:1 with Manager           │
 │  Tue 31 Mar          │  14:00-15:00  Sprint Planning            │
 │                      │  15:30-16:00  Code Review                │
 │  upd 10:45           │                                          │
 └──────────────────────┴──────────────────────────────────────────┘
```

- Current meeting shown with **inverted colors** (white text on black)
- Up to 11 events visible
- All-day events shown at top of list

---

## Project Structure

```
firmware/
└── calendar_display/
    ├── calendar_display.ino   ← main sketch
    ├── config.h               ← WiFi, NTP, timezone settings
    ├── secrets.h              ← Google OAuth tokens (gitignored)
    ├── secrets.h.example      ← template
    └── EPD*.cpp/h, spi.*      ← display driver

tools/
└── get_token.py               ← one-time OAuth2 token helper

plan.md                        ← full roadmap (v1 → v3)
```

---

## Roadmap

| Version | Feature |
|---------|---------|
| **v1** (current) | Static display, auto-refresh every 5 min |
| v2 | Browse meetings with hardware buttons |
| v3 | AI agent enrichment — context per meeting |

See [`plan.md`](plan.md) for details.
