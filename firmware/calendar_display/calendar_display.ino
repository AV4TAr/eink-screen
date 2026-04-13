/*
 * E-ink Google Calendar Display v1.6
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
 *
 * Meeting alerts:
 *   5 min before — 3 slow blinks
 *   1 min before — 5 slow blinks
 *   During meeting — black background, white text
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

// ── Meeting mode + alerts ─────────────────────────────────────────────
bool inMeetingMode            = false;
int  alerted5minFor[MAX_EVENTS];  // startMins of events already given 5-min alert
int  alerted1minFor[MAX_EVENTS];  // startMins of events already given 1-min alert
int  alerted5minCount         = 0;
int  alerted1minCount         = 0;
int  lastAlertDay             = -1;
int  lastMeetingCheckMins     = -1;

// FG/BG helpers — flip with meeting mode
#define FG_COLOR (inMeetingMode ? WHITE : BLACK)
#define BG_COLOR (inMeetingMode ? BLACK : WHITE)

// ── Forward declarations ──────────────────────────────────────────────
void connectWiFi();
String getAccessToken();
bool fetchEvents(const String& token);
void refreshData();
void renderDisplay();
void renderMeeting();
void renderOverview();
void renderDetail();
void pushToDisplay();
void showMessage(const char* line1, const char* line2 = nullptr);
void parseHourMin(const char* dt, int* h, int* m);
String urlEncodeTime(time_t t);
void handleButtons();
bool currentlyInMeeting();
void blinkAlert(int times, CalEvent& ev, int minutesUntil);
void showAlertScreen(CalEvent& ev, int minutesUntil, uint8_t bg);
void checkAlerts();
void checkMeetingMode();


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

  inMeetingMode = currentlyInMeeting();
  renderDisplay();
  lastRefreshMs = millis();
}


// ─────────────────────────────────────────────────────────────────────
// loop — button polling + alerts + periodic refresh
// ─────────────────────────────────────────────────────────────────────
void loop() {
  handleButtons();
  checkAlerts();
  checkMeetingMode();

  if (millis() - lastRefreshMs >= REFRESH_INTERVAL_MS) {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (WiFi.status() == WL_CONNECTED) refreshData();
    viewMode = VIEW_OVERVIEW;
    scrollIndex = 0;
    inMeetingMode = currentlyInMeeting();
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
    inMeetingMode = currentlyInMeeting();
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
// Returns true if any non-all-day event is happening right now
// ─────────────────────────────────────────────────────────────────────
bool currentlyInMeeting() {
  time_t now;
  time(&now);
  struct tm* t = localtime(&now);
  int nowMins = t->tm_hour * 60 + t->tm_min;
  for (int i = 0; i < eventCount; i++) {
    if (events[i].allDay) continue;
    int startMins = events[i].startHour * 60 + events[i].startMin;
    int endMins   = events[i].endHour   * 60 + events[i].endMin;
    if (nowMins >= startMins && nowMins < endMins) return true;
  }
  return false;
}


// ─────────────────────────────────────────────────────────────────────
// Show centered alert screen with meeting info.
// bg determines background; fg is derived automatically for contrast.
// ─────────────────────────────────────────────────────────────────────
void showAlertScreen(CalEvent& ev, int minutesUntil, uint8_t bg) {
  uint8_t fg = (bg == BLACK) ? WHITE : BLACK;

  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, bg);
  Paint_Clear(bg);

  // Build strings
  char header[40];
  snprintf(header, sizeof(header), "!! Meeting in %d min !!", minutesUntil);

  int titleSize = (strlen(ev.title) <= 20) ? 48 : 24;
  int titleMax  = (titleSize == 48) ? 23 : 47;
  char titleLine[48];
  strncpy(titleLine, ev.title, titleMax);
  titleLine[titleMax] = '\0';

  char timeLine[20];
  snprintf(timeLine, sizeof(timeLine), "%02d:%02d - %02d:%02d",
           ev.startHour, ev.startMin, ev.endHour, ev.endMin);

  // Vertical centering
  const int headerSize = 24;
  const int timeSize   = 24;
  const int gap        = 12;
  int totalH = headerSize + gap + titleSize + gap + timeSize;
  int y = (EPD_H - totalH) / 2;

  // Horizontal centering helper: x = (width - chars * charWidth) / 2
  // charWidth = fontSize / 2
  auto centerX = [](const char* s, int sz) -> int {
    int w = (int)strlen(s) * sz / 2;
    int x = (792 - w) / 2;
    return x < 10 ? 10 : x;
  };

  EPD_ShowString(centerX(header,    headerSize), y,                                    header,    headerSize, fg);
  EPD_ShowString(centerX(titleLine, titleSize),  y + headerSize + gap,                 titleLine, titleSize,  fg);
  EPD_ShowString(centerX(timeLine,  timeSize),   y + headerSize + gap + titleSize + gap, timeLine, timeSize,  fg);

  pushToDisplay();
}


// ─────────────────────────────────────────────────────────────────────
// Blink N times (each frame shows meeting info with proper contrast)
// then hold the black-bg alert screen for ALERT_SCREEN_MS
// ─────────────────────────────────────────────────────────────────────
void blinkAlert(int times, CalEvent& ev, int minutesUntil) {
  for (int i = 0; i < times; i++) {
    showAlertScreen(ev, minutesUntil, BLACK);
    delay(600);
    showAlertScreen(ev, minutesUntil, WHITE);
    delay(400);
  }
  showAlertScreen(ev, minutesUntil, BLACK);
  delay(ALERT_SCREEN_MS);
}


// ─────────────────────────────────────────────────────────────────────
// Check if any event is approaching and fire blink alerts
// Tracks by event start time so re-fetches don't re-trigger
// ─────────────────────────────────────────────────────────────────────
void checkAlerts() {
  time_t now;
  time(&now);
  struct tm* t = localtime(&now);
  int nowMins = t->tm_hour * 60 + t->tm_min;

  // Reset alert history once per day (early morning)
  if (t->tm_mday != lastAlertDay && t->tm_hour == 0) {
    alerted5minCount = 0;
    alerted1minCount = 0;
    lastAlertDay = t->tm_mday;
  }

  for (int i = 0; i < eventCount; i++) {
    CalEvent& ev = events[i];
    if (ev.allDay) continue;
    int startMins = ev.startHour * 60 + ev.startMin;

    // 5-minute warning
    if (nowMins == startMins - ALERT_WARN_MINUTES) {
      bool already = false;
      for (int j = 0; j < alerted5minCount; j++) {
        if (alerted5minFor[j] == startMins) { already = true; break; }
      }
      if (!already) {
        if (alerted5minCount < MAX_EVENTS) alerted5minFor[alerted5minCount++] = startMins;
        Serial.printf("Alert: 5-min warning for %s\n", ev.title);
        blinkAlert(ALERT_WARN_BLINKS, ev, ALERT_WARN_MINUTES);
        renderDisplay();
      }
    }

    // 1-minute warning
    if (nowMins == startMins - ALERT_URGENT_MINUTES) {
      bool already = false;
      for (int j = 0; j < alerted1minCount; j++) {
        if (alerted1minFor[j] == startMins) { already = true; break; }
      }
      if (!already) {
        if (alerted1minCount < MAX_EVENTS) alerted1minFor[alerted1minCount++] = startMins;
        Serial.printf("Alert: 1-min warning for %s\n", ev.title);
        blinkAlert(ALERT_URGENT_BLINKS, ev, ALERT_URGENT_MINUTES);
        renderDisplay();
      }
    }
  }
}


// ─────────────────────────────────────────────────────────────────────
// Check once per minute if meeting mode changed; re-render if so
// ─────────────────────────────────────────────────────────────────────
void checkMeetingMode() {
  time_t now;
  time(&now);
  struct tm* t = localtime(&now);
  int nowMins = t->tm_hour * 60 + t->tm_min;

  if (nowMins == lastMeetingCheckMins) return;
  lastMeetingCheckMins = nowMins;

  bool newMode = currentlyInMeeting();
  if (newMode != inMeetingMode) {
    inMeetingMode = newMode;
    Serial.printf("Meeting mode: %s\n", inMeetingMode ? "ON" : "OFF");
    renderDisplay();
  }
}


// ─────────────────────────────────────────────────────────────────────
// Render dispatcher
// ─────────────────────────────────────────────────────────────────────
void renderDisplay() {
  uint8_t bg = inMeetingMode ? BLACK : WHITE;
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, bg);
  Paint_Clear(bg);

  if (viewMode == VIEW_DETAIL && eventCount > 0)
    renderDetail();
  else if (inMeetingMode)
    renderMeeting();
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
// Meeting in progress: title + time info + block progress bar
//
//  x=0                                                           x=792
//   ┌────────────────────────────────────────────────────────────────┐ y=0
//   │  MEETING IN PROGRESS                                           │ y=20
//   │                                                                │
//   │  Team Standup                                                  │ y=50 (48px)
//   │                                                                │
//   │  10:30 - 11:00  •  12 min left                                │ y=112
//   │                                                                │
//   │  [■■■][■■■][■■■][□□□][□□□][□□□]                              │ y=190
//   └────────────────────────────────────────────────────────────────┘ y=271
//
// Each block = 10 min. Filled = elapsed, empty = remaining.
// ─────────────────────────────────────────────────────────────────────
void renderMeeting() {
  time_t now;
  time(&now);
  struct tm* t = localtime(&now);
  int nowMins = t->tm_hour * 60 + t->tm_min;

  // Find the active meeting
  CalEvent* ev = nullptr;
  for (int i = 0; i < eventCount; i++) {
    if (events[i].allDay) continue;
    int s = events[i].startHour * 60 + events[i].startMin;
    int e = events[i].endHour   * 60 + events[i].endMin;
    if (nowMins >= s && nowMins < e) { ev = &events[i]; break; }
  }
  if (!ev) { renderOverview(); return; }  // fallback

  int startMins    = ev->startHour * 60 + ev->startMin;
  int endMins      = ev->endHour   * 60 + ev->endMin;
  int durationMins = endMins - startMins;
  int elapsedMins  = nowMins - startMins;
  int remainMins   = endMins - nowMins;

  // ── Header ──────────────────────────────────────────────────────────
  EPD_ShowString(20, 14, "MEETING IN PROGRESS", 24, WHITE);

  // ── Title (auto-size to fit) ─────────────────────────────────────────
  int titleLen  = strlen(ev->title);
  int titleSize = (titleLen <= 20) ? 48 : 24;
  int titleY    = 46;
  int titleMax  = (titleSize == 48) ? 23 : 47;
  char titleLine[48];
  strncpy(titleLine, ev->title, titleMax);
  titleLine[titleMax] = '\0';
  EPD_ShowString(20, titleY, titleLine, titleSize, WHITE);

  // ── Time + remaining ────────────────────────────────────────────────
  int timeY = titleY + titleSize + 10;
  char timeLine[48];
  snprintf(timeLine, sizeof(timeLine), "%02d:%02d - %02d:%02d   %d min left",
           ev->startHour, ev->startMin, ev->endHour, ev->endMin, remainMins);
  EPD_ShowString(20, timeY, timeLine, 24, WHITE);

  // ── Block progress bar ───────────────────────────────────────────────
  int numBlocks    = max(1, (durationMins + 9) / 10);  // ceil(duration/10)
  int elapsedBlocks = elapsedMins / 10;

  const int barX = 20;
  const int barY = 182;
  const int barH = 46;
  const int totalW = 752;
  const int gap    = 8;
  int blockW = (totalW - gap * (numBlocks - 1)) / numBlocks;

  for (int b = 0; b < numBlocks; b++) {
    int bx = barX + b * (blockW + gap);
    int filled = (b < elapsedBlocks) ? 1 : 0;
    EPD_DrawRectangle(bx, barY, bx + blockW, barY + barH, WHITE, filled);
  }
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
  EPD_ShowString(10, 6, timeStr, 48, FG_COLOR);

  EPD_ShowString(10, 62, t->tm_hour < 12 ? "AM" : "PM", 24, FG_COLOR);

  const char* days[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                           "Jul","Aug","Sep","Oct","Nov","Dec"};
  char dateStr[20];
  snprintf(dateStr, sizeof(dateStr), "%s %d %s",
           days[t->tm_wday], t->tm_mday, months[t->tm_mon]);
  EPD_ShowString(10, 94, dateStr, 24, FG_COLOR);

  char updStr[16];
  snprintf(updStr, sizeof(updStr), "upd %s", lastUpdated);
  EPD_ShowString(10, 255, updStr, 12, FG_COLOR);

  // ── Divider ─────────────────────────────────────────────────────────
  EPD_DrawLine(192, 0, 192, 271, FG_COLOR);

  // ── Right panel ─────────────────────────────────────────────────────
  const int evX = 200;
  const int evW = 575;
  int nowMins = t->tm_hour * 60 + t->tm_min;

  if (eventCount == 0) {
    EPD_ShowString(evX, 120, "No events ahead", 16, FG_COLOR);
  } else {
    // ── Section 1: prominent next/current meeting card ─────────────────
    CalEvent& first = events[0];
    int cardSepY = 72;  // default separator y (normal mode)

    if (first.allDay) {
      // All-day event: simple single line
      char line[80];
      snprintf(line, sizeof(line), "All day  %.55s", first.title);
      EPD_ShowString(evX, 6, line, 16, FG_COLOR);
      cardSepY = 30;
    } else {
      int startMins    = first.startHour * 60 + first.startMin;
      int endMins      = first.endHour   * 60 + first.endMin;
      bool isNow       = (nowMins >= startMins && nowMins < endMins);
      int minutesUntil = startMins - nowMins;
      bool soonAlert   = (!isNow && minutesUntil >= 0 && minutesUntil <= ALERT_WARN_MINUTES);

      // Label row
      char labelStr[40];
      if (isNow) {
        snprintf(labelStr, sizeof(labelStr), "NOW");
      } else if (soonAlert) {
        snprintf(labelStr, sizeof(labelStr), "!! %d MIN !!", minutesUntil);
      } else if (minutesUntil > 60) {
        snprintf(labelStr, sizeof(labelStr), "NEXT  %02d:%02d",
                 first.startHour, first.startMin);
      } else if (minutesUntil > 0) {
        snprintf(labelStr, sizeof(labelStr), "NEXT UP  -  in %d min", minutesUntil);
      } else {
        snprintf(labelStr, sizeof(labelStr), "STARTING NOW");
      }
      EPD_ShowString(evX, 6, labelStr, 16, FG_COLOR);

      // Title — always 48px for section 1
      int titleY   = 26;
      int titleMax = 23;  // 48px font = 24px/char, 575px wide → 23 chars
      char titleLine[24];
      strncpy(titleLine, first.title, titleMax);
      titleLine[titleMax] = '\0';

      if (isNow) {
        EPD_DrawRectangle(evX - 2, titleY - 2, evX + evW, titleY + 50, FG_COLOR, 1);
        EPD_ShowString(evX, titleY, titleLine, 48, BG_COLOR);
      } else {
        EPD_ShowString(evX, titleY, titleLine, 48, FG_COLOR);
      }

      // Time range
      int timeY = 82;
      char timeLine[20];
      snprintf(timeLine, sizeof(timeLine), "%02d:%02d - %02d:%02d",
               first.startHour, first.startMin, first.endHour, first.endMin);
      EPD_ShowString(evX, timeY, timeLine, 24, FG_COLOR);

      cardSepY = 114;
    }

    // ── Separator ───────────────────────────────────────────────────────
    if (eventCount > 1) {
      EPD_DrawLine(evX - 2, cardSepY, evX + evW, cardSepY, FG_COLOR);
    }

    // ── Section 2: remaining events ─────────────────────────────────────
    int evY = cardSepY + 8;
    for (int i = 1; i < eventCount && evY + 24 <= 255; i++) {
      CalEvent& ev = events[i];
      bool isNow = false;
      char line[80];

      if (ev.allDay) {
        snprintf(line, sizeof(line), "All day  %.35s", ev.title);
      } else {
        int startMins = ev.startHour * 60 + ev.startMin;
        int endMins   = ev.endHour   * 60 + ev.endMin;
        isNow = (nowMins >= startMins && nowMins < endMins);
        char titlePart[36];
        strncpy(titlePart, ev.title, 35);
        titlePart[35] = '\0';
        snprintf(line, sizeof(line), "%02d:%02d-%02d:%02d %s",
                 ev.startHour, ev.startMin, ev.endHour, ev.endMin, titlePart);
      }

      if (isNow) {
        EPD_DrawRectangle(evX - 2, evY - 2, evX + evW, evY + 25, FG_COLOR, 1);
        EPD_ShowString(evX, evY, line, 24, BG_COLOR);
      } else {
        EPD_ShowString(evX, evY, line, 24, FG_COLOR);
      }
      evY += 30;
    }

    if (eventCount > 1) {
      EPD_ShowString(680, 255, "[v]more", 16, FG_COLOR);
    }
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

    EPD_ShowString(15, baseY, label, 16, FG_COLOR);
    char idxStr[12];
    snprintf(idxStr, sizeof(idxStr), "%d of %d", idx + 1, eventCount);
    int idxX = 780 - (int)strlen(idxStr) * 6;
    if (idxX < 600) idxX = 600;
    EPD_ShowString(idxX, baseY, idxStr, 12, FG_COLOR);

    // ── Title (size 24, up to 32 chars) ───────────────────────────────
    char titleLine[33];
    strncpy(titleLine, ev.title, 32);
    titleLine[32] = '\0';
    EPD_ShowString(15, baseY + 20, titleLine, 24, FG_COLOR);

    // ── Time range (size 24) ──────────────────────────────────────────
    char timeLine[24];
    if (ev.allDay) {
      snprintf(timeLine, sizeof(timeLine), "All day");
    } else {
      snprintf(timeLine, sizeof(timeLine), "%02d:%02d - %02d:%02d",
               ev.startHour, ev.startMin, ev.endHour, ev.endMin);
    }
    EPD_ShowString(15, baseY + 48, timeLine, 24, FG_COLOR);

    // ── Attendees (size 16) ───────────────────────────────────────────
    if (strlen(ev.attendees) > 0) {
      char attLine[90];
      snprintf(attLine, sizeof(attLine), "With: %.80s", ev.attendees);
      EPD_ShowString(15, baseY + 78, attLine, 16, FG_COLOR);
    }

    // ── Description snippet (size 16) ────────────────────────────────
    if (strlen(ev.description) > 0) {
      char descLine[91];
      strncpy(descLine, ev.description, 90);
      descLine[90] = '\0';
      for (int c = 0; c < 90 && descLine[c]; c++) {
        if (descLine[c] == '\n' || descLine[c] == '\r') descLine[c] = ' ';
      }
      int descY = baseY + (strlen(ev.attendees) > 0 ? 98 : 78);
      if (descY + 16 <= 268) {
        EPD_ShowString(15, descY, descLine, 16, FG_COLOR);
      }
    }

    // ── Separator between the two cards ───────────────────────────────
    if (slot == 0 && scrollIndex + 1 < eventCount) {
      int sepY = baseY + 128;
      if (sepY < 271) {
        EPD_DrawLine(15, sepY, 775, sepY, FG_COLOR);
      }
    }
  }

  // ── Navigation hints at bottom ──────────────────────────────────────
  EPD_ShowString(15, 255, "[^]overview", 16, FG_COLOR);
  if (scrollIndex + 1 < eventCount) {
    EPD_ShowString(680, 255, "[v]next", 16, FG_COLOR);
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
