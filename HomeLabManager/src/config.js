import fs from "node:fs";
import path from "node:path";
import dotenv from "dotenv";

dotenv.config();

const configDir = process.env.HOMELAB_CONFIG_DIR || "/config";
const envFilePath = process.env.HOMELAB_ENV_FILE || path.join(configDir, ".env");
const jsonFilePath = process.env.HOMELAB_CONFIG_FILE || path.join(configDir, "config.json");

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

function getRequired(key, filePathParts = []) {
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

  if (!value || !value.trim()) {
    throw new Error(`Missing required env var: ${key}`);
  }
  return value.trim();
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

function loadPrivateKey() {
  const keyPath = getOptional("UNRAID_PRIVATE_KEY_PATH", path.join(configDir, "unraid_key"), ["unraid", "privateKeyPath"]);
  if (!keyPath) return "";
  const resolved = path.isAbsolute(keyPath) ? keyPath : path.resolve(process.cwd(), keyPath);
  if (!fs.existsSync(resolved)) {
    throw new Error(`UNRAID private key not found at: ${resolved}`);
  }
  return fs.readFileSync(resolved, "utf8");
}

export const config = {
  jellyfin: {
    baseUrl: getRequired("JELLYFIN_BASE_URL", ["jellyfin", "baseUrl"]).replace(/\/$/, ""),
    apiKey: getRequired("JELLYFIN_API_KEY", ["jellyfin", "apiKey"]),
    intervalMs: getNumber("JELLYFIN_MONITOR_INTERVAL_MS", 60_000, ["jellyfin", "intervalMs"])
  },

  jellyseerr: {
    baseUrl: getRequired("JELLYSEERR_BASE_URL", ["jellyseerr", "baseUrl"]).replace(/\/$/, ""),
    apiKey: getRequired("JELLYSEERR_API_KEY", ["jellyseerr", "apiKey"]),
    intervalMs: getNumber("JELLYSEERR_MONITOR_INTERVAL_MS", 60_000, ["jellyseerr", "intervalMs"])
  },

  unraid: {
    host: getRequired("UNRAID_HOST", ["unraid", "host"]),
    port: getNumber("UNRAID_PORT", 22, ["unraid", "port"]),
    username: getRequired("UNRAID_USERNAME", ["unraid", "username"]),
    privateKey: loadPrivateKey(),
    privateKeyPassphrase: getOptional("UNRAID_PRIVATE_KEY_PASSPHRASE", "", ["unraid", "privateKeyPassphrase"]),
    containers: {
      jellyfin: getRequired("JELLYFIN_CONTAINER_NAME", ["unraid", "containers", "jellyfin"]),
      jellyseerr: getRequired("JELLYSEERR_CONTAINER_NAME", ["unraid", "containers", "jellyseerr"])
    }
  },

  bridge: {
    enabled: getOptional("HOMELAB_BRIDGE_ENABLED", "true", ["bridge", "enabled"]).toLowerCase() !== "false",
    host: getOptional("HOMELAB_BRIDGE_HOST", "0.0.0.0", ["bridge", "host"]),
    port: getNumber("HOMELAB_BRIDGE_PORT", 8787, ["bridge", "port"]),
    token: getOptional("HOMELAB_BRIDGE_TOKEN", "", ["bridge", "token"])
  }
};
