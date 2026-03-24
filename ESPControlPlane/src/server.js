import fs from "node:fs";
import path from "node:path";
import express from "express";
import cookieParser from "cookie-parser";
import { config, hasUnraidConfig } from "./config.js";
import { getContainerStatus, restartContainer, startContainer } from "./unraid.js";
import { fetchJellyfinStatus, fetchJellyseerrStatus } from "./services.js";
import { signSession, verifySession, COOKIE_NAME } from "./auth.js";
import {
  authenticateUser,
  changeUserPassword,
  ensureDbReady,
  getUserById,
  listDeviceSelections,
  logDeviceSelection,
  listDevices,
  getDeviceById,
  registerDevice,
  updateDevice,
  deleteDevice,
  logDeviceCheckin,
  getDeviceCheckins
} from "./db.js";

const app = express();

app.use(express.json({ limit: "64kb" }));
app.use(cookieParser());

// CORS — allow requests from the respond2.me domain
app.use((req, res, next) => {
  const origin = req.get("Origin") || "";
  const allowedOrigins = [
    "https://respond2.me",
    "https://bitbridge.respond2.me",
    "http://respond2.me"
  ];
  if (allowedOrigins.includes(origin)) {
    res.setHeader("Access-Control-Allow-Origin", origin);
    res.setHeader("Access-Control-Allow-Credentials", "true");
    res.setHeader("Access-Control-Allow-Headers", "Content-Type, x-control-plane-key");
    res.setHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  }
  if (req.method === "OPTIONS") {
    res.sendStatus(204);
    return;
  }
  next();
});

function isSessionValid(req) {
  const secret = getSessionSecret();
  const sessionCookie = req.cookies?.[COOKIE_NAME];
  if (!sessionCookie) {
    return null;
  }

  const payload = verifySession(sessionCookie, secret);
  if (!payload || !payload.sub) {
    return null;
  }

  return {
    id: Number(payload.sub),
    username: payload.username || "",
    role: payload.role || "admin"
  };
}

function getSessionSecret() {
  return config.server.token || "bitbridge-dev-session-secret";
}

async function requireSession(req, res, next) {
  try {
    const sessionUser = isSessionValid(req);
    if (!sessionUser) {
      if (req.path.startsWith("/api/")) {
        return res.status(401).json({ ok: false, error: "unauthorized" });
      }
      const nextParam = encodeURIComponent(req.originalUrl);
      return res.redirect(`/login?next=${nextParam}`);
    }

    const dbUser = await getUserById(sessionUser.id);
    if (!dbUser) {
      res.clearCookie(COOKIE_NAME);
      if (req.path.startsWith("/api/")) {
        return res.status(401).json({ ok: false, error: "unauthorized" });
      }
      const nextParam = encodeURIComponent(req.originalUrl);
      return res.redirect(`/login?next=${nextParam}`);
    }

    req.sessionUser = dbUser;
    if (dbUser.mustChangePassword && req.path.startsWith("/api/admin/") && req.path !== "/api/auth/change-password") {
      return res.status(403).json({ ok: false, error: "password_change_required" });
    }

    if (dbUser.mustChangePassword && req.path === "/admin") {
      req.mustChangePassword = true;
    }

    if (dbUser.mustChangePassword && req.path === "/admin.html") {
      req.mustChangePassword = true;
    }

    return next();
  } catch (error) {
    return next(error);
  }
}

function isAuthorized(req) {
  if (req.sessionUser) {
    return true;
  }
  return Boolean(isSessionValid(req));
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

app.get("/login", (_req, res) => {
  const loginPath = path.join(config.server.staticRoot, "login.html");
  if (!fs.existsSync(loginPath)) {
    res.status(404).send("login.html not found");
    return;
  }
  res.sendFile(loginPath);
});

app.get("/admin", requireSession, (_req, res) => {
  const adminPath = path.join(config.server.staticRoot, "admin.html");
  if (!fs.existsSync(adminPath)) {
    res.status(404).send("admin.html not found");
    return;
  }
  res.sendFile(adminPath);
});

app.get("/admin.html", requireSession, (_req, res) => {
  const adminPath = path.join(config.server.staticRoot, "admin.html");
  if (!fs.existsSync(adminPath)) {
    res.status(404).send("admin.html not found");
    return;
  }
  res.sendFile(adminPath);
});

app.get("/admin.js", requireSession, (_req, res) => {
  const adminScriptPath = path.join(config.server.staticRoot, "admin.js");
  if (!fs.existsSync(adminScriptPath)) {
    res.status(404).send("admin.js not found");
    return;
  }
  res.sendFile(adminScriptPath);
});

app.post("/api/auth/login", async (req, res) => {
  try {
    const username = typeof req.body?.username === "string" ? req.body.username : "";
    const password = typeof req.body?.password === "string" ? req.body.password : "";
    const result = await authenticateUser(username, password);

    if (!result.ok || !result.user) {
      if (result.reason === "db_unavailable") {
        res.status(503).json({ ok: false, error: "Database unavailable." });
        return;
      }
      res.status(401).json({ ok: false, error: "Invalid username or password." });
      return;
    }

    const sessionJwt = signSession(getSessionSecret(), result.user);
    res.cookie(COOKIE_NAME, sessionJwt, {
      httpOnly: true,
      sameSite: "lax",
      secure: req.secure || req.get("x-forwarded-proto") === "https",
      maxAge: 8 * 60 * 60 * 1000
    });
    res.status(200).json({
      ok: true,
      user: {
        id: result.user.id,
        username: result.user.username,
        role: result.user.role,
        mustChangePassword: result.user.mustChangePassword
      }
    });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.get("/api/auth/me", requireSession, (req, res) => {
  const user = req.sessionUser;
  res.status(200).json({
    ok: true,
    user: {
      id: user.id,
      username: user.username,
      role: user.role,
      mustChangePassword: Boolean(user.mustChangePassword)
    }
  });
});

app.post("/api/auth/change-password", requireSession, async (req, res) => {
  try {
    const currentPassword = typeof req.body?.currentPassword === "string" ? req.body.currentPassword : "";
    const newPassword = typeof req.body?.newPassword === "string" ? req.body.newPassword : "";

    const result = await changeUserPassword(req.sessionUser.id, currentPassword, newPassword);
    if (!result.ok) {
      if (result.reason === "invalid_current_password") {
        res.status(400).json({ ok: false, error: "Current password is incorrect." });
        return;
      }
      if (result.reason === "password_too_short") {
        res.status(400).json({ ok: false, error: "New password must be at least 8 characters." });
        return;
      }
      res.status(400).json({ ok: false, error: "Unable to change password." });
      return;
    }

    const freshUser = await getUserById(req.sessionUser.id);
    if (!freshUser) {
      res.clearCookie(COOKIE_NAME);
      res.status(401).json({ ok: false, error: "Session expired. Please sign in again." });
      return;
    }

    const sessionJwt = signSession(getSessionSecret(), freshUser);
    res.cookie(COOKIE_NAME, sessionJwt, {
      httpOnly: true,
      sameSite: "lax",
      secure: req.secure || req.get("x-forwarded-proto") === "https",
      maxAge: 8 * 60 * 60 * 1000
    });

    res.status(200).json({ ok: true });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.get("/api/auth/logout", (_req, res) => {
  res.clearCookie(COOKIE_NAME);
  res.redirect("/login");
});

app.post("/api/public/device-selection", async (req, res) => {
  try {
    const deviceGroup = typeof req.body?.deviceGroup === "string" ? req.body.deviceGroup : "";
    const deviceLabel = typeof req.body?.deviceLabel === "string" ? req.body.deviceLabel : "";
    const packageId = typeof req.body?.packageId === "string" ? req.body.packageId : "";
    const firmwareId = typeof req.body?.firmwareId === "string" ? req.body.firmwareId : "";
    const firmwareLabel = typeof req.body?.firmwareLabel === "string" ? req.body.firmwareLabel : "";
    const manifestPath = typeof req.body?.manifestPath === "string" ? req.body.manifestPath : "";
    const ipAddress = (req.get("x-forwarded-for") || req.socket.remoteAddress || "").split(",")[0].trim();
    const userAgent = req.get("user-agent") || "";

    if (!deviceGroup || !packageId || !firmwareId) {
      res.status(400).json({ ok: false, error: "missing required device/firmware fields" });
      return;
    }

    const saveResult = await logDeviceSelection({
      deviceGroup,
      deviceLabel,
      packageId,
      firmwareId,
      firmwareLabel,
      manifestPath,
      ipAddress,
      userAgent
    });

    res.status(200).json({ ok: true, saved: saveResult.saved });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.post("/api/public/devices/register", async (req, res) => {
  try {
    const deviceGroup = typeof req.body?.deviceGroup === "string" ? req.body.deviceGroup : "";
    const deviceLabel = typeof req.body?.deviceLabel === "string" ? req.body.deviceLabel : "";
    const serialNumber = typeof req.body?.serialNumber === "string" ? req.body.serialNumber : "";
    const initialFirmwareVersion = typeof req.body?.initialFirmwareVersion === "string" ? req.body.initialFirmwareVersion : "";

    if (!deviceGroup || !serialNumber) {
      res.status(400).json({ ok: false, error: "deviceGroup and serialNumber are required" });
      return;
    }

    const result = await registerDevice(deviceGroup, deviceLabel, serialNumber, initialFirmwareVersion);
    if (!result.ok) {
      if (result.reason === "device_already_registered") {
        res.status(409).json({ ok: false, error: "Device with this serial number already exists." });
        return;
      }
      res.status(400).json({ ok: false, error: "Failed to register device." });
      return;
    }

    const device = await getDeviceById(result.deviceId);
    res.status(201).json({ ok: true, device });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.post("/api/public/devices/:id/checkin", async (req, res) => {
  try {
    const deviceId = req.params.id;
    const firmwareVersion = typeof req.body?.firmwareVersion === "string" ? req.body.firmwareVersion : "";
    const status = typeof req.body?.status === "string" ? req.body.status : "ok";
    const data = typeof req.body?.data === "object" ? req.body.data : null;

    if (!firmwareVersion) {
      res.status(400).json({ ok: false, error: "firmwareVersion is required" });
      return;
    }

    const result = await logDeviceCheckin(deviceId, firmwareVersion, status, data);
    if (!result.ok) {
      if (result.reason === "device_not_found") {
        res.status(404).json({ ok: false, error: "Device not found." });
        return;
      }
      res.status(400).json({ ok: false, error: "Failed to log check-in." });
      return;
    }

    res.status(200).json({ ok: true });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.get("/api/admin/device-selections", requireSession, async (req, res) => {
  try {
    const limitRaw = Number.parseInt(String(req.query.limit || "100"), 10);
    const limit = Number.isFinite(limitRaw) ? limitRaw : 100;
    const selections = await listDeviceSelections(limit);
    res.status(200).json({ ok: true, selections });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.get("/api/admin/devices", requireSession, async (req, res) => {
  try {
    const devices = await listDevices();
    res.status(200).json({ ok: true, devices });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.get("/api/admin/devices/:id", requireSession, async (req, res) => {
  try {
    const deviceId = req.params.id;
    const device = await getDeviceById(deviceId);
    if (!device) {
      res.status(404).json({ ok: false, error: "device not found" });
      return;
    }
    res.status(200).json({ ok: true, device });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.post("/api/admin/devices", requireSession, async (req, res) => {
  try {
    const deviceGroup = typeof req.body?.deviceGroup === "string" ? req.body.deviceGroup : "";
    const deviceLabel = typeof req.body?.deviceLabel === "string" ? req.body.deviceLabel : "";
    const serialNumber = typeof req.body?.serialNumber === "string" ? req.body.serialNumber : "";
    const initialFirmwareVersion = typeof req.body?.initialFirmwareVersion === "string" ? req.body.initialFirmwareVersion : "";

    if (!deviceGroup || !serialNumber) {
      res.status(400).json({ ok: false, error: "deviceGroup and serialNumber are required" });
      return;
    }

    const result = await registerDevice(deviceGroup, deviceLabel, serialNumber, initialFirmwareVersion);
    if (!result.ok) {
      if (result.reason === "device_already_registered") {
        res.status(409).json({ ok: false, error: "Device with this serial number already exists." });
        return;
      }
      res.status(400).json({ ok: false, error: "Failed to register device." });
      return;
    }

    const device = await getDeviceById(result.deviceId);
    res.status(201).json({ ok: true, device });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.patch("/api/admin/devices/:id", requireSession, async (req, res) => {
  try {
    const deviceId = req.params.id;
    const updates = {};

    if (typeof req.body?.deviceLabel === "string") {
      updates.deviceLabel = req.body.deviceLabel;
    }
    if (typeof req.body?.firmwareVersion === "string") {
      updates.firmwareVersion = req.body.firmwareVersion;
    }
    if (typeof req.body?.lastCheckin === "string" || req.body?.lastCheckin instanceof Date) {
      updates.lastCheckin = req.body.lastCheckin;
    }

    const result = await updateDevice(deviceId, updates);
    if (!result.ok) {
      if (result.reason === "no_updates") {
        res.status(400).json({ ok: false, error: "No valid updates provided." });
        return;
      }
      res.status(400).json({ ok: false, error: "Failed to update device." });
      return;
    }

    const device = await getDeviceById(deviceId);
    if (!device) {
      res.status(404).json({ ok: false, error: "device not found after update" });
      return;
    }

    res.status(200).json({ ok: true, device });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.delete("/api/admin/devices/:id", requireSession, async (req, res) => {
  try {
    const deviceId = req.params.id;
    const result = await deleteDevice(deviceId);
    if (!result.ok) {
      res.status(404).json({ ok: false, error: "device not found" });
      return;
    }
    res.status(200).json({ ok: true });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.get("/api/admin/devices/:id/checkins", requireSession, async (req, res) => {
  try {
    const deviceId = req.params.id;
    const limitRaw = Number.parseInt(String(req.query.limit || "100"), 10);
    const limit = Number.isFinite(limitRaw) ? limitRaw : 100;

    const checkins = await getDeviceCheckins(deviceId, limit);
    res.status(200).json({ ok: true, checkins });
  } catch (error) {
    res.status(500).json({ ok: false, error: String(error.message || error) });
  }
});

app.get("/api/status/:service", requireSession, async (req, res) => {
  const service = req.params.service;
  const status = await buildServiceStatus(service);

  if (!status) {
    res.status(404).json({ ok: false, error: "unknown service" });
    return;
  }

  res.status(200).json({ ok: true, ...status });
});

app.get("/api/status", requireSession, async (_req, res) => {
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

app.get("/api/wifi/known", requireSession, (req, res) => {
  if (!isAuthorized(req)) {
    res.status(401).json({ ok: false, error: "unauthorized" });
    return;
  }

  const networks = (config.wifi?.knownNetworks || []).filter((entry) => entry.enabled !== false);
  res.status(200).json({
    ok: true,
    networks
  });
});

app.post("/api/:action/:service", requireSession, async (req, res) => {
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
ensureDbReady().catch((error) => {
  console.warn(`DB startup initialization failed: ${String(error.message || error)}`);
});

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
  console.log("Admin auth enabled (DB accounts + session cookie)");
  if (!config.server.token) {
    console.warn("Startup warning: server.token is empty; using fallback session secret. Set server.token for secure signed sessions.");
  }
});
