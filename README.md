# ESP32 Local All-in-One Installer

This project runs a local webpage + local firmware files so you can install ESP32 firmware without relying on remote manifest URLs.

Aura is already included as the first local package in `packages/aura`.

## Run locally

1. Open PowerShell in this folder.
2. Run:

```powershell
.\Start-LocalInstaller.ps1
```

3. Browser opens at `http://localhost:8080`.
4. Select the Aura package and click install.

## IP location helper

The page includes a Location helper card.

1. Click Find location by IP.
2. It returns city, region, country, timezone, and lat/lon.
3. Click Copy lat,lon and paste into Aura configuration.

Notes:

- IP geolocation is approximate.
- If one provider fails, the page tries a backup provider automatically.

## Package structure

Each firmware package lives under `packages/<package-name>/` and is listed in `packages/catalog.json`.

Included now:

- `packages/aura/manifest.json`
- `packages/aura/manifest-inverted.json`
- `packages/aura/aura-firmware.bin`
- `packages/aura/aura-firmware-inverted.bin`

## Add another ESP32 package

1. Create a folder, for example `packages/my-board/`.
2. Put your firmware `.bin` file(s) there.
3. Add one or more manifest JSON files in that folder.
4. Add an entry to `packages/catalog.json`.

Example catalog entry:

```json
{
  "id": "my-board",
  "name": "My ESP32 Board",
  "boardHint": "ESP32-S3 with 1.9in display",
  "docsUrl": "",
  "packagePath": "./packages/my-board/",
  "variants": [
    {
      "id": "stable",
      "label": "Install My Firmware",
      "notes": "Main release",
      "manifest": "./packages/my-board/manifest.json"
    }
  ]
}
```

## Notes

- Use Chrome or Edge for Web Serial support.
- Keep manifest `parts` offsets exactly as exported by your build system.
