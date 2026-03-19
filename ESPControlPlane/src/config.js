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
  jellyfin: {
    baseUrl: getOptionalUrl("JELLYFIN_BASE_URL", ["jellyfin", "baseUrl"]),
    apiKey: getOptionalSecret("JELLYFIN_API_KEY", ["jellyfin", "apiKey"])
  },
  jellyseerr: {
    baseUrl: getOptionalUrl("JELLYSEERR_BASE_URL", ["jellyseerr", "baseUrl"]),
    apiKey: getOptionalSecret("JELLYSEERR_API_KEY", ["jellyseerr", "apiKey"])
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
