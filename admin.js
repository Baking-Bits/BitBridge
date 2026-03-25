(function () {
  const refreshButtonEl      = document.getElementById("refreshSelections");
  const adminStatusEl        = document.getElementById("adminStatus");
  const tableBodyEl          = document.getElementById("selectionTableBody");
  const accountIdentityEl    = document.getElementById("accountIdentity");
  const passwordNoticeEl     = document.getElementById("passwordNotice");
  const changePasswordFormEl = document.getElementById("changePasswordForm");
  const currentPasswordEl    = document.getElementById("currentPassword");
  const newPasswordEl        = document.getElementById("newPassword");
  const confirmPasswordEl    = document.getElementById("confirmPassword");
  const passwordStatusEl     = document.getElementById("passwordStatus");

  // Device management
  const refreshDevicesEl   = document.getElementById("refreshDevices");
  const deviceStatusEl     = document.getElementById("deviceStatus");
  const deviceTableBodyEl  = document.getElementById("deviceTableBody");

  // App module management
  const refreshModulesEl   = document.getElementById("refreshModules");
  const addModuleFormEl    = document.getElementById("addModuleForm");
  const moduleStatusEl     = document.getElementById("moduleStatus");
  const moduleStatus2El    = document.getElementById("moduleStatus2");
  const moduleTableBodyEl  = document.getElementById("moduleTableBody");
  const deployLocalFormEl  = document.getElementById("deployLocalModuleForm");
  const deployStatusEl     = document.getElementById("deployModuleStatus");
  const deployFileNameEl   = document.getElementById("deployFileName");
  const refreshWasmFilesEl = document.getElementById("refreshWasmFiles");

  const APP_TYPES = ["system", "weather", "homelab", "pet"];

  let currentUser = null;

  refreshButtonEl?.addEventListener("click", () => loadSelections());
  refreshDevicesEl?.addEventListener("click", () => loadDevices());
  refreshModulesEl?.addEventListener("click", () => loadModules());
  addModuleFormEl?.addEventListener("submit", async (e) => { e.preventDefault(); await addModule(); });
  refreshWasmFilesEl?.addEventListener("click", () => loadWasmFiles());
  deployLocalFormEl?.addEventListener("submit", async (e) => { e.preventDefault(); await deployLocalModule(); });
  changePasswordFormEl?.addEventListener("submit", async (event) => {
    event.preventDefault();
    await changePassword();
  });

  loadAccount();
  loadSelections();
  loadDevices();
  loadModules();
  loadWasmFiles();

  // ── Account ────────────────────────────────────────────────────────────────

  async function loadAccount() {
    if (!accountIdentityEl) return;
    accountIdentityEl.textContent = "Loading account...";
    try {
      const response = await fetch("/api/auth/me", { cache: "no-store", credentials: "same-origin" });
      if (!response.ok) throw new Error(`Request failed (${response.status})`);
      const payload = await response.json();
      currentUser = payload?.user || null;
      if (!currentUser) throw new Error("No user in session.");
      accountIdentityEl.textContent = `${currentUser.username} (${currentUser.role || "admin"})`;
      if (passwordNoticeEl) {
        passwordNoticeEl.style.display = currentUser.mustChangePassword ? "" : "none";
      }
    } catch (error) {
      accountIdentityEl.textContent = "Account unavailable";
      if (passwordStatusEl) {
        passwordStatusEl.textContent = `Could not load account: ${String(error.message || error)}`;
      }
    }
  }

  async function changePassword() {
    if (!currentPasswordEl || !newPasswordEl || !confirmPasswordEl || !passwordStatusEl) return;
    const currentPassword = currentPasswordEl.value || "";
    const newPassword = newPasswordEl.value || "";
    const confirmPassword = confirmPasswordEl.value || "";
    if (!currentPassword || !newPassword) { passwordStatusEl.textContent = "Enter current and new password."; return; }
    if (newPassword.length < 8) { passwordStatusEl.textContent = "New password must be at least 8 characters."; return; }
    if (newPassword !== confirmPassword) { passwordStatusEl.textContent = "Passwords do not match."; return; }
    passwordStatusEl.textContent = "Updating password...";
    try {
      const response = await fetch("/api/auth/change-password", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        credentials: "same-origin",
        body: JSON.stringify({ currentPassword, newPassword })
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok || !payload?.ok) throw new Error(payload?.error || `Request failed (${response.status})`);
      currentPasswordEl.value = "";
      newPasswordEl.value = "";
      confirmPasswordEl.value = "";
      passwordStatusEl.textContent = "Password updated successfully.";
      if (passwordNoticeEl) passwordNoticeEl.style.display = "none";
      await loadAccount();
      await loadSelections();
    } catch (error) {
      passwordStatusEl.textContent = `Password update failed: ${String(error.message || error)}`;
    }
  }

  // ── Device Management ──────────────────────────────────────────────────────

  async function loadDevices() {
    if (!deviceStatusEl || !deviceTableBodyEl) return;
    deviceStatusEl.textContent = "Loading devices...";
    deviceTableBodyEl.innerHTML = "";
    try {
      const response = await fetch("/api/admin/devices", { cache: "no-store", credentials: "same-origin" });
      if (!response.ok) throw new Error(`Request failed (${response.status})`);
      const payload = await response.json();
      const devices = Array.isArray(payload?.devices) ? payload.devices : [];
      if (!devices.length) {
        deviceStatusEl.textContent = "No registered devices yet.";
        return;
      }
      for (const device of devices) {
        deviceTableBodyEl.appendChild(buildDeviceRow(device));
      }
      deviceStatusEl.textContent = `${devices.length} device(s) registered.`;
    } catch (error) {
      deviceStatusEl.textContent = `Could not load devices: ${String(error.message || error)}`;
    }
  }

  function buildDeviceRow(device) {
    const tr = document.createElement("tr");
    tr.id = `device-row-${device.id}`;

    tr.appendChild(createCell(String(device.id)));
    const labelCell = document.createElement("td");
    labelCell.innerHTML = `<span class="fw-semibold">${escHtml(device.deviceLabel || "-")}</span><br/><span class="text-muted small">${escHtml(device.serialNumber || "-")}</span>`;
    tr.appendChild(labelCell);
    tr.appendChild(createCell(device.deviceGroup || "-"));
    tr.appendChild(createCell(device.firmwareVersion || "-"));

    // App type selector
    const appTd = document.createElement("td");
    const sel = document.createElement("select");
    sel.className = "form-select form-select-sm";
    sel.style.minWidth = "100px";
    for (const type of APP_TYPES) {
      const opt = document.createElement("option");
      opt.value = type;
      opt.textContent = type;
      if ((device.appType || "system") === type) opt.selected = true;
      sel.appendChild(opt);
    }
    sel.addEventListener("change", async () => {
      const badge = tr.querySelector(".app-status-badge");
      if (badge) badge.textContent = "Saving...";
      const ok = await assignApp(device.id, sel.value);
      if (badge) badge.textContent = ok ? "✓ Saved" : "✗ Error";
      setTimeout(() => { if (badge) badge.textContent = ""; }, 3000);
    });
    const badge = document.createElement("span");
    badge.className = "app-status-badge small text-muted ms-1";
    appTd.appendChild(sel);
    appTd.appendChild(badge);
    tr.appendChild(appTd);

    tr.appendChild(createCell(formatDate(device.lastCheckin)));

    // Delete button
    const actionsTd = document.createElement("td");
    const delBtn = document.createElement("button");
    delBtn.className = "btn btn-outline-danger btn-sm";
    delBtn.textContent = "Delete";
    delBtn.addEventListener("click", async () => {
      if (!confirm(`Delete device ${device.id} (${device.deviceLabel || device.serialNumber})?`)) return;
      const ok = await deleteDevice(device.id);
      if (ok) tr.remove();
    });
    actionsTd.appendChild(delBtn);
    tr.appendChild(actionsTd);

    return tr;
  }

  async function assignApp(deviceId, appType) {
    try {
      const response = await fetch(`/api/admin/devices/${deviceId}/app`, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        credentials: "same-origin",
        body: JSON.stringify({ appType })
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok || !payload?.ok) {
        console.error("assignApp error:", payload?.error);
        return false;
      }
      return true;
    } catch (e) {
      console.error("assignApp exception:", e);
      return false;
    }
  }

  async function deleteDevice(deviceId) {
    try {
      const response = await fetch(`/api/admin/devices/${deviceId}`, {
        method: "DELETE",
        credentials: "same-origin"
      });
      return response.ok;
    } catch (e) {
      console.error("deleteDevice exception:", e);
      return false;
    }
  }

  // ── App Module Registry ────────────────────────────────────────────────────

  async function loadModules() {
    if (!moduleStatus2El || !moduleTableBodyEl) return;
    moduleStatus2El.textContent = "Loading modules...";
    moduleTableBodyEl.innerHTML = "";
    try {
      const response = await fetch("/api/admin/app-modules", { cache: "no-store", credentials: "same-origin" });
      if (!response.ok) throw new Error(`Request failed (${response.status})`);
      const payload = await response.json();
      const modules = Array.isArray(payload?.modules) ? payload.modules : [];
      if (!modules.length) {
        moduleStatus2El.textContent = "No app modules registered yet.";
        return;
      }
      for (const mod of modules) {
        const tr = document.createElement("tr");
        tr.appendChild(createCell(mod.appType || "-"));
        tr.appendChild(createCell(mod.version || "-"));
        const urlTd = document.createElement("td");
        const a = document.createElement("a");
        a.href = mod.moduleUrl || "#";
        a.textContent = (mod.moduleUrl || "-").replace(/^https?:\/\/[^/]+/, "");
        a.target = "_blank";
        a.className = "text-break small";
        urlTd.appendChild(a);
        tr.appendChild(urlTd);
        const activeTd = document.createElement("td");
        activeTd.innerHTML = mod.isActive ? '<span class="badge bg-success">Active</span>' : '<span class="badge bg-secondary">Inactive</span>';
        tr.appendChild(activeTd);
        tr.appendChild(createCell(formatDate(mod.createdAt)));
        moduleTableBodyEl.appendChild(tr);
      }
      moduleStatus2El.textContent = `${modules.length} module record(s).`;
    } catch (error) {
      moduleStatus2El.textContent = `Could not load modules: ${String(error.message || error)}`;
    }
  }

  async function addModule() {
    if (!moduleStatusEl) return;
    const appType    = document.getElementById("moduleAppType")?.value?.trim() || "";
    const version    = document.getElementById("moduleVersion")?.value?.trim() || "";
    const moduleUrl  = document.getElementById("moduleUrl")?.value?.trim() || "";
    const checksum   = document.getElementById("moduleChecksum")?.value?.trim() || "";

    if (!appType || !version || !moduleUrl) {
      moduleStatusEl.textContent = "appType, version, and moduleUrl are required.";
      return;
    }

    moduleStatusEl.textContent = "Registering module...";
    try {
      const response = await fetch("/api/admin/app-modules", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        credentials: "same-origin",
        body: JSON.stringify({ appType, version, moduleUrl, checksumSha256: checksum })
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok || !payload?.ok) throw new Error(payload?.error || `Request failed (${response.status})`);
      moduleStatusEl.textContent = `Module registered (ID: ${payload.moduleId}). Devices will download on next check-in.`;
      document.getElementById("moduleVersion").value = "";
      document.getElementById("moduleUrl").value = "";
      document.getElementById("moduleChecksum").value = "";
      await loadModules();
    } catch (error) {
      moduleStatusEl.textContent = `Failed: ${String(error.message || error)}`;
    }
  }

  async function loadWasmFiles() {
    if (!deployFileNameEl || !deployStatusEl) return;
    deployStatusEl.textContent = "Loading local wasm files...";
    deployFileNameEl.innerHTML = "";
    try {
      const response = await fetch("/api/admin/wasm-files", { cache: "no-store", credentials: "same-origin" });
      if (!response.ok) throw new Error(`Request failed (${response.status})`);
      const payload = await response.json();
      const files = Array.isArray(payload?.files) ? payload.files : [];
      if (!files.length) {
        const opt = document.createElement("option");
        opt.value = "";
        opt.textContent = "No .wasm files found in packages/wasm";
        deployFileNameEl.appendChild(opt);
        deployStatusEl.textContent = "No local wasm files found.";
        return;
      }
      for (const file of files) {
        const opt = document.createElement("option");
        opt.value = file.fileName;
        opt.textContent = `${file.fileName} (${Math.round((Number(file.sizeBytes) || 0) / 1024)} KB)`;
        deployFileNameEl.appendChild(opt);
      }
      deployStatusEl.textContent = `${files.length} local wasm file(s) available.`;
    } catch (error) {
      deployStatusEl.textContent = `Could not load wasm files: ${String(error.message || error)}`;
    }
  }

  async function deployLocalModule() {
    if (!deployStatusEl) return;
    const appType = document.getElementById("deployAppType")?.value?.trim() || "";
    const fileName = deployFileNameEl?.value?.trim() || "";
    const version = document.getElementById("deployVersion")?.value?.trim() || "";
    const assign = document.getElementById("deployAssignMode")?.value || "none";
    const deviceIdRaw = document.getElementById("deployDeviceId")?.value?.trim() || "";
    const deviceId = deviceIdRaw ? Number(deviceIdRaw) : null;

    if (!appType || !fileName || !version) {
      deployStatusEl.textContent = "appType, fileName, and version are required.";
      return;
    }
    if (assign === "one" && (!Number.isFinite(deviceId) || deviceId <= 0)) {
      deployStatusEl.textContent = "When assign=one, provide a valid Device ID.";
      return;
    }

    deployStatusEl.textContent = "Deploying local module...";
    try {
      const response = await fetch("/api/admin/app-modules/deploy-local", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        credentials: "same-origin",
        body: JSON.stringify({ appType, fileName, version, assign, deviceId })
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok || !payload?.ok) {
        throw new Error(payload?.error || `Request failed (${response.status})`);
      }

      deployStatusEl.textContent = `Deployed ${payload.fileName} → ${payload.appType} v${payload.version}; assigned ${payload.assignment?.assignedDevices || 0} device(s).`;
      await loadModules();
      await loadDevices();
    } catch (error) {
      deployStatusEl.textContent = `Deploy failed: ${String(error.message || error)}`;
    }
  }

  // ── Recent Selections ──────────────────────────────────────────────────────

  async function loadSelections() {
    if (!adminStatusEl || !tableBodyEl) return;
    adminStatusEl.textContent = "Loading selection data...";
    tableBodyEl.innerHTML = "";
    try {
      const response = await fetch("/api/admin/device-selections?limit=100", { cache: "no-store", credentials: "same-origin" });
      if (!response.ok) throw new Error(`Request failed (${response.status})`);
      const payload = await response.json();
      const selections = Array.isArray(payload?.selections) ? payload.selections : [];
      if (!selections.length) { adminStatusEl.textContent = "No selection data yet."; return; }
      for (const item of selections) {
        const row = document.createElement("tr");
        row.appendChild(createCell(formatDate(item.createdAt)));
        row.appendChild(createCell(item.deviceLabel || item.deviceGroup || "-"));
        row.appendChild(createCell(item.firmwareLabel || item.firmwareId || "-"));
        row.appendChild(createCell(item.packageId || "-"));
        row.appendChild(createCell(item.ipAddress || "-"));
        tableBodyEl.appendChild(row);
      }
      adminStatusEl.textContent = `Loaded ${selections.length} selection record(s).`;
    } catch (error) {
      adminStatusEl.textContent = `Could not load selection data: ${String(error.message || error)}`;
    }
  }

  // ── Utilities ──────────────────────────────────────────────────────────────

  function createCell(value) {
    const td = document.createElement("td");
    td.textContent = value;
    return td;
  }

  function formatDate(value) {
    if (!value) return "-";
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) return "-";
    return date.toLocaleString();
  }

  function escHtml(str) {
    return String(str)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }
})();