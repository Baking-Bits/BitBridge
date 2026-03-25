# BitBridge ESP32 Base Firmware

Base firmware for ESP32-S3 devices in the BitBridge ecosystem. Handles WiFi provisioning, device registration, periodic check-ins, and OTA update detection.

## Features

✅ **WiFi Management**
- Automatic WiFi provisioning on first boot via captive portal
- WiFiManager library for easy setup
- Long-press BOOT button (5+ seconds) to reset config

✅ **Device Registration**
- Auto-registration on first boot
- Generates device ID from MAC address if needed
- Stores device ID in SPIFFS for persistence

✅ **Periodic Check-ins**
- Sends check-in every 60 seconds to control plane
- Includes system data: WiFi signal strength, uptime, free heap
- Supports custom data payload for extensions

✅ **Status Indication**
- LED blink patterns for status feedback
- 1 blink: success
- 2 blinks: WiFi connected
- 3 blinks: error
- 4 blinks: registration failed
- 5 blinks: fatal error

✅ **OTA Detection**
- Checks for available firmware updates
- Calls `/api/devices/:id/ota-available` endpoint

✅ **Modular Architecture**
- `DeviceConfig` - SPIFFS storage and persistence
- `WiFiMgr` - WiFi provisioning and connection management
- `APIClient` - HTTP communication with control plane
- Easy to extend with weather, homelab management, etc.

## Hardware Requirements

- **Board**: ESP32-S3-DevKitC-1 or compatible
- **Flash Size**: 4MB minimum (16MB recommended for OTA)
- **PSRAM**: Optional but recommended for larger data structures

## Building & Flashing

### Prerequisites

1. Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) or use VS Code extension
2. Ensure USB-to-Serial driver is installed for your board

### Build

```bash
cd esp32-base-firmware
platformio run -e esp32-s3-devkitc-1
```

### Flash to Device

```bash
platformio run -e esp32-s3-devkitc-1 --target upload
```

### Monitor Serial Output

```bash
platformio device monitor -e esp32-s3-devkitc-1
```

## First Boot Flow

1. **Serial Output**: Device boots and shows `BitBridge-ESP32-Base v1.0.0`
2. **LED**: Blinks twice (WiFi init started)
3. **WiFi**: Enters provisioning mode (if no WiFi config saved)
   - Look for SSID: `BitBridge-Setup-XXXXXX`
   - Connect and visit: `http://192.168.4.1`
   - Select WiFi network and enter credentials
4. **WiFi Connected**: LED blinks once
5. **Device Registration**: First contact with control plane at `http://bitbridge.local:3000` (or configured URL)
   - Registers device with serial number
   - Saves device ID to SPIFFS
6. **Check-in**: Sends initial check-in with boot status
7. **Ready**: Device enters normal loop, sending check-ins every 60 seconds

## Configuration Storage

Stored in SPIFFS at `/spiffs/device_config.json`:

```json
{
  "deviceId": "123",
  "serialNumber": "a1b2c3d4e5f6",
  "deviceLabel": "My ESP32",
  "wifiSSID": "MyNetwork",
  "wifiPassword": "password123",
  "controlPlaneUrl": "http://bitbridge.local:3000"
}
```

## Reset Device Configuration

**Option 1**: Long press BOOT button (GPIO 0) for 5+ seconds during runtime
**Option 2**: Connect via serial and send command to clear SPIFFS

## API Integration

### Device Registration (First Boot)

```
POST /api/public/devices/register
{
  "deviceGroup": "esp32-s3",
  "deviceLabel": "My Device Label",
  "serialNumber": "a1b2c3d4e5f6",
  "initialFirmwareVersion": "1.0.0"
}

Response (HTTP 201):
{
  "ok": true,
  "device": {
    "id": 123,
    "serialNumber": "a1b2c3d4e5f6",
    "deviceGroup": "esp32-s3",
    "deviceLabel": "My Device Label",
    "firmwareVersion": "1.0.0",
    "createdAt": "2026-03-24T10:00:00Z"
  }
}
```

### Device Check-in (Periodic)

```
POST /api/public/devices/{deviceId}/checkin
{
  "firmwareVersion": "1.0.0",
  "status": "ok",
  "data": {
    "rssi": -65,
    "ip": "192.168.1.100",
    "uptime": 3600,
    "heap_free": 234567,
    "heap_min": 123456,
    "signal_strength": -65,
    "uptime_seconds": 3600
  }
}

Response (HTTP 200):
{
  "ok": true
}
```

### OTA Update Check (Periodic)

```
GET /api/devices/{deviceId}/ota-available

Response (HTTP 200):
{
  "ok": true,
  "updateAvailable": true,
  "newVersion": "1.1.0",
  "updateUrl": "http://bitbridge.local:3000/api/ota/1.1.0/esp32-s3.bin"
}
```

## Extending the Firmware

### Adding Weather Module

Create `src/weather.cpp` and `include/weather.h`:

```cpp
// include/weather.h
class WeatherModule {
public:
  bool begin();
  String getWeatherData();
};
```

Then in `src/main.cpp`:

```cpp
#include "weather.h"

WeatherModule weather;

void setup() {
  // ... existing code ...
  weather.begin();
}

void loop() {
  // ... existing checkin code ...
  
  StaticJsonDocument<256> customData;
  customData["weather"] = weather.getWeatherData();
  apiClient.sendCheckin("ok", &customData);
}
```

### Adding Homelab Management

Similarly create `src/homelab.cpp` and `include/homelab.h` for:
- Docker container status polling
- Service health checks
- Performance metrics
- System resource monitoring

Each module should:
1. Have its own initialization
2. Return data as JSON
3. Integrate into custom check-in payload

## Memory Management

- **SPIFFS**: 1.9MB available for config and future storage
- **PSRAM**: ~4MB available (if board has it)
- **Heap**: Dynamic allocation as needed
- **OTA**: 2MB partition per OTA slot (app0, app1)

Check free memory in logs: `heap_free: XXXXX bytes`

## Troubleshooting

**WiFi won't connect**
- Verify WiFi SSID and password in provisioning portal
- Long-press BOOT to reset and try again

**Device registration fails**
- Check control plane URL is correct
- Verify network connectivity to control plane
- Check device has valid internet access

**Check-ins failing**
- Verify WiFi connection is stable
- Check control plane is running
- Look for HTTP errors in serial output

**Serial monitor shows garbled text**
- Verify baud rate is 115200
- Check USB cable is properly connected
- Try different USB port

## Next Steps

1. ✅ Base firmware with WiFi and device registration
2. ⏳ OTA update implementation
3. ⏳ Weather API integration (OpenWeatherMap or similar)
4. ⏳ Homelab monitoring (Docker, Proxmox, etc.)
5. ⏳ Advanced configuration UI (web dashboard)
6. ⏳ Multiple user account support on devices
7. ⏳ Local data caching and sync

## License

Part of the BitBridge ecosystem. See main project for license details.
