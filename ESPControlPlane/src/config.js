import fs from "node:fs";
import path from "node:path";
import dotenv from "dotenv";

dotenv.config();

const configDir =
  process.env.CONTROL_PLANE_CONFIG_DIR ||
  process.env.HOMELAB_CONFIG_DIR ||
  "/config";
const envFilePath =
  process.env.CONTROL_PLANE_ENV_FILE ||
  process.env.HOMELAB_ENV_FILE ||
  path.join(configDir, ".env");
const jsonFilePath =
  process.env.CONTROL_PLANE_CONFIG_FILE ||
  process.env.HOMELAB_CONFIG_FILE ||
  path.join(configDir, "config.json");

if (fs.existsSync(envFilePath)) {
  dotenv.config({ path: envFilePath, override: true });
}

let fileConfig = {};
if (fs.existsSync(jsonFilePath)) {
  try {
    fileConfig = JSON.parse(fs.readFileSync(jsonFilePath, "utf8"));
  } catch (error) {
    throw new Error(`Invalid JSON in config file ${jsonFilePath}: ${String(error.message || error)}`);
  }
}

function getFileValue(pathParts) {
  let node = fileConfig;
  for (const part of pathParts) {
    if (!node || typeof node !== "object" || !(part in node)) {
      return undefined;
    }
    node = node[part];
  }
  return node;
}

function getOptional(key, fallback = "", filePathParts = []) {
  const value = process.env[key];
  if (value && value.trim()) {
    return value.trim();
  }

  const fileValue = getFileValue(filePathParts);
  if (typeof fileValue === "string" && fileValue.trim()) {
    return fileValue.trim();
  }
  if (typeof fileValue === "number" || typeof fileValue === "boolean") {
    return String(fileValue);
  }

  return fallback;
}

function getNumber(key, fallback, filePathParts = []) {
  const value = process.env[key];
  if (value && value.trim()) {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : fallback;
  }

  const fileValue = getFileValue(filePathParts);
  const parsed = Number(fileValue);
  return Number.isFinite(parsed) ? parsed : fallback;
}

function getOptionalUrl(key, filePathParts = []) {
  return getOptional(key, "", filePathParts).replace(/\/$/, "");
}

function getOptionalSecret(key, filePathParts = []) {
  return getOptional(key, "", filePathParts);
}

function getKnownWifiNetworks() {
  const list = getFileValue(["wifi", "knownNetworks"]);
  if (!Array.isArray(list)) {
    return [];
  }

  return list
    .map((entry, index) => {
      if (!entry || typeof entry !== "object") {
        return null;
      }

      const ssid = typeof entry.ssid === "string" ? entry.ssid.trim() : "";
      if (!ssid) {
        return null;
      }

      const label =
        typeof entry.label === "string" && entry.label.trim()
          ? entry.label.trim()
          : ssid;
      const password = typeof entry.password === "string" ? entry.password : "";
      const enabled = typeof entry.enabled === "boolean" ? entry.enabled : true;
      const priorityRaw = Number(entry.priority);
      const priority = Number.isFinite(priorityRaw) ? priorityRaw : index + 1;

      return {
        id: typeof entry.id === "string" && entry.id.trim() ? entry.id.trim() : `wifi-${index + 1}`,
        ssid,
        label,
        password,
        enabled,
        priority
      };
    })
    .filter(Boolean)
    .sort((left, right) => left.priority - right.priority);
}

function loadPrivateKey() {
  const keyPath = getOptional(
    "UNRAID_PRIVATE_KEY_PATH",
    path.join(configDir, "unraid_key"),
    ["unraid", "privateKeyPath"]
  );
  if (!keyPath) {
    return "";
  }

  const resolved = path.isAbsolute(keyPath) ? keyPath : path.resolve(process.cwd(), keyPath);
  if (!fs.existsSync(resolved)) {
    return "";
  }
  return fs.readFileSync(resolved, "utf8");
}

const staticRoot = path.resolve(
  getOptional("CONTROL_PLANE_STATIC_ROOT", path.resolve(process.cwd(), ".."), ["static", "root"])
);

export const config = {
  server: {
    host: getOptional("CONTROL_PLANE_HOST", "0.0.0.0", ["server", "host"]),
    port: getNumber("CONTROL_PLANE_PORT", 8787, ["server", "port"]),
    token: getOptionalSecret("CONTROL_PLANE_TOKEN", ["server", "token"]),
    staticRoot
  },
  wifi: {
    knownNetworks: getKnownWifiNetworks()
  },
  jellyfin: {
    baseUrl: getOptionalUrl("JELLYFIN_BASE_URL", ["jellyfin", "baseUrl"]),
    apiKey: getOptionalSecret("JELLYFIN_API_KEY", ["jellyfin", "apiKey"])
  },
  jellyseerr: {
    baseUrl: getOptionalUrl("JELLYSEERR_BASE_URL", ["jellyseerr", "baseUrl"]),
    apiKey: getOptionalSecret("JELLYSEERR_API_KEY", ["jellyseerr", "apiKey"])
  },
  db: {
    host: getOptional("DB_HOST", "", ["db", "host"]),
    port: getNumber("DB_PORT", 3306, ["db", "port"]),
    user: getOptional("DB_USER", "", ["db", "user"]),
    password: getOptionalSecret("DB_PASSWORD", ["db", "password"]),
    name: getOptional("DB_NAME", "", ["db", "name"])
  },
  unraid: {
    host: getOptional("UNRAID_HOST", "", ["unraid", "host"]),
    port: getNumber("UNRAID_PORT", 22, ["unraid", "port"]),
    username: getOptional("UNRAID_USERNAME", "", ["unraid", "username"]),
    privateKey: loadPrivateKey(),
    privateKeyPassphrase: getOptionalSecret("UNRAID_PRIVATE_KEY_PASSPHRASE", ["unraid", "privateKeyPassphrase"]),
    containers: {
      jellyfin: getOptional("JELLYFIN_CONTAINER_NAME", "jellyfin", ["unraid", "containers", "jellyfin"]),
      jellyseerr: getOptional("JELLYSEERR_CONTAINER_NAME", "jellyseerr", ["unraid", "containers", "jellyseerr"])
    }
  }
};

export function hasUnraidConfig() {
  return Boolean(config.unraid.host && config.unraid.username && config.unraid.privateKey);
}

export function hasDbConfig() {
  return Boolean(config.db.host && config.db.user && config.db.password && config.db.name);
}
