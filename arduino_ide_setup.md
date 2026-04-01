# Arduino IDE Setup — CrowPanel ESP32 5.79" E-ink

## Board
**Tools → Board → esp32 → ESP32S3 Dev Module**

## Required Driver

The CrowPanel ESP32-S3 uses a **CH340 chip** for USB-UART. macOS requires a driver.

- Download: https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html
- Install `CH34xVCPDriver.pkg`
- **System Settings → Privacy & Security → Allow** the kernel extension
- **Restart Mac**
- After reboot the board appears as `/dev/cu.wchusbserial...`

## Required Settings (Tools menu)

| Setting | Value |
|---------|-------|
| Board | ESP32S3 Dev Module |
| Port | /dev/cu.wchusbserial1410 |
| USB CDC On Boot | **Disabled** |
| CPU Frequency | 240MHz (WiFi) |
| Core Debug Level | None |
| USB DFU On Boot | Disabled |
| Erase All Flash Before Sketch Upload | Disabled |
| Events Run On | Core 1 |
| Flash Mode | QIO 80MHz |
| Flash Size | **8MB (64Mb)** |
| JTAG Adapter | Disabled |
| Arduino Runs On | Core 1 |
| USB Firmware MSC On Boot | Disabled |
| Partition Scheme | Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS) |
| PSRAM | **OPI PSRAM** |
| Upload Mode | UART0 / Hardware CDC |
| Upload Speed | **115200** |
| USB Mode | Hardware CDC and JTAG |

## Libraries Required

| Library | Version | Install via |
|---------|---------|-------------|
| ArduinoJson by Benoit Blanchon | 7.x | Tools → Manage Libraries |

## Board Package Required

| Package | Install via |
|---------|-------------|
| esp32 by Espressif Systems 3.x | Tools → Board → Boards Manager |

## Flashing Tips

- The CH340 driver handles auto-reset — no manual BOOT+RESET needed
- Connect via USB hub or directly (both work with CH340)
- Port appears as `/dev/cu.wchusbserial1410` after driver install
- If port doesn't appear, check driver is installed and Mac was rebooted
