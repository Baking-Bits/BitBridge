import fs from "node:fs";
import path from "node:path";
import express from "express";
import { config, hasUnraidConfig } from "./config.js";
import { getContainerStatus, restartContainer, startContainer } from "./unraid.js";
import { fetchJellyfinStatus, fetchJellyseerrStatus } from "./services.js";

const app = express();

app.use(express.json({ limit: "64kb" }));

function isAuthorized(req) {
  if (!config.server.token) {
    return true;
  }

  const headerToken = req.get("x-control-plane-key");
  const bodyToken = typeof req.body?.token === "string" ? req.body.token : "";
  return headerToken === config.server.token || bodyToken === config.server.token;
}

function serviceMap(service) {
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

async function buildServiceStatus(service) {
  const mapped = serviceMap(service);
  if (!mapped) {
    return null;
  }

  const appStatus = await mapped.fetchStatus();
  const containerStatus = hasUnraidConfig()
    ? await getContainerStatus(config.unraid, mapped.containerName)
    : { exists: false, statusText: "unconfigured", online: false };

  return {
    service,
    app: appStatus,
    container: containerStatus
  };
}

app.get("/health", (_req, res) => {
  res.status(200).json({
    ok: true,
    ts: new Date().toISOString(),
    staticRoot: config.server.staticRoot,
    unraidConfigured: hasUnraidConfig()
  });
});

app.get("/api/status/:service", async (req, res) => {
  const service = req.params.service;
  const status = await buildServiceStatus(service);

  if (!status) {
    res.status(404).json({ ok: false, error: "unknown service" });
    return;
  }

  res.status(200).json({ ok: true, ...status });
});

app.get("/api/status", async (_req, res) => {
  try {
    const [jellyfin, jellyseerr] = await Promise.all([
      buildServiceStatus("jellyfin"),
      buildServiceStatus("jellyseerr")
    ]);

    res.status(200).json({
      ok: true,
      services: {
        jellyfin,
        jellyseerr
      }
    });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.post("/api/:action/:service", async (req, res) => {
  const action = req.params.action;
  const service = req.params.service;

  if (action !== "restart" && action !== "start") {
    res.status(404).json({ ok: false, error: "unknown action" });
    return;
  }

  const mapped = serviceMap(service);
  if (!mapped) {
    res.status(404).json({ ok: false, error: "unknown service" });
    return;
  }

  if (!isAuthorized(req)) {
    res.status(401).json({ ok: false, error: "unauthorized" });
    return;
  }

  if (!hasUnraidConfig()) {
    res.status(503).json({ ok: false, error: "unraid configuration missing" });
    return;
  }

  try {
    if (action === "restart") {
      await restartContainer(config.unraid, mapped.containerName);
    } else {
      await startContainer(config.unraid, mapped.containerName);
    }

    res.status(200).json({ ok: true, action, service, container: mapped.containerName });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

function checkStartupPaths() {
  const staticRoot = config.server.staticRoot;
  const indexPath = path.join(staticRoot, "index.html");
  const packagesPath = path.join(staticRoot, "packages");
  const binPath = path.join(staticRoot, "bin");

  if (!fs.existsSync(staticRoot)) {
    throw new Error(`Static root not found: ${staticRoot}`);
  }

  if (!fs.existsSync(indexPath)) {
    throw new Error(`Required file missing: ${indexPath}`);
  }

  if (!fs.existsSync(packagesPath)) {
    console.warn(`Startup warning: packages directory missing: ${packagesPath}`);
    console.warn("Requests under /packages will return 404 until this path exists.");
  }

  if (!fs.existsSync(binPath)) {
    console.warn(`Startup warning: bin directory missing: ${binPath}`);
    console.warn("Requests under /bin will return 404 until this path exists.");
  }

  console.log(`Startup check OK: static root ${staticRoot}`);
}

checkStartupPaths();

app.use("/packages", express.static(path.join(config.server.staticRoot, "packages")));
app.use("/bin", express.static(path.join(config.server.staticRoot, "bin")));
app.use(express.static(config.server.staticRoot, { index: false }));

app.get("*", (_req, res) => {
  const indexPath = path.join(config.server.staticRoot, "index.html");
  if (!fs.existsSync(indexPath)) {
    res.status(404).send("index.html not found in static root");
    return;
  }
  res.sendFile(indexPath);
});

app.listen(config.server.port, config.server.host, () => {
  console.log(`ESP Control Plane listening on http://${config.server.host}:${config.server.port}`);
  console.log(`Server bind: host=${config.server.host} port=${config.server.port}`);
  console.log(`Static root: ${config.server.staticRoot}`);
  if (config.server.token) {
    console.log("Action auth enabled (x-control-plane-key)");
  } else {
    console.log("Action auth disabled (set CONTROL_PLANE_TOKEN to secure action routes)");
  }
});
