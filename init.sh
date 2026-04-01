#!/bin/bash
# init.sh — Project setup script
# Run once after cloning: bash init.sh

set -e

echo "=== E-ink Calendar Display — Setup ==="

# ── 1. Clone vendor repo ──────────────────────────────────────────────
if [ ! -d "repos/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792" ]; then
  echo "[1/4] Cloning CrowPanel reference repo..."
  mkdir -p repos
  git clone https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792 \
    repos/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792
else
  echo "[1/4] CrowPanel repo already present, skipping."
fi

# ── 2. Python dependencies ────────────────────────────────────────────
echo "[2/4] Installing Python dependencies..."
pip3 install google-auth-oauthlib 2>/dev/null || \
pip install google-auth-oauthlib 2>/dev/null || \
echo "  Warning: could not install google-auth-oauthlib. Run tools/get_token.py with the stdlib version instead."

# ── 3. secrets.h ─────────────────────────────────────────────────────
if [ ! -f "firmware/calendar_display/secrets.h" ]; then
  echo "[3/4] Creating secrets.h from template..."
  cp firmware/calendar_display/secrets.h.example firmware/calendar_display/secrets.h
  echo "  --> Edit firmware/calendar_display/secrets.h with your Google credentials."
  echo "      Run: python3 tools/get_token.py path/to/client_secret.json"
else
  echo "[3/4] secrets.h already exists, skipping."
fi

# ── 4. CH340 driver check ─────────────────────────────────────────────
echo "[4/5] Checking CH340 driver (required for CrowPanel USB)..."
if ls /dev/cu.wchusbserial* &>/dev/null || ls /dev/cu.usbserial* &>/dev/null; then
  echo "  CH340 driver appears to be installed."
elif system_profiler SPExtensionsDataType 2>/dev/null | grep -qi "ch34"; then
  echo "  CH340 driver is installed."
else
  echo "  WARNING: CH340 driver may not be installed."
  echo "  Download from: https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html"
  echo "  Install, allow in Privacy & Security, then restart Mac."
fi

# ── 5. Arduino IDE reminder ───────────────────────────────────────────
echo "[5/5] Arduino IDE setup required:"
echo "  - Install board:   esp32 by Espressif Systems 3.x (Boards Manager)"
echo "  - Install library: ArduinoJson 7.x (Library Manager)"
echo "  - See arduino_ide_setup.md for full board settings."

echo ""
echo "=== Done! Next steps ==="
echo "  1. Run: python3 tools/get_token.py ~/Downloads/client_secret.json"
echo "  2. Edit: firmware/calendar_display/config.h  (WiFi + timezone)"
echo "  3. Flash: open firmware/calendar_display/calendar_display.ino in Arduino IDE"
