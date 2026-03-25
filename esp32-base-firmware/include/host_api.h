// ============================================================
// host_api.h - Functions exported to Wasm app modules
// Host ABI Version 1
// Wasm3 API: uses installed version (m3ApiReturnType + 2-arg m3ApiGetArgMem)
// ============================================================
#ifndef HOST_API_H
#define HOST_API_H

#define HOST_ABI_VERSION 1

#if __has_include(<wasm3.h>)
#define WASM3_AVAILABLE 1
#include <wasm3.h>

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

#include "display_manager.h"
#include "device_config.h"
#include "wifi_manager.h"

// ---- Global host context ---------------------------------------------------
struct WasmHostContext {
  DisplayManager* display;
  DeviceConfig*   config;
  WiFiMgr*        wifi;
  String          activeAppType;
};

static WasmHostContext* g_wasm_ctx = nullptr;

inline void setWasmHostContext(DisplayManager* d, DeviceConfig* c, WiFiMgr* w) {
  static WasmHostContext ctx;
  ctx.display = d;
  ctx.config  = c;
  ctx.wifi    = w;
  g_wasm_ctx  = &ctx;
}

// ---- Display ----------------------------------------------------------------

// host_show_app_screen(title*, line1*, line2*, line3*, accent_color) -> void
m3ApiRawFunction(m3_host_show_app_screen) {
  m3ApiGetArgMem(const char*, title);
  m3ApiGetArgMem(const char*, line1);
  m3ApiGetArgMem(const char*, line2);
  m3ApiGetArgMem(const char*, line3);
  m3ApiGetArg   (int32_t, accentColor);
  if (g_wasm_ctx && g_wasm_ctx->display) {
    g_wasm_ctx->display->showAppScreen(
      title ? String(title) : String(""),
      line1 ? String(line1) : String(""),
      line2 ? String(line2) : String(""),
      line3 ? String(line3) : String(""),
      (uint16_t)accentColor
    );
  }
  m3ApiSuccess();
}

// host_fill_rect(x, y, w, h, color) -> void
m3ApiRawFunction(m3_host_fill_rect) {
  m3ApiGetArg(int32_t, x);
  m3ApiGetArg(int32_t, y);
  m3ApiGetArg(int32_t, w);
  m3ApiGetArg(int32_t, h);
  m3ApiGetArg(int32_t, color);
  if (g_wasm_ctx && g_wasm_ctx->display) {
    g_wasm_ctx->display->fillRectPublic(x, y, w, h, (uint16_t)color);
  }
  m3ApiSuccess();
}

// host_draw_text(x, y, text*, fg, bg, scale) -> void
m3ApiRawFunction(m3_host_draw_text) {
  m3ApiGetArg   (int32_t, x);
  m3ApiGetArg   (int32_t, y);
  m3ApiGetArgMem(const char*, text);
  m3ApiGetArg   (int32_t, fg);
  m3ApiGetArg   (int32_t, bg);
  m3ApiGetArg   (int32_t, scale);
  if (g_wasm_ctx && g_wasm_ctx->display && text) {
    g_wasm_ctx->display->drawTextPublic(
      String(text), x, y,
      (uint16_t)fg, (uint16_t)bg, (uint8_t)scale, 40
    );
  }
  m3ApiSuccess();
}

// ---- WiFi ------------------------------------------------------------------

// host_wifi_rssi() -> i32
m3ApiRawFunction(m3_host_wifi_rssi) {
  m3ApiReturnType(int32_t)
  int32_t rssi = (g_wasm_ctx && g_wasm_ctx->wifi) ? g_wasm_ctx->wifi->getRSSI() : 0;
  m3ApiReturn(rssi);
}

// host_wifi_ssid(buf*, buf_size) -> void
m3ApiRawFunction(m3_host_wifi_ssid) {
  m3ApiGetArgMem(char*, wbuf);
  m3ApiGetArg   (int32_t, wbuf_size);
  if (wbuf && wbuf_size > 0 && g_wasm_ctx && g_wasm_ctx->wifi) {
    String ssid = g_wasm_ctx->wifi->getSSID();
    strncpy(wbuf, ssid.c_str(), (size_t)(wbuf_size - 1));
    wbuf[wbuf_size - 1] = '\0';
  }
  m3ApiSuccess();
}

// host_wifi_ip(buf*, buf_size) -> void
m3ApiRawFunction(m3_host_wifi_ip) {
  m3ApiGetArgMem(char*, ibuf);
  m3ApiGetArg   (int32_t, ibuf_size);
  if (ibuf && ibuf_size > 0 && g_wasm_ctx && g_wasm_ctx->wifi) {
    String ip = g_wasm_ctx->wifi->getIP();
    strncpy(ibuf, ip.c_str(), (size_t)(ibuf_size - 1));
    ibuf[ibuf_size - 1] = '\0';
  }
  m3ApiSuccess();
}

// ---- System ----------------------------------------------------------------

// host_uptime_ms() -> i32
m3ApiRawFunction(m3_host_uptime_ms) {
  m3ApiReturnType(int32_t)
  m3ApiReturn((int32_t)millis());
}

// host_device_id(buf*, buf_size) -> void
m3ApiRawFunction(m3_host_device_id) {
  m3ApiGetArgMem(char*, dbuf);
  m3ApiGetArg   (int32_t, dbuf_size);
  if (dbuf && dbuf_size > 0 && g_wasm_ctx && g_wasm_ctx->config) {
    String id = g_wasm_ctx->config->deviceId;
    strncpy(dbuf, id.c_str(), (size_t)(dbuf_size - 1));
    dbuf[dbuf_size - 1] = '\0';
  }
  m3ApiSuccess();
}

// host_abi_version() -> i32
m3ApiRawFunction(m3_host_abi_version) {
  m3ApiReturnType(int32_t)
  m3ApiReturn((int32_t)HOST_ABI_VERSION);
}

// host_log(msg*) -> void
m3ApiRawFunction(m3_host_log) {
  m3ApiGetArgMem(const char*, logmsg);
  if (logmsg) Serial.printf("[WasmApp] %s\n", logmsg);
  m3ApiSuccess();
}

// ---- HTTP ------------------------------------------------------------------

// host_http_get(url*, resp_buf*, resp_buf_size) -> i32 (http status code)
m3ApiRawFunction(m3_host_http_get) {
  m3ApiReturnType(int32_t)
  m3ApiGetArgMem(const char*, hgurl);
  m3ApiGetArgMem(char*,       hgresp);
  m3ApiGetArg   (int32_t,     hgresp_size);

  if (!hgurl) { m3ApiReturn(-1); }

  HTTPClient http;
  WiFiClientSecure secureClient;
  WiFiClient       plainClient;

  String urlStr(hgurl);
  if (urlStr.startsWith("https://")) {
    secureClient.setInsecure();
    secureClient.setTimeout(12000);
    http.begin(secureClient, urlStr);
  } else {
    http.begin(plainClient, urlStr);
  }
  http.setTimeout(12000);
  int code = http.GET();

  if (code > 0 && hgresp && hgresp_size > 1) {
    String body = http.getString();
    int n = min((int)body.length(), hgresp_size - 1);
    memcpy(hgresp, body.c_str(), n);
    hgresp[n] = '\0';
  } else if (hgresp && hgresp_size > 0) {
    hgresp[0] = '\0';
  }
  http.end();
  m3ApiReturn(code);
}

// host_http_post(url*, body*, resp_buf*, resp_buf_size) -> i32 (http status code)
m3ApiRawFunction(m3_host_http_post) {
  m3ApiReturnType(int32_t)
  m3ApiGetArgMem(const char*, hpurl);
  m3ApiGetArgMem(const char*, hpbody);
  m3ApiGetArgMem(char*,       hpresp);
  m3ApiGetArg   (int32_t,     hpresp_size);

  if (!hpurl) { m3ApiReturn(-1); }

  HTTPClient http;
  WiFiClientSecure secureClient;
  WiFiClient       plainClient;

  String urlStr(hpurl);
  if (urlStr.startsWith("https://")) {
    secureClient.setInsecure();
    secureClient.setTimeout(12000);
    http.begin(secureClient, urlStr);
  } else {
    http.begin(plainClient, urlStr);
  }
  http.setTimeout(12000);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(hpbody ? String(hpbody) : String("{}"));

  if (code > 0 && hpresp && hpresp_size > 1) {
    String respBody = http.getString();
    int n = min((int)respBody.length(), hpresp_size - 1);
    memcpy(hpresp, respBody.c_str(), n);
    hpresp[n] = '\0';
  } else if (hpresp && hpresp_size > 0) {
    hpresp[0] = '\0';
  }
  http.end();
  m3ApiReturn(code);
}

// ---- Per-app config (SPIFFS JSON) ------------------------------------------

static String _appCfgPath(const char* appType) {
  if (!appType || !*appType) return String("/wasm_cfg_sys.json");
  String t(appType);
  t.toLowerCase();
  return String("/wasm_cfg_") + t + String(".json");
}

// host_config_read(key*, val_buf*, val_buf_size) -> i32 (1=found, 0=not found)
m3ApiRawFunction(m3_host_config_read) {
  m3ApiReturnType(int32_t)
  m3ApiGetArgMem(const char*, crkey);
  m3ApiGetArgMem(char*,       crvalbuf);
  m3ApiGetArg   (int32_t,     crvalsize);

  if (!crkey || !crvalbuf || crvalsize <= 0 || !g_wasm_ctx) { m3ApiReturn(0); }

  String path = _appCfgPath(g_wasm_ctx->activeAppType.c_str());
  if (!SPIFFS.exists(path)) { m3ApiReturn(0); }

  File f = SPIFFS.open(path, "r");
  if (!f) { m3ApiReturn(0); }

  StaticJsonDocument<1024> crdoc;
  auto err = deserializeJson(crdoc, f);
  f.close();
  if (err || !crdoc.containsKey(crkey)) { m3ApiReturn(0); }

  String val = crdoc[crkey].as<String>();
  strncpy(crvalbuf, val.c_str(), (size_t)(crvalsize - 1));
  crvalbuf[crvalsize - 1] = '\0';
  m3ApiReturn(1);
}

// host_config_write(key*, value*) -> void
m3ApiRawFunction(m3_host_config_write) {
  m3ApiGetArgMem(const char*, cwkey);
  m3ApiGetArgMem(const char*, cwval);

  if (!cwkey || !g_wasm_ctx) { m3ApiSuccess(); }

  String path = _appCfgPath(g_wasm_ctx->activeAppType.c_str());
  StaticJsonDocument<1024> cwdoc;
  if (SPIFFS.exists(path)) {
    File rf = SPIFFS.open(path, "r");
    if (rf) { deserializeJson(cwdoc, rf); rf.close(); }
  }

  if (cwval) { cwdoc[cwkey] = cwval; }
  else        { cwdoc.remove(cwkey); }

  File wf = SPIFFS.open(path, "w");
  if (wf) { serializeJson(cwdoc, wf); wf.close(); }
  m3ApiSuccess();
}

// ---- Link all host functions into a loaded Wasm module ---------------------

#define M3_LINK(mod, name, sig, fn) do { \
  M3Result _r = m3_LinkRawFunction(mod, "env", name, sig, fn); \
  if (_r && _r != m3Err_functionLookupFailed) { \
    Serial.printf("[HostAPI] Link warning '%s': %s\n", name, _r); \
  } \
} while (0)

inline M3Result registerHostApi(IM3Module module) {
  M3_LINK(module, "host_show_app_screen", "v(****i)",  m3_host_show_app_screen);
  M3_LINK(module, "host_fill_rect",       "v(iiiii)",  m3_host_fill_rect);
  M3_LINK(module, "host_draw_text",       "v(ii*iii)", m3_host_draw_text);
  M3_LINK(module, "host_wifi_rssi",       "i()",       m3_host_wifi_rssi);
  M3_LINK(module, "host_wifi_ssid",       "v(*i)",     m3_host_wifi_ssid);
  M3_LINK(module, "host_wifi_ip",         "v(*i)",     m3_host_wifi_ip);
  M3_LINK(module, "host_uptime_ms",       "i()",       m3_host_uptime_ms);
  M3_LINK(module, "host_device_id",       "v(*i)",     m3_host_device_id);
  M3_LINK(module, "host_abi_version",     "i()",       m3_host_abi_version);
  M3_LINK(module, "host_log",             "v(*)",      m3_host_log);
  M3_LINK(module, "host_http_get",        "i(**i)",    m3_host_http_get);
  M3_LINK(module, "host_http_post",       "i(***i)",   m3_host_http_post);
  M3_LINK(module, "host_config_read",     "i(**i)",    m3_host_config_read);
  M3_LINK(module, "host_config_write",    "v(**)",     m3_host_config_write);
  return m3Err_none;
}

#else
// ---- Stubs when wasm3 library is not installed ----------------------------
#define HOST_ABI_VERSION 1
struct WasmHostContext {};
inline void setWasmHostContext(void*, void*, void*) {}
#endif  // WASM3_AVAILABLE

#endif  // HOST_API_H
