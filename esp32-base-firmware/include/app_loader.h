// ============================================================
// app_loader.h — Download Wasm app modules from control plane
// Saves modules to SPIFFS under /wasm_<apptype>.wasm
// ============================================================
#ifndef APP_LOADER_H
#define APP_LOADER_H

#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#define APP_WASM_PREFIX   "/wasm_"
#define APP_WASM_EXT      ".wasm"
#define APP_MAX_SIZE      (200 * 1024)  // 200 KB hard limit per module

class AppLoader {
 public:
  static bool isValidWasmFile(const String& path) {
    if (!SPIFFS.exists(path)) return false;
    File f = SPIFFS.open(path, "r");
    if (!f) return false;

    uint8_t magic[4] = {0, 0, 0, 0};
    size_t n = f.read(magic, sizeof(magic));
    f.close();

    return n == 4 && magic[0] == 0x00 && magic[1] == 0x61 && magic[2] == 0x73 && magic[3] == 0x6D;
  }

  // Returns the SPIFFS path for an app type's module file
  static String wasmPath(const String& appType) {
    String t = appType;
    t.toLowerCase();
    t.trim();
    if (t.isEmpty()) t = "unknown";
    return String(APP_WASM_PREFIX) + t + String(APP_WASM_EXT);
  }

  // Returns true if a module file currently exists on SPIFFS
  static bool hasModule(const String& appType) {
    return SPIFFS.exists(wasmPath(appType));
  }

  // Remove the module file from SPIFFS (e.g. before replacing with a newer version)
  static bool clearModule(const String& appType) {
    String path = wasmPath(appType);
    if (!SPIFFS.exists(path)) return true;
    bool ok = SPIFFS.remove(path);
    if (ok) Serial.printf("[AppLoader] Removed: %s\n", path.c_str());
    return ok;
  }

  // Download a .wasm module from `url` and save it to SPIFFS for `appType`.
  // `expectedChecksum` is informational only in v1 (SHA-256 hex, not yet verified).
  // Returns true on success.
  bool download(const String& url, const String& appType, const String& expectedChecksum = "") {
    if (url.isEmpty()) {
      Serial.println("[AppLoader] Empty URL — skipping download");
      return false;
    }

    Serial.printf("[AppLoader] Downloading %s → %s\n", url.c_str(), appType.c_str());

    HTTPClient http;
    WiFiClientSecure secureClient;
    WiFiClient       plainClient;

    if (url.startsWith("https://")) {
      secureClient.setInsecure();
      secureClient.setTimeout(20);  // seconds
      if (!http.begin(secureClient, url)) {
        Serial.println("[AppLoader] https begin failed");
        return false;
      }
    } else {
      if (!http.begin(plainClient, url)) {
        Serial.println("[AppLoader] http begin failed");
        return false;
      }
    }

    http.setTimeout(20000);
    int httpCode = http.GET();

    if (httpCode != 200) {
      Serial.printf("[AppLoader] HTTP %d for %s\n", httpCode, url.c_str());
      http.end();
      return false;
    }

    int totalSize = http.getSize();
    Serial.printf("[AppLoader] Content-Length: %d bytes\n", totalSize);

    if (totalSize > APP_MAX_SIZE) {
      Serial.printf("[AppLoader] Module too large: %d bytes (max %d)\n", totalSize, APP_MAX_SIZE);
      http.end();
      return false;
    }

    String destPath = wasmPath(appType);

    // Write to a temp path then rename to avoid partial files on failure
    String tmpPath = destPath + ".tmp";
    File f = SPIFFS.open(tmpPath, "w");
    if (!f) {
      Serial.printf("[AppLoader] Cannot create %s\n", tmpPath.c_str());
      http.end();
      return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t     buf[512];
    int         bytesWritten   = 0;
    int         bytesRemaining = totalSize;
    unsigned long startMs      = millis();

    while (http.connected() && (bytesRemaining > 0 || totalSize < 0)) {
      if ((millis() - startMs) > 20000UL) {
        Serial.println("[AppLoader] Timeout");
        f.close();
        SPIFFS.remove(tmpPath);
        http.end();
        return false;
      }

      size_t available = stream->available();
      if (available) {
        size_t toRead = min(available, sizeof(buf));
        if (bytesRemaining > 0) toRead = min(toRead, (size_t)bytesRemaining);
        int n = stream->readBytes(buf, toRead);
        if (n > 0) {
          f.write(buf, n);
          bytesWritten   += n;
          bytesRemaining -= n;
        }
      } else {
        delay(1);
      }

      if (totalSize > 0 && bytesRemaining <= 0) break;
    }

    f.close();
    http.end();

    if (bytesWritten == 0) {
      Serial.println("[AppLoader] Zero bytes written");
      SPIFFS.remove(tmpPath);
      return false;
    }

    if (!isValidWasmFile(tmpPath)) {
      Serial.println("[AppLoader] Download is not a valid .wasm (bad magic header)");
      SPIFFS.remove(tmpPath);
      return false;
    }

    // Replace old module with new one
    if (SPIFFS.exists(destPath)) SPIFFS.remove(destPath);
    SPIFFS.rename(tmpPath, destPath);

    Serial.printf("[AppLoader] Saved %d bytes to %s. SPIFFS free: %d\n",
                  bytesWritten, destPath.c_str(),
                  SPIFFS.totalBytes() - SPIFFS.usedBytes());

    if (!expectedChecksum.isEmpty()) {
      Serial.printf("[AppLoader] Checksum (v2 TODO): %s\n", expectedChecksum.c_str());
    }

    return true;
  }
};

#endif  // APP_LOADER_H
