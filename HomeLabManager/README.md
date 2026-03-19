# HomeLab Manager

Home-lab management tooling for CYD AIO control only (no Discord integration).

Features:
- Jellyfin status (online, sessions, active streams)
- Jellyseerr status (movies/tv/all request counts)
- Start/Restart actions for Docker containers on Unraid over SSH private key auth

## Setup

1. Create env file:
   - Copy `.env.example` to `.env`
2. Fill required values:
   - Jellyfin/Jellyseerr URLs + API keys
   - Unraid SSH host/user and private key path
3. Place your private key file where `UNRAID_PRIVATE_KEY_PATH` points.
4. Install dependencies:
   - `npm install`
5. Start bridge mode (for CYD):
   - `npm run bridge`

## Valid runtime config location (Docker)

The container is configured to load config from:

- `/config/.env`
- `/config/config.json`

You can override paths with:

- `HOMELAB_CONFIG_DIR`
- `HOMELAB_ENV_FILE`
- `HOMELAB_CONFIG_FILE`

Recommended: mount your Unraid appdata folder to `/config` and keep all runtime config there.

## Notes

- Bridge endpoints:
   - `GET /health`
   - `GET /status/jellyfin`
   - `GET /status/jellyseerr`
   - `POST /restart/jellyfin`
   - `POST /restart/jellyseerr`
   - `POST /start/jellyfin`
   - `POST /start/jellyseerr`
- Restart/Start uses SSH key auth to run Docker commands on Unraid.
- Recommended: give the SSH key only the minimum needed permissions.

## CYD integration

Set your CYD firmware restart URLs to your bridge host:

```txt
http://<bridge-host>:8787/restart/jellyfin
http://<bridge-host>:8787/restart/jellyseerr
```

If `HOMELAB_BRIDGE_TOKEN` is set, include it in requests as header `x-homelab-key` (or JSON body field `token`).

## Docker

Build locally:

```bash
docker build -t homelab-manager:latest .
```

Run locally:

```bash
docker run -d \
   --name homelab-manager \
   --env-file .env \
   -v /mnt/user/appdata/homelab-manager:/config \
   -p 8787:8787 \
   --restart unless-stopped \
   homelab-manager:latest
```

## GitHub Actions image builds

Workflow: `.github/workflows/homelab-manager-docker-image.yml`

On push to `main` (when files under `HomeLabManager/` change), it builds and pushes:
- `${IMAGE_NAME}:latest`
- `${IMAGE_NAME}:<commit-sha>`

Required repository secrets (same pattern as your other projects):
- `DOCKER_REGISTRY` (example: `docker.io` or `ghcr.io`)
- `DOCKER_USERNAME`
- `DOCKER_PASSWORD`
- `IMAGE_NAME` (example: `bakingbits/homelab-manager` or `ghcr.io/Baking-Bits/homelab-manager`)

## Unraid deploy quick steps

1. Push to `main` and wait for workflow success.
2. Pull image on Unraid:

```bash
docker pull <IMAGE_NAME>:latest
```

3. Run/update container with your `.env` variables and optional key mount for SSH:

```bash
docker run -d \
   --name homelab-manager \
   --env-file /path/to/homelab-manager.env \
   -v /mnt/user/appdata/homelab-manager:/config \
   -p 8787:8787 \
   --restart unless-stopped \
   <IMAGE_NAME>:latest

Place your SSH key at `/mnt/user/appdata/homelab-manager/unraid_key` and set:

`UNRAID_PRIVATE_KEY_PATH=/config/unraid_key`
```

## Git safety

- Keep `.env` local only.
- Keep `.ssh/unraid_key` local only.
- Root `.gitignore` already excludes both.

## Example values from your reference

```env
JELLYFIN_MONITOR_INTERVAL_MS=60000
JELLYSEERR_MONITOR_INTERVAL_MS=60000
UNRAID_HOST=192.168.1.206
UNRAID_PORT=22
UNRAID_USERNAME=root
JELLYFIN_CONTAINER_NAME=M-Jellyfin
JELLYSEERR_CONTAINER_NAME=M-jellyseerr
```
