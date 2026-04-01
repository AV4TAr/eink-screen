/*
 * E-ink Google Calendar Display v1.5
 * CrowPanel ESP32 5.79" (272×792 BW)
 *
 * Shows current time + Google Calendar events (now → now+4h).
 * Two views:
 *   Overview — clock + event list (auto-refreshes every 5 min)
 *   Detail   — 2 meetings at a time with larger text (button-driven)
 *
 * Buttons:
 *   Rotary Down (IO4) — enter detail view / scroll to next meeting
 *   Rotary Up   (IO6) — return to overview
 *   HOME        (IO2) — force refresh from Google Calendar
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
  char description[128];
  char attendees[128];  // comma-separated names
  int  startHour, startMin;
  int  endHour,   endMin;
  bool allDay;
};

CalEvent events[MAX_EVENTS];
int      eventCount   = 0;
char     lastUpdated[8] = "--:--";

// ── View state ────────────────────────────────────────────────────────
#define VIEW_OVERVIEW 0
#define VIEW_DETAIL   1
int viewMode    = VIEW_OVERVIEW;
int scrollIndex = 0;  // first event shown in detail view

// ── Timing ────────────────────────────────────────────────────────────
unsigned long lastRefreshMs  = 0;
unsigned long lastButtonMs   = 0;
bool          timeReady      = false;

// ── Forward declarations ──────────────────────────────────────────────
void connectWiFi();
String getAccessToken();
bool fetchEvents(const String& token);
void refreshData();
void renderDisplay();
void renderOverview();
void renderDetail();
void pushToDisplay();
void showMessage(const char* line1, const char* line2 = nullptr);
void parseHourMin(const char* dt, int* h, int* m);
String urlEncodeTime(time_t t);
void handleButtons();


// ─────────────────────────────────────────────────────────────────────
// setup
// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Power on display
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
  delay(100);

  // Button pins (active LOW with internal pull-up)
  pinMode(BTN_HOME, INPUT_PULLUP);
  pinMode(BTN_UP,   INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK,   INPUT_PULLUP);
  pinMode(BTN_EXIT, INPUT_PULLUP);

  // Init display canvas
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
    time_t now = 0;
    for (int i = 0; i < 30 && now < 100000; i++) {
      delay(200);
      time(&now);
    }
    timeReady = (now > 100000);

    showMessage("Fetching calendar...");
    refreshData();
  } else {
    showMessage("WiFi failed.", "Check config.h");
    delay(3000);
  }

  renderDisplay();
  lastRefreshMs = millis();
}


// ─────────────────────────────────────────────────────────────────────
// loop — button polling + periodic refresh
// ─────────────────────────────────────────────────────────────────────
void loop() {
  handleButtons();

  if (millis() - lastRefreshMs >= REFRESH_INTERVAL_MS) {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (WiFi.status() == WL_CONNECTED) refreshData();
    viewMode = VIEW_OVERVIEW;
    scrollIndex = 0;
    renderDisplay();
    lastRefreshMs = millis();
  }

  delay(30);  // ~33 Hz button poll rate
}


// ─────────────────────────────────────────────────────────────────────
// Button handling
// ─────────────────────────────────────────────────────────────────────
void handleButtons() {
  if (millis() - lastButtonMs < BUTTON_DEBOUNCE_MS) return;

  bool changed = false;

  // DOWN — enter detail view or scroll forward
  if (digitalRead(BTN_DOWN) == LOW) {
    lastButtonMs = millis();
    if (viewMode == VIEW_OVERVIEW) {
      viewMode = VIEW_DETAIL;
      scrollIndex = 0;
      changed = true;
    } else if (scrollIndex + 1 < eventCount) {
      scrollIndex++;
      changed = true;
    }
  }

  // UP — back to overview
  if (digitalRead(BTN_UP) == LOW) {
    lastButtonMs = millis();
    if (viewMode == VIEW_DETAIL) {
      viewMode = VIEW_OVERVIEW;
      scrollIndex = 0;
      changed = true;
    }
  }

  // HOME — force refresh
  if (digitalRead(BTN_HOME) == LOW) {
    lastButtonMs = millis();
    showMessage("Refreshing...");
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (WiFi.status() == WL_CONNECTED) refreshData();
    viewMode = VIEW_OVERVIEW;
    scrollIndex = 0;
    changed = true;
  }

  if (changed) renderDisplay();
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
// Refresh data from Google Calendar
// ─────────────────────────────────────────────────────────────────────
void refreshData() {
  String token = getAccessToken();
  if (token.length() > 0) fetchEvents(token);
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
// Fetch calendar events (now-20min → now+4h)
// ─────────────────────────────────────────────────────────────────────
bool fetchEvents(const String& token) {
  time_t now;
  time(&now);

  time_t tMin = now - (long)PAST_WINDOW_MINUTES * 60L;
  time_t tMax = now + (long)LOOKAHEAD_HOURS * 3600L;

  String url = "https://www.googleapis.com/calendar/v3/calendars/";
  url += CALENDAR_ID;
  url += "/events?singleEvents=true&orderBy=startTime";
  url += "&maxResults=" + String(MAX_EVENTS);
  url += "&timeMin=" + urlEncodeTime(tMin);
  url += "&timeMax=" + urlEncodeTime(tMax);
  url += "&fields=items(summary,description,start,end,attendees/displayName)";

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

  String resp = http.getString();
  http.end();

  if (resp.length() == 0) {
    Serial.println("Empty calendar response");
    return false;
  }
  Serial.printf("Calendar response: %d bytes\n", resp.length());

  JsonDocument filter;
  filter["items"][0]["summary"] = true;
  filter["items"][0]["description"] = true;
  filter["items"][0]["start"]["dateTime"] = true;
  filter["items"][0]["start"]["date"] = true;
  filter["items"][0]["end"]["dateTime"] = true;
  filter["items"][0]["attendees"][0]["displayName"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp, DeserializationOption::Filter(filter));
  resp = "";

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

    const char* desc = item["description"] | "";
    strncpy(ev.description, desc, 127);
    ev.description[127] = '\0';

    // Build attendees string from array of displayName
    ev.attendees[0] = '\0';
    JsonArray att = item["attendees"].as<JsonArray>();
    int attLen = 0;
    for (JsonObject a : att) {
      const char* name = a["displayName"] | "";
      if (strlen(name) == 0) continue;
      if (attLen > 0 && attLen + 2 < 127) {
        ev.attendees[attLen++] = ',';
        ev.attendees[attLen++] = ' ';
      }
      int nameLen = strlen(name);
      if (attLen + nameLen >= 127) break;
      memcpy(ev.attendees + attLen, name, nameLen);
      attLen += nameLen;
    }
    ev.attendees[attLen] = '\0';

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
// Render dispatcher
// ─────────────────────────────────────────────────────────────────────
void renderDisplay() {
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);

  if (viewMode == VIEW_DETAIL && eventCount > 0)
    renderDetail();
  else
    renderOverview();

  pushToDisplay();
}

void pushToDisplay() {
  EPD_GPIOInit();
  EPD_FastMode1Init();
  EPD_Display(ImageBW);
  EPD_FastUpdate();
  EPD_DeepSleep();
}


// ─────────────────────────────────────────────────────────────────────
// Overview: clock on left, event list on right
//
//  x=0          x=192 x=200                                    x=792
//   ┌─────────────────┬──────────────────────────────────────────┐ y=0
//   │  10:45          │  10:30-11:00  Team Standup  (inverted)   │
//   │  AM             │  11:00-12:00  1:1 with Manager           │
//   │  Tue 31 Mar     │  14:00-15:00  Sprint Planning            │
//   │                 │  ...                                     │
//   │  upd 10:45      │  [v] detail                              │
//   └─────────────────┴──────────────────────────────────────────┘ y=271
//
// ─────────────────────────────────────────────────────────────────────
void renderOverview() {
  time_t now;
  time(&now);
  struct tm* t = localtime(&now);

  snprintf(lastUpdated, sizeof(lastUpdated), "%02d:%02d", t->tm_hour, t->tm_min);

  // ── Left panel ──────────────────────────────────────────────────────
  int hr12 = t->tm_hour % 12;
  if (hr12 == 0) hr12 = 12;
  char timeStr[8];
  snprintf(timeStr, sizeof(timeStr), "%d:%02d", hr12, t->tm_min);
  EPD_ShowString(10, 6, timeStr, 48, BLACK);

  EPD_ShowString(10, 62, t->tm_hour < 12 ? "AM" : "PM", 24, BLACK);

  const char* days[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                           "Jul","Aug","Sep","Oct","Nov","Dec"};
  char dateStr[20];
  snprintf(dateStr, sizeof(dateStr), "%s %d %s",
           days[t->tm_wday], t->tm_mday, months[t->tm_mon]);
  EPD_ShowString(10, 94, dateStr, 16, BLACK);

  char updStr[16];
  snprintf(updStr, sizeof(updStr), "upd %s", lastUpdated);
  EPD_ShowString(10, 255, updStr, 12, BLACK);

  // ── Divider ─────────────────────────────────────────────────────────
  EPD_DrawLine(192, 0, 192, 271, BLACK);

  // ── Right panel: event list ─────────────────────────────────────────
  const int evX  = 200;
  const int evW  = 575;
  const int rowH = 22;
  int evY = 6;
  int nowMins = t->tm_hour * 60 + t->tm_min;

  for (int i = 0; i < eventCount && evY + 16 <= 255; i++) {
    CalEvent& ev = events[i];
    bool isNow = false;
    char line[80];

    if (ev.allDay) {
      snprintf(line, sizeof(line), "All day  %.60s", ev.title);
    } else {
      int startMins = ev.startHour * 60 + ev.startMin;
      int endMins   = ev.endHour   * 60 + ev.endMin;
      isNow = (nowMins >= startMins && nowMins < endMins);

      char timeLabel[14];
      snprintf(timeLabel, sizeof(timeLabel), "%02d:%02d-%02d:%02d",
               ev.startHour, ev.startMin, ev.endHour, ev.endMin);

      char titlePart[61];
      strncpy(titlePart, ev.title, 60);
      titlePart[60] = '\0';
      snprintf(line, sizeof(line), "%s %s", timeLabel, titlePart);
    }

    if (isNow) {
      EPD_DrawRectangle(evX - 2, evY - 2, evX + evW, evY + 17, BLACK, 1);
      EPD_ShowString(evX, evY, line, 16, WHITE);
    } else {
      EPD_ShowString(evX, evY, line, 16, BLACK);
    }
    evY += rowH;
  }


  if (eventCount == 0) {
    EPD_ShowString(evX, 120, "No events ahead", 16, BLACK);
  }

  if (eventCount > 0) {
    EPD_ShowString(680, 255, "[v]more", 12, BLACK);
  }
}


// ─────────────────────────────────────────────────────────────────────
// Detail view: 2 meetings at a time with larger text
//
//  x=0                                                           x=792
//   ┌────────────────────────────────────────────────────────────────┐ y=0
//   │  NOW                                              1 of 4      │ y=8
//   │  Sprint Planning                                              │ y=28
//   │  11:30 - 12:00                                                │ y=58
//   │  ─────────────────────────────────────────────────────────    │ y=82
//   │  NEXT                                             2 of 4      │ y=96
//   │  Code Review                                                  │ y=116
//   │  14:00 - 15:00                                                │ y=146
//   │                                                               │
//   │  [^] overview   [v] next                                      │ y=255
//   └────────────────────────────────────────────────────────────────┘ y=271
//
// ─────────────────────────────────────────────────────────────────────
void renderDetail() {
  time_t now;
  time(&now);
  struct tm* t = localtime(&now);
  int nowMins = t->tm_hour * 60 + t->tm_min;

  // Show 2 events starting at scrollIndex
  for (int slot = 0; slot < 2; slot++) {
    int idx = scrollIndex + slot;
    if (idx >= eventCount) break;

    CalEvent& ev = events[idx];
    int baseY = slot * 132 + 4;  // slot 0: y=4, slot 1: y=136

    // ── Label: NOW / NEXT / upcoming + index ──────────────────────────
    bool isNow = false;
    if (!ev.allDay) {
      int startMins = ev.startHour * 60 + ev.startMin;
      int endMins   = ev.endHour   * 60 + ev.endMin;
      isNow = (nowMins >= startMins && nowMins < endMins);
    }

    const char* label;
    if (isNow) label = "NOW";
    else if (slot == 0 && scrollIndex == 0) label = "NEXT";
    else label = "UPCOMING";

    EPD_ShowString(15, baseY, label, 16, BLACK);
    char idxStr[12];
    snprintf(idxStr, sizeof(idxStr), "%d of %d", idx + 1, eventCount);
    int idxX = 780 - (int)strlen(idxStr) * 6;
    if (idxX < 600) idxX = 600;
    EPD_ShowString(idxX, baseY, idxStr, 12, BLACK);

    // ── Title (size 24, up to 32 chars) ───────────────────────────────
    char titleLine[33];
    strncpy(titleLine, ev.title, 32);
    titleLine[32] = '\0';
    EPD_ShowString(15, baseY + 20, titleLine, 24, BLACK);

    // ── Time range (size 16) ──────────────────────────────────────────
    char timeLine[24];
    if (ev.allDay) {
      snprintf(timeLine, sizeof(timeLine), "All day");
    } else {
      snprintf(timeLine, sizeof(timeLine), "%02d:%02d - %02d:%02d",
               ev.startHour, ev.startMin, ev.endHour, ev.endMin);
    }
    EPD_ShowString(15, baseY + 48, timeLine, 16, BLACK);

    // ── Attendees (size 12, truncated to fit) ─────────────────────────
    if (strlen(ev.attendees) > 0) {
      char attLine[90];
      snprintf(attLine, sizeof(attLine), "With: %.80s", ev.attendees);
      EPD_ShowString(15, baseY + 68, attLine, 12, BLACK);
    }

    // ── Description snippet (size 12, first ~90 chars) ────────────────
    if (strlen(ev.description) > 0) {
      char descLine[91];
      strncpy(descLine, ev.description, 90);
      descLine[90] = '\0';
      // Replace newlines with spaces for single-line display
      for (int c = 0; c < 90 && descLine[c]; c++) {
        if (descLine[c] == '\n' || descLine[c] == '\r') descLine[c] = ' ';
      }
      int descY = baseY + (strlen(ev.attendees) > 0 ? 82 : 68);
      if (descY + 12 <= 268) {
        EPD_ShowString(15, descY, descLine, 12, BLACK);
      }
    }

    // ── Separator between the two cards ───────────────────────────────
    if (slot == 0 && scrollIndex + 1 < eventCount) {
      int sepY = baseY + 126;
      if (sepY < 271) {
        EPD_DrawLine(15, sepY, 775, sepY, BLACK);
      }
    }
  }

  // ── Navigation hints at bottom ──────────────────────────────────────
  EPD_ShowString(15, 255, "[^]overview", 12, BLACK);
  if (scrollIndex + 1 < eventCount) {
    EPD_ShowString(680, 255, "[v]next", 12, BLACK);
  }
}


// ─────────────────────────────────────────────────────────────────────
// Show a simple 1- or 2-line status message (used during boot)
// ─────────────────────────────────────────────────────────────────────
void showMessage(const char* line1, const char* line2) {
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  EPD_ShowString(10, 110, line1, 24, BLACK);
  if (line2) EPD_ShowString(10, 142, line2, 24, BLACK);
  pushToDisplay();
}
