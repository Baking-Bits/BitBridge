import mysql from "mysql2/promise";
import bcrypt from "bcryptjs";
import { config, hasDbConfig } from "./config.js";

let pool;
let initialized = false;
let available = false;

const DEFAULT_ADMIN_USERNAME = "admin";
const DEFAULT_ADMIN_PASSWORD = "pass1234";
const PASSWORD_MIN_LENGTH = 8;

function toSafeString(value, maxLength) {
  if (typeof value !== "string") {
    return "";
  }
  return value.trim().slice(0, maxLength);
}

export async function ensureDbReady() {
  if (initialized) {
    return available;
  }

  initialized = true;

  if (!hasDbConfig()) {
    console.log("DB not configured: device selection logging disabled.");
    available = false;
    return available;
  }

  try {
    pool = mysql.createPool({
      host: config.db.host,
      port: config.db.port,
      user: config.db.user,
      password: config.db.password,
      database: config.db.name,
      waitForConnections: true,
      connectionLimit: 10,
      queueLimit: 0
    });

    await pool.query(`
      CREATE TABLE IF NOT EXISTS users (
        id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
        username VARCHAR(80) NOT NULL,
        password_hash VARCHAR(255) NOT NULL,
        role VARCHAR(32) NOT NULL DEFAULT 'admin',
        must_change_password TINYINT(1) NOT NULL DEFAULT 0,
        created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
        updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
        PRIMARY KEY (id),
        UNIQUE KEY uniq_users_username (username)
      ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;
    `);

    await pool.query(`
      CREATE TABLE IF NOT EXISTS device_selections (
        id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
        device_group VARCHAR(64) NOT NULL,
        device_label VARCHAR(160) NOT NULL,
        firmware_id VARCHAR(120) NOT NULL,
        firmware_label VARCHAR(255) NOT NULL,
        package_id VARCHAR(120) NOT NULL,
        manifest_path VARCHAR(255) NOT NULL,
        ip_address VARCHAR(64) NOT NULL,
        user_agent VARCHAR(255) NOT NULL,
        created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
        PRIMARY KEY (id),
        INDEX idx_device_created_at (created_at)
      ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;
    `);

    await pool.query(`
      CREATE TABLE IF NOT EXISTS devices (
        id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
        serial_number VARCHAR(120) NOT NULL,
        device_group VARCHAR(64) NOT NULL,
        device_label VARCHAR(160) NOT NULL,
        firmware_version VARCHAR(64) NOT NULL,
        last_checkin DATETIME,
        created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
        updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
        PRIMARY KEY (id),
        UNIQUE KEY uniq_devices_serial_number (serial_number),
        INDEX idx_devices_group (device_group),
        INDEX idx_devices_last_checkin (last_checkin)
      ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;
    `);

    await pool.query(`
      CREATE TABLE IF NOT EXISTS device_checkins (
        id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
        device_id BIGINT UNSIGNED NOT NULL,
        firmware_version VARCHAR(64) NOT NULL,
        status VARCHAR(32) NOT NULL DEFAULT 'ok',
        data JSON,
        created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
        PRIMARY KEY (id),
        KEY fk_device_checkins_device_id (device_id),
        INDEX idx_device_checkins_created_at (created_at),
        CONSTRAINT fk_device_checkins_device_id FOREIGN KEY (device_id) REFERENCES devices (id) ON DELETE CASCADE
      ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;
    `);

    await pool.query(`
      CREATE TABLE IF NOT EXISTS firmware_releases (
        id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
        version VARCHAR(64) NOT NULL,
        release_url VARCHAR(255) NOT NULL,
        release_date DATETIME NOT NULL,
        created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
        PRIMARY KEY (id),
        UNIQUE KEY uniq_firmware_releases_version (version)
      ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;
    `);

    await pool.query(`
      CREATE TABLE IF NOT EXISTS ota_campaigns (
        id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
        device_id BIGINT UNSIGNED NOT NULL,
        firmware_id BIGINT UNSIGNED NOT NULL,
        status VARCHAR(32) NOT NULL DEFAULT 'pending',
        created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
        updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
        PRIMARY KEY (id),
        KEY fk_ota_campaigns_device_id (device_id),
        KEY fk_ota_campaigns_firmware_id (firmware_id),
        CONSTRAINT fk_ota_campaigns_device_id FOREIGN KEY (device_id) REFERENCES devices (id) ON DELETE CASCADE,
        CONSTRAINT fk_ota_campaigns_firmware_id FOREIGN KEY (firmware_id) REFERENCES firmware_releases (id) ON DELETE CASCADE
      ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;
    `);

    // Migration: add app_type column to devices (safe on re-run)
    try {
      await pool.query(`ALTER TABLE devices ADD COLUMN app_type VARCHAR(32) NOT NULL DEFAULT 'system'`);
      console.log("DB migration: added app_type to devices table");
    } catch (e) {
      if (e.code !== "ER_DUP_FIELDNAME") throw e;
    }

    await pool.query(`
      CREATE TABLE IF NOT EXISTS app_modules (
        id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
        app_type VARCHAR(32) NOT NULL,
        version VARCHAR(64) NOT NULL,
        module_url VARCHAR(512) NOT NULL,
        checksum_sha256 VARCHAR(128) NOT NULL DEFAULT '',
        min_host_abi INT NOT NULL DEFAULT 1,
        is_active TINYINT(1) NOT NULL DEFAULT 1,
        notes TEXT,
        created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
        PRIMARY KEY (id),
        INDEX idx_app_modules_type (app_type),
        INDEX idx_app_modules_type_active (app_type, is_active)
      ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;
    `);

    await ensureDefaultAdmin();

    available = true;
    console.log(`DB logging ready: ${config.db.host}:${config.db.port}/${config.db.name}`);
  } catch (error) {
    available = false;
    console.warn(`DB init failed; continuing without DB logging: ${String(error.message || error)}`);
  }

  return available;
}

export async function logDeviceSelection(entry) {
  const ready = await ensureDbReady();
  if (!ready || !pool) {
    return { saved: false, reason: "db_unavailable" };
  }

  const deviceGroup = toSafeString(entry.deviceGroup, 64);
  const deviceLabel = toSafeString(entry.deviceLabel, 160);
  const firmwareId = toSafeString(entry.firmwareId, 120);
  const firmwareLabel = toSafeString(entry.firmwareLabel, 255);
  const packageId = toSafeString(entry.packageId, 120);
  const manifestPath = toSafeString(entry.manifestPath, 255);
  const ipAddress = toSafeString(entry.ipAddress, 64) || "unknown";
  const userAgent = toSafeString(entry.userAgent, 255) || "unknown";

  if (!deviceGroup || !firmwareId || !packageId) {
    return { saved: false, reason: "invalid_payload" };
  }

  await pool.execute(
    `
      INSERT INTO device_selections
      (device_group, device_label, firmware_id, firmware_label, package_id, manifest_path, ip_address, user_agent)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    `,
    [
      deviceGroup,
      deviceLabel,
      firmwareId,
      firmwareLabel,
      packageId,
      manifestPath,
      ipAddress,
      userAgent
    ]
  );

  return { saved: true };
}

export async function listDeviceSelections(limit = 100) {
  const ready = await ensureDbReady();
  if (!ready || !pool) {
    return [];
  }

  const normalizedLimit = Number.isFinite(limit) ? Math.max(1, Math.min(500, Math.floor(limit))) : 100;
  const [rows] = await pool.query(
    `
      SELECT
        id,
        device_group AS deviceGroup,
        device_label AS deviceLabel,
        firmware_id AS firmwareId,
        firmware_label AS firmwareLabel,
        package_id AS packageId,
        manifest_path AS manifestPath,
        ip_address AS ipAddress,
        user_agent AS userAgent,
        created_at AS createdAt
      FROM device_selections
      ORDER BY created_at DESC
      LIMIT ?
    `,
    [normalizedLimit]
  );

  return rows;
}

async function ensureDefaultAdmin() {
  if (!pool) {
    return;
  }

  const [rows] = await pool.execute(
    "SELECT id FROM users WHERE username = ? LIMIT 1",
    [DEFAULT_ADMIN_USERNAME]
  );

  if (Array.isArray(rows) && rows.length > 0) {
    return;
  }

  const passwordHash = await bcrypt.hash(DEFAULT_ADMIN_PASSWORD, 12);
  await pool.execute(
    `
      INSERT INTO users (username, password_hash, role, must_change_password)
      VALUES (?, ?, 'admin', 1)
    `,
    [DEFAULT_ADMIN_USERNAME, passwordHash]
  );

  console.warn("Created default admin account: username 'admin' with temporary password 'pass1234'. Change it after first login.");
}

function normalizeUsername(value) {
  return toSafeString(value, 80).toLowerCase();
}

export async function authenticateUser(username, password) {
  const ready = await ensureDbReady();
  if (!ready || !pool) {
    return { ok: false, reason: "db_unavailable" };
  }

  const normalizedUsername = normalizeUsername(username);
  const candidatePassword = typeof password === "string" ? password : "";

  if (!normalizedUsername || !candidatePassword) {
    return { ok: false, reason: "invalid_credentials" };
  }

  const [rows] = await pool.execute(
    `
      SELECT id, username, password_hash AS passwordHash, role, must_change_password AS mustChangePassword
      FROM users
      WHERE username = ?
      LIMIT 1
    `,
    [normalizedUsername]
  );

  if (!Array.isArray(rows) || rows.length === 0) {
    return { ok: false, reason: "invalid_credentials" };
  }

  const row = rows[0];
  const matches = await bcrypt.compare(candidatePassword, row.passwordHash);
  if (!matches) {
    return { ok: false, reason: "invalid_credentials" };
  }

  return {
    ok: true,
    user: {
      id: Number(row.id),
      username: row.username,
      role: row.role || "admin",
      mustChangePassword: Boolean(row.mustChangePassword)
    }
  };
}

export async function getUserById(userId) {
  const ready = await ensureDbReady();
  if (!ready || !pool) {
    return null;
  }

  const normalizedId = Number(userId);
  if (!Number.isFinite(normalizedId) || normalizedId <= 0) {
    return null;
  }

  const [rows] = await pool.execute(
    `
      SELECT id, username, role, must_change_password AS mustChangePassword
      FROM users
      WHERE id = ?
      LIMIT 1
    `,
    [normalizedId]
  );

  if (!Array.isArray(rows) || rows.length === 0) {
    return null;
  }

  const row = rows[0];
  return {
    id: Number(row.id),
    username: row.username,
    role: row.role || "admin",
    mustChangePassword: Boolean(row.mustChangePassword)
  };
}

export async function changeUserPassword(userId, currentPassword, newPassword) {
  const ready = await ensureDbReady();
  if (!ready || !pool) {
    return { ok: false, reason: "db_unavailable" };
  }

  const normalizedId = Number(userId);
  if (!Number.isFinite(normalizedId) || normalizedId <= 0) {
    return { ok: false, reason: "invalid_user" };
  }

  const nextPassword = typeof newPassword === "string" ? newPassword : "";
  if (nextPassword.length < PASSWORD_MIN_LENGTH) {
    return { ok: false, reason: "password_too_short" };
  }

  const [rows] = await pool.execute(
    `
      SELECT password_hash AS passwordHash
      FROM users
      WHERE id = ?
      LIMIT 1
    `,
    [normalizedId]
  );

  if (!Array.isArray(rows) || rows.length === 0) {
    return { ok: false, reason: "invalid_user" };
  }

  const row = rows[0];
  const currentMatches = await bcrypt.compare(typeof currentPassword === "string" ? currentPassword : "", row.passwordHash);
  if (!currentMatches) {
    return { ok: false, reason: "invalid_current_password" };
  }

  const nextHash = await bcrypt.hash(nextPassword, 12);
  await pool.execute(
    `
      UPDATE users
      SET password_hash = ?, must_change_password = 0
      WHERE id = ?
    `,
    [nextHash, normalizedId]
  );

  return { ok: true };
}

// Device management CRUD

export async function listDevices() {
  const ready = await ensureDbReady();
  if (!ready || !pool) {
    return [];
  }

  const [rows] = await pool.query(`
    SELECT
      id,
      serial_number AS serialNumber,
      device_group AS deviceGroup,
      device_label AS deviceLabel,
      firmware_version AS firmwareVersion,
      app_type AS appType,
      last_checkin AS lastCheckin,
      created_at AS createdAt,
      updated_at AS updatedAt
    FROM devices
    ORDER BY created_at DESC
  `);

  return rows || [];
}

export async function getDeviceById(deviceId) {
  const ready = await ensureDbReady();
  if (!ready || !pool) {
    return null;
  }

  const normalizedId = Number(deviceId);
  if (!Number.isFinite(normalizedId) || normalizedId <= 0) {
    return null;
  }

  const [rows] = await pool.execute(`
    SELECT
      id,
      serial_number AS serialNumber,
      device_group AS deviceGroup,
      device_label AS deviceLabel,
      firmware_version AS firmwareVersion,
      app_type AS appType,
      last_checkin AS lastCheckin,
      created_at AS createdAt,
      updated_at AS updatedAt
    FROM devices
    WHERE id = ?
    LIMIT 1
  `, [normalizedId]);

  if (!Array.isArray(rows) || rows.length === 0) {
    return null;
  }

  return rows[0];
}

export async function registerDevice(deviceGroup, deviceLabel, serialNumber, initialFirmwareVersion) {
  const ready = await ensureDbReady();
  if (!ready || !pool) {
    return { ok: false, reason: "db_unavailable" };
  }

  const normalizedGroup = toSafeString(deviceGroup, 64);
  const normalizedLabel = toSafeString(deviceLabel, 160);
  const normalizedSerial = toSafeString(serialNumber, 120);
  const normalizedVersion = toSafeString(initialFirmwareVersion, 64) || "unknown";

  if (!normalizedGroup || !normalizedSerial) {
    return { ok: false, reason: "invalid_payload" };
  }

  try {
    const [result] = await pool.execute(`
      INSERT INTO devices (serial_number, device_group, device_label, firmware_version)
      VALUES (?, ?, ?, ?)
    `, [normalizedSerial, normalizedGroup, normalizedLabel, normalizedVersion]);

    return {
      ok: true,
      deviceId: result.insertId
    };
  } catch (error) {
    if (error.code === "ER_DUP_ENTRY") {
      const [rows] = await pool.execute(`
        SELECT id
        FROM devices
        WHERE serial_number = ?
        LIMIT 1
      `, [normalizedSerial]);

      const existingId = Array.isArray(rows) && rows.length > 0 ? Number(rows[0].id) : null;
      return {
        ok: false,
        reason: "device_already_registered",
        deviceId: Number.isFinite(existingId) && existingId > 0 ? existingId : undefined
      };
    }
    return { ok: false, reason: "registration_failed" };
  }
}

export async function updateDevice(deviceId, updates) {
  const ready = await ensureDbReady();
  if (!ready || !pool) {
    return { ok: false, reason: "db_unavailable" };
  }

  const normalizedId = Number(deviceId);
  if (!Number.isFinite(normalizedId) || normalizedId <= 0) {
    return { ok: false, reason: "invalid_device_id" };
  }

  const allowedFields = {
    deviceLabel: "device_label",
    firmwareVersion: "firmware_version",
    appType: "app_type",
    lastCheckin: "last_checkin"
  };

  const setClauses = [];
  const values = [];

  Object.entries(updates).forEach(([key, value]) => {
    if (allowedFields[key]) {
      setClauses.push(`${allowedFields[key]} = ?`);
      if (key === "lastCheckin") {
        values.push(value ? new Date(value) : new Date());
      } else {
        values.push(toSafeString(value, 160));
      }
    }
  });

  if (setClauses.length === 0) {
    return { ok: false, reason: "no_updates" };
  }

  values.push(normalizedId);

  await pool.execute(`
    UPDATE devices
    SET ${setClauses.join(", ")}
    WHERE id = ?
  `, values);

  return { ok: true };
}

export async function deleteDevice(deviceId) {
  const ready = await ensureDbReady();
  if (!ready || !pool) {
    return { ok: false, reason: "db_unavailable" };
  }

  const normalizedId = Number(deviceId);
  if (!Number.isFinite(normalizedId) || normalizedId <= 0) {
    return { ok: false, reason: "invalid_device_id" };
  }

  const [result] = await pool.execute(`
    DELETE FROM devices
    WHERE id = ?
  `, [normalizedId]);

  if (result.affectedRows === 0) {
    return { ok: false, reason: "device_not_found" };
  }

  return { ok: true };
}

export async function logDeviceCheckin(deviceId, firmwareVersion, status, data) {
  const ready = await ensureDbReady();
  if (!ready || !pool) {
    return { ok: false, reason: "db_unavailable" };
  }

  const normalizedDeviceId = Number(deviceId);
  if (!Number.isFinite(normalizedDeviceId) || normalizedDeviceId <= 0) {
    return { ok: false, reason: "invalid_device_id" };
  }

  const normalizedVersion = toSafeString(firmwareVersion, 64) || "unknown";
  const normalizedStatus = toSafeString(status, 32) || "ok";
  const jsonData = data ? JSON.stringify(data) : null;

  try {
    await pool.execute(`
      INSERT INTO device_checkins (device_id, firmware_version, status, data)
      VALUES (?, ?, ?, ?)
    `, [normalizedDeviceId, normalizedVersion, normalizedStatus, jsonData]);

    // Update device's last_checkin
    await pool.execute(`
      UPDATE devices
      SET last_checkin = NOW(), firmware_version = ?
      WHERE id = ?
    `, [normalizedVersion, normalizedDeviceId]);

    return { ok: true };
  } catch (error) {
    if (error.code === "ER_NO_REFERENCED_ROW_2") {
      return { ok: false, reason: "device_not_found" };
    }
    return { ok: false, reason: "checkin_failed" };
  }
}

export async function setDeviceAppType(deviceId, appType) {
  return updateDevice(deviceId, { appType: toSafeString(appType, 32) || "system" });
}

export async function listAppModules(filterAppType = null) {
  const ready = await ensureDbReady();
  if (!ready || !pool) return [];

  if (filterAppType) {
    const [rows] = await pool.execute(`
      SELECT id, app_type AS appType, version, module_url AS moduleUrl,
             checksum_sha256 AS checksumSha256, min_host_abi AS minHostAbi,
             is_active AS isActive, notes, created_at AS createdAt
      FROM app_modules
      WHERE app_type = ?
      ORDER BY created_at DESC
    `, [toSafeString(filterAppType, 32)]);
    return rows || [];
  }

  const [rows] = await pool.query(`
    SELECT id, app_type AS appType, version, module_url AS moduleUrl,
           checksum_sha256 AS checksumSha256, min_host_abi AS minHostAbi,
           is_active AS isActive, notes, created_at AS createdAt
    FROM app_modules
    ORDER BY app_type, created_at DESC
  `);
  return rows || [];
}

export async function getActiveAppModule(appType) {
  const ready = await ensureDbReady();
  if (!ready || !pool) return null;

  const [rows] = await pool.execute(`
    SELECT id, app_type AS appType, version, module_url AS moduleUrl,
           checksum_sha256 AS checksumSha256, min_host_abi AS minHostAbi,
           is_active AS isActive, notes, created_at AS createdAt
    FROM app_modules
    WHERE app_type = ? AND is_active = 1
    ORDER BY created_at DESC
    LIMIT 1
  `, [toSafeString(appType, 32)]);

  if (!Array.isArray(rows) || rows.length === 0) return null;
  return rows[0];
}

export async function createAppModule({ appType, version, moduleUrl, checksumSha256 = "", minHostAbi = 1, notes = "" }) {
  const ready = await ensureDbReady();
  if (!ready || !pool) return { ok: false, reason: "db_unavailable" };

  const safeType    = toSafeString(appType, 32);
  const safeVersion = toSafeString(version, 64);
  const safeUrl     = toSafeString(moduleUrl, 512);

  if (!safeType || !safeVersion || !safeUrl) {
    return { ok: false, reason: "invalid_payload" };
  }

  // Deactivate previous modules for this app_type
  await pool.execute(
    `UPDATE app_modules SET is_active = 0 WHERE app_type = ?`,
    [safeType]
  );

  const [result] = await pool.execute(`
    INSERT INTO app_modules (app_type, version, module_url, checksum_sha256, min_host_abi, notes, is_active)
    VALUES (?, ?, ?, ?, ?, ?, 1)
  `, [safeType, safeVersion, safeUrl, checksumSha256.slice(0, 128), Number(minHostAbi) || 1, notes.slice(0, 500)]);

  return { ok: true, moduleId: result.insertId };
}

export async function getDeviceCheckins(deviceId, limit = 100) {
  const ready = await ensureDbReady();
  if (!ready || !pool) {
    return [];
  }

  const normalizedDeviceId = Number(deviceId);
  if (!Number.isFinite(normalizedDeviceId) || normalizedDeviceId <= 0) {
    return [];
  }

  const normalizedLimit = Number.isFinite(limit) ? Math.max(1, Math.min(500, Math.floor(limit))) : 100;

  const [rows] = await pool.query(`
    SELECT
      id,
      device_id AS deviceId,
      firmware_version AS firmwareVersion,
      status,
      data,
      created_at AS createdAt
    FROM device_checkins
    WHERE device_id = ?
    ORDER BY created_at DESC
    LIMIT ?
  `, [normalizedDeviceId, normalizedLimit]);

  return rows || [];
}