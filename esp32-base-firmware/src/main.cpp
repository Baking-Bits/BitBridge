#include <Arduino.h>
#include "device_config.h"
#include "wifi_manager.h"
#include "api_client.h"
#include "display_manager.h"
#include "app_shell.h"

// Define firmware version
#define FIRMWARE_VERSION "1.0.0"
#define FIRMWARE_NAME "BitBridge-ESP32-Base"

// Global objects
DeviceConfig deviceConfig;
WiFiMgr wifiMgr(&deviceConfig);
APIClient apiClient(&deviceConfig);
DisplayManager display;
AppShell appShell;
SystemStatusApp systemStatusApp;
WeatherApp weatherApp;
HomelabApp homelabApp;
PetApp petApp;
bool appShellInitialized = false;

// Timing variables
unsigned long lastCheckinTime = 0;
unsigned long checkinInterval = 60000; // 60 seconds
unsigned long bootTime = 0;

// LED pin for status indication (GPIO 2 on most ESP32 boards)
const int STATUS_LED = 2;
const int BUTTON_PIN = 0; // BOOT button for reset

void setupStatusLED() {
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
}

void indicateLEDStatus(int blinks = 1, int delayMs = 200) {
  for (int i = 0; i < blinks; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(delayMs);
    digitalWrite(STATUS_LED, LOW);
    delay(delayMs);
  }
}

String apiErrorLine(const APIClient& client) {
  int httpCode = client.getLastHttpCode();
  if (httpCode > 0) {
    return String("HTTP ") + String(httpCode);
  }

  String error = client.getLastError();
  if (error.isEmpty()) {
    return String("NETWORK ERROR");
  }

  error.replace("_", " ");
  error.toUpperCase();
  if (error.length() > 20) {
    error = error.substring(0, 20);
  }
  return error;
}

void ensureAppShellInitialized() {
  if (appShellInitialized) {
    return;
  }

  appShell.addApp(&systemStatusApp);
  appShell.addApp(&weatherApp);
  appShell.addApp(&homelabApp);
  appShell.addApp(&petApp);
  appShell.setSwitchInterval(12000);
  appShell.setRefreshInterval(2000);
  appShellInitialized = true;
}

void handleButtonPress() {
  // Reset device config on long press
  static unsigned long pressStart = 0;
  
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (pressStart == 0) {
      pressStart = millis();
    }
  } else {
    if (pressStart > 0 && (millis() - pressStart) > 5000) {
      Serial.println("[Device] Long press detected, resetting config...");
      SPIFFS.remove(CONFIG_FILE);
      delay(100);
      ESP.restart();
    }
    pressStart = 0;
  }
}

void setup() {
  // Initialize Serial
  Serial.begin(115200);
  delay(100);

  Serial.println("\n\n");
  Serial.println("=====================================");
  Serial.printf("%s v%s\n", FIRMWARE_NAME, FIRMWARE_VERSION);
  Serial.println("=====================================");
  Serial.println();

  bootTime = millis();

  display.begin();
  display.showBoot("Booting...");

  // Setup LED
  setupStatusLED();
  indicateLEDStatus(2);

  // Setup button
  pinMode(BUTTON_PIN, INPUT);

  // Load device configuration from SPIFFS
  if (!deviceConfig.begin()) {
    Serial.println("[Setup] Failed to initialize device config");
    display.showError("Config Error", "SPIFFS init failed");
    indicateLEDStatus(5);
    return;
  }

  Serial.println("[Setup] Device config loaded");
  Serial.printf("[Setup] Serial Number: %s\n", deviceConfig.getSerialNumber().c_str());
  Serial.printf("[Setup] Device Group: %s\n", DEVICE_GROUP);

  if (deviceConfig.requirePostFlashReboot(String(FIRMWARE_VERSION))) {
    Serial.printf("[Setup] New firmware version detected (%s). Performing one-time post-flash reboot...\n", FIRMWARE_VERSION);
    display.showRegistration("Post-flash reboot");
    delay(800);
    ESP.restart();
    return;
  }

  // Connect WiFi
  Serial.println("[Setup] Initializing WiFi...");
  display.showWiFiProvisioning();
  if (!wifiMgr.begin()) {
    Serial.println("[Setup] WiFi initialization failed - entering provisioning mode");
    display.showWiFiProvisioning();
    indicateLEDStatus(3);
    return;
  }

  indicateLEDStatus(1);
  Serial.printf("[Setup] WiFi connected! IP: %s\n", wifiMgr.getIP().c_str());
  display.showWiFiConnected(wifiMgr.getIP());

  // Configure control plane URL if not already set
  if (deviceConfig.controlPlaneUrl.isEmpty()) {
    deviceConfig.setControlPlaneUrl("https://respond2.me");
  }

  Serial.printf("[Setup] Control Plane URL: %s\n", deviceConfig.controlPlaneUrl.c_str());

  // Register device if not already registered
  if (deviceConfig.deviceId.isEmpty()) {
    Serial.println("[Setup] First run - registering device...");
    display.showRegistration("Registering...");
    if (apiClient.registerDevice()) {
      Serial.printf("[Setup] Device registered with ID: %s\n", deviceConfig.deviceId.c_str());
      display.showRegistration("Registration OK");
      indicateLEDStatus(2);
    } else {
      Serial.println("[Setup] Device registration failed - will retry on next boot");
      String line2 = apiErrorLine(apiClient);
      display.showError("REGISTER FAILED", line2.c_str());
      indicateLEDStatus(4);
    }
  } else {
    Serial.printf("[Setup] Device already registered with ID: %s\n", deviceConfig.deviceId.c_str());
    ensureAppShellInitialized();
    appShell.forceRender(display, deviceConfig, wifiMgr, millis());
  }

  // Send initial check-in
  if (!deviceConfig.deviceId.isEmpty()) {
    Serial.println("[Setup] Sending initial check-in...");
    if (apiClient.sendCheckin("booting")) {
      Serial.println("[Setup] Initial check-in successful");
      ensureAppShellInitialized();
      appShell.forceRender(display, deviceConfig, wifiMgr, millis());
      lastCheckinTime = millis();
    }
  }

  Serial.println("[Setup] Initialization complete");
  Serial.println();
}

void loop() {
  display.tick();

  // Handle button press
  handleButtonPress();

  // Check WiFi connection
  if (!wifiMgr.isConnected()) {
    Serial.println("[Loop] WiFi disconnected - attempting to reconnect");
    display.showError("Wi-Fi Lost", "Reconnecting...");
    indicateLEDStatus(2);
    wifiMgr.begin();
    if (wifiMgr.isConnected()) {
      display.showWiFiConnected(wifiMgr.getIP());
    }
    delay(5000);
    return;
  }

  // Send periodic check-in
  if ((millis() - lastCheckinTime) > checkinInterval) {
    if (deviceConfig.deviceId.isEmpty()) {
      Serial.println("[Loop] Device ID missing, attempting re-registration...");
      display.showRegistration("Recovering Device ID");
      if (apiClient.registerDevice()) {
        Serial.printf("[Loop] Re-registration succeeded, device ID: %s\n", deviceConfig.deviceId.c_str());
        ensureAppShellInitialized();
        appShell.forceRender(display, deviceConfig, wifiMgr, millis());
      } else {
        String line2 = apiErrorLine(apiClient);
        display.showError("REGISTER FAILED", line2.c_str());
      }
      lastCheckinTime = millis();
      delay(1000);
      return;
    }

    Serial.println("[Loop] Sending periodic check-in...");

    // Create custom data object
    StaticJsonDocument<256> customData;
    customData["signal_strength"] = wifiMgr.getRSSI();
    customData["uptime_seconds"] = (millis() - bootTime) / 1000;
    customData["loop_count"] = 0; // Can be incremented if needed

    if (apiClient.sendCheckin("ok", &customData)) {
      Serial.println("[Loop] Check-in successful");
      ensureAppShellInitialized();
      appShell.forceRender(display, deviceConfig, wifiMgr, millis());
      indicateLEDStatus(1);
      lastCheckinTime = millis();

      // Check for available OTA update
      if (apiClient.checkOTAUpdate()) {
        Serial.println("[Loop] OTA update available - implementation pending");
        // TODO: Implement OTA update logic using Update.h
      }
    } else {
      Serial.println("[Loop] Check-in failed - will retry");
      String line2 = apiErrorLine(apiClient);
      display.showError("CHECKIN FAILED", line2.c_str());
      indicateLEDStatus(3);
    }
  }

  if (!deviceConfig.deviceId.isEmpty()) {
    ensureAppShellInitialized();
    appShell.update(display, deviceConfig, wifiMgr, millis());
  }

  delay(1000);
}
