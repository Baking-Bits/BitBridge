# Aura S3 — AI Session Reference Document

> Generated for new-session onboarding. Keep this open alongside the code.

---

## 1. Project Overview

**Aura** is an ESP32-S3 2.8" IPS touchscreen desk appliance running:
- Live weather (Open-Meteo, 7-day/hourly forecast)
- HomeLab status tab (Jellyfin sessions + Jellyseerr queue)
- Clock, battery/charging status always visible
- Startup chime + alert-volume control
- Night-mode auto-dimming
- Over-the-air config via captive portal / mDNS web UI

---

## 2. Hardware

| Component | Detail |
|---|---|
| MCU | ESP32-S3 (QFN56, rev 0.2, dual-core 240 MHz, no PSRAM) |
| Display | 2.8" IPS 240×320px ILI9341, SPI |
| Touch | FT6336 capacitive (I2C addr 0x38, SDA=16, SCL=15, RST=18, INT=21) |
| Audio codec | ES8311 (I2C + I2S), AMP enable GPIO11 (AUDIO_AMP_ENABLE_PIN) |
| I2S pins | BCLK=4, LRCLK=5, DOUT=7, DIN=8 |
| Backlight | GPIO21 (LCD_BACKLIGHT_PIN, PWM) |
| LCD enable | GPIO42 (LCD_ENABLE_PIN, keep HIGH) |
| Battery ADC | BATTERY_ADC_PIN (board-defined, divider ratio 2.0) |
| Charge pin | BATTERY_CHARGE_PIN (active level = LOW by default) |
| USB | USB-Serial/JTAG via COM4 — MAC 44:1b:f6:cf:74:98 |
| Flash | 8 MB QD, huge_app partition (3 145 728 byte app space) |

**Flash/RAM usage (latest build):** ~70.4% flash, ~20.3% RAM.

---

## 3. Codebase Layout

```
B:\ESP32-Tab\
├── Aura\
│   ├── aura\
│   │   └── weather.ino          ← MAIN FILE (~2960 lines, all logic here)
│   ├── include\
│   │   └── audio_es8311_min.h   ← I2S/ES8311 audio helpers
│   ├── platformio.ini           ← build environments
│   ├── lv_conf.h, lv_font_*.c  ← LVGL config + custom fonts
│   └── TFT_eSPI\User_Setup.h   ← display pin config
├── packages\
│   └── aura-s3\                 ← web-flasher artifacts (firmware.bin, etc.)
├── index.html                   ← local web installer UI
├── app.js                       ← installer JS
├── Start-LocalInstaller.ps1     ← launches local http server on port 8080
└── README.md
```

### Key build env: `aura_s3`
```
platform = espressif32
board    = esp32-s3-devkitc-1
framework= arduino
build_flags: -D AURA_NO_PET (pet game disabled)
             -D S3_CAP_TOUCH=1
             -D USE_RAW_LCD (or TFT_eSPI depending on variant)
```

Build command:
```powershell
Set-Location "B:\ESP32-Tab\Aura"
b:/ESP32-Tab/.venv/Scripts/python.exe -m platformio run -e aura_s3
# upload:
b:/ESP32-Tab/.venv/Scripts/python.exe -m platformio run -e aura_s3 -t upload
```
Copy artifacts after build:
```powershell
Copy-Item ".pio\build\aura_s3\firmware.bin"   "B:\ESP32-Tab\packages\aura-s3\firmware.bin"   -Force
Copy-Item ".pio\build\aura_s3\bootloader.bin" "B:\ESP32-Tab\packages\aura-s3\bootloader.bin" -Force
Copy-Item ".pio\build\aura_s3\partitions.bin" "B:\ESP32-Tab\packages\aura-s3\partitions.bin" -Force
```

---

## 4. Framework & Libraries

| Library | Version | Purpose |
|---|---|---|
| LVGL | 9.2.2 | All UI widgets, layout, animation |
| TFT_eSPI | 2.5.43 | SPI display driver |
| ArduinoJson | 7.4.1 | JSON parse (note: `DynamicJsonDocument` deprecated → use `JsonDocument`) |
| WiFiManager | 2.0.17 | Captive portal Wi-Fi onboarding |
| XPT2046_Touchscreen | 1.4 | Resistive touch fallback (not used on S3) |
| ESP-IDF (built-in) | — | I2S, Preferences, Wire, mDNS |

**LVGL version note:** LVGL 9 API differs from v8 — layout is asynchronous. `lv_obj_get_y()` returns 0 before the first draw pass. Use `lv_obj_align_to()` relative to other objects instead.

---

## 5. UI Architecture

### Screen hierarchy
```
lv_scr_act() — weather screen (default)
  ├── top nav bar (22px, single row)
  │     ├── btn_homelab_nav  (left, 70×22, green)
  │     ├── lbl_power_weather (right, battery/charging compact)
  │     └── lbl_clock         (right, time)
  ├── img_today_icon, lbl_today_temp, lbl_today_feels_like
  ├── lbl_forecast
  ├── box_daily  (7-day, toggleable)
  └── box_hourly (7-hour, toggleable)

homelab_screen  (separate lv_obj NULL screen)
  ├── header bar (26px)
  │     ├── btn_back (Weather)   left
  │     ├── lbl_title "HomeLab"  center
  │     └── lbl_power_homelab   right
  └── cont (scrollable, 294px tall, y=26)
        ├── "JELLYFIN" section → lbl_hl_jf_status
        ├── "JELLYSEERR" section → lbl_hl_js_status
        ├── lbl_hint (config URL)
        ├── lbl_hl_err
        └── lbl_hl_updated

settings_win  (lv_win overlay on current screen)
  └── cont (scrollable, LV_SCROLL_SNAP_NONE, anim_time=50ms)
        ├── Unit toggle, 24hr clock, night-mode, testing-mode
        ├── Volume slider (alert_vol, 0–100, persisted)
        ├── Language dropdown (aligned via lv_obj_align_to — NOT lv_obj_get_y)
        ├── Change Location button, Reset Pet button (if AURA_NO_PET not set)
        ├── Mic label (lbl_hw_mic, I2S read only when settings open)
        ├── Config URL label
        └── Close button (120×40, centered below config URL)
```

### Gesture
- **Swipe down** on weather screen OR HomeLab screen → opens settings  
- `screen_gesture_event_cb` registered on both `scr` and `homelab_screen` (and its scroll `cont`)

### Navigation
- Weather → HomeLab: tap **HomeLab** nav button (top-left)
- HomeLab → Weather: tap **Weather** back button (top-left)
- Any screen → Settings: **swipe down**

---

## 6. Key Global State

```cpp
// Display labels kept valid across screen transitions
static lv_obj_t *lbl_clock;            // updated every 1 s by lv_timer
static lv_obj_t *lbl_power_weather;   // battery+charging compact on weather screen
static lv_obj_t *lbl_power_homelab;   // same on HomeLab screen

// HomeLab screen labels (null when screen not loaded)
static lv_obj_t *homelab_screen;
static lv_obj_t *lbl_hl_jf_status;
static lv_obj_t *lbl_hl_js_status;
static lv_obj_t *lbl_hl_err;
static lv_obj_t *lbl_hl_updated;

// Settings overlay (null when closed)
static lv_obj_t *settings_win;
static lv_obj_t *lbl_hw_mic;          // nulled on settings close — I2S read gated

// Config (persisted in NVS via Preferences, namespace "weather")
static bool use_fahrenheit;
static bool use_24_hour;
static bool use_night_mode;
static uint8_t cfgAlertVolumePct;      // "alert_vol", 0–100
static String cfg_hl_jellyfin_base;   // "hl_jf_base"
static String cfg_hl_jellyfin_key;    // "hl_jf_key"
static String cfg_hl_jellyseerr_base; // "hl_js_base"
static String cfg_hl_jellyseerr_key;  // "hl_js_key"
```

---

## 7. Audio System

**Header:** `Aura/include/audio_es8311_min.h`

```cpp
namespace aura_audio {
  struct AudioState { int codecAddress; /* I2S handle etc */ };

  bool initCodec(TwoWire &wire, AudioState &state);
  bool initI2S(AudioState &state);
  bool playI2STone(AudioState &state, uint16_t freq, uint16_t durationMs,
                   int16_t amplitude = 14000);       // amplitude added as optional param
  int  readI2SMicPercent(AudioState &state, uint16_t captureMs = 120); // ~120ms blocking
  bool playPinScanTone(AudioState &state, int pin);  // fallback to passive buzzer
  void probeCodecAddress(TwoWire &wire, int &outAddr);
}
```

**Startup chime** (`play_startup_ready_chime`):
```cpp
// C major arpeggio  C5→E5→G5→C6
playI2STone(audioState, 523, 120, amp);   delay(30);
playI2STone(audioState, 659, 120, amp);   delay(30);
playI2STone(audioState, 784, 120, amp);   delay(30);
playI2STone(audioState, 1047, 220, amp);
// amp = (14000 * cfgAlertVolumePct) / 100, clamped to min 1000
```

**Important:** `readI2SMicPercent` blocks ~120 ms. It is only called when the settings window is open (`if (settings_win) update_hardware_test_labels()`). Power labels still refresh every 1.5 s regardless.

---

## 8. Power / Battery

```cpp
static void update_global_power_status_labels();  // called every 1500ms always
// Format: "75%" when not charging, "75%+" when charging, "--" when no ADC pin
```

- `read_battery()` → ADC read on `cfgBatteryAdcPin`, scaled by `cfgBatteryDividerRatio`
- `read_battery_charging_state()` → GPIO read on `cfgBatteryChargePin`, level `cfgBatteryChargeActiveLevel`
- Labels update in both weather and HomeLab nav bars

---

## 9. Boot Sequence (setup())

```
init Serial → init LCD → init LVGL → init Touch → load prefs
→ analogWrite(LCD_BACKLIGHT_PIN, brightness)
→ enable_backlight_failsafe()          ← forces HIGH on common backlight pins
→ apply_hardware_pin_modes()
→ handle_serial_wifi_provisioning()
→ lv_timer_create(update_clock, 1000)
→ open_weather_screen(false)           ← UI appears BEFORE WiFi blocks
→ play_startup_ready_chime()           ← chime plays BEFORE WiFi blocks
→ wm.autoConnect(DEFAULT_CAPTIVE_SSID) ← timeout 8 s connect, 45 s portal
```

**Critical ordering fix (applied):** UI and chime happen *before* `wm.autoConnect()` so a hanging Wi-Fi join cannot cause black screen + no chime.

---

## 10. Night Mode

- Activated by `check_for_night_mode()` (called every 1 s from `update_clock` timer)
- Guard: `if (year < 2024) return false` — prevents dimming with invalid/unsynced clock
- Tap any touch during night mode → temp wake for 15 s, first tap does NOT propagate to UI
- `use_night_mode` persisted as `"useNightMode"` in prefs

---

## 11. HomeLab Data (fetch_homelab_summary)

**Jellyfin:** `GET {base}/Sessions?ActiveWithinSeconds=600`  
→ counts total sessions + active ones  

**Jellyseerr:** Two calls:
- `GET {base}/api/v1/request?filter=pending&take=1` → pending count from `pageInfo.total`
- `GET {base}/api/v1/request?filter=processing&take=1` → queue count

Display: `"Active: N  |  Total sessions: N"` and `"Pending: N  |  Processing: N"`

**⚠️ Still TODO (user requested, not yet built):**
- TV vs movie breakdown in Jellyfin sessions
- Over/under 10-day filter for Jellyseerr requests
- Unreleased items list
- Reboot action button
- Pending-item auto-chime (detect new pending count increase)

---

## 12. Settings Window — Known Issues & Fixes

| Issue | Root Cause | Fix Applied |
|---|---|---|
| Close button invisible | `LV_ALIGN_OUT_BOTTOM_RIGHT + 132` pushed button off 240px screen | Changed to `LV_ALIGN_OUT_BOTTOM_LEFT, 46` (120px wide, centered in 212px cont) |
| Language dropdown misaligned | `lv_obj_get_y()` returns 0 before render pass in LVGL 9 | Changed to `lv_obj_align_to(dropdown, lbl_lang, LV_ALIGN_OUT_RIGHT_MID, ...)` |
| Scroll lag | Default scroll snap + slow animation | Added `LV_SCROLL_SNAP_NONE` + `anim_time=50ms` |
| HW tests blocking loop | Mic I2S read runs every 1.5 s whether settings open or not | `update_hardware_test_labels()` now gated on `if (settings_win)` |

---

## 13. Troubleshooting Log

### Black screen + no chime after flash
**Finding:** `wm.autoConnect()` was called BEFORE `open_weather_screen()` and `play_startup_ready_chime()`. If Wi-Fi took >8s or hung, both UI draw and chime were skipped entirely.  
**Fix:** Moved UI creation + chime to before `autoConnect()`. Added `wm.setConnectTimeout(8)` and `wm.setConfigPortalTimeout(45)`.

### Battery showing N/A
**Finding:** `cfgBatteryAdcPin` was -1 (not set via `BATTERY_ADC_PIN` define or prefs). `read_battery()` correctly returns false.  
**Status:** Hardware-dependent; shows `--` as expected if no ADC pin configured.

### Mic always 0%
**Finding:** I2S mic read (`readI2SMicPercent`) was being called unconditionally every 1.5 s before `initCodec`/`initI2S` had a valid codec address from the current invocation context.  
**Fix:** Mic read only happens when settings window is open (user can see it).

### Clock label null panic risk
**Finding:** `lbl_clock` was being created in two separate places; after the single-row nav refactor the second creation was removed but `update_clock` timer still ran — now `lbl_clock` is created once in `create_ui()` and nulled in `open_weather_screen()` before screen teardown.

### LVGL `lv_obj_get_y()` = 0 at layout time
**Finding:** In LVGL 9, layout resolution is deferred. Any position queries during object creation return 0. All alignment that needs to be relative to a sibling must use `lv_obj_align_to()` not coordinate queries.

---

## 14. NVS Prefs Keys (namespace "weather")

| Key | Type | Default | Purpose |
|---|---|---|---|
| `latitude` | String | LATITUDE_DEFAULT | Weather fetch lat |
| `longitude` | String | LONGITUDE_DEFAULT | Weather fetch lon |
| `location` | String | LOCATION_DEFAULT | Display name |
| `useFahrenheit` | Bool | false | Unit toggle |
| `use24Hour` | Bool | false | Clock format |
| `useNightMode` | Bool | false | Auto-dim |
| `brightness` | UInt | 255 | Backlight PWM |
| `language` | UInt | 0 (LANG_EN) | UI language |
| `alert_vol` | UChar | 45 | Chime volume 0–100 |
| `hl_jf_base` | String | JELLYFIN_BASE_URL | Jellyfin server URL |
| `hl_jf_key` | String | JELLYFIN_API_KEY | Jellyfin token |
| `hl_js_base` | String | JELLYSEERR_BASE_URL | Jellyseerr server URL |
| `hl_js_key` | String | JELLYSEERR_API_KEY | Jellyseerr token |
| `hw_spk_pin` | Int | SPEAKER_PIN | Fallback buzzer GPIO |

---

## 15. Web Installer

Local flasher runs at `http://localhost:8080`. Start with:
```powershell
Set-Location "B:\ESP32-Tab"
Set-ExecutionPolicy -Scope Process Bypass
.\Start-LocalInstaller.ps1
```
Firmware packages live in `packages/` — each folder needs `firmware.bin`, `bootloader.bin`, `partitions.bin`, `boot_app0.bin`, and a `manifest.json`.

The `aura-s3` manifest flashes at standard ESP32-S3 offsets:
| Partition | Offset |
|---|---|
| bootloader | 0x0000 |
| partitions | 0x8000 |
| boot_app0 | 0xe000 |
| firmware | 0x10000 |

---

## 16. What's Done / What's Pending

### ✅ Done
- Single-line top nav (HomeLab btn left, power+clock right, 22px)
- HomeLab screen (26px header, scrollable content, power in header)
- Swipe-down anywhere to open settings
- Settings: scroll fix, language dropdown alignment, no WiFi Setup btn, no battery/charging/audio HW rows, only mic
- Volume slider in settings, alert_vol persisted
- Startup chime: C5→E5→G5→C6 arpeggio, volume-scaled amplitude
- Battery + charging visible in nav on both screens
- Boot ordering fix (UI + chime before WiFi blocks)
- Night mode guard against invalid clock year
- Mic I2S read gated to settings-open only
- `lbl_hw_mic` nulled on both settings close paths
- HomeLab status label removed from weather screen (lives only on HomeLab screen)
- Settings Close button fixed (was off-screen to the right)

### ❌ Still Pending
- HomeLab full breakdown:
  - TV vs movie split in Jellyfin sessions
  - Over/under 10 days filter for Jellyseerr
  - Unreleased items list
  - Reboot device button
  - Auto-chime on new pending item detected
