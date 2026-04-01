# Arduino IDE Setup — CrowPanel ESP32 5.79" E-ink

## Board
**Tools → Board → esp32 → ESP32S3 Dev Module**

## Required Settings (Tools menu)

| Setting | Value |
|---------|-------|
| Board | ESP32S3 Dev Module |
| Port | /dev/cu.usbmodemSN234567892 |
| USB CDC On Boot | **Enabled** |
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

- Connect via USB hub (direct USB-C to Mac doesn't work for this board)
- If upload fails with "No serial data received":
  1. Click Upload
  2. When `Connecting......` appears, press and hold BOOT, tap RESET, release BOOT
- **USB CDC On Boot must be Enabled** for the port to be recognized properly
