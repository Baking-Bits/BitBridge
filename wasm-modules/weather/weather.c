// Weather Wasm module for BitBridge host ABI v1
// Exports: module_abi_required, on_init, render

typedef signed int int32_t;
typedef unsigned int uint32_t;

#define HOST_ABI_REQUIRED 1
#define MAX_STR 64
#define MAX_RESP 1024

__attribute__((import_module("env"), import_name("host_show_app_screen")))
void host_show_app_screen(const char* title, const char* line1, const char* line2, const char* line3, int32_t accent_color);

__attribute__((import_module("env"), import_name("host_wifi_rssi")))
int32_t host_wifi_rssi(void);

__attribute__((import_module("env"), import_name("host_uptime_ms")))
int32_t host_uptime_ms(void);

__attribute__((import_module("env"), import_name("host_log")))
void host_log(const char* msg);

__attribute__((import_module("env"), import_name("host_http_get")))
int32_t host_http_get(const char* url, char* resp_buf, int32_t resp_buf_size);

__attribute__((import_module("env"), import_name("host_config_read")))
int32_t host_config_read(const char* key, char* val_buf, int32_t val_buf_size);

__attribute__((import_module("env"), import_name("host_config_write")))
void host_config_write(const char* key, const char* value);

static char g_lat[MAX_STR] = "40.7128";
static char g_lon[MAX_STR] = "-74.0060";
static char g_units[MAX_STR] = "fahrenheit";
static char g_timezone[MAX_STR] = "auto";
static char g_location_mode[MAX_STR] = "auto_ip";
static char g_city[MAX_STR] = "Auto";

static int32_t g_last_fetch_ms = 0;
static int32_t g_last_http_code = 0;
static int32_t g_temp_tenths = 0;
static int32_t g_weather_code = 0;
static int32_t g_have_data = 0;
static int32_t g_geo_ok = 0;
static int32_t g_last_geo_http_code = 0;

static char g_line1[MAX_STR];
static char g_line2[MAX_STR];
static char g_line3[MAX_STR];

static int cstr_len(const char* s) {
  int n = 0;
  if (!s) return 0;
  while (s[n] != '\0') n++;
  return n;
}

static void cstr_copy(char* dst, int dst_size, const char* src) {
  int i = 0;
  if (!dst || dst_size <= 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  while (i < dst_size - 1 && src[i] != '\0') {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static void cstr_append(char* dst, int dst_size, const char* src) {
  int i = cstr_len(dst);
  int j = 0;
  if (!dst || dst_size <= 0 || !src) return;
  while (i < dst_size - 1 && src[j] != '\0') {
    dst[i++] = src[j++];
  }
  dst[i] = '\0';
}

static int starts_with(const char* s, const char* prefix) {
  int i = 0;
  if (!s || !prefix) return 0;
  while (prefix[i] != '\0') {
    if (s[i] != prefix[i]) return 0;
    i++;
  }
  return 1;
}

static const char* find_substr(const char* hay, const char* needle) {
  int i = 0;
  int nlen = cstr_len(needle);
  if (!hay || !needle || nlen == 0) return 0;
  while (hay[i] != '\0') {
    if (starts_with(&hay[i], needle)) {
      return &hay[i];
    }
    i++;
  }
  return 0;
}

static int is_digit(char c) {
  return c >= '0' && c <= '9';
}

static int parse_int(const char* s, int* out_value) {
  int i = 0;
  int sign = 1;
  int value = 0;
  int seen_digit = 0;

  if (!s || !out_value) return 0;

  while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == ':') i++;
  if (s[i] == '-') {
    sign = -1;
    i++;
  }

  while (is_digit(s[i])) {
    seen_digit = 1;
    value = value * 10 + (s[i] - '0');
    i++;
  }

  if (!seen_digit) return 0;
  *out_value = value * sign;
  return 1;
}

static int parse_temp_tenths(const char* s, int* out_tenths) {
  int i = 0;
  int sign = 1;
  int whole = 0;
  int frac = 0;
  int seen_digit = 0;

  if (!s || !out_tenths) return 0;

  while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == ':') i++;
  if (s[i] == '-') {
    sign = -1;
    i++;
  }

  while (is_digit(s[i])) {
    seen_digit = 1;
    whole = whole * 10 + (s[i] - '0');
    i++;
  }

  if (s[i] == '.') {
    i++;
    if (is_digit(s[i])) {
      frac = s[i] - '0';
    }
  }

  if (!seen_digit) return 0;
  *out_tenths = sign * (whole * 10 + frac);
  return 1;
}

static int parse_json_string(const char* json, const char* key, char* out, int out_size) {
  char needle[64];
  const char* p;
  int i = 0;
  if (!json || !key || !out || out_size <= 0) return 0;

  cstr_copy(needle, sizeof(needle), "\"");
  cstr_append(needle, sizeof(needle), key);
  cstr_append(needle, sizeof(needle), "\":\"");
  p = find_substr(json, needle);
  if (!p) return 0;

  p += cstr_len(needle);
  while (*p != '\0' && *p != '"' && i < out_size - 1) {
    out[i++] = *p++;
  }
  out[i] = '\0';
  return i > 0;
}

static int parse_json_number_tenths(const char* json, const char* key, int* out_tenths) {
  char needle[64];
  const char* p;
  if (!json || !key || !out_tenths) return 0;
  cstr_copy(needle, sizeof(needle), "\"");
  cstr_append(needle, sizeof(needle), key);
  cstr_append(needle, sizeof(needle), "\":");
  p = find_substr(json, needle);
  if (!p) return 0;
  p += cstr_len(needle);
  return parse_temp_tenths(p, out_tenths);
}

static int parse_json_int(const char* json, const char* key, int* out_value) {
  char needle[64];
  const char* p;
  if (!json || !key || !out_value) return 0;
  cstr_copy(needle, sizeof(needle), "\"");
  cstr_append(needle, sizeof(needle), key);
  cstr_append(needle, sizeof(needle), "\":");
  p = find_substr(json, needle);
  if (!p) return 0;
  p += cstr_len(needle);
  return parse_int(p, out_value);
}

static void int_to_str(int value, char* out, int out_size) {
  char tmp[16];
  int i = 0;
  int j = 0;
  int v = value;
  int neg = 0;

  if (!out || out_size <= 0) return;
  if (value == 0) {
    if (out_size >= 2) {
      out[0] = '0';
      out[1] = '\0';
    } else {
      out[0] = '\0';
    }
    return;
  }

  if (v < 0) {
    neg = 1;
    v = -v;
  }

  while (v > 0 && i < (int)sizeof(tmp) - 1) {
    tmp[i++] = (char)('0' + (v % 10));
    v /= 10;
  }

  if (neg && j < out_size - 1) out[j++] = '-';
  while (i > 0 && j < out_size - 1) out[j++] = tmp[--i];
  out[j] = '\0';
}

static void format_temp_line(char* out, int out_size, int temp_tenths, const char* units) {
  int whole = temp_tenths / 10;
  int frac = temp_tenths;
  char num[20];

  if (frac < 0) frac = -frac;
  frac = frac % 10;

  int_to_str(whole, num, sizeof(num));
  cstr_copy(out, out_size, "Temp: ");
  cstr_append(out, out_size, num);
  cstr_append(out, out_size, ".");

  {
    char digit[2];
    digit[0] = (char)('0' + frac);
    digit[1] = '\0';
    cstr_append(out, out_size, digit);
  }

  if (units && starts_with(units, "fahrenheit")) {
    cstr_append(out, out_size, " F");
  } else {
    cstr_append(out, out_size, " C");
  }
}

static const char* weather_text_from_code(int code) {
  if (code == 0) return "Clear";
  if (code == 1 || code == 2 || code == 3) return "Cloudy";
  if (code == 45 || code == 48) return "Fog";
  if (code == 51 || code == 53 || code == 55) return "Drizzle";
  if (code == 61 || code == 63 || code == 65) return "Rain";
  if (code == 66 || code == 67) return "Freezing Rain";
  if (code == 71 || code == 73 || code == 75 || code == 77) return "Snow";
  if (code == 80 || code == 81 || code == 82) return "Rain Showers";
  if (code == 85 || code == 86) return "Snow Showers";
  if (code == 95 || code == 96 || code == 99) return "Thunderstorm";
  return "Unknown";
}

static const char* weather_icon_from_code(int code) {
  if (code == 0) return "SUN";
  if (code == 1 || code == 2 || code == 3) return "CLD";
  if (code == 45 || code == 48) return "FOG";
  if (code == 51 || code == 53 || code == 55) return "DRZ";
  if (code == 61 || code == 63 || code == 65 || code == 80 || code == 81 || code == 82) return "RAN";
  if (code == 71 || code == 73 || code == 75 || code == 77 || code == 85 || code == 86) return "SNW";
  if (code == 95 || code == 96 || code == 99) return "THR";
  return "WX";
}

static void build_weather_url(char* out, int out_size) {
  cstr_copy(out, out_size, "https://api.open-meteo.com/v1/forecast?latitude=");
  cstr_append(out, out_size, g_lat);
  cstr_append(out, out_size, "&longitude=");
  cstr_append(out, out_size, g_lon);
  cstr_append(out, out_size, "&current=temperature_2m,weather_code");
  cstr_append(out, out_size, "&temperature_unit=");
  cstr_append(out, out_size, g_units);
  cstr_append(out, out_size, "&timezone=");
  cstr_append(out, out_size, g_timezone);
}

static void load_config(void) {
  char buf[MAX_STR];

  if (host_config_read("lat", buf, sizeof(buf)) == 1 && cstr_len(buf) > 0) {
    cstr_copy(g_lat, sizeof(g_lat), buf);
  }
  if (host_config_read("lon", buf, sizeof(buf)) == 1 && cstr_len(buf) > 0) {
    cstr_copy(g_lon, sizeof(g_lon), buf);
  }
  if (host_config_read("units", buf, sizeof(buf)) == 1 && cstr_len(buf) > 0) {
    cstr_copy(g_units, sizeof(g_units), buf);
  }
  if (host_config_read("timezone", buf, sizeof(buf)) == 1 && cstr_len(buf) > 0) {
    cstr_copy(g_timezone, sizeof(g_timezone), buf);
  }
  if (host_config_read("location_mode", buf, sizeof(buf)) == 1 && cstr_len(buf) > 0) {
    cstr_copy(g_location_mode, sizeof(g_location_mode), buf);
  }
  if (host_config_read("city", buf, sizeof(buf)) == 1 && cstr_len(buf) > 0) {
    cstr_copy(g_city, sizeof(g_city), buf);
  }
}

static void try_ip_geolocate(void) {
  char resp[MAX_RESP];
  char city[MAX_STR];
  char tz[MAX_STR];
  int lat_tenths = 0;
  int lon_tenths = 0;
  char numbuf[16];

  if (!starts_with(g_location_mode, "auto_ip")) {
    g_geo_ok = 1;
    return;
  }

  g_last_geo_http_code = host_http_get("https://ipapi.co/json/", resp, sizeof(resp));
  if (g_last_geo_http_code <= 0) {
    g_geo_ok = 0;
    return;
  }

  if (!parse_json_number_tenths(resp, "latitude", &lat_tenths)) {
    g_geo_ok = 0;
    return;
  }
  if (!parse_json_number_tenths(resp, "longitude", &lon_tenths)) {
    g_geo_ok = 0;
    return;
  }

  int_to_str(lat_tenths / 10, numbuf, sizeof(numbuf));
  cstr_copy(g_lat, sizeof(g_lat), numbuf);
  cstr_append(g_lat, sizeof(g_lat), ".");
  {
    char digit[2];
    int frac = lat_tenths;
    if (frac < 0) frac = -frac;
    frac = frac % 10;
    digit[0] = (char)('0' + frac);
    digit[1] = '\0';
    cstr_append(g_lat, sizeof(g_lat), digit);
  }

  int_to_str(lon_tenths / 10, numbuf, sizeof(numbuf));
  cstr_copy(g_lon, sizeof(g_lon), numbuf);
  cstr_append(g_lon, sizeof(g_lon), ".");
  {
    char digit[2];
    int frac = lon_tenths;
    if (frac < 0) frac = -frac;
    frac = frac % 10;
    digit[0] = (char)('0' + frac);
    digit[1] = '\0';
    cstr_append(g_lon, sizeof(g_lon), digit);
  }

  if (parse_json_string(resp, "city", city, sizeof(city))) {
    cstr_copy(g_city, sizeof(g_city), city);
    host_config_write("city", g_city);
  }
  if (parse_json_string(resp, "timezone", tz, sizeof(tz))) {
    cstr_copy(g_timezone, sizeof(g_timezone), tz);
    host_config_write("timezone", g_timezone);
  }

  host_config_write("lat", g_lat);
  host_config_write("lon", g_lon);
  g_geo_ok = 1;
}

static void fetch_weather(void) {
  char url[320];
  char resp[MAX_RESP];
  int parsed_temp = 0;
  int parsed_code = 0;

  build_weather_url(url, sizeof(url));

  g_last_http_code = host_http_get(url, resp, sizeof(resp));
  if (g_last_http_code <= 0) {
    g_have_data = 0;
    return;
  }

  if (!parse_json_number_tenths(resp, "temperature_2m", &parsed_temp)) {
    g_have_data = 0;
    return;
  }
  if (!parse_json_int(resp, "weather_code", &parsed_code)) {
    g_have_data = 0;
    return;
  }

  g_temp_tenths = parsed_temp;
  g_weather_code = parsed_code;
  g_have_data = 1;
}

int32_t module_abi_required(void) {
  return HOST_ABI_REQUIRED;
}

void on_init(void) {
  load_config();
  try_ip_geolocate();
  g_last_fetch_ms = 0;
  g_have_data = 0;
  host_log("weather module initialized");
}

void render(int32_t now_ms) {
  int32_t elapsed = now_ms - g_last_fetch_ms;
  if (g_last_fetch_ms == 0 || elapsed > 10 * 60 * 1000) {
    fetch_weather();
    g_last_fetch_ms = now_ms;
  }

  if (g_have_data) {
    cstr_copy(g_line1, sizeof(g_line1), g_city);
    cstr_append(g_line1, sizeof(g_line1), "  ");
    cstr_append(g_line1, sizeof(g_line1), weather_icon_from_code(g_weather_code));

    cstr_copy(g_line2, sizeof(g_line2), weather_text_from_code(g_weather_code));
    cstr_append(g_line2, sizeof(g_line2), "  ");
    format_temp_line(g_line3, sizeof(g_line3), g_temp_tenths, g_units);
    cstr_append(g_line2, sizeof(g_line2), g_line3 + 6);

    cstr_copy(g_line3, sizeof(g_line3), "RSSI ");
    {
      char rssi_buf[16];
      int_to_str(host_wifi_rssi(), rssi_buf, sizeof(rssi_buf));
      cstr_append(g_line3, sizeof(g_line3), rssi_buf);
      cstr_append(g_line3, sizeof(g_line3), " dBm");
      if (starts_with(g_location_mode, "auto_ip")) {
        cstr_append(g_line3, sizeof(g_line3), g_geo_ok ? " | AUTO" : " | GEO?");
      }
    }
  } else {
    cstr_copy(g_line1, sizeof(g_line1), g_city);
    cstr_copy(g_line2, sizeof(g_line2), "Weather fetch failed");
    cstr_copy(g_line3, sizeof(g_line3), "HTTP ");
    {
      char code_buf[16];
      int_to_str(g_last_http_code, code_buf, sizeof(code_buf));
      cstr_append(g_line3, sizeof(g_line3), code_buf);
      if (starts_with(g_location_mode, "auto_ip") && !g_geo_ok) {
        cstr_append(g_line3, sizeof(g_line3), " | GEO ");
        int_to_str(g_last_geo_http_code, code_buf, sizeof(code_buf));
        cstr_append(g_line3, sizeof(g_line3), code_buf);
      }
    }
  }

  host_show_app_screen("Weather Aura", g_line1, g_line2, g_line3, 0x07FF);
}
