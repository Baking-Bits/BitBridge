# Weather Wasm Module (ABI v1)

This module runs inside the ESP32 Wasm3 host and renders an Aura-style weather card using Open-Meteo.

## Exports

- `module_abi_required() -> i32` (returns `1`)
- `on_init() -> void`
- `render(now_ms: i32) -> void`

## Host functions used

- `host_show_app_screen`
- `host_wifi_rssi`
- `host_http_get`
- `host_config_read`
- `host_config_write`
- `host_log`

## Config keys (per-app SPIFFS namespace)

- `lat` (default `40.7128`)
- `lon` (default `-74.0060`)
- `units` (`fahrenheit` or `celsius`, default `fahrenheit`)
- `timezone` (default `auto`)
- `location_mode` (`auto_ip` or `manual`, default `auto_ip`)
- `city` (optional display label; auto-populated when `auto_ip` succeeds)

### IP location finder option

When `location_mode=auto_ip`, the module calls `https://ipapi.co/json/` on init, then stores:

- `lat`
- `lon`
- `timezone`
- `city`

This gives you a built-in “find by IP” flow similar to Aura's location helper, but running inside the Wasm module itself.

## Build

From this folder:

```powershell
./build.ps1
```

Output:

- `out/weather-v1.wasm`

## Publish to control plane static path

Copy output to `packages/wasm` (mapped in Unraid):

```powershell
Copy-Item .\out\weather-v1.wasm ..\..\packages\wasm\weather-v1.wasm -Force
```

Then register in admin with:

- `appType`: `weather`
- `version`: `1.0.0`
- `moduleUrl`: `/packages/wasm/weather-v1.wasm`
- `checksumSha256`: optional

## One-command OTA prep (recommended)

From `ESPControlPlane` folder, after `weather-v1.wasm` exists in `packages/wasm`:

```powershell
npm run prepare:weather-ota
```

Optional assignment modes:

```powershell
# assign one device
npm run prepare:weather-ota -- --assign one --device-id 3

# assign all devices
npm run prepare:weather-ota -- --assign all
```

What this does:

1. Validates `.wasm` magic header
2. Computes SHA-256
3. Registers/activates weather module in DB
4. Optionally sets device app type to `weather`

## Notes

- Fetch interval is 10 minutes (module-side cache).
- If fetch fails, screen shows weather HTTP code; when auto geolocation fails it also shows GEO HTTP code.
- JSON parsing is intentionally lightweight for Wasm size.
- Display format is: city + condition + temperature + RSSI/AUTO status.
