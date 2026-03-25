// ============================================================
// wasm_host.h — Wasm3 runtime lifecycle manager
// Loads a .wasm module from SPIFFS, links host API, calls exports
// ============================================================
#ifndef WASM_HOST_H
#define WASM_HOST_H

#include <Arduino.h>
#include <SPIFFS.h>
#include "host_api.h"

// Stack depth for Wasm3 interpreter (number of 64-bit slots)
#define WASM_STACK_SLOTS  4096

#if defined(WASM3_AVAILABLE)

class WasmHost {
 public:
  WasmHost()
      : env(nullptr), runtime(nullptr), module(nullptr),
        renderFn(nullptr), initFn(nullptr), loaded(false) {}

  ~WasmHost() { unload(); }

  // Load and initialise a .wasm module from SPIFFS.
  // appType is stored in the host context so config calls know which namespace to use.
  bool loadFromSPIFFS(const String& path, const String& appType) {
    Serial.printf("[WasmHost] Loading from SPIFFS: %s\n", path.c_str());

    if (!SPIFFS.exists(path)) {
      Serial.println("[WasmHost] File not found");
      return false;
    }

    File f = SPIFFS.open(path, "r");
    if (!f) {
      Serial.println("[WasmHost] Cannot open file");
      return false;
    }

    size_t fileSize = f.size();
    Serial.printf("[WasmHost] Module size: %d bytes\n", fileSize);

    if (fileSize == 0 || fileSize > 200 * 1024) {
      Serial.printf("[WasmHost] Module size out of range (%d bytes)\n", fileSize);
      f.close();
      return false;
    }

    uint8_t* buf = (uint8_t*)malloc(fileSize);
    if (!buf) {
      Serial.println("[WasmHost] malloc failed — not enough heap");
      f.close();
      return false;
    }

    size_t bytesRead = f.read(buf, fileSize);
    f.close();

    if (bytesRead != fileSize) {
      Serial.println("[WasmHost] Short read");
      free(buf);
      return false;
    }

    bool ok = _loadFromBytes(buf, fileSize, appType);
    free(buf);
    return ok;
  }

  // Call the Wasm module's render(nowMs) export
  bool callRender(int32_t nowMs) {
    if (!loaded || !renderFn) return false;
    M3Result r = m3_CallV(renderFn, nowMs);
    if (r) {
      Serial.printf("[WasmHost] render() error: %s\n", r);
      return false;
    }
    return true;
  }

  // Call the optional on_init() export (called once after load)
  void callOnInit() {
    if (!loaded || !initFn) return;
    M3Result r = m3_CallV(initFn);
    if (r) Serial.printf("[WasmHost] on_init() error: %s\n", r);
  }

  bool isLoaded() const { return loaded; }

  void unload() {
    renderFn = nullptr;
    initFn   = nullptr;
    if (runtime) { m3_FreeRuntime(runtime); runtime = nullptr; }
    if (env)     { m3_FreeEnvironment(env); env = nullptr; }
    module   = nullptr;
    loaded   = false;
    if (_wasLoaded) {
      Serial.printf("[WasmHost] Unloaded. Heap free: %d\n", esp_get_free_heap_size());
    }
    _wasLoaded = false;
  }

 private:
  IM3Environment env;
  IM3Runtime     runtime;
  IM3Module      module;
  IM3Function    renderFn;
  IM3Function    initFn;
  bool           loaded;
  bool           _wasLoaded = false;

  bool _loadFromBytes(const uint8_t* bytes, size_t size, const String& appType) {
    unload();

    env = m3_NewEnvironment();
    if (!env) {
      Serial.println("[WasmHost] m3_NewEnvironment failed");
      return false;
    }

    runtime = m3_NewRuntime(env, WASM_STACK_SLOTS, nullptr);
    if (!runtime) {
      Serial.println("[WasmHost] m3_NewRuntime failed");
      m3_FreeEnvironment(env); env = nullptr;
      return false;
    }

    M3Result r = m3_ParseModule(env, &module, bytes, (uint32_t)size);
    if (r) {
      Serial.printf("[WasmHost] ParseModule: %s\n", r);
      m3_FreeRuntime(runtime); runtime = nullptr;
      m3_FreeEnvironment(env); env = nullptr;
      return false;
    }

    r = m3_LoadModule(runtime, module);
    if (r) {
      Serial.printf("[WasmHost] LoadModule: %s\n", r);
      m3_FreeRuntime(runtime); runtime = nullptr;
      m3_FreeEnvironment(env); env = nullptr;
      return false;
    }

    // Set active app type in context before linking (used by config calls)
    if (g_wasm_ctx) g_wasm_ctx->activeAppType = appType;

    // Register all host functions
    registerHostApi(module);

    // ABI version check (optional — module must export module_abi_required() → i32)
    // Skipping variadic result read to avoid wasm3 version differences; guard in v2.
    // If present and returns a value > HOST_ABI_VERSION, refuse load.
    IM3Function abiFn = nullptr;
    m3_FindFunction(&abiFn, runtime, "module_abi_required");
    // (ABI enforcement via m3_GetResultsV deferred to v1.4 when wasm3 API stabilises)

    // Cache function handles (render is mandatory)
    m3_FindFunction(&renderFn, runtime, "render");
    m3_FindFunction(&initFn,   runtime, "on_init");

    if (!renderFn) {
      Serial.println("[WasmHost] Module missing required 'render' export");
      m3_FreeRuntime(runtime); runtime = nullptr;
      m3_FreeEnvironment(env); env = nullptr;
      return false;
    }

    loaded     = true;
    _wasLoaded = true;
    Serial.printf("[WasmHost] Module loaded OK. Heap free: %d\n", esp_get_free_heap_size());
    return true;
  }
};

#else  // ── Stub when wasm3 not installed ──────────────────────────────────

class WasmHost {
 public:
  bool loadFromSPIFFS(const String&, const String&) { return false; }
  bool callRender(int32_t) { return false; }
  void callOnInit() {}
  bool isLoaded() const { return false; }
  void unload() {}
};

#endif  // WASM3_AVAILABLE

#endif  // WASM_HOST_H
