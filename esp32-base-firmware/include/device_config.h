#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#define FIRMWARE_VERSION "1.0.0"
#define FIRMWARE_NAME "BitBridge-ESP32-Base"
#define DEVICE_GROUP "esp32-s3"

#define CONFIG_FILE "/spiffs/device_config.json"
#define SPIFFS_FORMAT_IF_FAILED true

class DeviceConfig {
 public:
  String deviceId;
  String serialNumber;
  String deviceLabel;
  String wifiSSID;
  String wifiPassword;
  String controlPlaneUrl;
  String lastBootFirmwareVersion;
  bool isConfigured;

  DeviceConfig() : isConfigured(false) {}

  bool begin() {
    if (!SPIFFS.begin(SPIFFS_FORMAT_IF_FAILED)) {
      Serial.println("[DeviceConfig] SPIFFS mount failed");
      return false;
    }

    if (!loadFromFile()) {
      Serial.println("[DeviceConfig] No config file found, needs provisioning");
      return true;
    }

    isConfigured = true;
    return true;
  }

  bool loadFromFile() {
    if (!SPIFFS.exists(CONFIG_FILE)) {
      return false;
    }

    File file = SPIFFS.open(CONFIG_FILE, "r");
    if (!file) {
      return false;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
      Serial.printf("[DeviceConfig] JSON parse error: %s\n", error.c_str());
      return false;
    }

    deviceId = doc["deviceId"].as<String>();
    serialNumber = doc["serialNumber"].as<String>();
    deviceLabel = doc["deviceLabel"].as<String>();
    wifiSSID = doc["wifiSSID"].as<String>();
    wifiPassword = doc["wifiPassword"].as<String>();
    controlPlaneUrl = doc["controlPlaneUrl"].as<String>();
    lastBootFirmwareVersion = doc["lastBootFirmwareVersion"].as<String>();

    return !deviceId.isEmpty() && !serialNumber.isEmpty();
  }

  bool saveToFile() {
    StaticJsonDocument<1024> doc;
    doc["deviceId"] = deviceId;
    doc["serialNumber"] = serialNumber;
    doc["deviceLabel"] = deviceLabel;
    doc["wifiSSID"] = wifiSSID;
    doc["wifiPassword"] = wifiPassword;
    doc["controlPlaneUrl"] = controlPlaneUrl;
    doc["lastBootFirmwareVersion"] = lastBootFirmwareVersion;

    File file = SPIFFS.open(CONFIG_FILE, "w");
    if (!file) {
      Serial.println("[DeviceConfig] Failed to open file for writing");
      return false;
    }

    if (serializeJson(doc, file) == 0) {
      Serial.println("[DeviceConfig] Failed to write JSON");
      file.close();
      return false;
    }

    file.close();
    isConfigured = true;
    return true;
  }

  void setWiFiCredentials(const String& ssid, const String& password) {
    wifiSSID = ssid;
    wifiPassword = password;
  }

  void setControlPlaneUrl(const String& url) {
    controlPlaneUrl = url;
  }

  void setDeviceInfo(const String& id, const String& serial, const String& label) {
    deviceId = id;
    serialNumber = serial;
    deviceLabel = label;
  }

  bool requirePostFlashReboot(const String& currentFirmwareVersion) {
    if (currentFirmwareVersion.isEmpty()) {
      return false;
    }

    if (lastBootFirmwareVersion == currentFirmwareVersion) {
      return false;
    }

    lastBootFirmwareVersion = currentFirmwareVersion;
    saveToFile();
    return true;
  }

  String getSerialNumber() {
    if (!serialNumber.isEmpty()) {
      return serialNumber;
    }
    // Generate from MAC address if not set
    byte mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buffer[32];
    sprintf(buffer, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    serialNumber = String(buffer);
    return serialNumber;
  }
};

#endif // DEVICE_CONFIG_H
