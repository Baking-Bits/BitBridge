#include <Arduino.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include <lvgl.h>
#if defined(USE_RAW_LCD)
#include "raw_tft_ili9341.h"
#else
#include <TFT_eSPI.h>
#endif
#include <XPT2046_Touchscreen.h>
#include <Wire.h>
#include <Preferences.h>
#include "esp_system.h"
#include "translations.h"
#include "libs/qrcode/lv_qrcode.h"
#include "audio_es8311_min.h"

#if __has_include("homelab_secrets.h")
#include "homelab_secrets.h"
#endif

#if !defined(AURA_NO_PET)
#include "pet_poc.h"
#else
static const char *pet_poc_get_name() { return "Aura"; }
static bool pet_poc_set_name(const char *name) { LV_UNUSED(name); return false; }
static bool pet_poc_is_test_mode() { return false; }
static void pet_poc_set_test_mode(bool enabled) { LV_UNUSED(enabled); }
static void pet_poc_init() {}
static void pet_poc_loop() {}
static void pet_poc_deactivate() {}
static void pet_poc_reset_data() {}
#endif

#ifndef XPT2046_IRQ
#define XPT2046_IRQ 36   // T_IRQ
#endif
#ifndef XPT2046_MOSI
#define XPT2046_MOSI 32  // T_DIN
#endif
#ifndef XPT2046_MISO
#define XPT2046_MISO 39  // T_OUT
#endif
#ifndef XPT2046_CLK
#define XPT2046_CLK 25   // T_CLK
#endif
#ifndef XPT2046_CS
#define XPT2046_CS 33    // T_CS
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

#ifndef BATTERY_ADC_PIN
#define BATTERY_ADC_PIN -1
#endif
#ifndef BATTERY_CHARGE_PIN
#define BATTERY_CHARGE_PIN -1
#endif
#ifndef BATTERY_CHARGE_ACTIVE_LEVEL
#define BATTERY_CHARGE_ACTIVE_LEVEL LOW
#endif
#ifndef BATTERY_DIVIDER_RATIO
#define BATTERY_DIVIDER_RATIO 2.0f
#endif
#ifndef MIC_ADC_PIN
#define MIC_ADC_PIN -1
#endif
#ifndef SPEAKER_PIN
#define SPEAKER_PIN -1
#endif

#ifndef JELLYFIN_BASE_URL
#define JELLYFIN_BASE_URL ""
#endif
#ifndef JELLYFIN_API_KEY
#define JELLYFIN_API_KEY ""
#endif
#ifndef JELLYSEERR_BASE_URL
#define JELLYSEERR_BASE_URL ""
#endif
#ifndef JELLYSEERR_API_KEY
#define JELLYSEERR_API_KEY ""
#endif
#ifndef JELLYSEERR_RESTART_URL
#define JELLYSEERR_RESTART_URL ""
#endif

#ifndef AURA_ENABLE_HOMELAB
#define AURA_ENABLE_HOMELAB 1
#endif
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))

#define LATITUDE_DEFAULT "51.5074"
#define LONGITUDE_DEFAULT "-0.1278"
#define LOCATION_DEFAULT "London"
#define DEFAULT_CAPTIVE_SSID "Aura"
#define UPDATE_INTERVAL 600000UL  // 10 minutes

// Night mode starts at 10pm and ends at 6am
#define NIGHT_MODE_START_HOUR 22
#define NIGHT_MODE_END_HOUR 6

LV_FONT_DECLARE(lv_font_montserrat_latin_12);
LV_FONT_DECLARE(lv_font_montserrat_latin_14);
LV_FONT_DECLARE(lv_font_montserrat_latin_16);
LV_FONT_DECLARE(lv_font_montserrat_latin_20);
LV_FONT_DECLARE(lv_font_montserrat_latin_42);

static Language current_language = LANG_EN;

// Font selection based on language
const lv_font_t* get_font_12() {
  return &lv_font_montserrat_latin_12;
}

const lv_font_t* get_font_14() {
  return &lv_font_montserrat_latin_14;
}

const lv_font_t* get_font_16() {
  return &lv_font_montserrat_latin_16;
}

const lv_font_t* get_font_20() {
  return &lv_font_montserrat_latin_20;
}

const lv_font_t* get_font_42() {
  return &lv_font_montserrat_latin_42;
}

SPIClass touchscreenSPI = SPIClass(TOUCH_SPI_PORT);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
uint32_t draw_buf[DRAW_BUF_SIZE / 4];
static TFT_eSPI tft;
int x, y, z;

// Preferences
static Preferences prefs;
static bool use_fahrenheit = false;
static bool use_24_hour = false; 
static bool use_night_mode = false;
static char latitude[16] = LATITUDE_DEFAULT;
static char longitude[16] = LONGITUDE_DEFAULT;
static String location = String(LOCATION_DEFAULT);
static char dd_opts[512];
static DynamicJsonDocument geoDoc(8 * 1024);
static JsonArray geoResults;

// Screen dimming variables
static bool night_mode_active = false;
static bool temp_screen_wakeup_active = false;
static lv_timer_t *temp_screen_wakeup_timer = nullptr;

// UI components
static lv_obj_t *lbl_today_temp;
static lv_obj_t *lbl_today_feels_like;
static lv_obj_t *img_today_icon;
static lv_obj_t *lbl_forecast;
static lv_obj_t *box_daily;
static lv_obj_t *box_hourly;
static lv_obj_t *lbl_daily_day[7];
static lv_obj_t *lbl_daily_high[7];
static lv_obj_t *lbl_daily_low[7];
static lv_obj_t *img_daily[7];
static lv_obj_t *lbl_hourly[7];
static lv_obj_t *lbl_precipitation_probability[7];
static lv_obj_t *lbl_hourly_temp[7];
static lv_obj_t *img_hourly[7];
static lv_obj_t *lbl_loc;
static lv_obj_t *loc_ta;
static lv_obj_t *results_dd;
static lv_obj_t *btn_close_loc;
static lv_obj_t *btn_close_obj;
static lv_obj_t *kb;
static lv_obj_t *settings_win;
static lv_obj_t *location_win = nullptr;
static lv_obj_t *unit_switch;
static lv_obj_t *clock_24hr_switch;
static lv_obj_t *night_mode_switch;
static lv_obj_t *language_dropdown;
static lv_obj_t *testing_mode_switch;
static lv_obj_t *lbl_clock;
static lv_obj_t *lbl_homelab_status;
static lv_obj_t *homelab_screen = nullptr;
static lv_obj_t *lbl_hl_jf_status = nullptr;
static lv_obj_t *lbl_hl_jf_library = nullptr;
static lv_obj_t *lbl_hl_js_status = nullptr;
static lv_obj_t *lbl_hl_js_queue_new = nullptr;
static lv_obj_t *lbl_hl_js_queue_old = nullptr;
static lv_obj_t *lbl_hl_js_unreleased = nullptr;
static lv_obj_t *lbl_hl_updated = nullptr;
static lv_obj_t *lbl_hl_err = nullptr;
static lv_obj_t *btn_hl_jf_restart = nullptr;
static lv_obj_t *btn_hl_js_restart = nullptr;
static lv_obj_t *lbl_power_weather = nullptr;
static lv_obj_t *lbl_power_homelab = nullptr;
static lv_obj_t *lbl_hw_battery;
static lv_obj_t *lbl_hw_charging;
static lv_obj_t *lbl_hw_mic;
static lv_obj_t *lbl_hw_audio;
static String config_hostname = "aura";
static bool mdns_started = false;
static WebServer config_server(80);
static bool config_server_ready = false;

static String cfg_hl_jellyfin_base = JELLYFIN_BASE_URL;
static String cfg_hl_jellyfin_key = JELLYFIN_API_KEY;
static String cfg_hl_jellyseerr_base = JELLYSEERR_BASE_URL;
static String cfg_hl_jellyseerr_key = JELLYSEERR_API_KEY;
static String cfg_hl_jellyseerr_restart = JELLYSEERR_RESTART_URL;
static int last_hl_pending_total = -1;
static int cfgBatteryAdcPin = BATTERY_ADC_PIN;
static int cfgBatteryChargePin = BATTERY_CHARGE_PIN;
static int cfgBatteryChargeActiveLevel = BATTERY_CHARGE_ACTIVE_LEVEL;
static float cfgBatteryDividerRatio = BATTERY_DIVIDER_RATIO;
static int cfgMicAdcPin = MIC_ADC_PIN;
static int cfgSpeakerPin = SPEAKER_PIN;
static uint8_t cfgAlertVolumePct = 45;
static aura_audio::AudioState audioState;

// Weather icons
LV_IMG_DECLARE(icon_blizzard);
LV_IMG_DECLARE(icon_blowing_snow);
LV_IMG_DECLARE(icon_clear_night);
LV_IMG_DECLARE(icon_cloudy);
LV_IMG_DECLARE(icon_drizzle);
LV_IMG_DECLARE(icon_flurries);
LV_IMG_DECLARE(icon_haze_fog_dust_smoke);
LV_IMG_DECLARE(icon_heavy_rain);
LV_IMG_DECLARE(icon_heavy_snow);
LV_IMG_DECLARE(icon_isolated_scattered_tstorms_day);
LV_IMG_DECLARE(icon_isolated_scattered_tstorms_night);
LV_IMG_DECLARE(icon_mostly_clear_night);
LV_IMG_DECLARE(icon_mostly_cloudy_day);
LV_IMG_DECLARE(icon_mostly_cloudy_night);
LV_IMG_DECLARE(icon_mostly_sunny);
LV_IMG_DECLARE(icon_partly_cloudy);
LV_IMG_DECLARE(icon_partly_cloudy_night);
LV_IMG_DECLARE(icon_scattered_showers_day);
LV_IMG_DECLARE(icon_scattered_showers_night);
LV_IMG_DECLARE(icon_showers_rain);
LV_IMG_DECLARE(icon_sleet_hail);
LV_IMG_DECLARE(icon_snow_showers_snow);
LV_IMG_DECLARE(icon_strong_tstorms);
LV_IMG_DECLARE(icon_sunny);
LV_IMG_DECLARE(icon_tornado);
LV_IMG_DECLARE(icon_wintry_mix_rain_snow);

// Weather Images
LV_IMG_DECLARE(image_blizzard);
LV_IMG_DECLARE(image_blowing_snow);
LV_IMG_DECLARE(image_clear_night);
LV_IMG_DECLARE(image_cloudy);
LV_IMG_DECLARE(image_drizzle);
LV_IMG_DECLARE(image_flurries);
LV_IMG_DECLARE(image_haze_fog_dust_smoke);
LV_IMG_DECLARE(image_heavy_rain);
LV_IMG_DECLARE(image_heavy_snow);
LV_IMG_DECLARE(image_isolated_scattered_tstorms_day);
LV_IMG_DECLARE(image_isolated_scattered_tstorms_night);
LV_IMG_DECLARE(image_mostly_clear_night);
LV_IMG_DECLARE(image_mostly_cloudy_day);
LV_IMG_DECLARE(image_mostly_cloudy_night);
LV_IMG_DECLARE(image_mostly_sunny);
LV_IMG_DECLARE(image_partly_cloudy);
LV_IMG_DECLARE(image_partly_cloudy_night);
LV_IMG_DECLARE(image_scattered_showers_day);
LV_IMG_DECLARE(image_scattered_showers_night);
LV_IMG_DECLARE(image_showers_rain);
LV_IMG_DECLARE(image_sleet_hail);
LV_IMG_DECLARE(image_snow_showers_snow);
LV_IMG_DECLARE(image_strong_tstorms);
LV_IMG_DECLARE(image_sunny);
LV_IMG_DECLARE(image_tornado);
LV_IMG_DECLARE(image_wintry_mix_rain_snow);

void create_ui();
void fetch_and_update_weather();
void create_settings_window();
const lv_img_dsc_t *choose_image(int wmo_code, int is_day);
const lv_img_dsc_t *choose_icon(int wmo_code, int is_day);
bool do_ip_geolocation(String &resolved_name, double &resolved_lat, double &resolved_lon);
void apModeCallback(WiFiManager *mgr);
static bool weather_ui_ready();
static void switch_to_pet_screen();
void open_weather_screen(bool open_settings);
void on_pet_name_changed(const char *newName);
static void apply_pet_hostname();
static void ensure_config_server_started();
static String sanitize_hostname(const char *rawName);
static String html_escape(const String &input);
void handle_serial_wifi_provisioning();
void do_geocode_query(const char *q);
void wifi_splash_screen();
void screen_event_cb(lv_event_t *e);
static void screen_gesture_event_cb(lv_event_t *e);
void daily_cb(lv_event_t *e);
void hourly_cb(lv_event_t *e);
void create_location_dialog();
static void settings_event_handler(lv_event_t *e);
static void reset_confirm_yes_cb(lv_event_t *e);
static void reset_confirm_no_cb(lv_event_t *e);
static void wifi_setup_now_event_cb(lv_event_t *e);
static void open_homelab_event_cb(lv_event_t *e);
void create_homelab_screen();
static void refresh_homelab_screen_data();
static void update_hardware_test_labels();
static int read_mic_percent();
static bool read_battery(float &voltageOut, int &pctOut);
static int read_battery_charging_state();
static void update_global_power_status_labels();
static bool play_test_tone();
static void play_startup_ready_chime();
static bool http_get_json_with_header(const String &url, DynamicJsonDocument &doc, const char *headerName, const char *headerValue, String &errorOut, uint16_t timeoutMs = 20000);
static bool http_post_with_header(const String &url, const char *headerName, const char *headerValue, String &errorOut, uint16_t timeoutMs = 15000);
static bool fetch_homelab_summary(String &outText, String &errorOut);
struct HomelabMetrics {
  int jfActive = 0;
  int jfSessions = 0;
  int jfMovieLibrary = 0;
  int jfTvLibrary = 0;
  int jsMoviePending = 0;
  int jsTvPending = 0;
  int jsMovieQueueNew = 0;
  int jsTvQueueNew = 0;
  int jsMovieQueueOld = 0;
  int jsTvQueueOld = 0;
  int jsMovieUnreleased = 0;
  int jsTvUnreleased = 0;
};
static bool fetch_homelab_metrics(HomelabMetrics &metrics, String &errorOut);
static void homelab_restart_jf_event_cb(lv_event_t *e);
static void homelab_restart_js_event_cb(lv_event_t *e);
static void refresh_homelab_status_label();
static void apply_hardware_pin_modes();
static void enable_backlight_failsafe();
static bool read_ft6336_touch(int &xOut, int &yOut);
static void weather_lcd_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void apply_vendor_ili9341_init(TFT_eSPI &tftRef) {
  tftRef.startWrite();
  
  tftRef.writecommand(0xCF); tftRef.writedata(0x00); tftRef.writedata(0xC1); tftRef.writedata(0x30);
  tftRef.writecommand(0xED); tftRef.writedata(0x64); tftRef.writedata(0x03); tftRef.writedata(0x12); tftRef.writedata(0x81);
  tftRef.writecommand(0xE8); tftRef.writedata(0x85); tftRef.writedata(0x00); tftRef.writedata(0x78);
  tftRef.writecommand(0xCB); tftRef.writedata(0x39); tftRef.writedata(0x2C); tftRef.writedata(0x00); tftRef.writedata(0x34); tftRef.writedata(0x02);
  tftRef.writecommand(0xF7); tftRef.writedata(0x20);
  tftRef.writecommand(0xEA); tftRef.writedata(0x00); tftRef.writedata(0x00);
  tftRef.writecommand(0xC0); tftRef.writedata(0x13);
  tftRef.writecommand(0xC1); tftRef.writedata(0x13);
  tftRef.writecommand(0xC5); tftRef.writedata(0x22); tftRef.writedata(0x35);
  tftRef.writecommand(0xC7); tftRef.writedata(0xBD);
  tftRef.writecommand(0x21);
  tftRef.writecommand(0x36); tftRef.writedata(0x08);
  tftRef.writecommand(0xB6); tftRef.writedata(0x08); tftRef.writedata(0x82);
  tftRef.writecommand(0x3A); tftRef.writedata(0x55);
  tftRef.writecommand(0xF6); tftRef.writedata(0x01); tftRef.writedata(0x30);
  tftRef.writecommand(0xB1); tftRef.writedata(0x00); tftRef.writedata(0x1B);
  tftRef.writecommand(0xF2); tftRef.writedata(0x00);
  tftRef.writecommand(0x26); tftRef.writedata(0x01);
  
  const uint8_t e0[] = {0x0F,0x35,0x31,0x0B,0x0E,0x06,0x49,0xA7,0x33,0x07,0x0F,0x03,0x0C,0x0A,0x00};
  tftRef.writecommand(0xE0);
  for (uint8_t value : e0) tftRef.writedata(value);
  
  const uint8_t e1[] = {0x00,0x0A,0x0F,0x04,0x11,0x08,0x36,0x58,0x4D,0x07,0x10,0x0C,0x32,0x34,0x0F};
  tftRef.writecommand(0xE1);
  for (uint8_t value : e1) tftRef.writedata(value);
  
  tftRef.writecommand(0x11);
  tftRef.endWrite();
  delay(120);
  
  tftRef.startWrite();
  tftRef.writecommand(0x29);
  tftRef.endWrite();
}

static void weather_lcd_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t width = (uint32_t)(area->x2 - area->x1 + 1);
  uint32_t height = (uint32_t)(area->y2 - area->y1 + 1);
  uint32_t pixelCount = width * height;

  tft.startWrite();
  tft.setAddrWindow((uint16_t)area->x1, (uint16_t)area->y1, (uint16_t)width, (uint16_t)height);
  tft.pushColors((uint16_t *)px_map, pixelCount, false);
  tft.endWrite();

  lv_display_flush_ready(disp);
}

static void open_pet_event_cb(lv_event_t *e) {
  LV_UNUSED(e);
  switch_to_pet_screen();
}

static void open_settings_event_cb(lv_event_t *e) {
  LV_UNUSED(e);
  create_settings_window();
}

static String sanitize_hostname(const char *rawName) {
  String source = rawName ? String(rawName) : String("aura");
  source.trim();
  source.toLowerCase();

  String out = "";
  for (uint16_t i = 0; i < source.length(); i++) {
    char ch = source.charAt(i);
    bool allowed = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
    if (allowed) {
      out += ch;
      continue;
    }
    if (ch == ' ' || ch == '_' || ch == '.') {
      if (!out.endsWith("-")) {
        out += '-';
      }
    }
  }

  while (out.startsWith("-")) {
    out.remove(0, 1);
  }
  while (out.endsWith("-")) {
    out.remove(out.length() - 1, 1);
  }

  if (out.length() == 0) {
    out = "aura";
  }
  if (out.length() > 32) {
    out = out.substring(0, 32);
  }
  return out;
}

static String html_escape(const String &input) {
  String out = input;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  out.replace("'", "&#39;");
  return out;
}

static void apply_pet_hostname() {
  #if defined(AURA_NO_PET)
  config_hostname = "aura";
  #else
  config_hostname = sanitize_hostname(pet_poc_get_name());
  #endif
  WiFi.setHostname(config_hostname.c_str());

  if (mdns_started) {
    MDNS.end();
    mdns_started = false;
  }

  if (MDNS.begin(config_hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    mdns_started = true;
    Serial.printf("mDNS started at http://%s.local\n", config_hostname.c_str());
  } else {
    Serial.println("mDNS start failed");
  }
}

void on_pet_name_changed(const char *newName) {
  LV_UNUSED(newName);
  apply_pet_hostname();
}

static void ensure_config_server_started() {
  if (config_server_ready) {
    return;
  }

  config_server.on("/", HTTP_GET, []() {
    String host = config_hostname.length() ? config_hostname : String("aura");
    String ip = WiFi.localIP().toString();

    #if defined(AURA_NO_PET)
    String page;
    page.reserve(2400);
    page += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<title>Aura Config</title><style>body{font-family:Arial,sans-serif;max-width:620px;margin:24px auto;padding:0 12px;}small{color:#666}input,button,select{font-size:16px;padding:8px;margin-top:6px;}input,select{width:100%;box-sizing:border-box;}button{width:100%;margin-top:12px;}fieldset{margin-top:16px;padding:12px;}label{font-weight:600}</style></head><body>";
    page += "<h2>Aura Config</h2>";
    page += "<p><strong>Hostname:</strong> " + html_escape(host) + ".local<br><strong>IP:</strong> " + html_escape(ip) + "</p>";
    page += "<small>Pet controls are disabled in this build.</small>";
    page += "<fieldset><legend>Hardware pin mapping</legend>";
    page += "<small>Pins are locked to board defaults in firmware.</small><br>";
    page += "<small>Battery ADC GPIO: " + String(cfgBatteryAdcPin) + "</small><br>";
    if (cfgBatteryChargePin >= 0) {
      page += "<small>Charge status GPIO: " + String(cfgBatteryChargePin) + "</small><br>";
    } else {
      page += "<small>Charge status GPIO: n/a</small><br>";
    }
    page += "<small>Mic source: ES8311/I2S</small><br>";
    page += "<small>Speaker GPIO: " + String(cfgSpeakerPin) + "</small>";
    page += "</fieldset>";
    page += "<small>Use Settings > HW Refresh to update live readings.</small>";
    page += "</body></html>";
    config_server.send(200, "text/html", page);
    return;
    #else
    String name = html_escape(String(pet_poc_get_name()));

    String page;
    page.reserve(1400);
    page += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<title>Aura Pet Config</title><style>body{font-family:Arial,sans-serif;max-width:520px;margin:24px auto;padding:0 12px;}";
    page += "input,button{font-size:16px;padding:10px;margin-top:8px;}input{width:100%;box-sizing:border-box;}button{width:100%;}";
    page += "small{color:#666}label{font-weight:600}</style></head><body>";
    page += "<h2>Aura Pet Config</h2>";
    page += "<p><strong>Hostname:</strong> " + html_escape(host) + ".local<br><strong>IP:</strong> " + html_escape(ip) + "</p>";
    page += "<form method='POST' action='/set-name'>";
    page += "<label>Pet name</label><input name='name' maxlength='20' value='" + name + "'/>";
    page += "<button type='submit'>Save name</button></form>";
    page += "<form method='POST' action='/set-testing'>";
    page += "<label><input type='checkbox' name='enabled' value='1'";
    if (pet_poc_is_test_mode()) {
      page += " checked";
    }
    page += "> Testing mode</label><button type='submit'>Save testing mode</button></form>";
    page += "<small>Tip: use http://" + html_escape(host) + ".local/ from devices on same Wi-Fi.</small>";
    page += "</body></html>";
    config_server.send(200, "text/html", page);
    #endif
  });

  #if !defined(AURA_NO_PET)
  config_server.on("/set-name", HTTP_POST, []() {
    String name = config_server.hasArg("name") ? config_server.arg("name") : String("");
    bool ok = pet_poc_set_name(name.c_str());
    config_server.sendHeader("Location", ok ? "/?saved=1" : "/?saved=0", true);
    config_server.send(303, "text/plain", "");
  });

  config_server.on("/set-testing", HTTP_POST, []() {
    bool enabled = config_server.hasArg("enabled") && config_server.arg("enabled") == "1";
    pet_poc_set_test_mode(enabled);
    config_server.sendHeader("Location", "/?testing=1", true);
    config_server.send(303, "text/plain", "");
  });
  #endif

  config_server.on("/set-hw", HTTP_POST, []() {
    apply_hardware_pin_modes();
    update_hardware_test_labels();
    config_server.sendHeader("Location", "/?hw=locked", true);
    config_server.send(303, "text/plain", "");
  });

  config_server.begin();
  config_server_ready = true;
  Serial.println("Config server started on port 80");
}

static void switch_to_pet_screen() {
  lv_obj_t *next = lv_obj_create(NULL);
  lv_obj_clear_flag(next, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(next, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(next, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_scr_load_anim(next, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
  pet_poc_init();
}

void open_weather_screen(bool open_settings) {
  if (settings_win) {
    lv_obj_del(settings_win);
    settings_win = nullptr;
  }

  // Destroy HomeLab screen to free LVGL memory
  if (homelab_screen) {
    lv_obj_del(homelab_screen);
    homelab_screen    = nullptr;
    lbl_hl_jf_status  = nullptr;
    lbl_hl_js_status  = nullptr;
    lbl_hl_updated    = nullptr;
    lbl_hl_err        = nullptr;
    lbl_power_homelab = nullptr;
  }

  lv_obj_t *next = lv_obj_create(NULL);
  lv_obj_clear_flag(next, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(next, lv_color_hex(0x4c8cb9), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(next, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_scr_load_anim(next, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);

  create_ui();
  fetch_and_update_weather();
  if (open_settings) {
    create_settings_window();
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

void configureWiFiManagerPortal(WiFiManager &wm) {
  wm.setAPCallback(apModeCallback);
  wm.setCustomHeadElement(WIFI_PORTAL_CACHE_HEAD);
}

// Screen dimming functions
bool night_mode_should_be_active();
void activate_night_mode();
void deactivate_night_mode();
void check_for_night_mode();
void handle_temp_screen_wakeup_timeout(lv_timer_t *timer);


int day_of_week(int y, int m, int d) {
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

String hour_of_day(int hour) {
  const LocalizedStrings* strings = get_strings(current_language);
  if(hour < 0 || hour > 23) return String(strings->invalid_hour);

  if (use_24_hour) {
    if (hour < 10)
      return String("0") + String(hour);
    else
      return String(hour);
  } else {
    if(hour == 0)   return String("12") + strings->am;
    if(hour == 12)  return String(strings->noon);

    bool isMorning = (hour < 12);
    String suffix = isMorning ? strings->am : strings->pm;

    int displayHour = hour % 12;

    return String(displayHour) + suffix;
  }
}

String urlencode(const String &str) {
  String encoded = "";
  char buf[5];
  for (size_t i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    // Unreserved characters according to RFC 3986
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      // Percent-encode others
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

static void update_clock(lv_timer_t *timer) {
  struct tm timeinfo;

  check_for_night_mode();

  if (!getLocalTime(&timeinfo)) return;

  const LocalizedStrings* strings = get_strings(current_language);
  char buf[16];
  if (use_24_hour) {
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  } else {
    int hour = timeinfo.tm_hour % 12;
    if(hour == 0) hour = 12;
    const char *ampm = (timeinfo.tm_hour < 12) ? strings->am : strings->pm;
    snprintf(buf, sizeof(buf), "%d:%02d%s", hour, timeinfo.tm_min, ampm);
  }
  lv_label_set_text(lbl_clock, buf);
}

static void ta_event_cb(lv_event_t *e) {
  lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
  lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);

  // Show keyboard
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_move_foreground(kb);
  lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

static void kb_event_cb(lv_event_t *e) {
  lv_obj_t *kb = static_cast<lv_obj_t *>(lv_event_get_target(e));
  lv_obj_add_flag((lv_obj_t *)lv_event_get_target(e), LV_OBJ_FLAG_HIDDEN);

  if (lv_event_get_code(e) == LV_EVENT_READY) {
    const char *loc = lv_textarea_get_text(loc_ta);
    if (strlen(loc) > 0) {
      do_geocode_query(loc);
    }
  }
}

static void ta_defocus_cb(lv_event_t *e) {
  lv_obj_add_flag((lv_obj_t *)lv_event_get_user_data(e), LV_OBJ_FLAG_HIDDEN);
}

void touchscreen_read(lv_indev_t *indev, lv_indev_data_t *data) {
  bool isTouched = false;
  int tx = 0;
  int ty = 0;

#if S3_CAP_TOUCH
  isTouched = read_ft6336_touch(tx, ty);
#else
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    TS_Point p = touchscreen.getPoint();
    tx = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    ty = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
    z = p.z;
    isTouched = true;
  }
#endif

  if (isTouched) {
    x = tx;
    y = ty;

    // Handle touch during dimmed screen
    if (night_mode_active) {
      // Temporarily wake the screen for 15 seconds
      analogWrite(LCD_BACKLIGHT_PIN, prefs.getUInt("brightness", 128));
    
      if (temp_screen_wakeup_timer) {
        lv_timer_del(temp_screen_wakeup_timer);
      }
      temp_screen_wakeup_timer = lv_timer_create(handle_temp_screen_wakeup_timeout, 15000, NULL);
      lv_timer_set_repeat_count(temp_screen_wakeup_timer, 1); // Run only once
      Serial.println("Woke up screen. Setting timer to turn of screen after 15 seconds of inactivity.");

      if (!temp_screen_wakeup_active) {
          // If this is the wake-up tap, don't pass this touch to the UI - just undim the screen
          temp_screen_wakeup_active = true;
          data->state = LV_INDEV_STATE_RELEASED;
          return;
      }

      temp_screen_wakeup_active = true;
    }

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
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
#if !defined(USE_RAW_LCD)
  apply_vendor_ili9341_init(tft);
#endif
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);

  lv_init();

  // Init touchscreen
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

#if defined(USE_RAW_LCD)
  lv_display_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_flush_cb(disp, weather_lcd_flush);
  lv_display_set_buffers(disp, draw_buf, nullptr, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
#else
  lv_display_t *disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
#endif
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchscreen_read);

  // Load saved prefs
  prefs.begin("weather", false);
  String lat = prefs.getString("latitude", LATITUDE_DEFAULT);
  lat.toCharArray(latitude, sizeof(latitude));
  String lon = prefs.getString("longitude", LONGITUDE_DEFAULT);
  lon.toCharArray(longitude, sizeof(longitude));
  use_fahrenheit = prefs.getBool("useFahrenheit", false);
  location = prefs.getString("location", LOCATION_DEFAULT);
  use_night_mode = prefs.getBool("useNightMode", false);
  uint32_t brightness = prefs.getUInt("brightness", 255);
  use_24_hour = prefs.getBool("use24Hour", false);
  current_language = (Language)prefs.getUInt("language", LANG_EN);
  cfg_hl_jellyfin_base = prefs.getString("hl_jf_base", JELLYFIN_BASE_URL);
  cfg_hl_jellyfin_key = prefs.getString("hl_jf_key", JELLYFIN_API_KEY);
  cfg_hl_jellyseerr_base = prefs.getString("hl_js_base", JELLYSEERR_BASE_URL);
  cfg_hl_jellyseerr_key = prefs.getString("hl_js_key", JELLYSEERR_API_KEY);
  cfg_hl_jellyseerr_restart = prefs.getString("hl_js_restart", JELLYSEERR_RESTART_URL);
  cfgBatteryAdcPin = BATTERY_ADC_PIN;
  cfgBatteryChargePin = BATTERY_CHARGE_PIN;
  cfgBatteryChargeActiveLevel = BATTERY_CHARGE_ACTIVE_LEVEL;
  cfgBatteryDividerRatio = BATTERY_DIVIDER_RATIO;
  cfgMicAdcPin = MIC_ADC_PIN;
  cfgSpeakerPin = prefs.getInt("hw_spk_pin", SPEAKER_PIN);
  cfgAlertVolumePct = prefs.getUChar("alert_vol", 45);
  if (cfgAlertVolumePct > 100) cfgAlertVolumePct = 100;
  if (cfgSpeakerPin < 0 && SPEAKER_PIN >= 0) {
    cfgSpeakerPin = SPEAKER_PIN;
    prefs.putInt("hw_spk_pin", cfgSpeakerPin);
  }
  analogWrite(LCD_BACKLIGHT_PIN, brightness);
  enable_backlight_failsafe();
  apply_hardware_pin_modes();

  handle_serial_wifi_provisioning();

  // Check for Wi-Fi config and request it if not available
  WiFiManager wm;
  configureWiFiManagerPortal(wm);
  wm.autoConnect(DEFAULT_CAPTIVE_SSID);
  wm.setConnectTimeout(8);
  wm.setConfigPortalTimeout(45);
  bool wifiOk = wm.autoConnect(DEFAULT_CAPTIVE_SSID);
  Serial.println(wifiOk ? "WiFi autoConnect: connected" : "WiFi autoConnect: timed out/offline");
  lv_timer_create(update_clock, 1000, NULL);

  #if defined(AURA_NO_PET)
  apply_pet_hostname();
  ensure_config_server_started();
  open_weather_screen(false);
  #else
  switch_to_pet_screen();
  apply_pet_hostname();
  ensure_config_server_started();
  // fetch_and_update_weather();  // Comment out for now - pet game focus
  #endif

  play_startup_ready_chime();
}

void flush_wifi_splashscreen(uint32_t ms = 200) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    lv_timer_handler();
    delay(5);
  }
}

void apModeCallback(WiFiManager *mgr) {
  wifi_splash_screen();
  flush_wifi_splashscreen();
}

void loop() {
  lv_timer_handler();
  static uint32_t last_weather_update = millis();
  static uint32_t last_hw_update = millis();

  handle_serial_wifi_provisioning();

  #if !defined(AURA_NO_PET)
  pet_poc_loop();
  #endif

  if (config_server_ready && WiFi.status() == WL_CONNECTED) {
    config_server.handleClient();
  }

  if (millis() - last_weather_update >= UPDATE_INTERVAL) {
    if (weather_ui_ready() && WiFi.status() == WL_CONNECTED) {
      fetch_and_update_weather();
    }
    last_weather_update = millis();
  }

  if (millis() - last_hw_update >= 1500) {
    update_global_power_status_labels();  // always refresh battery/clock in nav
    if (settings_win) update_hardware_test_labels();  // mic I2S read only when settings open
    last_hw_update = millis();
  }

  lv_tick_inc(5);
  delay(5);
}

void handle_serial_wifi_provisioning() {
  static String serial_line;

  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == '\r') {
      continue;
    }

    if (ch != '\n') {
      if (serial_line.length() < 384) {
        serial_line += ch;
      }
      continue;
    }

    String line = serial_line;
    serial_line = "";
    line.trim();

    const String prefix = "AURA_WIFI ";
    if (!line.startsWith(prefix)) {
      continue;
    }

    String payload = line.substring(prefix.length());
    DynamicJsonDocument wifi_doc(384);
    auto err = deserializeJson(wifi_doc, payload);
    if (err) {
      Serial.printf("AURA_WIFI_ERR JSON parse failed: %s\n", err.c_str());
      continue;
    }

    String ssid = wifi_doc["ssid"] | "";
    String password = wifi_doc["password"] | "";
    ssid.trim();

    if (ssid.isEmpty()) {
      Serial.println("AURA_WIFI_ERR SSID missing");
      continue;
    }

    Serial.printf("AURA_WIFI applying SSID '%s'\n", ssid.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.persistent(true);
    WiFi.begin(ssid.c_str(), password.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
      delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("AURA_WIFI_OK %s\n", WiFi.localIP().toString().c_str());
      delay(500);
      ESP.restart();
    } else {
      Serial.println("AURA_WIFI_ERR connect timeout");
    }
  }
}

static bool weather_ui_ready() {
  return lbl_today_temp != nullptr &&
         lbl_today_feels_like != nullptr &&
         lbl_forecast != nullptr &&
         lbl_loc != nullptr;
}

void wifi_splash_screen() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x4c8cb9), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0xa6cdec), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

  const LocalizedStrings* strings = get_strings(current_language);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, strings->wifi_config);
  lv_obj_set_style_text_font(title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  lv_obj_t *subtitle = lv_label_create(scr);
  lv_label_set_text(subtitle, "Scan QR to join Aura Wi-Fi");
  lv_obj_set_style_text_font(subtitle, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0xe4ffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 32);

#if LV_USE_QRCODE
  static const char *wifi_qr_payload = "WIFI:T:nopass;S:Aura;;";
  lv_obj_t *qr = lv_qrcode_create(scr);
  lv_qrcode_set_size(qr, 132);
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_qrcode_update(qr, wifi_qr_payload, strlen(wifi_qr_payload));
  lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 56);
#endif

  lv_obj_t *ssid = lv_label_create(scr);
  lv_label_set_text(ssid, "SSID: Aura (open)");
  lv_obj_set_style_text_font(ssid, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(ssid, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(ssid, LV_ALIGN_BOTTOM_MID, 0, -24);

  lv_obj_t *portal = lv_label_create(scr);
  lv_label_set_text(portal, "Portal: http://192.168.4.1");
  lv_obj_set_style_text_font(portal, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(portal, lv_color_hex(0xe4ffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(portal, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_scr_load(scr);
}

void create_ui() {
  #if !defined(AURA_NO_PET)
  pet_poc_deactivate();
  #endif
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x2d688e), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x8ec6e8), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

  // Swipe down anywhere to open settings
  lv_obj_add_event_cb(scr, screen_gesture_event_cb, LV_EVENT_GESTURE, NULL);

  img_today_icon = lv_img_create(scr);
  lv_img_set_src(img_today_icon, &image_partly_cloudy);
  lv_obj_align(img_today_icon, LV_ALIGN_TOP_MID, -64, 4);

  static lv_style_t default_label_style;
  lv_style_init(&default_label_style);
  lv_style_set_text_color(&default_label_style, lv_color_hex(0xFFFFFF));
  lv_style_set_text_opa(&default_label_style, LV_OPA_COVER);

  const LocalizedStrings* strings = get_strings(current_language);

  lbl_today_temp = lv_label_create(scr);
  lv_label_set_text(lbl_today_temp, strings->temp_placeholder);
  lv_obj_set_style_text_font(lbl_today_temp, get_font_42(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_today_temp, LV_ALIGN_TOP_MID, 45, 25);
  lv_obj_add_style(lbl_today_temp, &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);

  lbl_today_feels_like = lv_label_create(scr);
  lv_label_set_text(lbl_today_feels_like, strings->feels_like_temp);
  lv_obj_set_style_text_font(lbl_today_feels_like, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_today_feels_like, lv_color_hex(0xe4ffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_today_feels_like, LV_ALIGN_TOP_MID, 45, 75);

  lbl_forecast = lv_label_create(scr);
  lv_label_set_text(lbl_forecast, strings->seven_day_forecast);
  lv_obj_set_style_text_font(lbl_forecast, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_forecast, lv_color_hex(0xe4ffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_forecast, LV_ALIGN_TOP_LEFT, 20, 118);

  lbl_homelab_status = nullptr;  // HL status lives on its own screen only

  box_daily = lv_obj_create(scr);
  lv_obj_set_size(box_daily, 220, 180);
  lv_obj_align(box_daily, LV_ALIGN_TOP_LEFT, 10, 135);
  lv_obj_set_style_bg_color(box_daily, lv_color_hex(0x3f7fa8), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(box_daily, 230, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(box_daily, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(box_daily, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(box_daily, lv_color_hex(0xa8dfff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_width(box_daily, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_opa(box_daily, 70, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_color(box_daily, lv_color_hex(0x1f4b68), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(box_daily, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(box_daily, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(box_daily, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(box_daily, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(box_daily, daily_cb, LV_EVENT_CLICKED, NULL);

  for (int i = 0; i < 7; i++) {
    lbl_daily_day[i] = lv_label_create(box_daily);
    lbl_daily_high[i] = lv_label_create(box_daily);
    lbl_daily_low[i] = lv_label_create(box_daily);
    img_daily[i] = lv_img_create(box_daily);

    lv_obj_add_style(lbl_daily_day[i], &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_daily_day[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_daily_day[i], LV_ALIGN_TOP_LEFT, 2, i * 24);

    lv_obj_add_style(lbl_daily_high[i], &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_daily_high[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_daily_high[i], LV_ALIGN_TOP_RIGHT, 0, i * 24);

    lv_label_set_text(lbl_daily_low[i], "");
    lv_obj_set_style_text_color(lbl_daily_low[i], lv_color_hex(0xb9ecff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_daily_low[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_daily_low[i], LV_ALIGN_TOP_RIGHT, -50, i * 24);

    lv_img_set_src(img_daily[i], &icon_partly_cloudy);
    lv_obj_align(img_daily[i], LV_ALIGN_TOP_LEFT, 72, i * 24);
  }

  box_hourly = lv_obj_create(scr);
  lv_obj_set_size(box_hourly, 220, 180);
  lv_obj_align(box_hourly, LV_ALIGN_TOP_LEFT, 10, 135);
  lv_obj_set_style_bg_color(box_hourly, lv_color_hex(0x3f7fa8), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(box_hourly, 230, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(box_hourly, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(box_hourly, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(box_hourly, lv_color_hex(0xa8dfff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_width(box_hourly, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_opa(box_hourly, 70, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_color(box_hourly, lv_color_hex(0x1f4b68), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(box_hourly, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(box_hourly, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(box_hourly, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(box_hourly, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(box_hourly, hourly_cb, LV_EVENT_CLICKED, NULL);

  for (int i = 0; i < 7; i++) {
    lbl_hourly[i] = lv_label_create(box_hourly);
    lbl_precipitation_probability[i] = lv_label_create(box_hourly);
    lbl_hourly_temp[i] = lv_label_create(box_hourly);
    img_hourly[i] = lv_img_create(box_hourly);

    lv_obj_add_style(lbl_hourly[i], &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_hourly[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_hourly[i], LV_ALIGN_TOP_LEFT, 2, i * 24);

    lv_obj_add_style(lbl_hourly_temp[i], &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_hourly_temp[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_hourly_temp[i], LV_ALIGN_TOP_RIGHT, 0, i * 24);

    lv_label_set_text(lbl_precipitation_probability[i], "");
    lv_obj_set_style_text_color(lbl_precipitation_probability[i], lv_color_hex(0xb9ecff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_precipitation_probability[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_precipitation_probability[i], LV_ALIGN_TOP_RIGHT, -55, i * 24);

    lv_img_set_src(img_hourly[i], &icon_partly_cloudy);
    lv_obj_align(img_hourly[i], LV_ALIGN_TOP_LEFT, 72, i * 24);
  }

  lv_obj_add_flag(box_hourly, LV_OBJ_FLAG_HIDDEN);

  // ----- Single-row top bar: [HomeLab] left | power+clock right -----
  // All at y=2, h=22, font_12.  No second row needed.

  lv_obj_t *btn_homelab_nav = lv_btn_create(scr);
  lv_obj_set_size(btn_homelab_nav, 70, 22);
  lv_obj_align(btn_homelab_nav, LV_ALIGN_TOP_LEFT, 4, 2);
  lv_obj_set_style_bg_color(btn_homelab_nav, lv_color_hex(0x1e5f3a), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_homelab_nav, lv_color_hex(0x2a7c4e), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_homelab_nav, open_homelab_event_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_homelab_nav = lv_label_create(btn_homelab_nav);
  lv_obj_set_style_text_font(lbl_homelab_nav, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(lbl_homelab_nav, "HomeLab");
  lv_obj_center(lbl_homelab_nav);

  // Clock – right-aligned in same row
  lbl_clock = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_clock, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_clock, lv_color_hex(0xb9ecff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(lbl_clock, "");
  lv_obj_align(lbl_clock, LV_ALIGN_TOP_RIGHT, -4, 7);

  // Power label just left of clock
  lbl_power_weather = lv_label_create(scr);
  lv_label_set_text(lbl_power_weather, "--");
  lv_obj_set_style_text_font(lbl_power_weather, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_power_weather, lv_color_hex(0xe4ffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_power_weather, LV_ALIGN_TOP_RIGHT, -56, 7);
  update_global_power_status_labels();
}

void populate_results_dropdown() {
  dd_opts[0] = '\0';
  for (JsonObject item : geoResults) {
    strcat(dd_opts, item["name"].as<const char *>());
    if (item["admin1"]) {
      strcat(dd_opts, ", ");
      strcat(dd_opts, item["admin1"].as<const char *>());
    }

    strcat(dd_opts, "\n");
  }

  if (geoResults.size() > 0) {
    lv_dropdown_set_options_static(results_dd, dd_opts);
    lv_obj_add_flag(results_dd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(btn_close_loc, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_close_loc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_close_loc, lv_palette_darken(LV_PALETTE_GREEN, 1), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_flag(btn_close_loc, LV_OBJ_FLAG_CLICKABLE);
  }
}

static void location_save_event_cb(lv_event_t *e) {
  JsonArray *pres = static_cast<JsonArray *>(lv_event_get_user_data(e));
  uint16_t idx = lv_dropdown_get_selected(results_dd);

  JsonObject obj = (*pres)[idx];
  double lat = obj["latitude"].as<double>();
  double lon = obj["longitude"].as<double>();

  snprintf(latitude, sizeof(latitude), "%.6f", lat);
  snprintf(longitude, sizeof(longitude), "%.6f", lon);
  prefs.putString("latitude", latitude);
  prefs.putString("longitude", longitude);

  String opts;
  const char *name = obj["name"];
  const char *admin = obj["admin1"];
  const char *country = obj["country_code"];
  opts += name;
  if (admin) {
    opts += ", ";
    opts += admin;
  }

  prefs.putString("location", opts);
  location = prefs.getString("location");

  // Re‐fetch weather immediately
  lv_label_set_text(lbl_loc, opts.c_str());
  fetch_and_update_weather();

  lv_obj_del(location_win);
  location_win = nullptr;
}

static void location_cancel_event_cb(lv_event_t *e) {
  lv_obj_del(location_win);
  location_win = nullptr;
}

bool do_ip_geolocation(String &resolved_name, double &resolved_lat, double &resolved_lon) {
  HTTPClient http;
  const String url = "http://ip-api.com/json/?fields=status,message,country,regionName,city,lat,lon";

  http.begin(url);
  int http_code = http.GET();
  if (http_code != HTTP_CODE_OK) {
    Serial.println("IP geolocation request failed");
    http.end();
    return false;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();

  if (err) {
    Serial.println("Failed parsing IP geolocation JSON response");
    return false;
  }

  const char *status = doc["status"];
  if (!status || strcmp(status, "success") != 0) {
    Serial.println("IP geolocation status was not success");
    return false;
  }

  const char *city = doc["city"] | "";
  const char *region = doc["regionName"] | "";
  const char *country = doc["country"] | "";

  resolved_lat = doc["lat"].as<double>();
  resolved_lon = doc["lon"].as<double>();

  resolved_name = String(city);
  if (strlen(region) > 0) {
    if (resolved_name.length() > 0) resolved_name += ", ";
    resolved_name += region;
  }
  if (strlen(country) > 0) {
    if (resolved_name.length() > 0) resolved_name += ", ";
    resolved_name += country;
  }

  if (resolved_name.length() == 0) {
    resolved_name = LOCATION_DEFAULT;
  }

  return true;
}

static void location_ip_event_cb(lv_event_t *e) {
  const LocalizedStrings* strings = get_strings(current_language);
  String resolved_name;
  double resolved_lat = 0;
  double resolved_lon = 0;

  if (!do_ip_geolocation(resolved_name, resolved_lat, resolved_lon)) {
    lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
    lv_msgbox_add_title(mbox, strings->change_location);
    lv_obj_t *text = lv_msgbox_add_text(mbox, "Could not find location from IP. Please try city search.");
    lv_obj_set_style_text_font(text, get_font_12(), 0);
    lv_msgbox_add_close_button(mbox);
    lv_obj_set_width(mbox, 220);
    lv_obj_center(mbox);
    return;
  }

  snprintf(latitude, sizeof(latitude), "%.6f", resolved_lat);
  snprintf(longitude, sizeof(longitude), "%.6f", resolved_lon);

  prefs.putString("latitude", latitude);
  prefs.putString("longitude", longitude);
  prefs.putString("location", resolved_name);
  location = prefs.getString("location", LOCATION_DEFAULT);

  lv_label_set_text(lbl_loc, location.c_str());
  fetch_and_update_weather();

  if (location_win) {
    lv_obj_del(location_win);
    location_win = nullptr;
  }
}

void screen_event_cb(lv_event_t *e) {
  lv_obj_t *target = (lv_obj_t *)lv_event_get_target(e);
  if (target != lv_scr_act()) {
    return;
  }
  create_settings_window();
}

// Saves prefs and slides the settings window off the top of the screen,
// then deletes it. Safe to call from gesture or any other trigger.
static void close_settings_anim() {
  if (!settings_win) return;
  prefs.putBool("useFahrenheit", use_fahrenheit);
  prefs.putBool("use24Hour", use_24_hour);
  prefs.putBool("useNightMode", use_night_mode);
  prefs.putUInt("language", current_language);
  if (kb) {
    lv_keyboard_set_textarea(kb, nullptr);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
  lbl_hw_mic = nullptr;
  btn_close_obj = nullptr;
  lv_obj_t *win = settings_win;
  settings_win = nullptr;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, win);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t val) {
    lv_obj_set_y((lv_obj_t *)obj, val);
  });
  lv_anim_set_values(&a, lv_obj_get_y(win), -320);
  lv_anim_set_duration(&a, 60);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_completed_cb(&a, [](lv_anim_t *a) {
    lv_obj_del((lv_obj_t *)a->var);
    fetch_and_update_weather();
  });
  lv_anim_start(&a);
}

static void screen_gesture_event_cb(lv_event_t *e) {
  LV_UNUSED(e);
  lv_indev_t *indev = lv_indev_get_act();
  if (!indev) return;
  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if (dir == LV_DIR_BOTTOM && !settings_win) {
    create_settings_window();
    if (settings_win) {
      lv_anim_t a;
      lv_anim_init(&a);
      lv_anim_set_var(&a, settings_win);
      lv_anim_set_exec_cb(&a, [](void *obj, int32_t val) {
        lv_obj_set_y((lv_obj_t *)obj, val);
      });
      lv_anim_set_values(&a, -320, 0);
      lv_anim_set_duration(&a, 60);
      lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
      lv_anim_start(&a);
    }
  } else if (dir == LV_DIR_TOP && settings_win) {
    close_settings_anim();
  }
}

void daily_cb(lv_event_t *e) {
  const LocalizedStrings* strings = get_strings(current_language);
  lv_obj_add_flag(box_daily, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(lbl_forecast, strings->hourly_forecast);
  lv_obj_clear_flag(box_hourly, LV_OBJ_FLAG_HIDDEN);
}

void hourly_cb(lv_event_t *e) {
  const LocalizedStrings* strings = get_strings(current_language);
  lv_obj_add_flag(box_hourly, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(lbl_forecast, strings->seven_day_forecast);
  lv_obj_clear_flag(box_daily, LV_OBJ_FLAG_HIDDEN);
}


static void reset_wifi_event_handler(lv_event_t *e) {
  const LocalizedStrings* strings = get_strings(current_language);
  lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
  lv_obj_t *title = lv_msgbox_add_title(mbox, strings->reset);
  lv_obj_set_style_margin_left(title, 10, 0);
  lv_obj_set_style_text_font(title, get_font_16(), 0);

  lv_obj_t *text = lv_msgbox_add_text(mbox, strings->reset_confirmation);
  lv_obj_set_style_text_font(text, get_font_12(), 0);
  lv_msgbox_add_close_button(mbox);

  lv_obj_t *btn_no = lv_msgbox_add_footer_button(mbox, strings->cancel);
  lv_obj_set_style_text_font(btn_no, get_font_12(), 0);
  lv_obj_t *btn_yes = lv_msgbox_add_footer_button(mbox, strings->reset);
  lv_obj_set_style_text_font(btn_yes, get_font_12(), 0);

  lv_obj_set_style_bg_color(btn_yes, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_yes, lv_palette_darken(LV_PALETTE_RED, 1), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn_yes, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_set_width(mbox, 230);
  lv_obj_center(mbox);

  lv_obj_set_style_border_width(mbox, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(mbox, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_opa(mbox, LV_OPA_COVER,   LV_PART_MAIN);
  lv_obj_set_style_radius(mbox, 4, LV_PART_MAIN);

  lv_obj_add_event_cb(btn_yes, reset_confirm_yes_cb, LV_EVENT_CLICKED, mbox);
  lv_obj_add_event_cb(btn_no, reset_confirm_no_cb, LV_EVENT_CLICKED, mbox);
}

static void reset_confirm_yes_cb(lv_event_t *e) {
  lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
  Serial.println("Clearing Wi-Fi creds and rebooting");
  WiFiManager wm;
  wm.resetSettings();
  delay(100);
  esp_restart();
}

static void reset_confirm_no_cb(lv_event_t *e) {
  lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
  lv_obj_del(mbox);
}

static void wifi_setup_now_event_cb(lv_event_t *e) {
  LV_UNUSED(e);

  lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
  lv_msgbox_add_title(mbox, "WiFi Setup");
  lv_obj_t *text = lv_msgbox_add_text(mbox, "Opening WiFi portal...\nConnect to Aura AP and open 192.168.4.1");
  lv_obj_set_style_text_font(text, get_font_12(), 0);
  lv_msgbox_add_close_button(mbox);
  lv_obj_set_width(mbox, 240);
  lv_obj_center(mbox);
  lv_timer_handler();
  delay(120);

  WiFiManager wm;
  configureWiFiManagerPortal(wm);
  wm.setConfigPortalTimeout(180);
  bool ok = wm.startConfigPortal(DEFAULT_CAPTIVE_SSID);

  if (mbox) {
    lv_obj_del(mbox);
  }

  lv_obj_t *result = lv_msgbox_create(lv_scr_act());
  lv_msgbox_add_title(result, "WiFi Setup");
  String msg = ok ? "WiFi saved/connected." : "Portal closed or timed out.";
  lv_obj_t *resultText = lv_msgbox_add_text(result, msg.c_str());
  lv_obj_set_style_text_font(resultText, get_font_12(), 0);
  lv_msgbox_add_close_button(result);
  lv_obj_set_width(result, 220);
  lv_obj_center(result);
}

static void refresh_homelab_screen_data() {
  if (!homelab_screen) return;

  HomelabMetrics metrics;
  String err;
  bool ok = fetch_homelab_metrics(metrics, err);

  if (lbl_hl_err) {
    lv_label_set_text(lbl_hl_err, ok ? "" : err.c_str());
    lv_obj_set_style_text_color(lbl_hl_err, lv_color_hex(0xff6b6b), LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  if (ok) {
    if (lbl_hl_jf_status) {
      char buf[80];
      snprintf(buf, sizeof(buf), "Active: %d  |  Sessions: %d", metrics.jfActive, metrics.jfSessions);
      lv_label_set_text(lbl_hl_jf_status, buf);
      lv_obj_set_style_text_color(lbl_hl_jf_status,
        metrics.jfActive > 0 ? lv_color_hex(0x6bdfb0) : lv_color_hex(0xaabbcc),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (lbl_hl_jf_library) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Library: %dm/%dt", metrics.jfMovieLibrary, metrics.jfTvLibrary);
      lv_label_set_text(lbl_hl_jf_library, buf);
    }

    if (lbl_hl_js_status) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Pending: %dm/%dt", metrics.jsMoviePending, metrics.jsTvPending);
      lv_label_set_text(lbl_hl_js_status, buf);
      lv_obj_set_style_text_color(lbl_hl_js_status,
        (metrics.jsMoviePending + metrics.jsTvPending) > 0 ? lv_color_hex(0xf7c948) : lv_color_hex(0xaabbcc),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (lbl_hl_js_queue_new) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Queued <10d: %dm/%dt", metrics.jsMovieQueueNew, metrics.jsTvQueueNew);
      lv_label_set_text(lbl_hl_js_queue_new, buf);
    }
    if (lbl_hl_js_queue_old) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Queued >=10d: %dm/%dt", metrics.jsMovieQueueOld, metrics.jsTvQueueOld);
      lv_label_set_text(lbl_hl_js_queue_old, buf);
      lv_obj_set_style_text_color(lbl_hl_js_queue_old,
        (metrics.jsMovieQueueOld + metrics.jsTvQueueOld) > 0 ? lv_color_hex(0xffa94d) : lv_color_hex(0xaabbcc),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (lbl_hl_js_unreleased) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Unreleased: %dm/%dt", metrics.jsMovieUnreleased, metrics.jsTvUnreleased);
      lv_label_set_text(lbl_hl_js_unreleased, buf);
    }

    int pendingTotal = metrics.jsMoviePending + metrics.jsTvPending;
    if (last_hl_pending_total >= 0 && pendingTotal > last_hl_pending_total) {
      play_startup_ready_chime();
    }
    last_hl_pending_total = pendingTotal;
  } else {
    if (lbl_hl_jf_status) lv_label_set_text(lbl_hl_jf_status, "");
    if (lbl_hl_jf_library) lv_label_set_text(lbl_hl_jf_library, "");
    if (lbl_hl_js_status) lv_label_set_text(lbl_hl_js_status, "");
    if (lbl_hl_js_queue_new) lv_label_set_text(lbl_hl_js_queue_new, "");
    if (lbl_hl_js_queue_old) lv_label_set_text(lbl_hl_js_queue_old, "");
    if (lbl_hl_js_unreleased) lv_label_set_text(lbl_hl_js_unreleased, "");
  }

  if (lbl_hl_updated) {
    struct tm t;
    if (getLocalTime(&t)) {
      char buf[24];
      strftime(buf, sizeof(buf), "Updated: %H:%M:%S", &t);
      lv_label_set_text(lbl_hl_updated, buf);
    } else {
      lv_label_set_text(lbl_hl_updated, "Updated: --");
    }
  }
}

static void homelab_restart_jf_event_cb(lv_event_t *e) {
  LV_UNUSED(e);
  String err;
  bool ok = http_post_with_header(cfg_hl_jellyfin_base + "/System/Restart",
                                  "X-Emby-Token",
                                  cfg_hl_jellyfin_key.c_str(),
                                  err,
                                  15000);
  if (lbl_hl_err) {
    lv_label_set_text(lbl_hl_err, ok ? "Jellyfin restart sent" : (String("Jellyfin restart failed: ") + err).c_str());
  }
  refresh_homelab_screen_data();
}

static void homelab_restart_js_event_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (cfg_hl_jellyseerr_restart.length() == 0) {
    if (lbl_hl_err) lv_label_set_text(lbl_hl_err, "Set JELLYSEERR_RESTART_URL in homelab_secrets.h");
    return;
  }
  String err;
  bool ok = http_post_with_header(cfg_hl_jellyseerr_restart,
                                  "X-Api-Key",
                                  cfg_hl_jellyseerr_key.c_str(),
                                  err,
                                  15000);
  if (lbl_hl_err) {
    lv_label_set_text(lbl_hl_err, ok ? "Jellyseerr restart sent" : (String("Jellyseerr restart failed: ") + err).c_str());
  }
  refresh_homelab_screen_data();
}

void create_homelab_screen() {
  // Nuke any leftover HomeLab screen first
  if (homelab_screen) {
    lv_obj_del(homelab_screen);
    homelab_screen = nullptr;
    lbl_hl_jf_status = nullptr;
    lbl_hl_jf_library = nullptr;
    lbl_hl_js_status = nullptr;
    lbl_hl_js_queue_new = nullptr;
    lbl_hl_js_queue_old = nullptr;
    lbl_hl_js_unreleased = nullptr;
    lbl_hl_updated   = nullptr;
    lbl_hl_err       = nullptr;
    btn_hl_jf_restart = nullptr;
    btn_hl_js_restart = nullptr;
  }

  homelab_screen = lv_obj_create(NULL);
  lv_obj_clear_flag(homelab_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(homelab_screen, lv_color_hex(0x0e1e2c), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(homelab_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_add_event_cb(homelab_screen, screen_gesture_event_cb, LV_EVENT_GESTURE, NULL);

  // ---- Single-row header bar (26px): [Weather] left | power+title right ----
  lv_obj_t *header = lv_obj_create(homelab_screen);
  lv_obj_set_size(header, 240, 26);
  lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x071219), 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  // Back to Weather button (left)
  lv_obj_t *btn_back = lv_btn_create(header);
  lv_obj_set_size(btn_back, 66, 22);
  lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x2d688e), 0);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x1e4a6a), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_back, [](lv_event_t*) {
    open_weather_screen(false);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_bk = lv_label_create(btn_back);
  lv_obj_set_style_text_font(lbl_bk, get_font_12(), 0);
  lv_label_set_text(lbl_bk, "Weather");
  lv_obj_center(lbl_bk);

  // Power label right-aligned
  lbl_power_homelab = lv_label_create(header);
  lv_label_set_text(lbl_power_homelab, "--");
  lv_obj_set_style_text_font(lbl_power_homelab, get_font_12(), 0);
  lv_obj_set_style_text_color(lbl_power_homelab, lv_color_hex(0xb9ecff), 0);
  lv_obj_align(lbl_power_homelab, LV_ALIGN_RIGHT_MID, -4, 0);

  // HomeLab title centred
  lv_obj_t *lbl_title = lv_label_create(header);
  lv_label_set_text(lbl_title, "HomeLab");
  lv_obj_set_style_text_font(lbl_title, get_font_14(), 0);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x6bdfb0), 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

  // ---- Scrollable content area ----
  lv_obj_t *cont = lv_obj_create(homelab_screen);
  lv_obj_set_size(cont, 240, 294);
  lv_obj_align(cont, LV_ALIGN_TOP_LEFT, 0, 26);
  lv_obj_set_style_bg_color(cont, lv_color_hex(0x0e1e2c), 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_left(cont, 12, 0);
  lv_obj_set_style_pad_right(cont, 12, 0);
  lv_obj_set_style_pad_top(cont, 10, 0);
  lv_obj_set_style_pad_bottom(cont, 10, 0);
  lv_obj_set_scroll_dir(cont, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_event_cb(cont, screen_gesture_event_cb, LV_EVENT_GESTURE, NULL);

  // ---- Jellyfin section ----
  lv_obj_t *lbl_jf_head = lv_label_create(cont);
  lv_label_set_text(lbl_jf_head, "JELLYFIN");
  lv_obj_set_style_text_font(lbl_jf_head, get_font_14(), 0);
  lv_obj_set_style_text_color(lbl_jf_head, lv_color_hex(0x6bdfb0), 0);
  lv_obj_align(lbl_jf_head, LV_ALIGN_TOP_LEFT, 0, 0);

  btn_hl_jf_restart = lv_btn_create(cont);
  lv_obj_set_size(btn_hl_jf_restart, 68, 22);
  lv_obj_align(btn_hl_jf_restart, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(btn_hl_jf_restart, lv_color_hex(0x7a1f1f), 0);
  lv_obj_set_style_bg_color(btn_hl_jf_restart, lv_color_hex(0x5b1616), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_hl_jf_restart, homelab_restart_jf_event_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_jf_btn = lv_label_create(btn_hl_jf_restart);
  lv_label_set_text(lbl_jf_btn, "Restart");
  lv_obj_set_style_text_font(lbl_jf_btn, get_font_12(), 0);
  lv_obj_center(lbl_jf_btn);

  lv_obj_t *sep1 = lv_obj_create(cont);
  lv_obj_set_size(sep1, 216, 1);
  lv_obj_align_to(sep1, lbl_jf_head, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 3);
  lv_obj_set_style_bg_color(sep1, lv_color_hex(0x1e5f3a), 0);
  lv_obj_set_style_border_width(sep1, 0, 0);
  lv_obj_clear_flag(sep1, LV_OBJ_FLAG_SCROLLABLE);

  lbl_hl_jf_status = lv_label_create(cont);
  lv_label_set_text(lbl_hl_jf_status, "Loading...");
  lv_obj_set_style_text_font(lbl_hl_jf_status, get_font_12(), 0);
  lv_obj_set_style_text_color(lbl_hl_jf_status, lv_color_hex(0xaabbcc), 0);
  lv_obj_set_width(lbl_hl_jf_status, 216);
  lv_obj_align_to(lbl_hl_jf_status, sep1, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

  lbl_hl_jf_library = lv_label_create(cont);
  lv_label_set_text(lbl_hl_jf_library, "Library: --m/--t");
  lv_obj_set_style_text_font(lbl_hl_jf_library, get_font_12(), 0);
  lv_obj_set_style_text_color(lbl_hl_jf_library, lv_color_hex(0xaabbcc), 0);
  lv_obj_set_width(lbl_hl_jf_library, 216);
  lv_obj_align_to(lbl_hl_jf_library, lbl_hl_jf_status, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

  // ---- Jellyseerr section ----
  lv_obj_t *lbl_js_head = lv_label_create(cont);
  lv_label_set_text(lbl_js_head, "JELLYSEERR");
  lv_obj_set_style_text_font(lbl_js_head, get_font_14(), 0);
  lv_obj_set_style_text_color(lbl_js_head, lv_color_hex(0xf7c948), 0);
  lv_obj_align_to(lbl_js_head, lbl_hl_jf_library, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);

  btn_hl_js_restart = lv_btn_create(cont);
  lv_obj_set_size(btn_hl_js_restart, 68, 22);
  lv_obj_align_to(btn_hl_js_restart, lbl_js_head, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
  lv_obj_set_style_bg_color(btn_hl_js_restart, lv_color_hex(0x7a1f1f), 0);
  lv_obj_set_style_bg_color(btn_hl_js_restart, lv_color_hex(0x5b1616), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_hl_js_restart, homelab_restart_js_event_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_js_btn = lv_label_create(btn_hl_js_restart);
  lv_label_set_text(lbl_js_btn, "Restart");
  lv_obj_set_style_text_font(lbl_js_btn, get_font_12(), 0);
  lv_obj_center(lbl_js_btn);

  lv_obj_t *sep2 = lv_obj_create(cont);
  lv_obj_set_size(sep2, 216, 1);
  lv_obj_align_to(sep2, lbl_js_head, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 3);
  lv_obj_set_style_bg_color(sep2, lv_color_hex(0x7a6418), 0);
  lv_obj_set_style_border_width(sep2, 0, 0);
  lv_obj_clear_flag(sep2, LV_OBJ_FLAG_SCROLLABLE);

  lbl_hl_js_status = lv_label_create(cont);
  lv_label_set_text(lbl_hl_js_status, "Pending: --m/--t");
  lv_obj_set_style_text_font(lbl_hl_js_status, get_font_12(), 0);
  lv_obj_set_style_text_color(lbl_hl_js_status, lv_color_hex(0xaabbcc), 0);
  lv_obj_set_width(lbl_hl_js_status, 216);
  lv_obj_align_to(lbl_hl_js_status, sep2, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

  lbl_hl_js_queue_new = lv_label_create(cont);
  lv_label_set_text(lbl_hl_js_queue_new, "Queued <10d: --m/--t");
  lv_obj_set_style_text_font(lbl_hl_js_queue_new, get_font_12(), 0);
  lv_obj_set_style_text_color(lbl_hl_js_queue_new, lv_color_hex(0xaabbcc), 0);
  lv_obj_set_width(lbl_hl_js_queue_new, 216);
  lv_obj_align_to(lbl_hl_js_queue_new, lbl_hl_js_status, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

  lbl_hl_js_queue_old = lv_label_create(cont);
  lv_label_set_text(lbl_hl_js_queue_old, "Queued >=10d: --m/--t");
  lv_obj_set_style_text_font(lbl_hl_js_queue_old, get_font_12(), 0);
  lv_obj_set_style_text_color(lbl_hl_js_queue_old, lv_color_hex(0xaabbcc), 0);
  lv_obj_set_width(lbl_hl_js_queue_old, 216);
  lv_obj_align_to(lbl_hl_js_queue_old, lbl_hl_js_queue_new, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

  lbl_hl_js_unreleased = lv_label_create(cont);
  lv_label_set_text(lbl_hl_js_unreleased, "Unreleased: --m/--t");
  lv_obj_set_style_text_font(lbl_hl_js_unreleased, get_font_12(), 0);
  lv_obj_set_style_text_color(lbl_hl_js_unreleased, lv_color_hex(0xaabbcc), 0);
  lv_obj_set_width(lbl_hl_js_unreleased, 216);
  lv_obj_align_to(lbl_hl_js_unreleased, lbl_hl_js_queue_old, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

  // ---- Config hint ----
  lv_obj_t *lbl_hint = lv_label_create(cont);
  String cfgHint = "Configure at http://" + config_hostname + ".local";
  lv_label_set_text(lbl_hint, cfgHint.c_str());
  lv_obj_set_style_text_font(lbl_hint, get_font_12(), 0);
  lv_obj_set_style_text_color(lbl_hint, lv_color_hex(0x556677), 0);
  lv_label_set_long_mode(lbl_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_hint, 216);
  lv_obj_align_to(lbl_hint, lbl_hl_js_unreleased, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);

  // ---- Error label ----
  lbl_hl_err = lv_label_create(cont);
  lv_label_set_text(lbl_hl_err, "");
  lv_obj_set_style_text_font(lbl_hl_err, get_font_12(), 0);
  lv_obj_set_style_text_color(lbl_hl_err, lv_color_hex(0xff6b6b), 0);
  lv_label_set_long_mode(lbl_hl_err, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_hl_err, 216);
  lv_obj_align_to(lbl_hl_err, lbl_hint, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

  // ---- Updated stamp ----
  lbl_hl_updated = lv_label_create(cont);
  lv_label_set_text(lbl_hl_updated, "");
  lv_obj_set_style_text_font(lbl_hl_updated, get_font_12(), 0);
  lv_obj_set_style_text_color(lbl_hl_updated, lv_color_hex(0x445566), 0);
  lv_obj_align_to(lbl_hl_updated, lbl_hl_err, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

  update_global_power_status_labels();

  lv_scr_load(homelab_screen);
  refresh_homelab_screen_data();
}

static void open_homelab_event_cb(lv_event_t *e) {
  LV_UNUSED(e);
  create_homelab_screen();
}

static void reset_pet_confirm_yes_cb(lv_event_t *e) {
  lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
  pet_poc_reset_data();
  if (mbox) {
    lv_obj_del(mbox);
  }
}

static void reset_pet_confirm_no_cb(lv_event_t *e) {
  lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
  if (mbox) {
    lv_obj_del(mbox);
  }
}

static void reset_pet_event_handler(lv_event_t *e) {
  LV_UNUSED(e);
  lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
  lv_msgbox_add_title(mbox, "Reset Pet");
  lv_obj_t *text = lv_msgbox_add_text(mbox, "Reset pet name, stats, and progress?");
  lv_obj_set_style_text_font(text, get_font_12(), 0);
  lv_msgbox_add_close_button(mbox);

  lv_obj_t *btn_no = lv_msgbox_add_footer_button(mbox, "Cancel");
  lv_obj_t *btn_yes = lv_msgbox_add_footer_button(mbox, "Reset");
  lv_obj_set_style_text_font(btn_no, get_font_12(), 0);
  lv_obj_set_style_text_font(btn_yes, get_font_12(), 0);
  lv_obj_set_style_bg_color(btn_yes, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_yes, lv_palette_darken(LV_PALETTE_RED, 1), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn_yes, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_set_width(mbox, 230);
  lv_obj_center(mbox);

  lv_obj_add_event_cb(btn_yes, reset_pet_confirm_yes_cb, LV_EVENT_CLICKED, mbox);
  lv_obj_add_event_cb(btn_no, reset_pet_confirm_no_cb, LV_EVENT_CLICKED, mbox);
}

static void change_location_event_cb(lv_event_t *e) {
  if (location_win) return;

  create_location_dialog();
}

void create_location_dialog() {
  const LocalizedStrings* strings = get_strings(current_language);
  location_win = lv_win_create(lv_scr_act());
  lv_obj_t *title = lv_win_add_title(location_win, strings->change_location);
  lv_obj_t *header = lv_win_get_header(location_win);
  lv_obj_set_style_height(header, 30, 0);
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_set_style_margin_left(title, 10, 0);
  lv_obj_set_size(location_win, 240, 320);
  lv_obj_center(location_win);

  lv_obj_t *cont = lv_win_get_content(location_win);

  lv_obj_t *lbl = lv_label_create(cont);
  lv_label_set_text(lbl, strings->city);
  lv_obj_set_style_text_font(lbl, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 5, 10);

  loc_ta = lv_textarea_create(cont);
  lv_textarea_set_one_line(loc_ta, true);
  lv_textarea_set_placeholder_text(loc_ta, strings->city_placeholder);
  lv_obj_set_width(loc_ta, 170);
  lv_obj_align_to(loc_ta, lbl, LV_ALIGN_OUT_RIGHT_MID, 5, 0);

  lv_obj_add_event_cb(loc_ta, ta_event_cb, LV_EVENT_CLICKED, kb);
  lv_obj_add_event_cb(loc_ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, kb);

  lv_obj_t *btn_ip_loc = lv_btn_create(cont);
  lv_obj_set_size(btn_ip_loc, 95, 28);
  lv_obj_align(btn_ip_loc, LV_ALIGN_TOP_LEFT, 5, 45);
  lv_obj_add_event_cb(btn_ip_loc, location_ip_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_ip = lv_label_create(btn_ip_loc);
  lv_label_set_text(lbl_ip, strings->location_ip_btn);
  lv_obj_set_style_text_font(lbl_ip, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_ip);

  lv_obj_t *lbl2 = lv_label_create(cont);
  lv_label_set_text(lbl2, strings->search_results);
  lv_obj_set_style_text_font(lbl2, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl2, LV_ALIGN_TOP_LEFT, 5, 80);

  results_dd = lv_dropdown_create(cont);
  lv_obj_set_width(results_dd, 200);
  lv_obj_align(results_dd, LV_ALIGN_TOP_LEFT, 5, 100);
  lv_obj_set_style_text_font(results_dd, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(results_dd, get_font_14(), LV_PART_SELECTED | LV_STATE_DEFAULT);

  lv_obj_t *list = lv_dropdown_get_list(results_dd);
  lv_obj_set_style_text_font(list, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_dropdown_set_options(results_dd, "");
  lv_obj_clear_flag(results_dd, LV_OBJ_FLAG_CLICKABLE);

  btn_close_loc = lv_btn_create(cont);
  lv_obj_set_size(btn_close_loc, 80, 40);
  lv_obj_align(btn_close_loc, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  lv_obj_add_event_cb(btn_close_loc, location_save_event_cb, LV_EVENT_CLICKED, &geoResults);
  lv_obj_set_style_bg_color(btn_close_loc, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(btn_close_loc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_close_loc, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_clear_flag(btn_close_loc, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *lbl_close = lv_label_create(btn_close_loc);
  lv_label_set_text(lbl_close, strings->save);
  lv_obj_set_style_text_font(lbl_close, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_close);

  lv_obj_t *btn_cancel_loc = lv_btn_create(cont);
  lv_obj_set_size(btn_cancel_loc, 80, 40);
  lv_obj_align_to(btn_cancel_loc, btn_close_loc, LV_ALIGN_OUT_LEFT_MID, -5, 0);
  lv_obj_add_event_cb(btn_cancel_loc, location_cancel_event_cb, LV_EVENT_CLICKED, &geoResults);

  lv_obj_t *lbl_cancel = lv_label_create(btn_cancel_loc);
  lv_label_set_text(lbl_cancel, strings->cancel);
  lv_obj_set_style_text_font(lbl_cancel, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_cancel);
}

void create_settings_window() {
  if (settings_win) return;

  // All positions are absolute (LV_ALIGN_TOP_LEFT + fixed y) so LVGL 9's
  // deferred layout pass never causes controls to stack at y=0.
  const lv_coord_t lx = 0;    // label column
  const lv_coord_t cx = 122;  // control column  (cx+cw = 220 = content width)
  const lv_coord_t cw = 98;   // control width
  const lv_coord_t rh = 28;   // row pitch — roomier

  const LocalizedStrings* strings = get_strings(current_language);
  settings_win = lv_win_create(lv_scr_act());

  lv_obj_t *header = lv_win_get_header(settings_win);
  lv_obj_set_style_height(header, 28, 0);

  lv_obj_t *title = lv_win_add_title(settings_win, strings->aura_settings);
  lv_obj_set_style_text_font(title, get_font_14(), 0);
  lv_obj_set_style_margin_left(title, 6, 0);

  // Window starts above screen — gesture handler animates it down into place
  lv_obj_set_size(settings_win, 236, 320);
  lv_obj_set_pos(settings_win, 2, -320);

  lv_obj_t *cont = lv_win_get_content(settings_win);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_left(cont, 8, 0);
  lv_obj_set_style_pad_right(cont, 8, 0);
  lv_obj_set_style_pad_top(cont, 4, 0);
  lv_obj_set_style_pad_bottom(cont, 4, 0);

  // Swipe-up on the settings surface closes it
  lv_obj_add_event_cb(settings_win, screen_gesture_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(cont,         screen_gesture_event_cb, LV_EVENT_GESTURE, NULL);

  lv_coord_t y = 0;

  // Row 0: Brightness
  lv_obj_t *lbl_b = lv_label_create(cont);
  lv_label_set_text(lbl_b, strings->brightness);
  lv_obj_set_style_text_font(lbl_b, get_font_12(), 0);
  lv_obj_align(lbl_b, LV_ALIGN_TOP_LEFT, lx, y + 5);
  lv_obj_t *slider = lv_slider_create(cont);
  lv_slider_set_range(slider, 1, 255);
  lv_obj_set_width(slider, cw);  // width BEFORE value fixes thumb visual position
  lv_slider_set_value(slider, (int32_t)prefs.getUInt("brightness", 128), LV_ANIM_OFF);
  lv_obj_align(slider, LV_ALIGN_TOP_LEFT, cx, y + 8);
  lv_obj_add_event_cb(slider, [](lv_event_t *e){
    uint32_t v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    analogWrite(LCD_BACKLIGHT_PIN, v);
    prefs.putUInt("brightness", v);
  }, LV_EVENT_VALUE_CHANGED, NULL);
  y += rh;

  // Row 1: Alert volume
  lv_obj_t *lbl_vol = lv_label_create(cont);
  lv_label_set_text(lbl_vol, "Alert vol");
  lv_obj_set_style_text_font(lbl_vol, get_font_12(), 0);
  lv_obj_align(lbl_vol, LV_ALIGN_TOP_LEFT, lx, y + 5);
  lv_obj_t *slider_vol = lv_slider_create(cont);
  lv_slider_set_range(slider_vol, 0, 100);
  lv_obj_set_width(slider_vol, cw);  // width BEFORE value
  lv_slider_set_value(slider_vol, (int32_t)cfgAlertVolumePct, LV_ANIM_OFF);
  lv_obj_align(slider_vol, LV_ALIGN_TOP_LEFT, cx, y + 8);
  lv_obj_add_event_cb(slider_vol, [](lv_event_t *e){
    cfgAlertVolumePct = (uint8_t)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    prefs.putUChar("alert_vol", cfgAlertVolumePct);
  }, LV_EVENT_VALUE_CHANGED, NULL);
  y += rh;

  // Row 2: Night mode
  lv_obj_t *lbl_night = lv_label_create(cont);
  lv_label_set_text(lbl_night, strings->use_night_mode);
  lv_obj_set_style_text_font(lbl_night, get_font_12(), 0);
  lv_obj_align(lbl_night, LV_ALIGN_TOP_LEFT, lx, y + 4);
  night_mode_switch = lv_switch_create(cont);
  lv_obj_align(night_mode_switch, LV_ALIGN_TOP_LEFT, cx, y + 2);
  if (use_night_mode) lv_obj_add_state(night_mode_switch, LV_STATE_CHECKED);
  else lv_obj_remove_state(night_mode_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(night_mode_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
  y += rh;

  // Row 3: Use °F
  lv_obj_t *lbl_u = lv_label_create(cont);
  lv_label_set_text(lbl_u, strings->use_fahrenheit);
  lv_obj_set_style_text_font(lbl_u, get_font_12(), 0);
  lv_obj_align(lbl_u, LV_ALIGN_TOP_LEFT, lx, y + 4);
  unit_switch = lv_switch_create(cont);
  lv_obj_align(unit_switch, LV_ALIGN_TOP_LEFT, cx, y + 2);
  if (use_fahrenheit) lv_obj_add_state(unit_switch, LV_STATE_CHECKED);
  else lv_obj_remove_state(unit_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(unit_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
  y += rh;

  // Row 4: 24-hr clock
  lv_obj_t *lbl_24hr = lv_label_create(cont);
  lv_label_set_text(lbl_24hr, strings->use_24hr);
  lv_obj_set_style_text_font(lbl_24hr, get_font_12(), 0);
  lv_obj_align(lbl_24hr, LV_ALIGN_TOP_LEFT, lx, y + 4);
  clock_24hr_switch = lv_switch_create(cont);
  lv_obj_align(clock_24hr_switch, LV_ALIGN_TOP_LEFT, cx, y + 2);
  if (use_24_hour) lv_obj_add_state(clock_24hr_switch, LV_STATE_CHECKED);
  else lv_obj_clear_state(clock_24hr_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(clock_24hr_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
  y += rh;

  #if !defined(AURA_NO_PET)
  // Row 5 (pet builds): Testing mode
  lv_obj_t *lbl_test = lv_label_create(cont);
  lv_label_set_text(lbl_test, "Testing mode");
  lv_obj_set_style_text_font(lbl_test, get_font_12(), 0);
  lv_obj_align(lbl_test, LV_ALIGN_TOP_LEFT, lx, y + 4);
  testing_mode_switch = lv_switch_create(cont);
  lv_obj_align(testing_mode_switch, LV_ALIGN_TOP_LEFT, cx, y + 2);
  if (pet_poc_is_test_mode()) lv_obj_add_state(testing_mode_switch, LV_STATE_CHECKED);
  else lv_obj_clear_state(testing_mode_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(testing_mode_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
  y += rh;
  #endif

  // Row 5/6: Location
  lv_obj_t *lbl_loc_l = lv_label_create(cont);
  lv_label_set_text(lbl_loc_l, strings->location);
  lv_obj_set_style_text_font(lbl_loc_l, get_font_12(), 0);
  lv_obj_align(lbl_loc_l, LV_ALIGN_TOP_LEFT, lx, y + 4);
  lbl_loc = lv_label_create(cont);
  lv_label_set_text(lbl_loc, location.c_str());
  lv_obj_set_style_text_font(lbl_loc, get_font_12(), 0);
  lv_label_set_long_mode(lbl_loc, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lbl_loc, cw);
  lv_obj_align(lbl_loc, LV_ALIGN_TOP_LEFT, cx, y + 4);
  y += rh;

  // Row 6/7: Language
  lv_obj_t *lbl_lang = lv_label_create(cont);
  lv_label_set_text(lbl_lang, strings->language_label);
  lv_obj_set_style_text_font(lbl_lang, get_font_12(), 0);
  lv_obj_align(lbl_lang, LV_ALIGN_TOP_LEFT, lx, y + 6);
  language_dropdown = lv_dropdown_create(cont);
  lv_dropdown_set_options(language_dropdown, "English\nEspañol\nDeutsch\nFrançais\nTürkçe\nSvenska\nItaliano");
  lv_dropdown_set_selected(language_dropdown, current_language);
  lv_obj_set_width(language_dropdown, cw);
  lv_obj_set_style_text_font(language_dropdown, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(language_dropdown, get_font_12(), LV_PART_SELECTED | LV_STATE_DEFAULT);
  lv_obj_t *list = lv_dropdown_get_list(language_dropdown);
  lv_obj_set_style_text_font(list, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(language_dropdown, LV_ALIGN_TOP_LEFT, cx, y + 2);
  lv_obj_add_event_cb(language_dropdown, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
  y += 32;

  // Buttons row: Change Location + Reset WiFi
  lv_obj_t *btn_change_loc = lv_btn_create(cont);
  lv_obj_set_size(btn_change_loc, 104, 32);
  lv_obj_align(btn_change_loc, LV_ALIGN_TOP_LEFT, lx, y);
  lv_obj_add_event_cb(btn_change_loc, change_location_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_chg = lv_label_create(btn_change_loc);
  lv_label_set_text(lbl_chg, strings->location_btn);
  lv_obj_set_style_text_font(lbl_chg, get_font_12(), 0);
  lv_obj_center(lbl_chg);

  if (!kb) {
    kb = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, NULL);
  }

  lv_obj_t *btn_reset = lv_btn_create(cont);
  lv_obj_set_style_bg_color(btn_reset, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_bg_color(btn_reset, lv_palette_darken(LV_PALETTE_RED, 1), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn_reset, lv_color_white(), 0);
  lv_obj_set_size(btn_reset, 108, 32);
  lv_obj_align(btn_reset, LV_ALIGN_TOP_LEFT, cx, y);
  lv_obj_add_event_cb(btn_reset, reset_wifi_event_handler, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_reset = lv_label_create(btn_reset);
  lv_label_set_text(lbl_reset, strings->reset_wifi);
  lv_obj_set_style_text_font(lbl_reset, get_font_12(), 0);
  lv_obj_center(lbl_reset);

  #if !defined(AURA_NO_PET)
  lv_obj_t *btn_reset_pet = lv_btn_create(cont);
  lv_obj_set_style_bg_color(btn_reset_pet, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_obj_set_style_bg_color(btn_reset_pet, lv_palette_darken(LV_PALETTE_ORANGE, 1), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn_reset_pet, lv_color_white(), 0);
  lv_obj_set_size(btn_reset_pet, 108, 32);
  lv_obj_align(btn_reset_pet, LV_ALIGN_TOP_LEFT, cx, y);
  lv_obj_add_event_cb(btn_reset_pet, reset_pet_event_handler, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_reset_pet = lv_label_create(btn_reset_pet);
  lv_label_set_text(lbl_reset_pet, "Reset Pet");
  lv_obj_set_style_text_font(lbl_reset_pet, get_font_12(), 0);
  lv_obj_center(lbl_reset_pet);
  #endif
  y += 36;

  if (cfgMicAdcPin >= 0 || audioState.codecReady) {
    lbl_hw_mic = lv_label_create(cont);
    lv_label_set_text(lbl_hw_mic, "Mic: --");
    lv_obj_set_style_text_font(lbl_hw_mic, get_font_12(), 0);
    lv_obj_set_style_text_color(lbl_hw_mic, lv_color_hex(0x445566), 0);
    lv_obj_align(lbl_hw_mic, LV_ALIGN_TOP_LEFT, lx, y);
    y += 18;
  }

  // Config URL
  lv_obj_t *lbl_cfg = lv_label_create(cont);
  String cfgUrl = String("Config: http://") + config_hostname + ".local";
  lv_label_set_text(lbl_cfg, cfgUrl.c_str());
  lv_obj_set_style_text_font(lbl_cfg, get_font_12(), 0);
  lv_obj_set_style_text_color(lbl_cfg, lv_color_hex(0x334455), 0);
  lv_label_set_long_mode(lbl_cfg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_cfg, 220);
  lv_obj_align(lbl_cfg, LV_ALIGN_TOP_LEFT, lx, y);

  update_hardware_test_labels();
}

static void settings_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *tgt = (lv_obj_t *)lv_event_get_target(e);

  if (tgt == unit_switch && code == LV_EVENT_VALUE_CHANGED) {
    use_fahrenheit = lv_obj_has_state(unit_switch, LV_STATE_CHECKED);
  }

  if (tgt == clock_24hr_switch && code == LV_EVENT_VALUE_CHANGED) {
    use_24_hour = lv_obj_has_state(clock_24hr_switch, LV_STATE_CHECKED);
  }

  if (tgt == night_mode_switch && code == LV_EVENT_VALUE_CHANGED) {
    use_night_mode = lv_obj_has_state(night_mode_switch, LV_STATE_CHECKED);
  }

  #if !defined(AURA_NO_PET)
  if (tgt == testing_mode_switch && code == LV_EVENT_VALUE_CHANGED) {
    bool enabled = lv_obj_has_state(testing_mode_switch, LV_STATE_CHECKED);
    pet_poc_set_test_mode(enabled);
  }
  #endif

  if (tgt == language_dropdown && code == LV_EVENT_VALUE_CHANGED) {
    current_language = (Language)lv_dropdown_get_selected(language_dropdown);
    // Update the UI immediately to reflect language change
    lbl_hw_mic = nullptr;
    lv_obj_del(settings_win);
    settings_win = nullptr;
    
    // Save preferences and recreate UI with new language
    prefs.putBool("useFahrenheit", use_fahrenheit);
    prefs.putBool("use24Hour", use_24_hour);
    prefs.putBool("useNightMode", use_night_mode);
    prefs.putUInt("language", current_language);

    lv_keyboard_set_textarea(kb, nullptr);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    
    open_weather_screen(false);
    return;
  }

  if (tgt == btn_close_obj && code == LV_EVENT_CLICKED) {
    close_settings_anim();
  }
}

// Screen dimming functions implementation
bool night_mode_should_be_active() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;

  int year = timeinfo.tm_year + 1900;
  if (year < 2024) return false;

  if (!use_night_mode) return false;
  
  int hour = timeinfo.tm_hour;
  return (hour >= NIGHT_MODE_START_HOUR || hour < NIGHT_MODE_END_HOUR);
}

void activate_night_mode() {
  analogWrite(LCD_BACKLIGHT_PIN, 0);
  night_mode_active = true;
}

void deactivate_night_mode() {
  analogWrite(LCD_BACKLIGHT_PIN, prefs.getUInt("brightness", 128));
  night_mode_active = false;
}

void check_for_night_mode() {
  bool night_mode_time = night_mode_should_be_active();

  if (night_mode_time && !night_mode_active && !temp_screen_wakeup_active) {
    activate_night_mode();
  } else if (!night_mode_time && night_mode_active) {
    deactivate_night_mode();
  }
}

void handle_temp_screen_wakeup_timeout(lv_timer_t *timer) {
  if (temp_screen_wakeup_active) {
    temp_screen_wakeup_active = false;

    if (night_mode_should_be_active()) {
      activate_night_mode();
    }
  }
  
  if (temp_screen_wakeup_timer) {
    lv_timer_del(temp_screen_wakeup_timer);
    temp_screen_wakeup_timer = nullptr;
  }
}

void do_geocode_query(const char *q) {
  geoDoc.clear();
  String url = String("https://geocoding-api.open-meteo.com/v1/search?name=") + urlencode(q) + "&count=15";

  HTTPClient http;
  http.begin(url);
  if (http.GET() == HTTP_CODE_OK) {
    Serial.println("Completed location search at open-meteo: " + url);
    auto err = deserializeJson(geoDoc, http.getString());
    if (!err) {
      geoResults = geoDoc["results"].as<JsonArray>();
      populate_results_dropdown();
    } else {
        Serial.println("Failed to parse search response from open-meteo: " + url);
    }
  } else {
      Serial.println("Failed location search at open-meteo: " + url);
  }
  http.end();
}

static bool http_get_json_with_header(const String &url, DynamicJsonDocument &doc, const char *headerName, const char *headerValue, String &errorOut, uint16_t timeoutMs) {
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

static bool http_post_with_header(const String &url, const char *headerName, const char *headerValue, String &errorOut, uint16_t timeoutMs) {
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(url)) {
    errorOut = "begin failed";
    return false;
  }
  if (headerName && headerValue && strlen(headerValue) > 0) {
    http.addHeader(headerName, headerValue);
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST("{}");
  http.end();
  if (code < 200 || code >= 300) {
    errorOut = String("HTTP ") + code;
    return false;
  }
  errorOut = "";
  return true;
}

static time_t parse_date_ymd_utc(const String &dateStr) {
  if (dateStr.length() < 10) return 0;
  int y = dateStr.substring(0, 4).toInt();
  int m = dateStr.substring(5, 7).toInt();
  int d = dateStr.substring(8, 10).toInt();
  if (y < 1971 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d;
  t.tm_hour = 12;
  t.tm_isdst = -1;
  return mktime(&t);
}

static bool fetch_homelab_metrics(HomelabMetrics &metrics, String &errorOut) {
#if AURA_ENABLE_HOMELAB
  if (cfg_hl_jellyfin_base.length() == 0 || cfg_hl_jellyseerr_base.length() == 0 ||
      cfg_hl_jellyfin_key.length() == 0 || cfg_hl_jellyseerr_key.length() == 0) {
    errorOut = "HomeLab config missing";
    return false;
  }

  DynamicJsonDocument jfDoc(4096);
  String jfErr;
  if (!http_get_json_with_header(cfg_hl_jellyfin_base + "/Sessions?ActiveWithinSeconds=600",
                                 jfDoc,
                                 "X-Emby-Token",
                                 cfg_hl_jellyfin_key.c_str(),
                                 jfErr,
                                 20000)) {
    errorOut = String("JF ") + jfErr;
    return false;
  }

  JsonArray sessions = jfDoc.as<JsonArray>();
  for (JsonVariant v : sessions) {
    metrics.jfSessions++;
    bool hasNowPlaying = !v["NowPlayingItem"].isNull();
    bool isPaused = v["PlayState"]["IsPaused"] | false;
    if (hasNowPlaying && !isPaused) metrics.jfActive++;
  }

  DynamicJsonDocument jfCountsDoc(2048);
  String jfCountsErr;
  if (http_get_json_with_header(cfg_hl_jellyfin_base + "/Items/Counts",
                                jfCountsDoc,
                                "X-Emby-Token",
                                cfg_hl_jellyfin_key.c_str(),
                                jfCountsErr,
                                20000)) {
    metrics.jfMovieLibrary = jfCountsDoc["MovieCount"] | 0;
    metrics.jfTvLibrary = jfCountsDoc["SeriesCount"] | 0;
  }

  DynamicJsonDocument jsCountDoc(4096);
  String jsCountErr;
  if (!http_get_json_with_header(cfg_hl_jellyseerr_base + "/api/v1/request/count",
                                 jsCountDoc,
                                 "X-Api-Key",
                                 cfg_hl_jellyseerr_key.c_str(),
                                 jsCountErr,
                                 20000)) {
    errorOut = String("JS count ") + jsCountErr;
    return false;
  }

  metrics.jsMoviePending = jsCountDoc["movie"]["pending"] | 0;
  metrics.jsTvPending = jsCountDoc["tv"]["pending"] | 0;

  DynamicJsonDocument processingDoc(12 * 1024);
  String processingErr;
  if (!http_get_json_with_header(cfg_hl_jellyseerr_base + "/api/v1/request?filter=processing&sort=added&take=20&skip=0",
                                 processingDoc,
                                 "X-Api-Key",
                                 cfg_hl_jellyseerr_key.c_str(),
                                 processingErr,
                                 20000)) {
    errorOut = String("JS processing ") + processingErr;
    return false;
  }

  time_t nowEpoch = time(nullptr);
  JsonArray requests = processingDoc["results"].as<JsonArray>();
  for (JsonVariant req : requests) {
    String type = req["type"] | "";
    bool isMovie = type != "tv";
    int tmdbId = req["media"]["tmdbId"] | 0;
    if (tmdbId <= 0) continue;

    String detailsUrl = cfg_hl_jellyseerr_base + (isMovie ? "/api/v1/movie/" : "/api/v1/tv/") + String(tmdbId);
    DynamicJsonDocument detailsDoc(2048);
    String detailErr;
    if (!http_get_json_with_header(detailsUrl,
                                   detailsDoc,
                                   "X-Api-Key",
                                   cfg_hl_jellyseerr_key.c_str(),
                                   detailErr,
                                   20000)) {
      continue;
    }

    String releaseStr = isMovie ? String((const char *)(detailsDoc["releaseDate"] | ""))
                                : String((const char *)(detailsDoc["firstAirDate"] | ""));
    time_t releaseEpoch = parse_date_ymd_utc(releaseStr);

    if (releaseEpoch <= 0 || (nowEpoch > 0 && releaseEpoch > nowEpoch)) {
      if (isMovie) metrics.jsMovieUnreleased++;
      else metrics.jsTvUnreleased++;
      continue;
    }

    long daysOld = 0;
    if (nowEpoch > 0) {
      daysOld = (long)((nowEpoch - releaseEpoch) / 86400);
      if (daysOld < 0) daysOld = 0;
    }
    if (daysOld < 10) {
      if (isMovie) metrics.jsMovieQueueNew++;
      else metrics.jsTvQueueNew++;
    } else {
      if (isMovie) metrics.jsMovieQueueOld++;
      else metrics.jsTvQueueOld++;
    }
  }

  errorOut = "";
  return true;
#else
  LV_UNUSED(metrics);
  errorOut = "HomeLab disabled";
  return false;
#endif
}

static bool fetch_homelab_summary(String &outText, String &errorOut) {
  HomelabMetrics metrics;
  if (!fetch_homelab_metrics(metrics, errorOut)) {
    outText = "HL unavailable";
    return false;
  }
  int pendingTotal = metrics.jsMoviePending + metrics.jsTvPending;
  int queueTotal = metrics.jsMovieQueueNew + metrics.jsTvQueueNew + metrics.jsMovieQueueOld + metrics.jsTvQueueOld;
  outText = String("HL JF ") + metrics.jfActive + "/" + metrics.jfSessions + "  JS P:" + pendingTotal + " Q:" + queueTotal;
  errorOut = "";
  return true;
}

static bool read_battery(float &voltageOut, int &pctOut) {
  voltageOut = 0.0f;
  pctOut = 0;
  if (cfgBatteryAdcPin < 0) {
    return false;
  }

  long mvSum = 0;
  int validSamples = 0;
  for (int i = 0; i < 8; ++i) {
    int mv = analogReadMilliVolts(cfgBatteryAdcPin);
    if (mv > 0) {
      mvSum += mv;
      validSamples++;
    }
    delay(2);
  }

  if (validSamples < 2) {
    return false;
  }

  float mv = (float)mvSum / (float)validSamples;

  float batteryV = (mv / 1000.0f) * cfgBatteryDividerRatio;
  if (batteryV < 2.5f || batteryV > 5.5f) {
    return false;
  }
  float pct = (batteryV - 3.30f) * (100.0f / (4.20f - 3.30f));
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;

  voltageOut = batteryV;
  pctOut = (int)(pct + 0.5f);
  return true;
}

static int read_battery_charging_state() {
  if (cfgBatteryChargePin < 0) {
    return -1;
  }

  pinMode(cfgBatteryChargePin, INPUT_PULLUP);
  int highCount = 0;
  int lowCount = 0;
  for (int i = 0; i < 12; ++i) {
    int state = digitalRead(cfgBatteryChargePin);
    if (state == HIGH) highCount++;
    if (state == LOW) lowCount++;
    delay(2);
  }

  int stableState = (highCount >= lowCount) ? HIGH : LOW;
  return (stableState == cfgBatteryChargeActiveLevel) ? 1 : 0;
}

static int read_mic_percent() {
  aura_audio::initCodec(Wire, audioState);

  int i2sPct = aura_audio::readI2SMicPercent(audioState);
  if (i2sPct >= 0) {
    return i2sPct;
  }

  if (cfgMicAdcPin < 0) {
    return -1;
  }

  int minRaw = 4095;
  int maxRaw = 0;
  for (int i = 0; i < 64; ++i) {
    int raw = analogRead(cfgMicAdcPin);
    if (raw < 0) continue;
    if (raw < minRaw) minRaw = raw;
    if (raw > maxRaw) maxRaw = raw;
    delayMicroseconds(150);
  }

  if (maxRaw < minRaw) {
    return -1;
  }

  int swing = maxRaw - minRaw;
  int pct = (swing * 100) / 2048;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

static bool play_test_tone() {
  pinMode(AUDIO_AMP_ENABLE_PIN, OUTPUT);
  digitalWrite(AUDIO_AMP_ENABLE_PIN, LOW);

  if (!aura_audio::initCodec(Wire, audioState)) {
    return aura_audio::playPinScanTone(audioState, cfgSpeakerPin);
  }
  if (!aura_audio::initI2S(audioState)) {
    return aura_audio::playPinScanTone(audioState, cfgSpeakerPin);
  }

  int16_t amp = (int16_t)((14000 * (int)cfgAlertVolumePct) / 100);
  if (amp < 1000) amp = 1000;

  // Gentle 4-note ascending arpeggio: C5 → E5 → G5 → C6
  bool ok = true;
  ok &= aura_audio::playI2STone(audioState, 523, 120, amp);
  delay(30);
  ok &= aura_audio::playI2STone(audioState, 659, 120, amp);
  delay(30);
  ok &= aura_audio::playI2STone(audioState, 784, 120, amp);
  delay(30);
  ok &= aura_audio::playI2STone(audioState, 1047, 220, amp);

  if (!ok) {
    return aura_audio::playPinScanTone(audioState, cfgSpeakerPin);
  }
  return true;
}

static void play_startup_ready_chime() {
  delay(150);
  play_test_tone();
}

static void update_global_power_status_labels() {
  float battV = 0.0f;
  int battPct = 0;
  bool haveBatt = read_battery(battV, battPct);
  int chg = read_battery_charging_state();

  char compact[24];
  if (haveBatt) {
    if (chg >= 0) {
      snprintf(compact, sizeof(compact), "%d%%%s", battPct, chg ? "+" : "");
    } else {
      snprintf(compact, sizeof(compact), "%d%%", battPct);
    }
  } else {
    if (chg >= 0) {
      snprintf(compact, sizeof(compact), "--%s", chg ? "+" : "");
    } else {
      snprintf(compact, sizeof(compact), "--");
    }
  }

  if (lbl_power_weather) {
    lv_label_set_text(lbl_power_weather, compact);
  }
  if (lbl_power_homelab) {
    lv_label_set_text_fmt(lbl_power_homelab, "%s", compact);
  }
}

static void apply_hardware_pin_modes() {
  if (cfgBatteryAdcPin >= 0) pinMode(cfgBatteryAdcPin, INPUT);
  if (cfgMicAdcPin >= 0) pinMode(cfgMicAdcPin, INPUT);
  if (cfgSpeakerPin >= 0) pinMode(cfgSpeakerPin, OUTPUT);
  if (cfgBatteryChargePin >= 0) pinMode(cfgBatteryChargePin, INPUT_PULLUP);
  pinMode(AUDIO_AMP_ENABLE_PIN, OUTPUT);
  digitalWrite(AUDIO_AMP_ENABLE_PIN, LOW);
}

static void enable_backlight_failsafe() {
  const int commonPins[] = {LCD_ENABLE_PIN, LCD_BACKLIGHT_PIN, 21, 38, 42, 45, 46, 47, 48};
  for (size_t i = 0; i < (sizeof(commonPins) / sizeof(commonPins[0])); i++) {
    int pin = commonPins[i];
    if (pin < 0 || pin > 48) continue;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
}

static bool read_ft6336_touch(int &xOut, int &yOut) {
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

  if (rawX < 0) rawX = 0;
  if (rawY < 0) rawY = 0;

  xOut = rawX;
  yOut = rawY;
  if (xOut >= SCREEN_WIDTH) xOut = SCREEN_WIDTH - 1;
  if (yOut >= SCREEN_HEIGHT) yOut = SCREEN_HEIGHT - 1;
  return true;
}

static void refresh_homelab_status_label() {
  if (!lbl_homelab_status) {
    return;
  }
  String out;
  String err;
  if (fetch_homelab_summary(out, err)) {
    lv_label_set_text(lbl_homelab_status, out.c_str());
  } else {
    String fail = String("HL err: ") + err;
    lv_label_set_text(lbl_homelab_status, fail.c_str());
  }
}

static void update_hardware_test_labels() {
  if (lbl_hw_battery) {
    float battV = 0.0f;
    int battPct = 0;
    if (read_battery(battV, battPct)) {
      lv_label_set_text_fmt(lbl_hw_battery, "Battery(GPIO%d): %.2fV (%d%%)", cfgBatteryAdcPin, battV, battPct);
    } else {
      lv_label_set_text_fmt(lbl_hw_battery, "Battery(GPIO%d): n/a", cfgBatteryAdcPin);
    }
  }

  if (lbl_hw_charging) {
    int chg = read_battery_charging_state();
    if (chg >= 0) {
      lv_label_set_text_fmt(lbl_hw_charging, "Charging(GPIO%d): %s", cfgBatteryChargePin, chg ? "YES" : "NO");
    } else {
      lv_label_set_text(lbl_hw_charging, "Charging: n/a (status pin not configured)");
    }
  }

  if (lbl_hw_mic) {
    int micPct = read_mic_percent();
    if (micPct >= 0) {
      if (cfgMicAdcPin >= 0) {
        lv_label_set_text_fmt(lbl_hw_mic, "Mic(GPIO%d): %d%%", cfgMicAdcPin, micPct);
      } else {
        lv_label_set_text_fmt(lbl_hw_mic, "Mic(I2S/ES8311): %d%%", micPct);
      }
    } else {
      if (cfgMicAdcPin >= 0) {
        lv_label_set_text_fmt(lbl_hw_mic, "Mic(GPIO%d): n/a", cfgMicAdcPin);
      } else {
        lv_label_set_text(lbl_hw_mic, "Mic(I2S/ES8311): n/a");
      }
    }
  }

  if (lbl_hw_audio) {
    int codecAddr = audioState.codecAddress;
    if (codecAddr < 0) {
      aura_audio::probeCodecAddress(Wire, codecAddr);
      audioState.codecAddress = codecAddr;
    }
    if (codecAddr >= 0) {
      lv_label_set_text_fmt(lbl_hw_audio, "Audio: ES8311 0x%02X, AMP GPIO%d, I2S 4/5/7/8", codecAddr, AUDIO_AMP_ENABLE_PIN);
    } else if (cfgSpeakerPin >= 0) {
      lv_label_set_text_fmt(lbl_hw_audio, "Audio(GPIO%d): fallback only", cfgSpeakerPin);
    } else {
      lv_label_set_text(lbl_hw_audio, "Audio: codec not found");
    }
  }
}

void fetch_and_update_weather() {
  if (WiFi.status() != WL_CONNECTED) {
     Serial.println("WiFi unavailable; skipping weather refresh.");
     return;
  }


  String url = String("http://api.open-meteo.com/v1/forecast?latitude=")
               + latitude + "&longitude=" + longitude
               + "&current=temperature_2m,apparent_temperature,is_day,weather_code"
               + "&daily=temperature_2m_min,temperature_2m_max,weather_code"
               + "&hourly=temperature_2m,precipitation_probability,is_day,weather_code"
               + "&forecast_hours=7"
               + "&timezone=auto";

  HTTPClient http;
  http.begin(url);

  if (http.GET() == HTTP_CODE_OK) {
    Serial.println("Updated weather from open-meteo: " + url);

    String payload = http.getString();
    DynamicJsonDocument doc(32 * 1024);

    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      float t_now = doc["current"]["temperature_2m"].as<float>();
      float t_ap = doc["current"]["apparent_temperature"].as<float>();
      int code_now = doc["current"]["weather_code"].as<int>();
      int is_day = doc["current"]["is_day"].as<int>();

      if (use_fahrenheit) {
        t_now = t_now * 9.0 / 5.0 + 32.0;
        t_ap = t_ap * 9.0 / 5.0 + 32.0;
      }
      const LocalizedStrings* strings = get_strings(current_language);

      int utc_offset_seconds = doc["utc_offset_seconds"].as<int>();
      configTime(utc_offset_seconds, 0, "pool.ntp.org", "time.nist.gov");
      Serial.print("Updating time from NTP with UTC offset: ");
      Serial.println(utc_offset_seconds);

      char unit = use_fahrenheit ? 'F' : 'C';
      lv_label_set_text_fmt(lbl_today_temp, "%.0f°%c", t_now, unit);
      lv_label_set_text_fmt(lbl_today_feels_like, "%s %.0f°%c", strings->feels_like_temp, t_ap, unit);
      lv_img_set_src(img_today_icon, choose_image(code_now, is_day));

      JsonArray times = doc["daily"]["time"].as<JsonArray>();
      JsonArray tmin = doc["daily"]["temperature_2m_min"].as<JsonArray>();
      JsonArray tmax = doc["daily"]["temperature_2m_max"].as<JsonArray>();
      JsonArray weather_codes = doc["daily"]["weather_code"].as<JsonArray>();

      for (int i = 0; i < 7; i++) {
        const char *date = times[i];
        int year = atoi(date + 0);
        int mon = atoi(date + 5);
        int dayd = atoi(date + 8);
        int dow = day_of_week(year, mon, dayd);
        const char *dayStr = (i == 0 && current_language != LANG_FR) ? strings->today : strings->weekdays[dow];

        float mn = tmin[i].as<float>();
        float mx = tmax[i].as<float>();
        if (use_fahrenheit) {
          mn = mn * 9.0 / 5.0 + 32.0;
          mx = mx * 9.0 / 5.0 + 32.0;
        }

        lv_label_set_text_fmt(lbl_daily_day[i], "%s", dayStr);
        lv_label_set_text_fmt(lbl_daily_high[i], "%.0f°%c", mx, unit);
        lv_label_set_text_fmt(lbl_daily_low[i], "%.0f°%c", mn, unit);
        lv_img_set_src(img_daily[i], choose_icon(weather_codes[i].as<int>(), (i == 0) ? is_day : 1));
      }

      JsonArray hours = doc["hourly"]["time"].as<JsonArray>();
      JsonArray hourly_temps = doc["hourly"]["temperature_2m"].as<JsonArray>();
      JsonArray precipitation_probabilities = doc["hourly"]["precipitation_probability"].as<JsonArray>();
      JsonArray hourly_weather_codes = doc["hourly"]["weather_code"].as<JsonArray>();
      JsonArray hourly_is_day = doc["hourly"]["is_day"].as<JsonArray>();

      for (int i = 0; i < 7; i++) {
        const char *date = hours[i];  // "YYYY-MM-DD"
        int hour = atoi(date + 11);
        int minute = atoi(date + 14);
        String hour_name = hour_of_day(hour);

        float precipitation_probability = precipitation_probabilities[i].as<float>();
        float temp = hourly_temps[i].as<float>();
        if (use_fahrenheit) {
          temp = temp * 9.0 / 5.0 + 32.0;
        }

        if (i == 0 && current_language != LANG_FR) {
          lv_label_set_text(lbl_hourly[i], strings->now);
        } else {
          lv_label_set_text(lbl_hourly[i], hour_name.c_str());
        }
        lv_label_set_text_fmt(lbl_precipitation_probability[i], "%.0f%%", precipitation_probability);
        lv_label_set_text_fmt(lbl_hourly_temp[i], "%.0f°%c", temp, unit);
        lv_img_set_src(img_hourly[i], choose_icon(hourly_weather_codes[i].as<int>(), hourly_is_day[i].as<int>()));
      }


    } else {
      Serial.println("JSON parse failed on result from " + url);
    }
  } else {
    Serial.println("HTTP GET failed at " + url);
  }
  http.end();
}

const lv_img_dsc_t* choose_image(int code, int is_day) {
  switch (code) {
    // Clear sky
    case  0:
      return is_day
        ? &image_sunny
        : &image_clear_night;

    // Mainly clear
    case  1:
      return is_day
        ? &image_mostly_sunny
        : &image_mostly_clear_night;

    // Partly cloudy
    case  2:
      return is_day
        ? &image_partly_cloudy
        : &image_partly_cloudy_night;

    // Overcast
    case  3:
      return &image_cloudy;

    // Fog / mist
    case 45:
    case 48:
      return &image_haze_fog_dust_smoke;

    // Drizzle (light → dense)
    case 51:
    case 53:
    case 55:
      return &image_drizzle;

    // Freezing drizzle
    case 56:
    case 57:
      return &image_sleet_hail;

    // Rain: slight showers
    case 61:
      return is_day
        ? &image_scattered_showers_day
        : &image_scattered_showers_night;

    // Rain: moderate
    case 63:
      return &image_showers_rain;

    // Rain: heavy
    case 65:
      return &image_heavy_rain;

    // Freezing rain
    case 66:
    case 67:
      return &image_wintry_mix_rain_snow;

    // Snow fall (light, moderate, heavy) & snow showers (light)
    case 71:
    case 73:
    case 75:
    case 85:
      return &image_snow_showers_snow;

    // Snow grains
    case 77:
      return &image_flurries;

    // Rain showers (slight → moderate)
    case 80:
    case 81:
      return is_day
        ? &image_scattered_showers_day
        : &image_scattered_showers_night;

    // Rain showers: violent
    case 82:
      return &image_heavy_rain;

    // Heavy snow showers
    case 86:
      return &image_heavy_snow;

    // Thunderstorm (light)
    case 95:
      return is_day
        ? &image_isolated_scattered_tstorms_day
        : &image_isolated_scattered_tstorms_night;

    // Thunderstorm with hail
    case 96:
    case 99:
      return &image_strong_tstorms;

    // Fallback for any other code
    default:
      return is_day
        ? &image_mostly_cloudy_day
        : &image_mostly_cloudy_night;
  }
}

const lv_img_dsc_t* choose_icon(int code, int is_day) {
  switch (code) {
    // Clear sky
    case  0:
      return is_day
        ? &icon_sunny
        : &icon_clear_night;

    // Mainly clear
    case  1:
      return is_day
        ? &icon_mostly_sunny
        : &icon_mostly_clear_night;

    // Partly cloudy
    case  2:
      return is_day
        ? &icon_partly_cloudy
        : &icon_partly_cloudy_night;

    // Overcast
    case  3:
      return &icon_cloudy;

    // Fog / mist
    case 45:
    case 48:
      return &icon_haze_fog_dust_smoke;

    // Drizzle (light → dense)
    case 51:
    case 53:
    case 55:
      return &icon_drizzle;

    // Freezing drizzle
    case 56:
    case 57:
      return &icon_sleet_hail;

    // Rain: slight showers
    case 61:
      return is_day
        ? &icon_scattered_showers_day
        : &icon_scattered_showers_night;

    // Rain: moderate
    case 63:
      return &icon_showers_rain;

    // Rain: heavy
    case 65:
      return &icon_heavy_rain;

    // Freezing rain
    case 66:
    case 67:
      return &icon_wintry_mix_rain_snow;

    // Snow fall (light, moderate, heavy) & snow showers (light)
    case 71:
    case 73:
    case 75:
    case 85:
      return &icon_snow_showers_snow;

    // Snow grains
    case 77:
      return &icon_flurries;

    // Rain showers (slight → moderate)
    case 80:
    case 81:
      return is_day
        ? &icon_scattered_showers_day
        : &icon_scattered_showers_night;

    // Rain showers: violent
    case 82:
      return &icon_heavy_rain;

    // Heavy snow showers
    case 86:
      return &icon_heavy_snow;

    // Thunderstorm (light)
    case 95:
      return is_day
        ? &icon_isolated_scattered_tstorms_day
        : &icon_isolated_scattered_tstorms_night;

    // Thunderstorm with hail
    case 96:
    case 99:
      return &icon_strong_tstorms;

    // Fallback for any other code
    default:
      return is_day
        ? &icon_mostly_cloudy_day
        : &icon_mostly_cloudy_night;
  }
}
