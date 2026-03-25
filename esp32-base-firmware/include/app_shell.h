#ifndef APP_SHELL_H
#define APP_SHELL_H

#include <Arduino.h>
#include "display_manager.h"
#include "device_config.h"
#include "wifi_manager.h"

class BaseApp {
 public:
  virtual ~BaseApp() = default;
  virtual const char* name() const = 0;
  virtual uint16_t accentColor() const = 0;
  virtual void render(DisplayManager& display,
                      const DeviceConfig& config,
                      const WiFiMgr& wifiMgr,
                      unsigned long nowMs) = 0;
};

class SystemStatusApp : public BaseApp {
 public:
  const char* name() const override {
    return "SYSTEM";
  }

  uint16_t accentColor() const override {
    return 0x07E0;
  }

  void render(DisplayManager& display,
              const DeviceConfig& config,
              const WiFiMgr& wifiMgr,
              unsigned long nowMs) override {
    (void)config;
    String line1 = String("WiFi ") + wifiMgr.getSSID();
    String line2 = String("IP ") + wifiMgr.getIP();
    String line3 = String("Up ") + String(nowMs / 1000UL) + String("s RSSI ") + String(wifiMgr.getRSSI());
    display.showAppScreen(name(), line1, line2, line3, accentColor());
  }
};

class WeatherApp : public BaseApp {
 public:
  const char* name() const override {
    return "WEATHER";
  }

  uint16_t accentColor() const override {
    return 0x07FF;
  }

  void render(DisplayManager& display,
              const DeviceConfig& config,
              const WiFiMgr& wifiMgr,
              unsigned long nowMs) override {
    (void)config;
    (void)wifiMgr;
    (void)nowMs;
    display.showAppScreen(name(), "Module ready", "API wiring next", "Add weather feed", accentColor());
  }
};

class HomelabApp : public BaseApp {
 public:
  const char* name() const override {
    return "HOMELAB";
  }

  uint16_t accentColor() const override {
    return 0xFD20;
  }

  void render(DisplayManager& display,
              const DeviceConfig& config,
              const WiFiMgr& wifiMgr,
              unsigned long nowMs) override {
    (void)config;
    (void)wifiMgr;
    (void)nowMs;
    display.showAppScreen(name(), "Module ready", "Service checks next", "Queue + media status", accentColor());
  }
};

class PetApp : public BaseApp {
 public:
  const char* name() const override {
    return "PET";
  }

  uint16_t accentColor() const override {
    return 0xF81F;
  }

  void render(DisplayManager& display,
              const DeviceConfig& config,
              const WiFiMgr& wifiMgr,
              unsigned long nowMs) override {
    (void)config;
    (void)wifiMgr;
    (void)nowMs;
    display.showAppScreen(name(), "Module ready", "Idle animation next", "Mood + events", accentColor());
  }
};

class AppShell {
 private:
  static const uint8_t MAX_APPS = 6;
  BaseApp* apps[MAX_APPS];
  uint8_t appCount;
  uint8_t activeIndex;
  unsigned long switchIntervalMs;
  unsigned long refreshIntervalMs;
  unsigned long lastSwitchMs;
  unsigned long lastRenderMs;

 public:
  AppShell()
      : apps{nullptr},
        appCount(0),
        activeIndex(0),
        switchIntervalMs(12000),
        refreshIntervalMs(2000),
        lastSwitchMs(0),
        lastRenderMs(0) {}

  void addApp(BaseApp* app) {
    if (!app || appCount >= MAX_APPS) {
      return;
    }
    apps[appCount++] = app;
  }

  // Set active app by name (case-insensitive match against BaseApp::name())
  void setActiveByName(const char* name) {
    if (!name || appCount == 0) return;
    for (uint8_t i = 0; i < appCount; ++i) {
      if (apps[i] && strcasecmp(apps[i]->name(), name) == 0) {
        activeIndex = i;
        lastRenderMs = 0; // force immediate re-render
        return;
      }
    }
    Serial.printf("[AppShell] App not found: %s (staying on %s)\n",
                  name, apps[activeIndex]->name());
  }

  const char* getActiveAppName() const {
    if (appCount == 0 || !apps[activeIndex]) return "unknown";
    return apps[activeIndex]->name();
  }

  bool hasApps() const {
    return appCount > 0;
  }

  void setSwitchInterval(unsigned long ms) {
    if (ms >= 3000) {
      switchIntervalMs = ms;
    }
  }

  void setRefreshInterval(unsigned long ms) {
    if (ms >= 500) {
      refreshIntervalMs = ms;
    }
  }

  void forceRender(DisplayManager& display,
                   const DeviceConfig& config,
                   const WiFiMgr& wifiMgr,
                   unsigned long nowMs) {
    if (!hasApps()) {
      return;
    }

    lastRenderMs = nowMs;
    apps[activeIndex]->render(display, config, wifiMgr, nowMs);
  }

  void update(DisplayManager& display,
              const DeviceConfig& config,
              const WiFiMgr& wifiMgr,
              unsigned long nowMs) {
    if (!hasApps()) {
      return;
    }

    // No auto-cycling: refresh active app at interval only
    if ((nowMs - lastRenderMs) >= refreshIntervalMs) {
      lastRenderMs = nowMs;
      apps[activeIndex]->render(display, config, wifiMgr, nowMs);
    }
  }
};

#endif // APP_SHELL_H
