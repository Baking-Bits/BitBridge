#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>
#include <qrcode.h>
#if defined(USE_RAW_LCD)
#include "raw_tft_ili9341.h"
#else
#include <TFT_eSPI.h>
#endif
#include <XPT2046_Touchscreen.h>
#include <Wire.h>

#if __has_include("homelab_secrets.h")
#include "homelab_secrets.h"
#endif

#ifndef XPT2046_IRQ
#define XPT2046_IRQ 36
#endif
#ifndef XPT2046_MOSI
#define XPT2046_MOSI 32
#endif
#ifndef XPT2046_MISO
#define XPT2046_MISO 39
#endif
#ifndef XPT2046_CLK
#define XPT2046_CLK 25
#endif
#ifndef XPT2046_CS
#define XPT2046_CS 33
#endif
#ifndef LCD_BACKLIGHT_PIN
#ifdef TFT_BL
#define LCD_BACKLIGHT_PIN TFT_BL
#else
#define LCD_BACKLIGHT_PIN 21
#endif
#endif
#ifndef LCD_ENABLE_PIN
#define LCD_ENABLE_PIN 42
#endif
#ifndef LCD_CS_PIN
#ifdef TFT_CS
#define LCD_CS_PIN TFT_CS
#else
#define LCD_CS_PIN 10
#endif
#endif
#ifndef LCD_DC_PIN
#ifdef TFT_DC
#define LCD_DC_PIN TFT_DC
#else
#define LCD_DC_PIN 46
#endif
#endif
#ifndef TOUCH_SPI_PORT
#define TOUCH_SPI_PORT HSPI
#endif

#ifndef S3_CAP_TOUCH
#define S3_CAP_TOUCH 0
#endif
#ifndef FT6336_SDA
#define FT6336_SDA 16
#endif
#ifndef FT6336_SCL
#define FT6336_SCL 15
#endif
#ifndef FT6336_INT
#define FT6336_INT 21
#endif
#ifndef FT6336_RST
#define FT6336_RST 18
#endif
#ifndef FT6336_ADDR
#define FT6336_ADDR 0x38
#endif

#define SCREEN_W 240
#define SCREEN_H 320
#define TOUCH_ZONE_H 56

static const char *CAPTIVE_SSID = "HomeLab-CYD";

// Direct API mode (no bridge/PC/server process required)
#ifndef JELLYFIN_BASE_URL
#define JELLYFIN_BASE_URL "http://192.168.1.206:8096"
#endif
#ifndef JELLYFIN_API_KEY
#define JELLYFIN_API_KEY ""
#endif

#ifndef JELLYSEERR_BASE_URL
#define JELLYSEERR_BASE_URL "http://192.168.1.206:5055"
#endif
#ifndef JELLYSEERR_API_KEY
#define JELLYSEERR_API_KEY ""
#endif

// Optional custom restart URL for Jellyseerr if you add one later.
#ifndef JELLYSEERR_RESTART_URL
#define JELLYSEERR_RESTART_URL ""
#endif

static const char *JELLYFIN_BASE = JELLYFIN_BASE_URL;
static const char *JELLYFIN_KEY = JELLYFIN_API_KEY;
static const char *JELLYSEERR_BASE = JELLYSEERR_BASE_URL;
static const char *JELLYSEERR_KEY = JELLYSEERR_API_KEY;
static const char *JELLYSEERR_RESTART = JELLYSEERR_RESTART_URL;

static const uint32_t REFRESH_INTERVAL_MS = 60 * 1000;

TFT_eSPI tft = TFT_eSPI();
SPIClass touchscreenSPI = SPIClass(TOUCH_SPI_PORT);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
Preferences prefs;
WebServer configServer(80);

static void applyVendorIli9341Init() {
  tft.startWrite();

  tft.writecommand(0xCF); tft.writedata(0x00); tft.writedata(0xC1); tft.writedata(0x30);
  tft.writecommand(0xED); tft.writedata(0x64); tft.writedata(0x03); tft.writedata(0x12); tft.writedata(0x81);
  tft.writecommand(0xE8); tft.writedata(0x85); tft.writedata(0x00); tft.writedata(0x78);
  tft.writecommand(0xCB); tft.writedata(0x39); tft.writedata(0x2C); tft.writedata(0x00); tft.writedata(0x34); tft.writedata(0x02);
  tft.writecommand(0xF7); tft.writedata(0x20);
  tft.writecommand(0xEA); tft.writedata(0x00); tft.writedata(0x00);
  tft.writecommand(0xC0); tft.writedata(0x13);
  tft.writecommand(0xC1); tft.writedata(0x13);
  tft.writecommand(0xC5); tft.writedata(0x22); tft.writedata(0x35);
  tft.writecommand(0xC7); tft.writedata(0xBD);
  tft.writecommand(0x21);
  tft.writecommand(0x36); tft.writedata(0x08);
  tft.writecommand(0xB6); tft.writedata(0x08); tft.writedata(0x82);
  tft.writecommand(0x3A); tft.writedata(0x55);
  tft.writecommand(0xF6); tft.writedata(0x01); tft.writedata(0x30);
  tft.writecommand(0xB1); tft.writedata(0x00); tft.writedata(0x1B);
  tft.writecommand(0xF2); tft.writedata(0x00);
  tft.writecommand(0x26); tft.writedata(0x01);

  const uint8_t e0[] = {0x0F,0x35,0x31,0x0B,0x0E,0x06,0x49,0xA7,0x33,0x07,0x0F,0x03,0x0C,0x0A,0x00};
  tft.writecommand(0xE0);
  for (uint8_t value : e0) tft.writedata(value);

  const uint8_t e1[] = {0x00,0x0A,0x0F,0x04,0x11,0x08,0x36,0x58,0x4D,0x07,0x10,0x0C,0x32,0x34,0x0F};
  tft.writecommand(0xE1);
  for (uint8_t value : e1) tft.writedata(value);

  tft.writecommand(0x11);
  tft.endWrite();
  delay(120);

  tft.startWrite();
  tft.writecommand(0x29);
  tft.endWrite();
}

static String cfgHostname = "homelab-cyd";
static String cfgJellyfinBase = JELLYFIN_BASE_URL;
static String cfgJellyfinKey = JELLYFIN_API_KEY;
static String cfgJellyseerrBase = JELLYSEERR_BASE_URL;
static String cfgJellyseerrKey = JELLYSEERR_API_KEY;
static String cfgJellyseerrRestart = JELLYSEERR_RESTART_URL;
static uint32_t cfgRefreshIntervalMs = REFRESH_INTERVAL_MS;
static bool configServerReady = false;
static bool mdnsReady = false;

void refreshAll();
void render();
void enableBacklightFailsafe();
bool readFt6336Touch(int &xOut, int &yOut);

void drawBootScreen(const char *line1, const char *line2 = nullptr) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(12, 30);
  tft.print("HomeLab AIO");

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(12, 78);
  tft.print(line1 ? line1 : "Starting...");

  if (line2 && strlen(line2) > 0) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(12, 108);
    tft.print(line2);
  }
}

static int qrDrawX = 0;
static int qrDrawY = 0;
static int qrModuleSize = 4;

void drawQrToTft(esp_qrcode_handle_t qr) {
  if (!qr) {
    return;
  }

  int size = esp_qrcode_get_size(qr);
  int boxSize = size * qrModuleSize;
  tft.fillRect(qrDrawX - 2, qrDrawY - 2, boxSize + 4, boxSize + 4, TFT_WHITE);

  for (int yModule = 0; yModule < size; yModule++) {
    for (int xModule = 0; xModule < size; xModule++) {
      bool dark = esp_qrcode_get_module(qr, xModule, yModule);
      uint16_t color = dark ? TFT_BLACK : TFT_WHITE;
      tft.fillRect(qrDrawX + xModule * qrModuleSize, qrDrawY + yModule * qrModuleSize, qrModuleSize, qrModuleSize, color);
    }
  }
}

static const char WIFI_PORTAL_CACHE_HEAD[] PROGMEM = R"rawliteral(
<script>
(function () {
  var KEY = 'auraLastWifiV1';

  function readCache() {
    try {
      var raw = localStorage.getItem(KEY);
      return raw ? JSON.parse(raw) : null;
    } catch (e) {
      return null;
    }
  }

  function saveCache(ssid, password) {
    try {
      localStorage.setItem(KEY, JSON.stringify({
        ssid: ssid || '',
        password: password || '',
        updatedAt: Date.now()
      }));
    } catch (e) {
    }
  }

  function findSsidInput() {
    return document.querySelector('input#s, input[name="s"], input[name="ssid"], input[id*="ssid" i]');
  }

  function findPasswordInput() {
    return document.querySelector('input#p, input[name="p"], input[type="password"], input[name*="pass" i], input[id*="pass" i]');
  }

  document.addEventListener('DOMContentLoaded', function () {
    var ssidInput = findSsidInput();
    var passwordInput = findPasswordInput();
    if (!ssidInput && !passwordInput) {
      return;
    }

    var cached = readCache();
    if (cached) {
      if (ssidInput && !ssidInput.value && typeof cached.ssid === 'string') {
        ssidInput.value = cached.ssid;
      }
      if (passwordInput && !passwordInput.value && typeof cached.password === 'string') {
        passwordInput.value = cached.password;
      }
    }

    if (ssidInput && passwordInput && passwordInput.parentNode) {
      var btn = document.createElement('button');
      btn.type = 'button';
      btn.textContent = 'Use last Wi-Fi';
      btn.style.marginTop = '8px';
      btn.style.display = 'block';
      btn.addEventListener('click', function () {
        var data = readCache();
        if (!data) {
          return;
        }
        ssidInput.value = data.ssid || '';
        passwordInput.value = data.password || '';
      });
      passwordInput.parentNode.appendChild(btn);
    }

    var form = (ssidInput && ssidInput.form) || (passwordInput && passwordInput.form) || document.querySelector('form');
    if (form) {
      form.addEventListener('submit', function () {
        saveCache(ssidInput ? ssidInput.value.trim() : '', passwordInput ? passwordInput.value : '');
      });
    }
  });
})();
</script>
)rawliteral";

void drawWifiQrCode(const String &payload, int x, int y, int moduleSize) {
  qrDrawX = x;
  qrDrawY = y;
  qrModuleSize = moduleSize;

  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = drawQrToTft;
  cfg.max_qrcode_version = 10;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  esp_qrcode_generate(&cfg, payload.c_str());
}

void configureWiFiManagerPortal(WiFiManager &wm);

void wifiPortalCallback(WiFiManager *mgr) {
  (void)mgr;
  drawBootScreen("WiFi Setup Mode", "Scan QR or open 192.168.4.1");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(12, 128);
  tft.print("AP: ");
  tft.print(CAPTIVE_SSID);

  String qrPayload = String("WIFI:T:nopass;S:") + CAPTIVE_SSID + ";;";
  drawWifiQrCode(qrPayload, 54, 150, 4);
}

void configureWiFiManagerPortal(WiFiManager &wm) {
  wm.setAPCallback(wifiPortalCallback);
  wm.setCustomHeadElement(WIFI_PORTAL_CACHE_HEAD);
  wm.setCaptivePortalEnable(true);
  wm.setWebPortalClientCheck(true);
  wm.setConfigPortalBlocking(true);
  wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
}

struct JellyfinStatus {
  bool online = false;
  int sessions = 0;
  int activeStreams = 0;
  int movieLibrary = 0;
  int tvLibrary = 0;
  String error;
};

struct JellyseerrStatus {
  bool online = false;
  int moviePendingApproval = 0;
  int tvPendingApproval = 0;
  int movieQueuedReleasedUnder10d = 0;
  int tvQueuedReleasedUnder10d = 0;
  int movieQueuedReleasedOver10d = 0;
  int tvQueuedReleasedOver10d = 0;
  int moviePendingUnreleased = 0;
  int tvPendingUnreleased = 0;
  String error;
};

static JellyfinStatus jellyfin;
static JellyseerrStatus jellyseerr;
static String lastAction = "Ready";
static unsigned long lastRefreshMs = 0;
static bool wasTouching = false;
static time_t buildEpochBase = 0;
static bool ntpSynced = false;
static int lastSeenPendingTotal = 0;  // Track previous pending count to detect new items
static unsigned long lastChimeMs = 0; // Debounce chimes (once per 30s)

struct ReleaseCacheEntry {
  bool used = false;
  bool isMovie = false;
  int tmdbId = 0;
  bool hasReleaseDate = false;
  time_t releaseEpoch = 0;
};

static ReleaseCacheEntry releaseCache[200];
static int releaseCacheNext = 0;

static const int JF_BOX_X = 8;
static const int JF_BOX_Y = 60;
static const int JF_BOX_W = 224;
static const int JF_BOX_H = 108;

static const int JS_BOX_X = 8;
static const int JS_BOX_Y = 174;
static const int JS_BOX_W = 224;
static const int JS_BOX_H = 126;

static const int BTN_REFRESH_X = 164;
static const int BTN_REFRESH_Y = 8;
static const int BTN_REFRESH_W = 70;
static const int BTN_REFRESH_H = 20;

static const int BTN_JF_RESTART_X = 170;
static const int BTN_JF_RESTART_Y = 66;
static const int BTN_JF_RESTART_W = 58;
static const int BTN_JF_RESTART_H = 18;

static const int BTN_JS_RESTART_X = 170;
static const int BTN_JS_RESTART_Y = 180;
static const int BTN_JS_RESTART_W = 58;
static const int BTN_JS_RESTART_H = 18;

static const int BTN_REBOOT_X = 8;
static const int BTN_REBOOT_Y = 302;
static const int BTN_REBOOT_W = 70;
static const int BTN_REBOOT_H = 14;

String trimSlash(String value) {
  value.trim();
  while (value.endsWith("/")) {
    value.remove(value.length() - 1);
  }
  return value;
}

String sanitizeHost(String value) {
  value.trim();
  value.toLowerCase();
  String out = "";
  for (uint16_t i = 0; i < value.length(); i++) {
    char ch = value.charAt(i);
    bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
    if (ok) out += ch;
  }
  if (out.length() == 0) out = "homelab-cyd";
  if (out.length() > 32) out = out.substring(0, 32);
  return out;
}

String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

void loadConfig() {
  prefs.begin("homelab", false);
  cfgHostname = sanitizeHost(prefs.getString("hostname", "homelab-cyd"));
  cfgJellyfinBase = trimSlash(prefs.getString("jf_base", JELLYFIN_BASE_URL));
  cfgJellyfinKey = prefs.getString("jf_key", JELLYFIN_API_KEY);
  cfgJellyseerrBase = trimSlash(prefs.getString("js_base", JELLYSEERR_BASE_URL));
  cfgJellyseerrKey = prefs.getString("js_key", JELLYSEERR_API_KEY);
  cfgJellyseerrRestart = trimSlash(prefs.getString("js_restart", JELLYSEERR_RESTART_URL));
  cfgRefreshIntervalMs = prefs.getULong("interval", REFRESH_INTERVAL_MS);
  if (cfgRefreshIntervalMs < 10000) {
    cfgRefreshIntervalMs = REFRESH_INTERVAL_MS;
  }
}

void saveConfig() {
  prefs.putString("hostname", cfgHostname);
  prefs.putString("jf_base", cfgJellyfinBase);
  prefs.putString("jf_key", cfgJellyfinKey);
  prefs.putString("js_base", cfgJellyseerrBase);
  prefs.putString("js_key", cfgJellyseerrKey);
  prefs.putString("js_restart", cfgJellyseerrRestart);
  prefs.putULong("interval", cfgRefreshIntervalMs);
}

void applyHostname() {
  WiFi.setHostname(cfgHostname.c_str());

  if (mdnsReady) {
    MDNS.end();
    mdnsReady = false;
  }

  if (MDNS.begin(cfgHostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    mdnsReady = true;
  }
}

void ensureConfigServerStarted() {
  if (configServerReady) {
    return;
  }

  configServer.on("/", HTTP_GET, []() {
    String ip = WiFi.localIP().toString();
    String page;
    page.reserve(4000);
    page += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<title>HomeLab CYD Config</title><style>body{font-family:Arial,sans-serif;max-width:680px;margin:20px auto;padding:0 12px;}";
    page += "input{width:100%;padding:8px;margin:6px 0 12px;box-sizing:border-box;}button{padding:10px 14px;}small{color:#666}</style></head><body>";
    page += "<h2>HomeLab CYD Config</h2>";
    page += "<p><strong>IP:</strong> " + htmlEscape(ip) + "<br><strong>Hostname:</strong> " + htmlEscape(cfgHostname) + ".local</p>";
    page += "<p><small>Jellyfin: " + htmlEscape(jellyfin.online ? String("Online") : jellyfin.error) + " · Jellyseerr: " + htmlEscape(jellyseerr.online ? String("Online") : jellyseerr.error) + "</small></p>";
    page += "<form method='POST' action='/save'>";
    page += "<label>Hostname (.local)</label><input name='hostname' value='" + htmlEscape(cfgHostname) + "'/>";
    page += "<label>Jellyfin Base URL</label><input name='jf_base' value='" + htmlEscape(cfgJellyfinBase) + "'/>";
    page += "<label>Jellyfin API Key</label><input name='jf_key' value='" + htmlEscape(cfgJellyfinKey) + "'/>";
    page += "<label>Jellyseerr Base URL</label><input name='js_base' value='" + htmlEscape(cfgJellyseerrBase) + "'/>";
    page += "<label>Jellyseerr API Key</label><input name='js_key' value='" + htmlEscape(cfgJellyseerrKey) + "'/>";
    page += "<label>Jellyseerr Restart URL (optional)</label><input name='js_restart' value='" + htmlEscape(cfgJellyseerrRestart) + "'/>";
    page += "<label>Refresh Interval (ms)</label><input name='interval' value='" + String(cfgRefreshIntervalMs) + "'/>";
    page += "<button type='submit'>Save & Apply</button>";
    page += "</form></body></html>";
    configServer.send(200, "text/html", page);
  });

  configServer.on("/save", HTTP_POST, []() {
    if (configServer.hasArg("hostname")) cfgHostname = sanitizeHost(configServer.arg("hostname"));
    if (configServer.hasArg("jf_base")) cfgJellyfinBase = trimSlash(configServer.arg("jf_base"));
    if (configServer.hasArg("jf_key")) cfgJellyfinKey = configServer.arg("jf_key");
    if (configServer.hasArg("js_base")) cfgJellyseerrBase = trimSlash(configServer.arg("js_base"));
    if (configServer.hasArg("js_key")) cfgJellyseerrKey = configServer.arg("js_key");
    if (configServer.hasArg("js_restart")) cfgJellyseerrRestart = trimSlash(configServer.arg("js_restart"));
    if (configServer.hasArg("interval")) {
      uint32_t parsed = (uint32_t)configServer.arg("interval").toInt();
      if (parsed >= 10000) cfgRefreshIntervalMs = parsed;
    }

    saveConfig();
    applyHostname();
    refreshAll();
    lastAction = "Config updated";
    configServer.sendHeader("Location", "/", true);
    configServer.send(303, "text/plain", "");
    render();
  });

  configServer.begin();
  configServerReady = true;
}

bool httpGetJson(const String &url, DynamicJsonDocument &doc, String &errorOut, const char *headerName = nullptr, const char *headerValue = nullptr, uint16_t timeoutMs = 20000) {
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(url)) {
    errorOut = "begin failed";
    return false;
  }

  if (headerName && headerValue && strlen(headerValue) > 0) {
    http.addHeader(headerName, headerValue);
  }

  int code = http.GET();
  if (code < 200 || code >= 300) {
    errorOut = String("HTTP ") + code;
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    errorOut = String("JSON ") + err.c_str();
    return false;
  }

  errorOut = "";
  return true;
}

bool httpGetJsonStream(const String &url, DynamicJsonDocument &doc, String &errorOut, const char *headerName = nullptr, const char *headerValue = nullptr, DynamicJsonDocument *filterDoc = nullptr, uint16_t timeoutMs = 20000) {
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(url)) {
    errorOut = "begin failed";
    return false;
  }

  if (headerName && headerValue && strlen(headerValue) > 0) {
    http.addHeader(headerName, headerValue);
  }

  int code = http.GET();
  if (code < 200 || code >= 300) {
    errorOut = String("HTTP ") + code;
    http.end();
    return false;
  }

  DeserializationError err;
  if (filterDoc != nullptr) {
    err = deserializeJson(doc, *http.getStreamPtr(), DeserializationOption::Filter(*filterDoc));
  } else {
    err = deserializeJson(doc, *http.getStreamPtr());
  }
  http.end();

  if (err) {
    errorOut = String("JSON ") + err.c_str();
    return false;
  }

  errorOut = "";
  return true;
}

bool fetchRequestTypeCounts(const char *filterName, int &movieOut, int &tvOut, String &errorOut) {
  movieOut = 0;
  tvOut = 0;

  if (cfgJellyseerrKey.length() == 0) {
    errorOut = "Missing API key";
    return false;
  }

  DynamicJsonDocument filterDoc(256);
  filterDoc["pageInfo"]["results"] = true;
  filterDoc["results"][0]["type"] = true;

  const int pageSize = 25;
  int skip = 0;
  int total = -1;
  int safety = 0;

  while (safety < 120) {
    String url = cfgJellyseerrBase + "/api/v1/request?sort=added&take=" + String(pageSize) + "&skip=" + String(skip) + "&filter=" + filterName;

    DynamicJsonDocument doc(4096);
    String pageErr;
    if (!httpGetJsonStream(url, doc, pageErr, "X-Api-Key", cfgJellyseerrKey.c_str(), &filterDoc)) {
      errorOut = pageErr;
      return false;
    }

    if (total < 0) {
      total = doc["pageInfo"]["results"] | 0;
      if (total == 0) {
        errorOut = "";
        return true;
      }
    }

    JsonArray results = doc["results"].as<JsonArray>();
    int pageCount = 0;
    for (JsonVariant item : results) {
      String type = item["type"] | "";
      if (type == "movie") {
        movieOut++;
      } else if (type == "tv") {
        tvOut++;
      }
      pageCount++;
    }

    if (pageCount <= 0) {
      break;
    }

    skip += pageCount;
    if (skip >= total) {
      break;
    }
    safety++;
  }

  errorOut = "";
  return true;
}

bool httpPost(const String &url, const char *headerName = nullptr, const char *headerValue = nullptr) {
  if (url.length() == 0) return false;
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(url)) {
    return false;
  }
  if (headerName && headerValue && strlen(headerValue) > 0) {
    http.addHeader(headerName, headerValue);
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST("{}");
  http.end();
  return code >= 200 && code < 300;
}

time_t parseIsoUtc(const String &value) {
  if (value.length() < 10) return 0;
  int year = value.substring(0, 4).toInt();
  int month = value.substring(5, 7).toInt();
  int day = value.substring(8, 10).toInt();
  int hour = 0;
  int minute = 0;
  int second = 0;

  if (value.length() >= 19) {
    hour = value.substring(11, 13).toInt();
    minute = value.substring(14, 16).toInt();
    second = value.substring(17, 19).toInt();
  }

  struct tm tmv;
  memset(&tmv, 0, sizeof(tmv));
  tmv.tm_year = year - 1900;
  tmv.tm_mon = month - 1;
  tmv.tm_mday = day;
  tmv.tm_hour = hour;
  tmv.tm_min = minute;
  tmv.tm_sec = second;
  tmv.tm_isdst = 0;

  return mktime(&tmv);
}

time_t parseBuildEpoch() {
  String buildDate = String(__DATE__); // "Mmm dd yyyy"
  String buildTime = String(__TIME__); // "hh:mm:ss"
  if (buildDate.length() < 11 || buildTime.length() < 8) return 0;

  String monStr = buildDate.substring(0, 3);
  int month = 0;
  if (monStr == "Jan") month = 1;
  else if (monStr == "Feb") month = 2;
  else if (monStr == "Mar") month = 3;
  else if (monStr == "Apr") month = 4;
  else if (monStr == "May") month = 5;
  else if (monStr == "Jun") month = 6;
  else if (monStr == "Jul") month = 7;
  else if (monStr == "Aug") month = 8;
  else if (monStr == "Sep") month = 9;
  else if (monStr == "Oct") month = 10;
  else if (monStr == "Nov") month = 11;
  else if (monStr == "Dec") month = 12;
  if (month == 0) return 0;

  int day = buildDate.substring(4, 6).toInt();
  int year = buildDate.substring(7, 11).toInt();
  int hour = buildTime.substring(0, 2).toInt();
  int minute = buildTime.substring(3, 5).toInt();
  int second = buildTime.substring(6, 8).toInt();

  struct tm tmv;
  memset(&tmv, 0, sizeof(tmv));
  tmv.tm_year = year - 1900;
  tmv.tm_mon = month - 1;
  tmv.tm_mday = day;
  tmv.tm_hour = hour;
  tmv.tm_min = minute;
  tmv.tm_sec = second;
  tmv.tm_isdst = 0;
  return mktime(&tmv);
}

time_t currentEpochSafe() {
  time_t nowEpoch = time(nullptr);
  if (nowEpoch > 100000) {
    ntpSynced = true;
    return nowEpoch;
  }
  if (buildEpochBase > 100000) {
    return buildEpochBase + (millis() / 1000);
  }
  return 0;
}

int findReleaseCache(bool isMovie, int tmdbId) {
  for (int i = 0; i < 200; i++) {
    if (releaseCache[i].used && releaseCache[i].isMovie == isMovie && releaseCache[i].tmdbId == tmdbId) {
      return i;
    }
  }
  return -1;
}

void putReleaseCache(bool isMovie, int tmdbId, bool hasReleaseDate, time_t releaseEpoch) {
  int idx = releaseCacheNext;
  releaseCache[idx].used = true;
  releaseCache[idx].isMovie = isMovie;
  releaseCache[idx].tmdbId = tmdbId;
  releaseCache[idx].hasReleaseDate = hasReleaseDate;
  releaseCache[idx].releaseEpoch = releaseEpoch;
  releaseCacheNext = (releaseCacheNext + 1) % 200;
}

bool fetchMediaReleaseDate(bool isMovie, int tmdbId, bool &hasReleaseDate, time_t &releaseEpoch, String &errorOut) {
  hasReleaseDate = false;
  releaseEpoch = 0;

  if (tmdbId <= 0) {
    errorOut = "invalid tmdbId";
    return false;
  }

  int cached = findReleaseCache(isMovie, tmdbId);
  if (cached >= 0) {
    hasReleaseDate = releaseCache[cached].hasReleaseDate;
    releaseEpoch = releaseCache[cached].releaseEpoch;
    errorOut = "";
    return true;
  }

  String url = cfgJellyseerrBase + (isMovie ? "/api/v1/movie/" : "/api/v1/tv/") + String(tmdbId);

  DynamicJsonDocument filterDoc(256);
  if (isMovie) {
    filterDoc["releaseDate"] = true;
  } else {
    filterDoc["firstAirDate"] = true;
  }

  DynamicJsonDocument detailsDoc(1024);
  String reqErr;
  if (!httpGetJsonStream(url, detailsDoc, reqErr, "X-Api-Key", cfgJellyseerrKey.c_str(), &filterDoc, 20000)) {
    errorOut = reqErr;
    return false;
  }

  String dateStr = isMovie ? String((const char *)(detailsDoc["releaseDate"] | ""))
                           : String((const char *)(detailsDoc["firstAirDate"] | ""));
  time_t parsed = parseIsoUtc(dateStr);
  bool hasDate = parsed > 0;

  putReleaseCache(isMovie, tmdbId, hasDate, parsed);
  hasReleaseDate = hasDate;
  releaseEpoch = parsed;
  errorOut = "";
  return true;
}

bool fetchProcessingReleaseBuckets(
  int &movieUnder10dOut,
  int &tvUnder10dOut,
  int &movieOver10dOut,
  int &tvOver10dOut,
  int &movieUnreleasedOut,
  int &tvUnreleasedOut,
  String &errorOut
) {
  movieUnder10dOut = 0;
  tvUnder10dOut = 0;
  movieOver10dOut = 0;
  tvOver10dOut = 0;
  movieUnreleasedOut = 0;
  tvUnreleasedOut = 0;

  time_t nowEpoch = currentEpochSafe();
  if (nowEpoch <= 0) {
    errorOut = "Clock unavailable";
    return false;
  }

  DynamicJsonDocument filterDoc(512);
  filterDoc["pageInfo"]["results"] = true;
  filterDoc["results"][0]["type"] = true;
  filterDoc["results"][0]["createdAt"] = true;
  filterDoc["results"][0]["updatedAt"] = true;
  filterDoc["results"][0]["media"]["tmdbId"] = true;

  const int pageSize = 25;
  int skip = 0;
  int total = -1;
  int safety = 0;

  while (safety < 120) {
    String url = cfgJellyseerrBase + "/api/v1/request?sort=added&take=" + String(pageSize) + "&skip=" + String(skip) + "&filter=processing";
    DynamicJsonDocument doc(4096);
    String pageErr;
    if (!httpGetJsonStream(url, doc, pageErr, "X-Api-Key", cfgJellyseerrKey.c_str(), &filterDoc, 20000)) {
      errorOut = pageErr;
      return false;
    }

    if (total < 0) {
      total = doc["pageInfo"]["results"] | 0;
      if (total == 0) {
        errorOut = "";
        return true;
      }
    }

    JsonArray results = doc["results"].as<JsonArray>();
    int pageCount = 0;
    for (JsonVariant item : results) {
      String type = item["type"] | "";
      bool isMovie = type != "tv";
      int tmdbId = item["media"]["tmdbId"] | 0;

      String approvedAtStr = item["updatedAt"] | "";
      if (approvedAtStr.length() == 0) {
        approvedAtStr = item["createdAt"] | "";
      }
      time_t approvedEpoch = parseIsoUtc(approvedAtStr);
      if (approvedEpoch <= 0) {
        approvedEpoch = nowEpoch;
      }

      bool hasReleaseDate = false;
      time_t releaseEpoch = 0;
      String detailErr;
      if (!fetchMediaReleaseDate(isMovie, tmdbId, hasReleaseDate, releaseEpoch, detailErr)) {
        errorOut = detailErr.length() ? detailErr : "release lookup failed";
        return false;
      }

      if (!hasReleaseDate || releaseEpoch > nowEpoch) {
        if (isMovie) movieUnreleasedOut++;
        else tvUnreleasedOut++;
      } else {
        time_t actionableStart = approvedEpoch;
        if (releaseEpoch > actionableStart) {
          actionableStart = releaseEpoch;
        }

        long days = (long)((nowEpoch - actionableStart) / 86400);
        if (days < 0) {
          days = 0;
        }
        if (days < 10) {
          if (isMovie) movieUnder10dOut++;
          else tvUnder10dOut++;
        } else {
          if (isMovie) movieOver10dOut++;
          else tvOver10dOut++;
        }
      }
      pageCount++;
    }

    if (pageCount <= 0) break;
    skip += pageCount;
    if (skip >= total) break;
    safety++;
  }

  errorOut = "";
  return true;
}

void fetchJellyfin() {
  jellyfin = JellyfinStatus();

  if (cfgJellyfinKey.length() == 0) {
    jellyfin.error = "Missing API key";
    return;
  }

  DynamicJsonDocument sessionsDoc(4096);
  String err;
  bool sessionsOk = httpGetJson(
    cfgJellyfinBase + "/Sessions?ActiveWithinSeconds=600",
    sessionsDoc,
    err,
    "X-Emby-Token",
    cfgJellyfinKey.c_str(),
    30000
  );
  if (!sessionsOk) {
    jellyfin.error = err.length() ? err : "API unavailable";
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

  DynamicJsonDocument countsDoc(2048);
  String countsErr;
  if (httpGetJson(cfgJellyfinBase + "/Items/Counts", countsDoc, countsErr, "X-Emby-Token", cfgJellyfinKey.c_str())) {
    jellyfin.movieLibrary = countsDoc["MovieCount"] | 0;
    jellyfin.tvLibrary = countsDoc["SeriesCount"] | 0;
  }
}

void fetchJellyseerr() {
  jellyseerr = JellyseerrStatus();

  if (cfgJellyseerrKey.length() == 0) {
    jellyseerr.error = "Missing API key";
    return;
  }

  DynamicJsonDocument statusDoc(2048);
  String errStatus;
  bool statusOk = httpGetJson(cfgJellyseerrBase + "/api/v1/status", statusDoc, errStatus, "X-Api-Key", cfgJellyseerrKey.c_str());

  if (!statusOk) {
    jellyseerr.error = errStatus;
    return;
  }

  jellyseerr.online = true;
  int moviePendingApproval = 0;
  int tvPendingApproval = 0;
  String pendingErr;
  if (!fetchRequestTypeCounts("pending", moviePendingApproval, tvPendingApproval, pendingErr)) {
    jellyseerr.error = pendingErr.length() ? pendingErr : "pending fetch failed";
    jellyseerr.online = false;
    return;
  }

  int movieQueueUnder10 = 0;
  int tvQueueUnder10 = 0;
  int movieQueueOver10 = 0;
  int tvQueueOver10 = 0;
  int moviePendingUnreleased = 0;
  int tvPendingUnreleased = 0;
  String typeErr;
  if (!fetchProcessingReleaseBuckets(
      movieQueueUnder10,
      tvQueueUnder10,
      movieQueueOver10,
      tvQueueOver10,
      moviePendingUnreleased,
      tvPendingUnreleased,
      typeErr)) {
    jellyseerr.error = typeErr.length() ? typeErr : "processing split failed";
    jellyseerr.online = false;
    return;
  }

  jellyseerr.moviePendingApproval = moviePendingApproval;
  jellyseerr.tvPendingApproval = tvPendingApproval;
  jellyseerr.movieQueuedReleasedUnder10d = movieQueueUnder10;
  jellyseerr.tvQueuedReleasedUnder10d = tvQueueUnder10;
  jellyseerr.movieQueuedReleasedOver10d = movieQueueOver10;
  jellyseerr.tvQueuedReleasedOver10d = tvQueueOver10;
  jellyseerr.moviePendingUnreleased = moviePendingUnreleased;
  jellyseerr.tvPendingUnreleased = tvPendingUnreleased;
}

String sinceLastRefreshText() {
  if (lastRefreshMs == 0) return "--";
  uint32_t sec = (millis() - lastRefreshMs) / 1000;
  if (sec < 60) return String(sec) + "s";
  uint32_t min = sec / 60;
  if (min < 60) return String(min) + "m";
  uint32_t hr = min / 60;
  return String(hr) + "h";
}

void checkAndAlertPending() {
  int currentPendingTotal = jellyseerr.moviePendingApproval + jellyseerr.tvPendingApproval;
  
  // Detect NEW pending items (increase from last seen)
  if (currentPendingTotal > lastSeenPendingTotal && currentPendingTotal > 0) {
    unsigned long nowMs = millis();
    if (nowMs - lastChimeMs >= 30000) { // Debounce: allow chime once per 30 seconds
      // Visual alert: flash screen
      tft.fillScreen(TFT_RED);
      delay(100);
      tft.fillScreen(TFT_BLACK);
      delay(100);
      tft.fillScreen(TFT_RED);
      delay(100);
      tft.fillScreen(TFT_BLACK);
      lastChimeMs = nowMs;
      lastAction = "NEW pending items!";
    }
  }
  
  lastSeenPendingTotal = currentPendingTotal;
}

void refreshAll() {
  fetchJellyfin();
  fetchJellyseerr();
  checkAndAlertPending();
  lastRefreshMs = millis();
}

void drawHeader() {
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  tft.print("HomeLab AIO");

  tft.drawRect(BTN_REFRESH_X, BTN_REFRESH_Y, BTN_REFRESH_W, BTN_REFRESH_H, TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(BTN_REFRESH_X + 8, BTN_REFRESH_Y + 6);
  tft.print("REFRESH");

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(8, 30);
  tft.print("WiFi: ");
  tft.print(WiFi.localIP());
  tft.setCursor(8, 44);
  tft.print("Host: ");
  tft.print(cfgHostname);
  tft.print(".local");

  tft.setCursor(166, 30);
  tft.print("Upd ");
  tft.print(sinceLastRefreshText());
}

// Two-column row: small dim label at (14, valY+4), large value at (110, valY).
// valY is the TextSize(2) baseline; label is vertically centred in the 16px row.
#define DRAW_ROW_LABEL(labelY, txt) \
  tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK); \
  tft.setCursor(14, (labelY)); tft.print(txt)
#define DRAW_ROW_VAL(valY, col, fmt, ...) \
  tft.setTextSize(2); tft.setTextColor((col), TFT_BLACK); \
  tft.setCursor(110, (valY)); tft.printf(fmt, ##__VA_ARGS__)

void drawServiceBoxes() {
  tft.drawRoundRect(JF_BOX_X, JF_BOX_Y, JF_BOX_W, JF_BOX_H, 8, TFT_DARKGREY);
  tft.drawRoundRect(JS_BOX_X, JS_BOX_Y, JS_BOX_W, JS_BOX_H, 8, TFT_DARKGREY);

  // ── Jellyfin panel ──────────────────────────────────────────────────────────
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(14, 68);
  tft.print("Jellyfin");
  tft.drawRect(BTN_JF_RESTART_X, BTN_JF_RESTART_Y, BTN_JF_RESTART_W, BTN_JF_RESTART_H, TFT_RED);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(BTN_JF_RESTART_X + 8, BTN_JF_RESTART_Y + 6);
  tft.print("RESTART");

  if (jellyfin.online) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(14, 88);
    tft.print("Online");

    // Row 1 (Y:100): Active Streams
    DRAW_ROW_LABEL(104, "Active Streams");
    DRAW_ROW_VAL(100, TFT_WHITE, "%d", jellyfin.activeStreams);

    // Row 2 (Y:120): Sessions
    DRAW_ROW_LABEL(124, "Sessions");
    DRAW_ROW_VAL(120, TFT_WHITE, "%d", jellyfin.sessions);

    // Row 3 (Y:140): Library
    DRAW_ROW_LABEL(144, "Library");
    DRAW_ROW_VAL(140, TFT_WHITE, "%dm/%dt", jellyfin.movieLibrary, jellyfin.tvLibrary);
  } else {
    tft.setTextSize(1);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(14, 88);
    tft.print("Offline");
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(14, 108);
    tft.print(jellyfin.error);
  }

  // ── Jellyseerr panel ────────────────────────────────────────────────────────
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(14, 182);
  tft.print("Jellyseerr");
  tft.drawRect(BTN_JS_RESTART_X, BTN_JS_RESTART_Y, BTN_JS_RESTART_W, BTN_JS_RESTART_H, TFT_RED);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(BTN_JS_RESTART_X + 8, BTN_JS_RESTART_Y + 6);
  tft.print("RESTART");

  if (jellyseerr.online) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(14, 202);
    tft.print("Online");

    int pendingApprovalTotal = jellyseerr.moviePendingApproval + jellyseerr.tvPendingApproval;
    int queueOverTotal = jellyseerr.movieQueuedReleasedOver10d + jellyseerr.tvQueuedReleasedOver10d;

    // Row 1 (Y:214): Pending Approval
    DRAW_ROW_LABEL(218, "Pending");
    DRAW_ROW_VAL(214, pendingApprovalTotal > 0 ? TFT_RED : TFT_WHITE,
                 "%dm/%dt", jellyseerr.moviePendingApproval, jellyseerr.tvPendingApproval);

    // Row 2 (Y:234): Queued – recently released (<10d)
    DRAW_ROW_LABEL(238, "Queued New");
    DRAW_ROW_VAL(234, TFT_WHITE,
                 "%dm/%dt", jellyseerr.movieQueuedReleasedUnder10d, jellyseerr.tvQueuedReleasedUnder10d);

    // Row 3 (Y:254): Queued – stale (>=10d)
    DRAW_ROW_LABEL(258, "Queued Old");
    DRAW_ROW_VAL(254, queueOverTotal > 0 ? TFT_RED : TFT_WHITE,
                 "%dm/%dt", jellyseerr.movieQueuedReleasedOver10d, jellyseerr.tvQueuedReleasedOver10d);

    // Row 4 (Y:274): Pending release
    DRAW_ROW_LABEL(278, "Unreleased");
    DRAW_ROW_VAL(274, TFT_WHITE,
                 "%dm/%dt", jellyseerr.moviePendingUnreleased, jellyseerr.tvPendingUnreleased);
  } else {
    tft.setTextSize(1);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(14, 202);
    tft.print("Offline");
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(14, 222);
    tft.print(jellyseerr.error);
  }
}

#undef DRAW_ROW_LABEL
#undef DRAW_ROW_VAL

void drawFooterStatus() {
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(1);
  tft.fillRect(0, 300, SCREEN_W, 20, TFT_BLACK);
  
  // Reboot button (bottom left)
  tft.drawRect(BTN_REBOOT_X, BTN_REBOOT_Y, BTN_REBOOT_W, BTN_REBOOT_H, TFT_ORANGE);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setCursor(BTN_REBOOT_X + 4, BTN_REBOOT_Y + 2);
  tft.print("REBOOT");
  
  // Status text (rest of footer)
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(90, 306);
  tft.print("Last: ");
  tft.print(lastAction);
}

void render() {
  tft.fillScreen(TFT_BLACK);
  drawHeader();
  drawServiceBoxes();
  drawFooterStatus();
}

bool pointInRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
}

void handleTouch() {
  bool touching = false;
  int x = 0;
  int y = 0;

#if S3_CAP_TOUCH
  touching = readFt6336Touch(x, y);
#else
  touching = touchscreen.touched();
#endif

  if (!touching) {
    wasTouching = false;
    return;
  }

  if (wasTouching) {
    return;
  }
  wasTouching = true;

#if !S3_CAP_TOUCH
  TS_Point p = touchscreen.getPoint();
  y = map(p.x, 200, 3700, 0, SCREEN_H);
  x = map(p.y, 240, 3800, 0, SCREEN_W);
#endif

  if (pointInRect(x, y, BTN_REFRESH_X, BTN_REFRESH_Y, BTN_REFRESH_W, BTN_REFRESH_H)) {
    lastAction = "Manual refresh";
    refreshAll();
    render();
    return;
  }

  if (pointInRect(x, y, BTN_JF_RESTART_X, BTN_JF_RESTART_Y, BTN_JF_RESTART_W, BTN_JF_RESTART_H)) {
    bool ok = httpPost(cfgJellyfinBase + "/System/Restart", "X-Emby-Token", cfgJellyfinKey.c_str());
    lastAction = ok ? "Restart Jellyfin sent" : "Restart Jellyfin failed";
    refreshAll();
    render();
    return;
  }

  if (pointInRect(x, y, BTN_JS_RESTART_X, BTN_JS_RESTART_Y, BTN_JS_RESTART_W, BTN_JS_RESTART_H)) {
    bool ok = false;
    if (cfgJellyseerrRestart.length() > 0) {
      ok = httpPost(cfgJellyseerrRestart, "X-Api-Key", cfgJellyseerrKey.c_str());
    }
    lastAction = ok ? "Restart Jellyseerr sent" : "Jellyseerr restart N/A";
    refreshAll();
    render();
    return;
  }

  if (pointInRect(x, y, BTN_REBOOT_X, BTN_REBOOT_Y, BTN_REBOOT_W, BTN_REBOOT_H)) {
    lastAction = "Device rebooting...";
    render();
    delay(500);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
#if defined(ARDUINO_USB_CDC_ON_BOOT)
  delay(3000);
#endif

  pinMode(LCD_ENABLE_PIN, OUTPUT);
  pinMode(LCD_CS_PIN, OUTPUT);
  pinMode(LCD_DC_PIN, OUTPUT);
  digitalWrite(LCD_ENABLE_PIN, HIGH);
  digitalWrite(LCD_CS_PIN, HIGH);
  digitalWrite(LCD_DC_PIN, HIGH);
  delay(50);

  tft.init();
  applyVendorIli9341Init();
  tft.setRotation(0);
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
  enableBacklightFailsafe();

  tft.fillScreen(TFT_RED);
  delay(250);
  tft.fillScreen(TFT_GREEN);
  delay(250);
  tft.fillScreen(TFT_BLUE);
  delay(250);
  tft.fillScreen(TFT_BLACK);

#if S3_CAP_TOUCH
  Wire.begin(FT6336_SDA, FT6336_SCL);
  pinMode(FT6336_RST, OUTPUT);
  digitalWrite(FT6336_RST, LOW);
  delay(5);
  digitalWrite(FT6336_RST, HIGH);
  delay(20);
  pinMode(FT6336_INT, INPUT);
#else
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(0);
#endif

  loadConfig();
  buildEpochBase = parseBuildEpoch();
  drawBootScreen("Starting...");

  WiFiManager wm;
  configureWiFiManagerPortal(wm);

  drawBootScreen("Connecting WiFi", "Using saved credentials...");
  bool connected = wm.autoConnect(CAPTIVE_SSID);
  if (!connected) {
    drawBootScreen("WiFi failed", "Rebooting...");
    delay(1500);
    ESP.restart();
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  for (int i = 0; i < 20; i++) {
    if (time(nullptr) > 100000) {
      ntpSynced = true;
      break;
    }
    delay(150);
  }

  applyHostname();
  ensureConfigServerStarted();

  lastAction = "Connected";
  refreshAll();
  render();
}

void loop() {
  if (!ntpSynced && (millis() % 15000 < 25)) {
    if (time(nullptr) > 100000) {
      ntpSynced = true;
    }
  }

  if (configServerReady) {
    configServer.handleClient();
  }

  handleTouch();

  if (millis() - lastRefreshMs >= cfgRefreshIntervalMs) {
    refreshAll();
    render();
  }

  delay(20);
}

void enableBacklightFailsafe() {
  const int commonPins[] = {LCD_ENABLE_PIN, LCD_BACKLIGHT_PIN, 21, 38, 42, 45, 46, 47, 48};
  for (size_t i = 0; i < (sizeof(commonPins) / sizeof(commonPins[0])); i++) {
    int pin = commonPins[i];
    if (pin < 0 || pin > 48) continue;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
}

bool readFt6336Touch(int &xOut, int &yOut) {
  xOut = 0;
  yOut = 0;

  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom((uint8_t)FT6336_ADDR, (uint8_t)1) != 1) {
    return false;
  }
  uint8_t touches = Wire.read() & 0x0F;
  if (touches == 0) {
    return false;
  }

  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(0x03);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom((uint8_t)FT6336_ADDR, (uint8_t)4) != 4) {
    return false;
  }

  uint8_t xh = Wire.read();
  uint8_t xl = Wire.read();
  uint8_t yh = Wire.read();
  uint8_t yl = Wire.read();

  int rawX = ((xh & 0x0F) << 8) | xl;
  int rawY = ((yh & 0x0F) << 8) | yl;

  xOut = rawX;
  yOut = rawY;
  if (xOut < 0) xOut = 0;
  if (yOut < 0) yOut = 0;
  if (xOut >= SCREEN_W) xOut = SCREEN_W - 1;
  if (yOut >= SCREEN_H) yOut = SCREEN_H - 1;
  return true;
}
