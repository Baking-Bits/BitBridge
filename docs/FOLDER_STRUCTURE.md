# Folder Structure Guide

This repo uses a split between **source**, **installable outputs**, and **archives**.

## Keep these roles separate

- `Aura/`
  - Firmware source code and PlatformIO project files.
  - This folder is required if you want to rebuild or change Aura firmware.

- `packages/`
  - Files that the installer serves directly.
  - Keep package folders self-contained: `manifest.json` + required `.bin` files.
  - Treat this as deployment artifacts, not long-term archives.

- `backups/`
  - Historical backups captured from physical devices.
  - Safe place for timestamped snapshots and logs.

- `docs/`
  - Project documentation, setup guides, and internal references.

## Practical policy

1. Build in `Aura/`.
2. Copy only release outputs into `packages/<name>/`.
3. Store rolling or timestamped backups in `backups/`.
4. Put all human-readable notes and reference docs in `docs/`.

This keeps installer content clean while preserving full history in backups.
