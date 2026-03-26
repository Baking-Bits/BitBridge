#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "device_config.h"

// App assignment received from the server at check-in
struct AppAssignment {
  bool    hasModule    = false;
  String  appType;       // "system", "weather", "homelab", "pet"
  String  appVersion;    // "1.0.0"
  String  appModuleUrl;  // full URL to .wasm file
  String  appChecksum;   // sha256 hex or empty
  int     minHostAbi    = 1;
};

class APIClient {
 private:
  DeviceConfig* config;
  HTTPClient httpClient;
  WiFiClient wifiClient;
  WiFiClientSecure wifiClientSecure;
  int lastHttpCode;
  String lastError;
  AppAssignment lastAppAssignment;

 public:
  APIClient(DeviceConfig* cfg) : config(cfg), lastHttpCode(0), lastError("") {}

  int getLastHttpCode() const {
    return lastHttpCode;
  }

  String getLastError() const {
    return lastError;
  }

  const AppAssignment& getLastAppAssignment() const {
    return lastAppAssignment;
  }

  // Register device with control plane
  bool registerDevice() {
    lastHttpCode = 0;
    lastError = "";

    if (!config->isConfigured || config->controlPlaneUrl.isEmpty()) {
      Serial.println("[APIClient] Not configured, skipping registration");
      lastError = "NOT_CONFIGURED";
      return false;
    }

    String url = buildApiUrl("/api/public/devices/register");
    String serialNumber = config->getSerialNumber();
    String response;

    int httpCode = postRegister(url, serialNumber, response);
    lastHttpCode = httpCode;

    if (httpCode == 201 || httpCode == 200) {
      Serial.printf("[APIClient] Device registered successfully (HTTP %d)\n", httpCode);

      if (parseDeviceIdFromResponse(response)) {
        lastError = "";
        return true;
      }

      lastError = "REGISTER_RESPONSE_PARSE";
    } else if (httpCode == 409) {
      Serial.println("[APIClient] Device already registered");
      // Must recover device ID from response before treating as success.
      if (parseDeviceIdFromResponse(response)) {
        lastError = "";
        return true;
      }

      // Conflict proves the device exists server-side, but check-in requires ID.
      // Do not create duplicate records with synthetic serials.
      lastError = "EXISTS_BUT_ID_MISSING";
    } else {
      if (httpCode == 0) {
        lastError = "HTTP_BEGIN_FAILED";
      }
      Serial.printf("[APIClient] Registration failed (HTTP %d): %s\n", httpCode, response.c_str());
      if (lastError.isEmpty()) {
        lastError = response;
      }
    }

    return false;
  }

  // Send device check-in with status and data
  bool sendCheckin(const String& status = "ok", JsonDocument* customData = nullptr) {
    lastHttpCode = 0;
    lastError = "";

    if (config->deviceId.isEmpty()) {
      Serial.println("[APIClient] Device ID not set, cannot send check-in");
      lastError = "DEVICE_ID_MISSING";
      return false;
    }

    String url = buildApiUrl(String("/api/public/devices/") + config->deviceId + "/checkin");

    StaticJsonDocument<512> doc;
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["status"] = status;

    // Add system info
    StaticJsonDocument<256> dataDoc;
    dataDoc["rssi"] = WiFi.RSSI();
    dataDoc["ip"] = WiFi.localIP().toString();
    dataDoc["uptime"] = millis() / 1000;
    dataDoc["heap_free"] = esp_get_free_heap_size();
    dataDoc["heap_min"] = esp_get_minimum_free_heap_size();

    // Merge custom data if provided
    if (customData) {
      for (JsonPair p : customData->as<JsonObject>()) {
        dataDoc[p.key()] = p.value();
      }
    }

    doc["data"] = dataDoc;

    String payload;
    serializeJson(doc, payload);

    Serial.printf("[APIClient] Sending check-in to: %s\n", url.c_str());
    Serial.printf("[APIClient] Payload: %s\n", payload.c_str());

    if (!beginRequest(url)) {
      Serial.println("[APIClient] Unable to initialize HTTP client");
      lastError = "HTTP_BEGIN_FAILED";
      return false;
    }
    httpClient.addHeader("Content-Type", "application/json");

    int httpCode = httpClient.POST(payload);
    String response = httpClient.getString();
    httpClient.end();
    lastHttpCode = httpCode;

    if (httpCode == 200) {
      Serial.printf("[APIClient] Check-in successful (HTTP %d)\n", httpCode);
      lastError = "";

      // Parse app assignment from response
      lastAppAssignment = AppAssignment{};
      StaticJsonDocument<1024> resp;
      if (!deserializeJson(resp, response) && resp.containsKey("app") && !resp["app"].isNull()) {
        lastAppAssignment.hasModule    = true;
        lastAppAssignment.appType      = resp["app"]["appType"].as<String>();
        lastAppAssignment.appVersion   = resp["app"]["appVersion"].as<String>();
        lastAppAssignment.appModuleUrl = resp["app"]["appModuleUrl"].as<String>();
        lastAppAssignment.appChecksum  = resp["app"]["appChecksum"].as<String>();
        lastAppAssignment.minHostAbi   = resp["app"]["minHostAbi"] | 1;
        Serial.printf("[APIClient] App assignment: %s v%s\n",
                      lastAppAssignment.appType.c_str(),
                      lastAppAssignment.appVersion.c_str());
      }

      return true;
    } else {
      Serial.printf("[APIClient] Check-in failed (HTTP %d): %s\n", httpCode, response.c_str());
      lastError = response;
      return false;
    }
  }

  // Check for available OTA update
  bool checkOTAUpdate() {
    if (config->deviceId.isEmpty() || config->controlPlaneUrl.isEmpty()) {
      return false;
    }

    String url = buildApiUrl(String("/api/devices/") + config->deviceId + "/ota-available");

    Serial.printf("[APIClient] Checking OTA updates at: %s\n", url.c_str());

    if (!beginRequest(url)) {
      Serial.println("[APIClient] Unable to initialize HTTP client");
      return false;
    }
    int httpCode = httpClient.GET();
    String response = httpClient.getString();
    httpClient.end();

    if (httpCode == 200) {
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, response);
      if (!error && doc["ok"]) {
        bool updateAvailable = doc["updateAvailable"];
        String newVersion = doc["newVersion"].as<String>();
        String updateUrl = doc["updateUrl"].as<String>();

        if (updateAvailable) {
          Serial.printf("[APIClient] OTA update available: %s from %s\n", newVersion.c_str(), updateUrl.c_str());
          return true;
        }
      }
    }

    return false;
  }

 private:
  static String extractHost(const String& url) {
    int schemeIdx = url.indexOf("://");
    int hostStart = schemeIdx >= 0 ? schemeIdx + 3 : 0;
    int hostEnd = url.indexOf('/', hostStart);
    String hostPort = (hostEnd >= 0) ? url.substring(hostStart, hostEnd) : url.substring(hostStart);
    int atIdx = hostPort.lastIndexOf('@');
    if (atIdx >= 0) {
      hostPort = hostPort.substring(atIdx + 1);
    }
    int colonIdx = hostPort.indexOf(':');
    String host = (colonIdx >= 0) ? hostPort.substring(0, colonIdx) : hostPort;
    host.trim();
    host.toLowerCase();
    return host;
  }

  static bool isPrivateIpv4Host(const String& host) {
    int a = -1, b = -1, c = -1, d = -1;
    if (sscanf(host.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
      return false;
    }
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) {
      return false;
    }
    if (a == 10) return true;
    if (a == 127) return true;
    if (a == 192 && b == 168) return true;
    if (a == 172 && b >= 16 && b <= 31) return true;
    return false;
  }

  static bool shouldDowngradeHttpsToHttp(const String& url) {
    if (!url.startsWith("https://")) {
      return false;
    }
    String host = extractHost(url);
    if (host.isEmpty()) {
      return false;
    }
    return host == "localhost" || host.endsWith(".local") || isPrivateIpv4Host(host);
  }

  String normalizedBaseUrl() {
    String baseUrl = config->controlPlaneUrl;
    baseUrl.trim();

    // Migrate legacy local default to local dev server.
    if (baseUrl.indexOf("bitbridge.local") >= 0 || baseUrl == "https://respond2.me" || baseUrl == "http://respond2.me") {
      baseUrl = "http://192.168.1.206:8787";
      config->controlPlaneUrl = baseUrl;
      config->saveToFile();
      Serial.printf("[APIClient] Migrated legacy URL to local dev server: %s\n", baseUrl.c_str());
    }

    // If user entered host only (no scheme), default to HTTPS.
    if (!baseUrl.isEmpty() && !baseUrl.startsWith("http://") && !baseUrl.startsWith("https://")) {
      baseUrl = "https://" + baseUrl;
      config->controlPlaneUrl = baseUrl;
      config->saveToFile();
    }

    if (shouldDowngradeHttpsToHttp(baseUrl)) {
      baseUrl = "http://" + baseUrl.substring(8);
      config->controlPlaneUrl = baseUrl;
      config->saveToFile();
      Serial.printf("[APIClient] Local/private host detected, using HTTP: %s\n", baseUrl.c_str());
    }

    while (baseUrl.endsWith("/")) {
      baseUrl.remove(baseUrl.length() - 1);
    }

    return baseUrl;
  }

  String buildApiUrl(const String& path) {
    String baseUrl = normalizedBaseUrl();
    if (baseUrl.isEmpty()) {
      return path;
    }

    if (path.startsWith("/")) {
      return baseUrl + path;
    }

    return baseUrl + "/" + path;
  }

  bool beginRequest(const String& url) {
    httpClient.setTimeout(15000);
    httpClient.setConnectTimeout(10000);
    httpClient.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (url.startsWith("https://")) {
      wifiClientSecure.setInsecure();
      wifiClientSecure.setTimeout(15000);
      return httpClient.begin(wifiClientSecure, url);
    }

    wifiClient.setTimeout(15000);
    return httpClient.begin(wifiClient, url);
  }

  int postRegister(const String& url, const String& serialNumber, String& responseOut) {
    StaticJsonDocument<256> doc;
    doc["deviceGroup"] = DEVICE_GROUP;
    doc["deviceLabel"] = config->deviceLabel.isEmpty() ? serialNumber : config->deviceLabel;
    doc["serialNumber"] = serialNumber;
    doc["initialFirmwareVersion"] = FIRMWARE_VERSION;

    String payload;
    serializeJson(doc, payload);

    Serial.printf("[APIClient] Registering device at: %s\n", url.c_str());
    Serial.printf("[APIClient] Payload: %s\n", payload.c_str());

    if (!beginRequest(url)) {
      Serial.println("[APIClient] Unable to initialize HTTP client");
      responseOut = "";
      return 0;
    }

    httpClient.addHeader("Content-Type", "application/json");
    int httpCode = httpClient.POST(payload);
    responseOut = httpClient.getString();
    httpClient.end();
    return httpCode;
  }

  bool parseDeviceIdFromResponse(const String& response) {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      return false;
    }

    String parsedId;

    if (doc["device"]["id"]) {
      parsedId = doc["device"]["id"].as<String>();
    } else if (doc["id"]) {
      parsedId = doc["id"].as<String>();
    } else if (doc["deviceId"]) {
      parsedId = doc["deviceId"].as<String>();
    } else if (doc["data"]["device"]["id"]) {
      parsedId = doc["data"]["device"]["id"].as<String>();
    } else if (doc["data"]["id"]) {
      parsedId = doc["data"]["id"].as<String>();
    }

    parsedId.trim();
    if (parsedId.isEmpty()) {
      return false;
    }

    config->deviceId = parsedId;
    config->saveToFile();
    return true;
  }
};

#endif // API_CLIENT_H
