#include <Arduino.h>
#include "device_config.h"
#include "wifi_manager.h"
#include "api_client.h"
#include "display_manager.h"
#include "app_shell.h"
#include "host_api.h"
#include "wasm_host.h"
#include "app_loader.h"

// FIRMWARE_VERSION and FIRMWARE_NAME are defined in device_config.h

// Global objects
DeviceConfig deviceConfig;
WiFiMgr wifiMgr(&deviceConfig);
APIClient apiClient(&deviceConfig);
DisplayManager display;
AppShell appShell;
WasmHost wasmHost;
AppLoader appLoader;
SystemStatusApp systemStatusApp;
WeatherApp weatherApp;
HomelabApp homelabApp;
PetApp petApp;
bool appShellInitialized = false;

// Wasm render timing
unsigned long lastWasmRenderMs = 0;
const unsigned long WASM_REFRESH_MS = 2000;

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

String resolveModuleUrl(const String& moduleUrl) {
  auto extractHost = [](const String& fullUrl) -> String {
    int schemeIdx = fullUrl.indexOf("://");
    int hostStart = schemeIdx >= 0 ? schemeIdx + 3 : 0;
    int hostEnd = fullUrl.indexOf('/', hostStart);
    String hostPort = (hostEnd >= 0) ? fullUrl.substring(hostStart, hostEnd) : fullUrl.substring(hostStart);
    int atIdx = hostPort.lastIndexOf('@');
    if (atIdx >= 0) {
      hostPort = hostPort.substring(atIdx + 1);
    }
    int colonIdx = hostPort.indexOf(':');
    String host = (colonIdx >= 0) ? hostPort.substring(0, colonIdx) : hostPort;
    host.trim();
    host.toLowerCase();
    return host;
  };

  auto isPrivateIpv4Host = [](const String& host) -> bool {
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
  };

  auto maybeDowngradeHttps = [&](const String& fullUrl) -> String {
    if (!fullUrl.startsWith("https://")) {
      return fullUrl;
    }
    String host = extractHost(fullUrl);
    if (host == "localhost" || host.endsWith(".local") || isPrivateIpv4Host(host)) {
      String downgraded = "http://" + fullUrl.substring(8);
      Serial.printf("[AppAssign] Local/private module host detected, using HTTP: %s\n", downgraded.c_str());
      return downgraded;
    }
    return fullUrl;
  };

  String url = moduleUrl;
  url.trim();
  if (url.isEmpty()) return String("");

  if (url.startsWith("http://") || url.startsWith("https://")) {
    return maybeDowngradeHttps(url);
  }

  String base = deviceConfig.controlPlaneUrl;
  base.trim();
  if (base.isEmpty()) return String("");

  while (base.endsWith("/")) {
    base.remove(base.length() - 1);
  }

  if (url.startsWith("/")) {
    return maybeDowngradeHttps(base + url);
  }

  return maybeDowngradeHttps(base + "/" + url);
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

  // Apply persisted app selection on boot
  appShell.setActiveByName("system");
  if (!deviceConfig.selectedAppType.isEmpty()) {
    Serial.printf("[Setup] Assigned app type: %s\n", deviceConfig.selectedAppType.c_str());
  }

  // Attempt to load a cached Wasm module for the selected app type
  setWasmHostContext(&display, &deviceConfig, &wifiMgr);
  String wasmAppType = deviceConfig.selectedAppType.isEmpty() ? "system" : deviceConfig.selectedAppType;
  if (AppLoader::hasModule(wasmAppType)) {
    Serial.printf("[Setup] Cached Wasm module found for '%s', loading...\n", wasmAppType.c_str());
    String path = AppLoader::wasmPath(wasmAppType);
    if (wasmHost.loadFromSPIFFS(path, wasmAppType)) {
      wasmHost.callOnInit();
      Serial.println("[Setup] Wasm module loaded from cache");
    } else {
      Serial.println("[Setup] Cached Wasm load failed — using native app");
      AppLoader::clearModule(wasmAppType);  // Remove corrupt/invalid module
    }
  }
}

// Handle app assignment received from check-in response
// Downloads new Wasm module if version changed; updates native selection always
void handleAppAssignment() {
  const AppAssignment& a = apiClient.getLastAppAssignment();
  if (!a.hasModule) return;

  String newType    = a.appType.isEmpty()    ? String("system") : a.appType;
  String newVersion = a.appVersion;
  String newUrl     = a.appModuleUrl;

  // Always persist admin's app type selection
  bool typeChanged = (deviceConfig.selectedAppType != newType);
  if (typeChanged) {
    Serial.printf("[AppAssign] App type changed: %s → %s\n",
                  deviceConfig.selectedAppType.c_str(), newType.c_str());
    deviceConfig.selectedAppType = newType;
  }

  // Update native shell selection
  ensureAppShellInitialized();
  appShell.setActiveByName("system");

  // Check if we need to download a new Wasm module
  String resolvedUrl = resolveModuleUrl(newUrl);
  bool versionChanged = (deviceConfig.selectedAppVersion != newVersion);
  bool urlChanged = (deviceConfig.selectedAppModuleUrl != resolvedUrl);
  bool checksumChanged = (deviceConfig.selectedAppChecksum != a.appChecksum);
  bool moduleMissing = !AppLoader::hasModule(newType);
  bool runtimeNotLoaded = !wasmHost.isLoaded();

  // If runtime is not loaded but a cached module exists, try loading cache first.
  // This avoids unnecessary downloads and makes app switching resilient when
  // network is flaky.
  if (runtimeNotLoaded && !moduleMissing) {
    Serial.printf("[AppAssign] Runtime not loaded; trying cached Wasm for '%s'...\n", newType.c_str());
    wasmHost.unload();
    setWasmHostContext(&display, &deviceConfig, &wifiMgr);
    String cachedPath = AppLoader::wasmPath(newType);
    if (wasmHost.loadFromSPIFFS(cachedPath, newType)) {
      wasmHost.callOnInit();
      lastWasmRenderMs = 0;

      if (typeChanged || versionChanged || urlChanged || checksumChanged) {
        deviceConfig.selectedAppVersion = newVersion;
        deviceConfig.selectedAppModuleUrl = resolvedUrl;
        deviceConfig.selectedAppChecksum = a.appChecksum;
        deviceConfig.saveToFile();
      }

      Serial.printf("[AppAssign] Loaded cached Wasm module for '%s'\n", newType.c_str());
      return;
    }

    Serial.println("[AppAssign] Cached Wasm load failed; clearing cache and attempting download");
    AppLoader::clearModule(newType);
    moduleMissing = true;
    runtimeNotLoaded = true;
  }

  bool shouldDownload = !resolvedUrl.isEmpty() && (
    typeChanged || versionChanged || urlChanged || checksumChanged || moduleMissing || runtimeNotLoaded
  );

  if (shouldDownload) {
    Serial.printf("[AppAssign] Downloading Wasm module: %s v%s from %s\n",
                  newType.c_str(), newVersion.c_str(), resolvedUrl.c_str());
    display.showRegistration("Updating App...");

    // Unload any running Wasm first
    wasmHost.unload();

    setWasmHostContext(&display, &deviceConfig, &wifiMgr);
    if (appLoader.download(resolvedUrl, newType, a.appChecksum)) {
      // Load the freshly downloaded module
      String path = AppLoader::wasmPath(newType);
      if (wasmHost.loadFromSPIFFS(path, newType)) {
        wasmHost.callOnInit();
        deviceConfig.selectedAppVersion   = newVersion;
        deviceConfig.selectedAppModuleUrl = resolvedUrl;
        deviceConfig.selectedAppChecksum  = a.appChecksum;
        deviceConfig.saveToFile();
        lastWasmRenderMs = 0;  // force immediate render
        Serial.printf("[AppAssign] Wasm module loaded: %s v%s\n",
                      newType.c_str(), newVersion.c_str());
      } else {
        Serial.println("[AppAssign] Wasm load failed after download");
        AppLoader::clearModule(newType);
        display.showError("APP LOAD FAIL", "Using native fallback");
        delay(2000);
      }
    } else {
      Serial.println("[AppAssign] Wasm download failed — keeping previous module");
      if (AppLoader::hasModule(newType)) {
        Serial.println("[AppAssign] Download failed; attempting cached module fallback");
        setWasmHostContext(&display, &deviceConfig, &wifiMgr);
        String cachedPath = AppLoader::wasmPath(newType);
        if (wasmHost.loadFromSPIFFS(cachedPath, newType)) {
          wasmHost.callOnInit();
          lastWasmRenderMs = 0;
          Serial.println("[AppAssign] Cached module fallback loaded");
          return;
        }
      }
      display.showError("APP DL FAIL", "Retrying next check-in");
      delay(2000);
    }
  } else if (typeChanged || versionChanged || urlChanged || checksumChanged) {
    // Assignment changed but no URL available — persist metadata only
    deviceConfig.selectedAppVersion = newVersion;
    deviceConfig.selectedAppModuleUrl = resolvedUrl;
    deviceConfig.selectedAppChecksum = a.appChecksum;
    deviceConfig.saveToFile();
  }
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
  display.showWiFiProvisioning(wifiMgr.getProvisioningSsid());
  if (!wifiMgr.begin()) {
    Serial.println("[Setup] WiFi initialization failed - entering provisioning mode");
    display.showWiFiProvisioning(wifiMgr.getProvisioningSsid());
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
    StaticJsonDocument<768> customData;
    String selectedType = deviceConfig.selectedAppType.isEmpty() ? "system" : deviceConfig.selectedAppType;
    unsigned long elapsedSinceCheckin = millis() - lastCheckinTime;
    unsigned long remainingMs = (elapsedSinceCheckin >= checkinInterval) ? 0 : (checkinInterval - elapsedSinceCheckin);

    customData["signal_strength"] = wifiMgr.getRSSI();
    customData["uptime_seconds"] = (millis() - bootTime) / 1000;
    customData["loop_count"] = 0; // Can be incremented if needed
    customData["runtime_mode"] = wasmHost.isLoaded() ? "wasm" : "native";
    customData["wasm_loaded"] = wasmHost.isLoaded();
    customData["wasm_cached"] = AppLoader::hasModule(selectedType);
    customData["wasm_app_type"] = selectedType;
    customData["wasm_app_version"] = deviceConfig.selectedAppVersion;
    customData["wasm_module_url"] = deviceConfig.selectedAppModuleUrl;
    customData["checkin_interval_sec"] = (int)(checkinInterval / 1000UL);
    customData["next_checkin_sec"] = (int)(remainingMs / 1000UL);

    if (apiClient.sendCheckin("ok", &customData)) {
      Serial.println("[Loop] Check-in successful");
      ensureAppShellInitialized();
      handleAppAssignment();  // apply any app type / module changes from server
      if (!wasmHost.isLoaded()) {
        appShell.forceRender(display, deviceConfig, wifiMgr, millis());
      }
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
    unsigned long nowMs = millis();
    if (wasmHost.isLoaded()) {
      // Wasm module is active: render via interpreter
      if ((nowMs - lastWasmRenderMs) >= WASM_REFRESH_MS) {
        lastWasmRenderMs = nowMs;
        if (!wasmHost.callRender((int32_t)nowMs)) {
          Serial.println("[Loop] Wasm render failed — falling back to native");
          wasmHost.unload();
          appShell.forceRender(display, deviceConfig, wifiMgr, nowMs);
        }
      }
    } else {
      // No Wasm module: use native C++ app shell
      appShell.update(display, deviceConfig, wifiMgr, nowMs);
    }
  }

  delay(1000);
}
