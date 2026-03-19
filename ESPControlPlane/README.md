# ESP Control Plane

Top-level control-plane service for this repo. It combines:

- Static hosting for the ESP installer UI (`index.html`, `app.js`, `styles.css`)
- Firmware asset hosting (`/packages`, `/bin`)
- HomeLab status APIs (Jellyfin/Jellyseerr)
- Unraid Docker actions over SSH (`start`/`restart`)

## API

- `GET /health`
- `GET /api/status`
- `GET /api/status/jellyfin`
- `GET /api/status/jellyseerr`
- `POST /api/start/:service`
- `POST /api/restart/:service`

Action routes can be secured with `CONTROL_PLANE_TOKEN` via request header `x-control-plane-key` or JSON body field `token`.

## Local run

```bash
cd ESPControlPlane
npm install
npm run start
```

Server defaults to `http://0.0.0.0:8787` and serves installer assets from repo root.

## Docker run

Build from repository root:

```bash
docker build -f ESPControlPlane/Dockerfile -t esp-control-plane .
```

Run container:

```bash
docker run -d \
  --name esp-control-plane \
  -p 8787:8787 \
  -v /path/to/control-plane-config:/config \
  -v /path/to/packages:/app/packages \
  -v /path/to/bin:/app/bin \
  esp-control-plane
```

## Docker Compose

From repository root:

```bash
docker compose up -d --build
```

Compose mounts:

- `./config -> /config`
- `./packages -> /app/packages`
- `./bin -> /app/bin`

Check logs and health:

```bash
docker compose logs -f esp-control-plane
docker compose ps
```

Config files read in this order:

1. Process env vars
2. `/config/.env`
3. `/config/config.json`

Use `config.example.json` and `.env.example` as templates.
