#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33
#define LCD_BACKLIGHT_PIN 21

#define SCREEN_W 240
#define SCREEN_H 320
#define TOUCH_ZONE_H 56

static const char *CAPTIVE_SSID = "HomeLab-CYD";

// Update these values for your setup
static const char *JELLYFIN_BASE_URL = "http://192.168.1.206:8096";
static const char *JELLYFIN_API_KEY = "";
static const char *JELLYSEERR_BASE_URL = "http://192.168.1.206:5055";
static const char *JELLYSEERR_API_KEY = "";

// Recommended: point these to your own secure webhook/automation endpoint on LAN.
// Example: n8n, Home Assistant, or lightweight API on Unraid that performs docker restart.
static const char *RESTART_JELLYFIN_URL = "";
static const char *RESTART_JELLYSEERR_URL = "";

static const uint32_t REFRESH_INTERVAL_MS = 60 * 1000;

TFT_eSPI tft = TFT_eSPI();
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

struct JellyfinStatus {
  bool online = false;
  int sessions = 0;
  int activeStreams = 0;
  String error;
};

struct JellyseerrStatus {
  bool online = false;
  int moviePending = 0;
  int movieQueue = 0;
  int movieInLibrary = 0;
  int tvPending = 0;
  int tvQueue = 0;
  int tvInLibrary = 0;
  String error;
};

static JellyfinStatus jellyfin;
static JellyseerrStatus jellyseerr;
static String lastAction = "Ready";
static unsigned long lastRefreshMs = 0;
static bool wasTouching = false;

bool httpGetJson(const String &url, DynamicJsonDocument &doc, const char *headerName = nullptr, const char *headerValue = nullptr) {
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(url)) {
    return false;
  }

  if (headerName && headerValue && strlen(headerValue) > 0) {
    http.addHeader(headerName, headerValue);
  }

  int code = http.GET();
  if (code < 200 || code >= 300) {
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  DeserializationError err = deserializeJson(doc, body);
  return !err;
}

bool httpPost(const String &url) {
  if (url.length() == 0) return false;
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(url)) {
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST("{}");
  http.end();
  return code >= 200 && code < 300;
}

void fetchJellyfin() {
  jellyfin = JellyfinStatus();

  if (strlen(JELLYFIN_API_KEY) == 0) {
    jellyfin.error = "Missing API key";
    return;
  }

  DynamicJsonDocument infoDoc(2048);
  DynamicJsonDocument sessionsDoc(16 * 1024);

  bool infoOk = httpGetJson(String(JELLYFIN_BASE_URL) + "/System/Info/Public", infoDoc, "X-Emby-Token", JELLYFIN_API_KEY);
  bool sessionsOk = httpGetJson(String(JELLYFIN_BASE_URL) + "/Sessions", sessionsDoc, "X-Emby-Token", JELLYFIN_API_KEY);

  if (!infoOk || !sessionsOk) {
    jellyfin.error = "API unavailable";
    return;
  }

  JsonArray sessions = sessionsDoc.as<JsonArray>();
  int active = 0;
  int total = 0;
  for (JsonVariant v : sessions) {
    total++;
    bool hasNowPlaying = !v["NowPlayingItem"].isNull();
    bool isPaused = v["PlayState"]["IsPaused"] | false;
    if (hasNowPlaying && !isPaused) {
      active++;
    }
  }

  jellyfin.online = true;
  jellyfin.sessions = total;
  jellyfin.activeStreams = active;
}

void fetchJellyseerr() {
  jellyseerr = JellyseerrStatus();

  if (strlen(JELLYSEERR_API_KEY) == 0) {
    jellyseerr.error = "Missing API key";
    return;
  }

  DynamicJsonDocument statusDoc(2048);
  DynamicJsonDocument countDoc(8192);

  bool statusOk = httpGetJson(String(JELLYSEERR_BASE_URL) + "/api/v1/status", statusDoc, "X-Api-Key", JELLYSEERR_API_KEY);
  bool countOk = httpGetJson(String(JELLYSEERR_BASE_URL) + "/api/v1/request/count", countDoc, "X-Api-Key", JELLYSEERR_API_KEY);

  if (!statusOk || !countOk) {
    jellyseerr.error = "API unavailable";
    return;
  }

  jellyseerr.online = true;
  jellyseerr.moviePending = countDoc["movie"]["pending"] | 0;
  jellyseerr.movieQueue = countDoc["movie"]["processing"] | 0;
  jellyseerr.movieInLibrary = countDoc["movie"]["available"] | 0;
  jellyseerr.tvPending = countDoc["tv"]["pending"] | 0;
  jellyseerr.tvQueue = countDoc["tv"]["processing"] | 0;
  jellyseerr.tvInLibrary = countDoc["tv"]["available"] | 0;
}

void refreshAll() {
  fetchJellyfin();
  fetchJellyseerr();
  lastRefreshMs = millis();
}

void drawHeader() {
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  tft.print("HomeLab AIO");

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(8, 30);
  tft.print("WiFi: ");
  tft.print(WiFi.localIP());
}

void drawServiceBoxes() {
  tft.drawRoundRect(8, 46, 224, 96, 8, TFT_DARKGREY);
  tft.drawRoundRect(8, 148, 224, 108, 8, TFT_DARKGREY);

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(14, 54);
  tft.print("Jellyfin");

  tft.setTextSize(1);
  if (jellyfin.online) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(14, 76);
    tft.print("Online");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(14, 94);
    tft.printf("Active Streams: %d", jellyfin.activeStreams);
    tft.setCursor(14, 108);
    tft.printf("Sessions: %d", jellyfin.sessions);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(14, 76);
    tft.print("Offline");
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(14, 94);
    tft.print(jellyfin.error);
  }

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(14, 156);
  tft.print("Jellyseerr");

  tft.setTextSize(1);
  if (jellyseerr.online) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(14, 178);
    tft.print("Online");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(14, 196);
    tft.printf("Movies  P:%d Q:%d L:%d", jellyseerr.moviePending, jellyseerr.movieQueue, jellyseerr.movieInLibrary);
    tft.setCursor(14, 210);
    tft.printf("TV      P:%d Q:%d L:%d", jellyseerr.tvPending, jellyseerr.tvQueue, jellyseerr.tvInLibrary);
    tft.setCursor(14, 224);
    tft.printf("All     P:%d Q:%d L:%d",
      jellyseerr.moviePending + jellyseerr.tvPending,
      jellyseerr.movieQueue + jellyseerr.tvQueue,
      jellyseerr.movieInLibrary + jellyseerr.tvInLibrary);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(14, 178);
    tft.print("Offline");
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(14, 196);
    tft.print(jellyseerr.error);
  }
}

void drawButtons() {
  int y = SCREEN_H - TOUCH_ZONE_H;
  int w = SCREEN_W / 3;

  tft.fillRect(0, y, SCREEN_W, TOUCH_ZONE_H, TFT_BLACK);
  tft.drawRect(0, y, w, TOUCH_ZONE_H, TFT_BLUE);
  tft.drawRect(w, y, w, TOUCH_ZONE_H, TFT_RED);
  tft.drawRect(w * 2, y, w, TOUCH_ZONE_H, TFT_RED);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(20, y + 10);
  tft.print("REFRESH");
  tft.setCursor(w + 10, y + 10);
  tft.print("RESTART");
  tft.setCursor(w + 22, y + 24);
  tft.print("JELLYFIN");
  tft.setCursor(w * 2 + 10, y + 10);
  tft.print("RESTART");
  tft.setCursor(w * 2 + 16, y + 24);
  tft.print("JELLYSEERR");

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(8, y - 12);
  tft.print("Last action: ");
  tft.print(lastAction);
}

void render() {
  tft.fillScreen(TFT_BLACK);
  drawHeader();
  drawServiceBoxes();
  drawButtons();
}

void handleTouch() {
  bool touching = touchscreen.touched();
  if (!touching) {
    wasTouching = false;
    return;
  }

  if (wasTouching) {
    return;
  }
  wasTouching = true;

  TS_Point p = touchscreen.getPoint();
  int y = map(p.x, 200, 3700, 0, SCREEN_H);
  int x = map(p.y, 240, 3800, 0, SCREEN_W);

  if (y < SCREEN_H - TOUCH_ZONE_H) {
    return;
  }

  int third = SCREEN_W / 3;
  if (x < third) {
    lastAction = "Manual refresh";
    refreshAll();
    render();
    return;
  }

  if (x < third * 2) {
    bool ok = httpPost(RESTART_JELLYFIN_URL);
    lastAction = ok ? "Restart Jellyfin sent" : "Restart Jellyfin failed";
    refreshAll();
    render();
    return;
  }

  bool ok = httpPost(RESTART_JELLYSEERR_URL);
  lastAction = ok ? "Restart Jellyseerr sent" : "Restart Jellyseerr failed";
  refreshAll();
  render();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  tft.init();
  tft.setRotation(0);
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);

  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(0);

  WiFiManager wm;
  wm.autoConnect(CAPTIVE_SSID);

  lastAction = "Connected";
  refreshAll();
  render();
}

void loop() {
  handleTouch();

  if (millis() - lastRefreshMs >= REFRESH_INTERVAL_MS) {
    refreshAll();
    render();
  }

  delay(20);
}
