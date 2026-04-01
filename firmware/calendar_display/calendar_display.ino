/*
 * E-ink Google Calendar Display
 * CrowPanel ESP32 5.79" (272×792 BW)
 *
 * Shows current time + today's meetings (from 20 min ago to EOD).
 * Refreshes every 5 minutes via Google Calendar API v3.
 *
 * Edit config.h for WiFi, timezone, and refresh settings.
 * Edit secrets.h for Google OAuth credentials (run tools/get_token.py first).
 */

#include <Arduino.h>
#include "EPD.h"
#include "config.h"
#include "secrets.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

// ── Display buffer ────────────────────────────────────────────────────
uint8_t ImageBW[EPD_W * EPD_H / 8];  // 800×272 / 8 = 27200 bytes

// ── Event storage ─────────────────────────────────────────────────────
struct CalEvent {
  char title[64];
  int  startHour, startMin;
  int  endHour,   endMin;
  bool allDay;
};

CalEvent events[MAX_EVENTS];
int      eventCount   = 0;
char     lastUpdated[8] = "--:--";

unsigned long lastRefreshMs = 0;
bool          timeReady     = false;

// ── Forward declarations ──────────────────────────────────────────────
void connectWiFi();
String getAccessToken();
bool fetchEvents(const String& token);
void renderDisplay();
void showMessage(const char* line1, const char* line2 = nullptr);
void parseHourMin(const char* dt, int* h, int* m);
String urlEncodeTime(time_t t);


// ─────────────────────────────────────────────────────────────────────
// setup
// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Power on display
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
  delay(100);

  // Init display canvas (clear to white)
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  EPD_FastMode1Init();
  EPD_Display_Clear();
  EPD_Update();

  showMessage("Connecting to WiFi...");
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    showMessage("Syncing time...");
    configTime(
      (long)TIMEZONE_OFFSET_HOURS * 3600L,
      (long)DAYLIGHT_SAVING_HOURS * 3600L,
      NTP_SERVER
    );
    // Wait up to 6 seconds for NTP sync
    time_t now = 0;
    for (int i = 0; i < 30 && now < 100000; i++) {
      delay(200);
      time(&now);
    }
    timeReady = (now > 100000);

    showMessage("Fetching calendar...");
    String token = getAccessToken();
    if (token.length() > 0) fetchEvents(token);
  } else {
    showMessage("WiFi failed.", "Check config.h");
    delay(3000);
  }

  renderDisplay();
  lastRefreshMs = millis();
}


// ─────────────────────────────────────────────────────────────────────
// loop
// ─────────────────────────────────────────────────────────────────────
void loop() {
  if (millis() - lastRefreshMs >= REFRESH_INTERVAL_MS) {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();

    if (WiFi.status() == WL_CONNECTED) {
      String token = getAccessToken();
      if (token.length() > 0) fetchEvents(token);
    }

    renderDisplay();
    lastRefreshMs = millis();
  }
}


// ─────────────────────────────────────────────────────────────────────
// WiFi
// ─────────────────────────────────────────────────────────────────────
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
  }
  Serial.print("WiFi: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "connected" : "FAILED");
}


// ─────────────────────────────────────────────────────────────────────
// Google OAuth2: exchange refresh token → access token
// ─────────────────────────────────────────────────────────────────────
String getAccessToken() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, "https://oauth2.googleapis.com/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setTimeout(10000);

  String body = "client_id=";     body += GOOGLE_CLIENT_ID;
  body += "&client_secret=";      body += GOOGLE_CLIENT_SECRET;
  body += "&refresh_token=";      body += GOOGLE_REFRESH_TOKEN;
  body += "&grant_type=refresh_token";

  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("Token error HTTP %d\n", code);
    http.end();
    return "";
  }

  String resp = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, resp) != DeserializationError::Ok) return "";
  return doc["access_token"].as<String>();
}


// ─────────────────────────────────────────────────────────────────────
// Parse HH and MM from RFC3339 "YYYY-MM-DDTHH:MM:SS±HH:MM"
// ─────────────────────────────────────────────────────────────────────
void parseHourMin(const char* dt, int* h, int* m) {
  if (!dt || strlen(dt) < 16) { *h = 0; *m = 0; return; }
  *h = (dt[11] - '0') * 10 + (dt[12] - '0');
  *m = (dt[14] - '0') * 10 + (dt[15] - '0');
}


// ─────────────────────────────────────────────────────────────────────
// URL-encode colons in an RFC3339 UTC timestamp
// ─────────────────────────────────────────────────────────────────────
String urlEncodeTime(time_t t) {
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
  String s = String(buf);
  s.replace(":", "%3A");
  return s;
}


// ─────────────────────────────────────────────────────────────────────
// Fetch today's calendar events
// ─────────────────────────────────────────────────────────────────────
bool fetchEvents(const String& token) {
  time_t now;
  time(&now);

  // timeMin = now - PAST_WINDOW_MINUTES (UTC)
  time_t tMin = now - (long)PAST_WINDOW_MINUTES * 60L;

  // timeMax = end of today in local time, converted to UTC epoch
  struct tm tEnd = *localtime(&now);
  tEnd.tm_hour = 23; tEnd.tm_min = 59; tEnd.tm_sec = 59;
  time_t tMax = mktime(&tEnd);

  String url = "https://www.googleapis.com/calendar/v3/calendars/";
  url += CALENDAR_ID;
  url += "/events?singleEvents=true&orderBy=startTime";
  url += "&maxResults=" + String(MAX_EVENTS);
  url += "&timeMin=" + urlEncodeTime(tMin);
  url += "&timeMax=" + urlEncodeTime(tMax);
  url += "&fields=items(summary,start,end)";  // only fetch what we need

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + token);
  http.setTimeout(10000);

  int code = http.GET();
  if (code != 200) {
    Serial.printf("Calendar error HTTP %d\n", code);
    http.end();
    return false;
  }

  // Use HTTP/1.0 to avoid chunked transfer, then stream-parse JSON
  String resp = http.getString();
  http.end();

  if (resp.length() == 0) {
    Serial.println("Empty calendar response");
    return false;
  }
  Serial.printf("Calendar response: %d bytes\n", resp.length());

  JsonDocument filter;
  filter["items"][0]["summary"] = true;
  filter["items"][0]["start"]["dateTime"] = true;
  filter["items"][0]["start"]["date"] = true;
  filter["items"][0]["end"]["dateTime"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp, DeserializationOption::Filter(filter));
  resp = "";  // free memory immediately after parsing

  if (err != DeserializationError::Ok) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return false;
  }

  eventCount = 0;
  JsonArray items = doc["items"].as<JsonArray>();
  for (JsonObject item : items) {
    if (eventCount >= MAX_EVENTS) break;
    CalEvent& ev = events[eventCount];

    const char* title = item["summary"] | "(No title)";
    strncpy(ev.title, title, 63);
    ev.title[63] = '\0';

    const char* startDT   = item["start"]["dateTime"] | "";
    const char* startDate = item["start"]["date"]     | "";
    const char* endDT     = item["end"]["dateTime"]   | "";

    if (strlen(startDT) > 0) {
      ev.allDay = false;
      parseHourMin(startDT, &ev.startHour, &ev.startMin);
      parseHourMin(endDT,   &ev.endHour,   &ev.endMin);
    } else {
      ev.allDay = true;
      ev.startHour = 0; ev.startMin = 0;
      ev.endHour   = 23; ev.endMin  = 59;
    }

    Serial.printf("Event: %s  %02d:%02d-%02d:%02d  allDay=%d\n",
      ev.title, ev.startHour, ev.startMin, ev.endHour, ev.endMin, ev.allDay);
    eventCount++;
  }

  return true;
}


// ─────────────────────────────────────────────────────────────────────
// Render the display
//
// Layout (792×272 landscape, Rotation=180):
//
//  x=0          x=192 x=200                                    x=792
//   ┌─────────────────┬──────────────────────────────────────────┐ y=0
//   │  10:45          │  10:30-11:00  Team Standup  (inverted)   │
//   │  AM             │  11:00-12:00  1:1 with Manager           │
//   │  Tue 31 Mar     │  14:00-15:00  Sprint Planning            │
//   │                 │  ...                                     │
//   │  upd 10:45      │                                          │
//   └─────────────────┴──────────────────────────────────────────┘ y=272
//
// ─────────────────────────────────────────────────────────────────────
void renderDisplay() {
  time_t now;
  time(&now);
  struct tm* t = localtime(&now);

  snprintf(lastUpdated, sizeof(lastUpdated), "%02d:%02d", t->tm_hour, t->tm_min);

  // ── Build frame buffer ──────────────────────────────────────────────
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);

  // ── Left panel: clock + date ────────────────────────────────────────
  //  Clock (size 48: chars are 24px wide, 48px tall)
  int hr12 = t->tm_hour % 12;
  if (hr12 == 0) hr12 = 12;
  char timeStr[8];
  snprintf(timeStr, sizeof(timeStr), "%d:%02d", hr12, t->tm_min);
  EPD_ShowString(10, 6, timeStr, 48, BLACK);        // y=6..53

  // AM/PM (size 24: chars 12px wide, 24px tall)
  EPD_ShowString(10, 62, t->tm_hour < 12 ? "AM" : "PM", 24, BLACK);  // y=62..85

  // Day + date (size 16: chars 8px wide, 16px tall)
  const char* days[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                           "Jul","Aug","Sep","Oct","Nov","Dec"};
  char dateStr[20];
  snprintf(dateStr, sizeof(dateStr), "%s %d %s",
           days[t->tm_wday], t->tm_mday, months[t->tm_mon]);
  EPD_ShowString(10, 94, dateStr, 16, BLACK);        // y=94..109

  // Last updated (size 12: chars 6px wide, 12px tall)
  char updStr[16];
  snprintf(updStr, sizeof(updStr), "upd %s", lastUpdated);
  EPD_ShowString(10, 255, updStr, 12, BLACK);

  // ── Vertical divider ────────────────────────────────────────────────
  EPD_DrawLine(192, 0, 192, 271, BLACK);

  // ── Right panel: event list ─────────────────────────────────────────
  const int evX  = 200;   // left edge of event area
  const int evW  = 575;   // width available (575 / 8 = 71 chars at size 16)
  const int rowH = 22;    // row height: 16px text + 6px gap
  int evY = 6;

  int nowMins = t->tm_hour * 60 + t->tm_min;

  for (int i = 0; i < eventCount && evY + 16 <= 266; i++) {
    CalEvent& ev = events[i];

    bool isNow = false;
    char line[80];

    if (ev.allDay) {
      snprintf(line, sizeof(line), "All day  %.60s", ev.title);
    } else {
      int startMins = ev.startHour * 60 + ev.startMin;
      int endMins   = ev.endHour   * 60 + ev.endMin;
      isNow = (nowMins >= startMins && nowMins < endMins);

      // "10:30-11:00 Title..."
      char timeLabel[14];
      snprintf(timeLabel, sizeof(timeLabel), "%02d:%02d-%02d:%02d",
               ev.startHour, ev.startMin, ev.endHour, ev.endMin);

      // Truncate title to fit: 71 - 11 (timeLabel+space) = 60 chars
      char titlePart[61];
      strncpy(titlePart, ev.title, 60);
      titlePart[60] = '\0';

      snprintf(line, sizeof(line), "%s %s", timeLabel, titlePart);
    }

    if (isNow) {
      // Inverted highlight for current event
      EPD_DrawRectangle(evX - 2, evY - 2, evX + evW, evY + 17, BLACK, 1);
      EPD_ShowString(evX, evY, line, 16, WHITE);
    } else {
      EPD_ShowString(evX, evY, line, 16, BLACK);
    }

    evY += rowH;
  }

  if (eventCount == 0) {
    EPD_ShowString(evX, 120, "No events for today", 16, BLACK);
  }

  // ── Push frame buffer to display ────────────────────────────────────
  EPD_GPIOInit();
  EPD_FastMode1Init();
  EPD_Display(ImageBW);
  EPD_FastUpdate();
  EPD_DeepSleep();
}


// ─────────────────────────────────────────────────────────────────────
// Show a simple 1- or 2-line status message (used during boot)
// ─────────────────────────────────────────────────────────────────────
void showMessage(const char* line1, const char* line2) {
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  EPD_ShowString(10, 110, line1, 24, BLACK);
  if (line2) EPD_ShowString(10, 142, line2, 24, BLACK);
  EPD_GPIOInit();
  EPD_FastMode1Init();
  EPD_Display(ImageBW);
  EPD_FastUpdate();
  EPD_DeepSleep();
}
