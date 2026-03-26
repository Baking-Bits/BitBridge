#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "device_config.h"

class WiFiMgr {
 private:
  WiFiManager wm;
  DeviceConfig* config;
  String activeApSsid;
  WiFiManagerParameter* controlPlaneParam;
  char controlPlaneUrlBuffer[128];

 public:
  WiFiMgr(DeviceConfig* cfg)
      : config(cfg),
        activeApSsid(""),
        controlPlaneParam(nullptr),
        controlPlaneUrlBuffer{0} {}

  bool begin() {
    // Aura-style blocking flow:
    // 1) try saved credentials
    // 2) if missing/invalid, open captive AP portal
    // 3) on Save, switch to submitted Wi-Fi and return only after connected
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    wm.setConfigPortalBlocking(true);
    wm.setConfigPortalTimeout(0);
    wm.setCaptivePortalEnable(true);
    wm.setWebPortalClientCheck(true);
    wm.setShowInfoUpdate(false);
    wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

    wm.setAPCallback([this](WiFiManager*) {
      Serial.printf("[WiFiMgr] AP SSID: %s\n", activeApSsid.c_str());
      Serial.println("[WiFiMgr] Open this SSID and visit: http://192.168.4.1");
    });

    wm.setSaveConfigCallback([this]() {
      Serial.println("[WiFiMgr] Credentials submitted, switching to STA...");
    });

    char suffix[7] = {0};
    snprintf(suffix, sizeof(suffix), "%06X", static_cast<unsigned int>(ESP.getEfuseMac() & 0xFFFFFFULL));
    activeApSsid = String("BitBridge-Setup-") + String(suffix);

    const char *defaultControlPlane = "http://192.168.1.206:8787";
    String currentControlPlane = config->controlPlaneUrl;
    if (currentControlPlane.isEmpty()) {
      currentControlPlane = String(defaultControlPlane);
    }
    currentControlPlane.toCharArray(controlPlaneUrlBuffer, sizeof(controlPlaneUrlBuffer));
    if (!controlPlaneParam) {
      controlPlaneParam = new WiFiManagerParameter(
        "cpurl",
        "Control Plane URL",
        controlPlaneUrlBuffer,
        sizeof(controlPlaneUrlBuffer) - 1
      );
      wm.addParameter(controlPlaneParam);
    }

    Serial.println("[WiFiMgr] Starting Wi-Fi (Aura-style autoConnect)...");
    bool connected = wm.autoConnect(activeApSsid.c_str());
    if (!connected || WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFiMgr] Wi-Fi connect failed");
      return false;
    }

    config->wifiSSID = WiFi.SSID();
    config->wifiPassword = WiFi.psk();
    if (controlPlaneParam) {
      String selectedControlPlane = String(controlPlaneParam->getValue());
      selectedControlPlane.trim();
      if (!selectedControlPlane.isEmpty()) {
        config->controlPlaneUrl = selectedControlPlane;
      }
    }
    config->saveToFile();

    activeApSsid = "";
    Serial.printf("[WiFiMgr] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFiMgr] Control plane URL: %s\n", config->controlPlaneUrl.c_str());
    return true;
  }

  bool isConnected() const {
    return WiFi.status() == WL_CONNECTED;
  }

  String getIP() const {
    return WiFi.localIP().toString();
  }

  String getSSID() const {
    return WiFi.SSID();
  }

  int8_t getRSSI() const {
    return WiFi.RSSI();
  }

  String getProvisioningSsid() const {
    if (!activeApSsid.isEmpty()) {
      return activeApSsid;
    }

    char suffix[7] = {0};
    snprintf(suffix, sizeof(suffix), "%06X", static_cast<unsigned int>(ESP.getEfuseMac() & 0xFFFFFFULL));
    return String("BitBridge-Setup-") + String(suffix);
  }

};

#endif // WIFI_MANAGER_H
