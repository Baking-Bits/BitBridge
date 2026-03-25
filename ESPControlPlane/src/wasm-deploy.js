import fs from "node:fs";
import path from "node:path";
import crypto from "node:crypto";
import {
  createAppModule,
  getActiveAppModule,
  listDevices,
  setDeviceAppType
} from "./db.js";

function sha256Hex(buffer) {
  return crypto.createHash("sha256").update(buffer).digest("hex");
}

function hasWasmMagic(buffer) {
  if (!buffer || buffer.length < 4) return false;
  return buffer[0] === 0x00 && buffer[1] === 0x61 && buffer[2] === 0x73 && buffer[3] === 0x6d;
}

function safeFileName(input) {
  const name = String(input || "").trim();
  if (!name) return "";
  if (!/^[a-zA-Z0-9._-]+\.wasm$/.test(name)) return "";
  return name;
}

function safeAppType(input) {
  return String(input || "").trim().toLowerCase().slice(0, 32);
}

function inferMetaFromFileName(fileName) {
  const base = String(fileName || "").replace(/\.wasm$/i, "");
  const dash = base.indexOf("-");

  if (dash > 0) {
    const left = safeAppType(base.slice(0, dash));
    const right = String(base.slice(dash + 1)).trim();
    if (left && right) {
      const version = right.startsWith("v") ? right.slice(1) : right;
      return { appType: left, version: version.slice(0, 64) || "local" };
    }
  }

  const appType = safeAppType(base);
  if (!appType) return null;
  return { appType, version: "local" };
}

export function listLocalWasmFiles(staticRoot) {
  const wasmDir = path.join(staticRoot, "packages", "wasm");
  if (!fs.existsSync(wasmDir)) return [];

  const entries = fs.readdirSync(wasmDir, { withFileTypes: true });
  const files = entries
    .filter((entry) => entry.isFile())
    .map((entry) => entry.name)
    .filter((name) => name.toLowerCase().endsWith(".wasm"))
    .map((name) => {
      const fullPath = path.join(wasmDir, name);
      const st = fs.statSync(fullPath);
      return {
        fileName: name,
        sizeBytes: st.size,
        updatedAt: st.mtime.toISOString(),
        moduleUrl: `/packages/wasm/${name}`
      };
    })
    .sort((a, b) => (a.fileName < b.fileName ? -1 : 1));

  return files;
}

export async function autoRegisterLocalWasmModules(staticRoot, filterAppType = null) {
  const files = listLocalWasmFiles(staticRoot);
  const targetAppType = filterAppType ? safeAppType(filterAppType) : null;
  const registered = [];
  const skipped = [];

  for (const file of files) {
    const meta = inferMetaFromFileName(file.fileName);
    if (!meta || !meta.appType) {
      skipped.push({ fileName: file.fileName, reason: "cannot_infer_app_type" });
      continue;
    }
    if (targetAppType && meta.appType !== targetAppType) {
      continue;
    }

    const fullPath = path.join(staticRoot, "packages", "wasm", file.fileName);
    const bytes = fs.readFileSync(fullPath);
    if (!hasWasmMagic(bytes)) {
      skipped.push({ fileName: file.fileName, reason: "invalid_wasm_magic" });
      continue;
    }

    const checksumSha256 = sha256Hex(bytes);
    const moduleUrl = `/packages/wasm/${file.fileName}`;
    const active = await getActiveAppModule(meta.appType);
    if (
      active
      && active.moduleUrl === moduleUrl
      && active.version === meta.version
      && String(active.checksumSha256 || "").toLowerCase() === checksumSha256
    ) {
      skipped.push({ fileName: file.fileName, reason: "already_active" });
      continue;
    }

    const create = await createAppModule({
      appType: meta.appType,
      version: meta.version,
      moduleUrl,
      checksumSha256,
      minHostAbi: 1,
      notes: "Auto-registered from packages/wasm"
    });

    if (create?.ok) {
      registered.push({ fileName: file.fileName, appType: meta.appType, version: meta.version, moduleId: create.moduleId });
    } else {
      skipped.push({ fileName: file.fileName, reason: create?.reason || "create_failed" });
    }
  }

  return {
    registeredCount: registered.length,
    skippedCount: skipped.length,
    registered,
    skipped
  };
}

async function assignDevices(appType, mode, deviceId) {
  if (mode === "none") return { assigned: 0 };

  if (mode === "one") {
    if (!Number.isFinite(deviceId) || deviceId <= 0) {
      throw new Error("assign=one requires a valid deviceId");
    }
    const result = await setDeviceAppType(deviceId, appType);
    if (!result?.ok) {
      throw new Error(`failed to assign device ${deviceId}: ${result?.reason || "unknown"}`);
    }
    return { assigned: 1 };
  }

  if (mode === "all") {
    const devices = await listDevices();
    let assigned = 0;
    for (const device of devices) {
      const result = await setDeviceAppType(device.id, appType);
      if (result?.ok) assigned += 1;
    }
    return { assigned };
  }

  throw new Error("assign must be one of: none, one, all");
}

export async function deployLocalWasmModule({
  staticRoot,
  appType,
  fileName,
  version,
  minHostAbi = 1,
  assign = "none",
  deviceId = null,
  notes = ""
}) {
  const safeType = safeAppType(appType);
  const safeFile = safeFileName(fileName);
  const safeVersion = String(version || "").trim().slice(0, 64);

  if (!safeType) throw new Error("appType is required");
  if (!safeFile) throw new Error("fileName must be a .wasm file name (no path)");
  if (!safeVersion) throw new Error("version is required");

  const wasmDir = path.join(staticRoot, "packages", "wasm");
  const fullPath = path.join(wasmDir, safeFile);
  if (!fs.existsSync(fullPath)) {
    throw new Error(`module file not found: ${fullPath}`);
  }

  const bytes = fs.readFileSync(fullPath);
  if (!hasWasmMagic(bytes)) {
    throw new Error(`file is not a valid wasm binary (bad magic): ${safeFile}`);
  }

  const checksumSha256 = sha256Hex(bytes);
  const moduleUrl = `/packages/wasm/${safeFile}`;

  const createResult = await createAppModule({
    appType: safeType,
    version: safeVersion,
    moduleUrl,
    checksumSha256,
    minHostAbi: Number(minHostAbi) || 1,
    notes: String(notes || "").slice(0, 500)
  });

  if (!createResult?.ok) {
    throw new Error(`createAppModule failed: ${createResult?.reason || "unknown"}`);
  }

  const assignment = await assignDevices(safeType, String(assign || "none"), Number(deviceId));

  return {
    ok: true,
    appType: safeType,
    version: safeVersion,
    fileName: safeFile,
    moduleUrl,
    moduleBytes: bytes.length,
    checksumSha256,
    moduleId: createResult.moduleId,
    assignment: {
      mode: String(assign || "none"),
      assignedDevices: assignment.assigned
    },
    next: "Assigned devices will download on next check-in (~60s)."
  };
}
