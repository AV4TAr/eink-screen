#!/usr/bin/env python3
"""
One-time Google Calendar OAuth2 token helper.
No external dependencies beyond the standard library + requests.

Usage:
    python3 get_token.py path/to/client_secret.json

How to get client_secret.json:
    1. Go to https://console.cloud.google.com
    2. Create or select a project
    3. Enable the Google Calendar API
    4. Go to Credentials → Create OAuth 2.0 Client ID
    5. Application type: Desktop app
    6. Download the JSON file
"""

import sys
import json
import urllib.request
import urllib.parse
import webbrowser
import http.server
import threading
import ssl

SCOPE = "https://www.googleapis.com/auth/calendar.readonly"
REDIRECT_PORT = 8765
REDIRECT_URI = f"http://localhost:{REDIRECT_PORT}/callback"

auth_code = None


class CallbackHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        global auth_code
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)
        auth_code = params.get("code", [None])[0]
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.end_headers()
        self.wfile.write(b"<h1>Authorized! You can close this tab.</h1>")

    def log_message(self, *args):
        pass  # silence request logs


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    with open(sys.argv[1]) as f:
        raw = json.load(f)

    # client_secret.json can have a "web" or "installed" key
    creds = raw.get("installed") or raw.get("web")
    if not creds:
        print("ERROR: Unexpected client_secret.json format.")
        sys.exit(1)

    client_id     = creds["client_id"]
    client_secret = creds["client_secret"]

    # ── Step 1: open browser for user authorization ──────────────────
    params = urllib.parse.urlencode({
        "client_id":     client_id,
        "redirect_uri":  REDIRECT_URI,
        "response_type": "code",
        "scope":         SCOPE,
        "access_type":   "offline",
        "prompt":        "consent",  # force refresh_token to be returned
    })
    auth_url = "https://accounts.google.com/o/oauth2/v2/auth?" + params

    # Start local callback server in a background thread
    server = http.server.HTTPServer(("localhost", REDIRECT_PORT), CallbackHandler)
    thread = threading.Thread(target=server.handle_request)
    thread.start()

    print(f"\nOpening browser for authorization...")
    webbrowser.open(auth_url)
    print("Waiting for Google to redirect back...")
    thread.join(timeout=120)

    if not auth_code:
        print("ERROR: Did not receive authorization code within 2 minutes.")
        sys.exit(1)

    # ── Step 2: exchange code for tokens ────────────────────────────
    token_data = urllib.parse.urlencode({
        "code":          auth_code,
        "client_id":     client_id,
        "client_secret": client_secret,
        "redirect_uri":  REDIRECT_URI,
        "grant_type":    "authorization_code",
    }).encode()

    req = urllib.request.Request(
        "https://oauth2.googleapis.com/token",
        data=token_data,
        method="POST",
    )
    req.add_header("Content-Type", "application/x-www-form-urlencoded")

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    with urllib.request.urlopen(req, context=ctx) as resp:
        tokens = json.loads(resp.read())

    refresh_token = tokens.get("refresh_token")
    if not refresh_token:
        print("ERROR: No refresh_token in response. Make sure the OAuth consent")
        print("screen is configured and you used 'prompt=consent'.")
        print("Full response:", tokens)
        sys.exit(1)

    # ── Output ───────────────────────────────────────────────────────
    print()
    print("=" * 60)
    print("Copy these into firmware/calendar_display/secrets.h")
    print("=" * 60)
    print("#pragma once")
    print()
    print(f'#define GOOGLE_CLIENT_ID     "{client_id}"')
    print(f'#define GOOGLE_CLIENT_SECRET "{client_secret}"')
    print(f'#define GOOGLE_REFRESH_TOKEN "{refresh_token}"')
    print("=" * 60)


if __name__ == "__main__":
    main()
