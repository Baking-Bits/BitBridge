import http from "node:http";
import { config } from "./config.js";
import { getContainerStatus, restartContainer, startContainer } from "./docker.js";
import { fetchJellyfinStatus, fetchJellyseerrStatus } from "./services.js";

function json(res, status, payload) {
  const body = JSON.stringify(payload);
  res.writeHead(status, {
    "Content-Type": "application/json",
    "Content-Length": Buffer.byteLength(body)
  });
  res.end(body);
}

function readJsonBody(req) {
  return new Promise((resolve) => {
    let raw = "";
    req.on("data", (chunk) => {
      raw += chunk.toString();
      if (raw.length > 1024 * 64) {
        raw = raw.slice(0, 1024 * 64);
      }
    });
    req.on("end", () => {
      if (!raw.trim()) {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(raw));
      } catch {
        resolve({});
      }
    });
    req.on("error", () => resolve({}));
  });
}

function isAuthorized(req, body) {
  if (!config.bridge.token) return true;
  const headerToken = req.headers["x-homelab-key"];
  const bodyToken = body?.token;
  return headerToken === config.bridge.token || bodyToken === config.bridge.token;
}

function routeForService(service) {
  if (service === "jellyfin") {
    return {
      containerName: config.unraid.containers.jellyfin,
      fetchStatus: () => fetchJellyfinStatus(config.jellyfin)
    };
  }
  if (service === "jellyseerr") {
    return {
      containerName: config.unraid.containers.jellyseerr,
      fetchStatus: () => fetchJellyseerrStatus(config.jellyseerr)
    };
  }
  return null;
}

const server = http.createServer(async (req, res) => {
  const method = req.method || "GET";
  const url = new URL(req.url || "/", `http://${req.headers.host || "localhost"}`);
  const parts = url.pathname.split("/").filter(Boolean);

  if (method === "GET" && url.pathname === "/health") {
    json(res, 200, { ok: true, ts: new Date().toISOString() });
    return;
  }

  if (parts.length === 2 && parts[0] === "status" && method === "GET") {
    const service = parts[1];
    const mapped = routeForService(service);
    if (!mapped) {
      json(res, 404, { ok: false, error: "unknown service" });
      return;
    }

    try {
      const [app, container] = await Promise.all([
        mapped.fetchStatus(),
        getContainerStatus(config.unraid, mapped.containerName)
      ]);
      json(res, 200, { ok: true, service, app, container });
    } catch (error) {
      json(res, 500, { ok: false, error: String(error.message || error) });
    }
    return;
  }

  if (parts.length === 2 && (parts[0] === "restart" || parts[0] === "start") && method === "POST") {
    const action = parts[0];
    const service = parts[1];
    const mapped = routeForService(service);
    if (!mapped) {
      json(res, 404, { ok: false, error: "unknown service" });
      return;
    }

    const body = await readJsonBody(req);
    if (!isAuthorized(req, body)) {
      json(res, 401, { ok: false, error: "unauthorized" });
      return;
    }

    try {
      if (action === "restart") {
        await restartContainer(config.unraid, mapped.containerName);
      } else {
        await startContainer(config.unraid, mapped.containerName);
      }
      json(res, 200, { ok: true, action, service, container: mapped.containerName });
    } catch (error) {
      json(res, 500, { ok: false, error: String(error.message || error) });
    }
    return;
  }

  json(res, 404, { ok: false, error: "not found" });
});

server.listen(config.bridge.port, config.bridge.host, () => {
  console.log(`HomeLab bridge listening on http://${config.bridge.host}:${config.bridge.port}`);
  if (config.bridge.token) {
    console.log("Bridge auth enabled (x-homelab-key)");
  } else {
    console.log("Bridge auth disabled (set HOMELAB_BRIDGE_TOKEN to secure it)");
  }
});
