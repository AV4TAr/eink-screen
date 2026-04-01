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

# ── 4. Arduino IDE reminder ───────────────────────────────────────────
echo "[4/4] Arduino IDE setup required:"
echo "  - Install board:   esp32 by Espressif Systems 3.x (Boards Manager)"
echo "  - Install library: ArduinoJson 7.x (Library Manager)"
echo "  - See arduino_ide_setup.md for full board settings."

echo ""
echo "=== Done! Next steps ==="
echo "  1. Run: python3 tools/get_token.py ~/Downloads/client_secret.json"
echo "  2. Edit: firmware/calendar_display/config.h  (WiFi + timezone)"
echo "  3. Flash: open firmware/calendar_display/calendar_display.ino in Arduino IDE"
