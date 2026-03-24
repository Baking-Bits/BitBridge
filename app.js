(async function () {
  const deviceSelectEl = document.getElementById("deviceSelect");
  const firmwareSelectEl = document.getElementById("firmwareSelect");
  const firmwareMetaEl = document.getElementById("firmwareMeta");
  const installContainerEl = document.getElementById("installContainer");
  const publicStatusEl = document.getElementById("publicStatus");
  const firmwareLinksEl = document.getElementById("firmwareLinks");

  if (deviceSelectEl && firmwareSelectEl && firmwareMetaEl && installContainerEl && publicStatusEl && firmwareLinksEl) {
    let packages = [];
    let currentDeviceGroup = "";
    let firmwareOptions = [];
    let lastTrackedSelection = "";

    const deviceGroupLabels = {
      cyd: "CYD — ESP32-2432S028R",
      s3: "ESP32-S3 — 2.8\" IPS Touch",
      original: "Original / Reference Targets"
    };

    try {
      const response = await fetch("./packages/catalog.json", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`Could not load package catalog (${response.status})`);
      }
      packages = await response.json();
    } catch (error) {
      deviceSelectEl.innerHTML = "<option>Catalog load failed</option>";
      firmwareSelectEl.innerHTML = "<option>Catalog load failed</option>";
      publicStatusEl.textContent = String(error.message || error);
      return;
    }

    if (!Array.isArray(packages) || packages.length === 0) {
      deviceSelectEl.innerHTML = "<option>No devices configured</option>";
      firmwareSelectEl.innerHTML = "<option>No firmware configured</option>";
      publicStatusEl.textContent = "No package entries found in packages/catalog.json";
      return;
    }

    renderDeviceOptions();
    deviceSelectEl.addEventListener("change", onDeviceChange);
    firmwareSelectEl.addEventListener("change", onFirmwareChange);
    onDeviceChange();
    return;

    function renderDeviceOptions() {
      const groups = [...new Set(packages.map((entry) => entry.group).filter(Boolean))];
      deviceSelectEl.innerHTML = "";

      for (const group of groups) {
        const option = document.createElement("option");
        option.value = group;
        option.textContent = deviceGroupLabels[group] || group.toUpperCase();
        deviceSelectEl.appendChild(option);
      }

      currentDeviceGroup = groups[0] || "";
      deviceSelectEl.value = currentDeviceGroup;
    }

    function onDeviceChange() {
      currentDeviceGroup = deviceSelectEl.value;

      const scopedPackages = packages.filter((entry) => entry.group === currentDeviceGroup);
      firmwareOptions = scopedPackages.flatMap((pkg) => {
        const variants = Array.isArray(pkg.variants) ? pkg.variants : [];
        return variants.map((variant) => ({
          key: `${pkg.id}::${variant.id}`,
          packageId: pkg.id,
          packageName: pkg.name,
          boardHint: pkg.boardHint || "",
          docsUrl: pkg.docsUrl || "",
          repoUrl: variant.repoUrl || "",
          guideUrl: variant.guideUrl || "",
          firmwareId: variant.id,
          firmwareLabel: variant.label,
          notes: variant.notes || "",
          buildLabel: variant.buildLabel || "",
          manifest: variant.manifest || ""
        }));
      });

      firmwareSelectEl.innerHTML = "";
      if (!firmwareOptions.length) {
        const emptyOption = document.createElement("option");
        emptyOption.value = "";
        emptyOption.textContent = "No firmware available for this device";
        firmwareSelectEl.appendChild(emptyOption);
      } else {
        for (const optionData of firmwareOptions) {
          const option = document.createElement("option");
          option.value = optionData.key;
          option.textContent = `${optionData.packageName} — ${optionData.firmwareLabel}`;
          firmwareSelectEl.appendChild(option);
        }
      }

      onFirmwareChange();
    }

    function onFirmwareChange() {
      const selected = firmwareOptions.find((entry) => entry.key === firmwareSelectEl.value) || firmwareOptions[0];
      if (!selected) {
        firmwareMetaEl.textContent = "No firmware selected.";
        installContainerEl.innerHTML = "";
        firmwareLinksEl.innerHTML = "";
        publicStatusEl.textContent = "Select a device to continue.";
        return;
      }

      firmwareSelectEl.value = selected.key;
      firmwareMetaEl.textContent = [selected.boardHint, selected.buildLabel, selected.notes].filter(Boolean).join(" · ");

      installContainerEl.innerHTML = "";
      if (selected.manifest) {
        const installButton = document.createElement("esp-web-install-button");
        installButton.setAttribute("manifest", selected.manifest);
        installContainerEl.appendChild(installButton);
        publicStatusEl.textContent = "Firmware ready. Connect device and click Install.";
      } else {
        publicStatusEl.textContent = "Selected firmware has no manifest yet.";
      }

      firmwareLinksEl.innerHTML = "";
      addOptionalLink(selected.docsUrl, "Project docs");
      addOptionalLink(selected.repoUrl, "Source repo");
      addOptionalLink(selected.guideUrl, "Build guide");

      trackSelection(selected).catch(() => {});
    }

    function addOptionalLink(href, label) {
      if (!href) {
        return;
      }
      const link = document.createElement("a");
      link.href = href;
      link.target = "_blank";
      link.rel = "noopener";
      link.className = "btn btn-outline-secondary btn-sm";
      link.textContent = label;
      firmwareLinksEl.appendChild(link);
    }

    async function trackSelection(selected) {
      const trackKey = `${selected.key}:${currentDeviceGroup}`;
      if (trackKey === lastTrackedSelection) {
        return;
      }

      lastTrackedSelection = trackKey;
      await fetch("./api/public/device-selection", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          deviceGroup: currentDeviceGroup,
          deviceLabel: deviceGroupLabels[currentDeviceGroup] || currentDeviceGroup,
          packageId: selected.packageId,
          firmwareId: selected.firmwareId,
          firmwareLabel: selected.firmwareLabel,
          manifestPath: selected.manifest
        })
      });
    }
  }

  const cydSelectEl = document.getElementById("cydSelect");
  const cydMetaEl = document.getElementById("cydMeta");
  const cydVariantListEl = document.getElementById("cydVariantList");
  const s3SelectEl = document.getElementById("s3Select");
  const s3MetaEl = document.getElementById("s3Meta");
  const s3VariantListEl = document.getElementById("s3VariantList");
  const wifiSsidEl = document.getElementById("wifiSsid");
  const wifiPasswordEl = document.getElementById("wifiPassword");
  const wifiTokenEl = document.getElementById("wifiToken");
  const knownWifiSelectEl = document.getElementById("knownWifiSelect");
  const loadKnownWifiButtonEl = document.getElementById("loadKnownWifiButton");
  const applyKnownWifiButtonEl = document.getElementById("applyKnownWifiButton");
  const sendWifiButtonEl = document.getElementById("sendWifiButton");
  const wifiProvisionStatusEl = document.getElementById("wifiProvisionStatus");
  const backupCurrentButtonEl = document.getElementById("backupCurrentButton");
  const backupStatusEl = document.getElementById("backupStatus");
  const backupPortEl = document.getElementById("backupPort");
  const backupChipEl = document.getElementById("backupChip");
  const backupBaudEl = document.getElementById("backupBaud");
  const controlPlaneTokenStorageKey = "controlPlaneToken";

  let packages = [];
  let knownNetworks = [];

  try {
    const response = await fetch("./packages/catalog.json", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`Could not load package catalog: ${response.status}`);
    }
    packages = await response.json();
  } catch (error) {
    cydSelectEl.innerHTML = "<option>Catalog load failed</option>";
    s3SelectEl.innerHTML  = "<option>Catalog load failed</option>";
    cydMetaEl.textContent = String(error.message || error);
    s3MetaEl.textContent  = String(error.message || error);
    return;
  }

  if (!Array.isArray(packages) || !packages.length) {
    cydSelectEl.innerHTML = "<option>No packages configured</option>";
    s3SelectEl.innerHTML = "<option>No packages configured</option>";
    cydMetaEl.textContent = "Add entries in packages/catalog.json";
    s3MetaEl.textContent = "Add entries in packages/catalog.json";
    return;
  }

  const cydPackages = packages.filter((item) => item.group === "cyd");
  const s3Packages  = packages.filter((item) => item.group === "s3");

  initializeGroup(cydPackages, cydSelectEl, cydMetaEl, cydVariantListEl, "No CYD builds configured");
  initializeGroup(s3Packages,  s3SelectEl,  s3MetaEl,  s3VariantListEl,  "No S3 builds configured");

  sendWifiButtonEl?.addEventListener("click", provisionWifiOverSerial);
  loadKnownWifiButtonEl?.addEventListener("click", () => {
    loadKnownWifiNetworks({ showStatus: true });
  });
  applyKnownWifiButtonEl?.addEventListener("click", applySelectedKnownWifi);
  wifiTokenEl?.addEventListener("change", () => {
    localStorage.setItem(controlPlaneTokenStorageKey, (wifiTokenEl.value || "").trim());
  });
  backupCurrentButtonEl?.addEventListener("click", saveCurrentImage);

  hydrateTokenInput();
  renderKnownWifiOptions();
  loadKnownWifiNetworks({ showStatus: false });
  loadBackupStatus();

  function hydrateTokenInput() {
    if (!wifiTokenEl) {
      return;
    }
    const saved = localStorage.getItem(controlPlaneTokenStorageKey) || "";
    wifiTokenEl.value = saved;
  }

  function getControlPlaneToken() {
    return (wifiTokenEl?.value || "").trim();
  }

  function getAuthHeaders() {
    const token = getControlPlaneToken();
    if (!token) {
      return {};
    }
    return {
      "x-control-plane-key": token
    };
  }

  function renderKnownWifiOptions() {
    if (!knownWifiSelectEl) {
      return;
    }

    knownWifiSelectEl.innerHTML = "";
    if (!knownNetworks.length) {
      const option = document.createElement("option");
      option.value = "";
      option.textContent = "No known Wi-Fi loaded";
      knownWifiSelectEl.appendChild(option);
      return;
    }

    const placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = "Select a known network";
    knownWifiSelectEl.appendChild(placeholder);

    for (const network of knownNetworks) {
      const option = document.createElement("option");
      option.value = network.id;
      option.textContent = `${network.label} (${network.ssid})`;
      knownWifiSelectEl.appendChild(option);
    }
  }

  async function loadKnownWifiNetworks({ showStatus }) {
    if (!knownWifiSelectEl) {
      return;
    }

    if (showStatus && wifiProvisionStatusEl) {
      wifiProvisionStatusEl.textContent = "Loading known Wi-Fi from server...";
    }

    try {
      const response = await fetch("./api/wifi/known", {
        cache: "no-store",
        headers: getAuthHeaders()
      });

      if (response.status === 401) {
        throw new Error("Unauthorized. Enter control plane token, then Load again.");
      }
      if (!response.ok) {
        throw new Error(`Known Wi-Fi fetch failed (${response.status})`);
      }

      const payload = await response.json();
      knownNetworks = Array.isArray(payload?.networks) ? payload.networks : [];
      renderKnownWifiOptions();

      if (wifiProvisionStatusEl && showStatus) {
        wifiProvisionStatusEl.textContent = knownNetworks.length
          ? `Loaded ${knownNetworks.length} known network(s).`
          : "Known Wi-Fi list is empty.";
      }
    } catch (error) {
      knownNetworks = [];
      renderKnownWifiOptions();
      if (wifiProvisionStatusEl && showStatus) {
        wifiProvisionStatusEl.textContent = `Known Wi-Fi unavailable: ${String(error.message || error)}`;
      }
    }
  }

  function applySelectedKnownWifi() {
    if (!knownWifiSelectEl || !wifiSsidEl || !wifiPasswordEl || !wifiProvisionStatusEl) {
      return;
    }

    const selectedId = knownWifiSelectEl.value;
    if (!selectedId) {
      wifiProvisionStatusEl.textContent = "Select a known network first.";
      return;
    }

    const selected = knownNetworks.find((entry) => entry.id === selectedId);
    if (!selected) {
      wifiProvisionStatusEl.textContent = "Selected network is no longer available.";
      return;
    }

    wifiSsidEl.value = selected.ssid || "";
    wifiPasswordEl.value = selected.password || "";
    wifiProvisionStatusEl.textContent = `Applied known network: ${selected.label || selected.ssid}`;
  }

  function initializeGroup(groupPackages, selectEl, metaEl, variantListEl, emptyMessage) {
    if (!groupPackages.length) {
      selectEl.innerHTML = `<option>${emptyMessage}</option>`;
      metaEl.textContent = emptyMessage;
      variantListEl.innerHTML = "";
      return;
    }

    for (const item of groupPackages) {
      const option = document.createElement("option");
      option.value = item.id;
      option.textContent = item.name;
      selectEl.appendChild(option);
    }

    selectEl.addEventListener("change", () => renderPackageGroup(groupPackages, selectEl, metaEl, variantListEl));
    renderPackageGroup(groupPackages, selectEl, metaEl, variantListEl);
  }

  function renderPackageGroup(groupPackages, selectEl, metaEl, variantListEl) {
    const selectedId = selectEl.value;
    const pkg = groupPackages.find((item) => item.id === selectedId) || groupPackages[0];

    const metaParts = [pkg.boardHint].filter(Boolean);
    if (pkg.docsUrl) {
      metaParts.push(`<a href="${pkg.docsUrl}" target="_blank" rel="noopener">Project docs</a>`);
    }
    if (pkg.packagePath) {
      metaParts.push(`Path: ${pkg.packagePath}`);
    }
    metaEl.innerHTML = metaParts.join(" · ");

    variantListEl.innerHTML = "";

    for (const variant of pkg.variants || []) {
      const card = document.createElement("article");
      card.className = "variant-card";

      const title = document.createElement("h3");
      title.textContent = variant.label;
      card.appendChild(title);

      if (variant.notes) {
        const note = document.createElement("p");
        note.className = "text-muted mb-2";
        note.textContent = variant.notes;
        card.appendChild(note);
      }

      if (variant.buildLabel) {
        const build = document.createElement("p");
        build.className = "small mb-2";
        build.textContent = `Build: ${variant.buildLabel}`;
        card.appendChild(build);
      }

      const imageStatus = document.createElement("p");
      imageStatus.className = "small text-muted mb-2";
      imageStatus.textContent = variant.manifest ? "Image status: checking..." : "Image status: source build required";
      card.appendChild(imageStatus);

      const actions = document.createElement("div");
      actions.className = "variant-actions";

      if (variant.manifest) {
        const installButton = document.createElement("esp-web-install-button");
        installButton.setAttribute("manifest", variant.manifest);
        actions.appendChild(installButton);
      }

      if (variant.manifest) {
        const copyButton = document.createElement("button");
        copyButton.type = "button";
        copyButton.className = "btn btn-outline-secondary btn-sm";
        copyButton.textContent = "Copy manifest path";
        copyButton.addEventListener("click", async () => {
          try {
            await navigator.clipboard.writeText(variant.manifest);
            copyButton.textContent = "Copied";
            setTimeout(() => {
              copyButton.textContent = "Copy manifest path";
            }, 1200);
          } catch (_error) {
            copyButton.textContent = "Copy failed";
          }
        });
        actions.appendChild(copyButton);
      }

      if (variant.repoUrl) {
        const repoButton = document.createElement("a");
        repoButton.className = "btn btn-outline-primary btn-sm";
        repoButton.href = variant.repoUrl;
        repoButton.target = "_blank";
        repoButton.rel = "noopener";
        repoButton.textContent = "Open source repo";
        actions.appendChild(repoButton);
      }

      if (variant.guideUrl) {
        const guideButton = document.createElement("a");
        guideButton.className = "btn btn-outline-secondary btn-sm";
        guideButton.href = variant.guideUrl;
        guideButton.target = "_blank";
        guideButton.rel = "noopener";
        guideButton.textContent = "Build guide";
        actions.appendChild(guideButton);
      }

      card.appendChild(actions);
      variantListEl.appendChild(card);

      if (variant.manifest) {
        updateVariantImageStatus(variant, imageStatus);
      }
    }
  }

  async function updateVariantImageStatus(variant, statusEl) {
    if (!variant || !variant.manifest || !statusEl) {
      return;
    }

    try {
      const manifestResponse = await fetch(variant.manifest, { cache: "no-store" });
      if (!manifestResponse.ok) {
        throw new Error(`Manifest unavailable (${manifestResponse.status})`);
      }

      const manifest = await manifestResponse.json();
      const builds = Array.isArray(manifest.builds) ? manifest.builds : [];
      const parts = builds.flatMap((build) => (Array.isArray(build.parts) ? build.parts : []));
      if (!parts.length) {
        statusEl.textContent = "Image status: manifest has no parts";
        return;
      }

      const manifestUrl = new URL(variant.manifest, window.location.href);
      const metaList = await Promise.all(
        parts.map(async (part) => {
          if (!part || !part.path) {
            return null;
          }
          const partUrl = new URL(part.path, manifestUrl);

          let response = await fetch(partUrl.toString(), { method: "HEAD", cache: "no-store" });
          if (!response.ok) {
            response = await fetch(partUrl.toString(), { cache: "no-store" });
          }
          if (!response.ok) {
            throw new Error(`Missing image part: ${part.path}`);
          }

          const contentLengthHeader = response.headers.get("content-length");
          const parsedLength = contentLengthHeader ? Number.parseInt(contentLengthHeader, 10) : NaN;
          const lastModifiedHeader = response.headers.get("last-modified");
          const lastModifiedMs = lastModifiedHeader ? Date.parse(lastModifiedHeader) : NaN;

          if (response.body && !response.body.locked) {
            response.body.cancel().catch(() => {});
          }

          return {
            size: Number.isFinite(parsedLength) ? parsedLength : 0,
            modifiedMs: Number.isFinite(lastModifiedMs) ? lastModifiedMs : 0
          };
        })
      );

      const validMeta = metaList.filter(Boolean);
      const totalBytes = validMeta.reduce((sum, item) => sum + item.size, 0);
      const latestMs = validMeta.reduce((max, item) => Math.max(max, item.modifiedMs), 0);
      const updatedText = latestMs > 0 ? new Date(latestMs).toLocaleString() : "unknown time";

      statusEl.textContent = `Image status: ${formatBytes(totalBytes)} total · updated ${updatedText}`;
    } catch (error) {
      statusEl.textContent = `Image status: unavailable (${String(error.message || error)})`;
    }
  }

  function formatBytes(bytes) {
    if (!Number.isFinite(bytes) || bytes <= 0) {
      return "0 B";
    }
    const units = ["B", "KB", "MB", "GB"];
    let value = bytes;
    let unitIndex = 0;
    while (value >= 1024 && unitIndex < units.length - 1) {
      value /= 1024;
      unitIndex += 1;
    }
    const decimals = value >= 100 || unitIndex === 0 ? 0 : 1;
    return `${value.toFixed(decimals)} ${units[unitIndex]}`;
  }

  async function provisionWifiOverSerial() {
    if (!wifiSsidEl || !wifiPasswordEl || !wifiProvisionStatusEl) {
      return;
    }

    const ssid = (wifiSsidEl.value || "").trim();
    const password = wifiPasswordEl.value || "";

    if (!ssid) {
      wifiProvisionStatusEl.textContent = "Enter SSID first.";
      return;
    }

    if (!navigator.serial) {
      wifiProvisionStatusEl.textContent = "Web Serial not supported in this browser. Use Chrome/Edge.";
      return;
    }

    let port;
    let writer;

    try {
      wifiProvisionStatusEl.textContent = "Select ESP32 serial port...";
      port = await navigator.serial.requestPort();
      await port.open({ baudRate: 115200 });

      const payload = {
        ssid,
        password
      };
      const line = `AURA_WIFI ${JSON.stringify(payload)}\n`;
      const encoder = new TextEncoder();

      writer = port.writable?.getWriter();
      if (!writer) {
        throw new Error("Serial writer unavailable");
      }

      await writer.write(encoder.encode(line));
      wifiProvisionStatusEl.textContent = "Wi-Fi sent. Aura should connect and reboot.";
    } catch (error) {
      wifiProvisionStatusEl.textContent = `Wi-Fi send failed: ${String(error.message || error)}`;
    } finally {
      if (writer) {
        try {
          writer.releaseLock();
        } catch (_error) {}
      }
      if (port) {
        try {
          await port.close();
        } catch (_error) {}
      }
    }
  }

  async function loadBackupStatus() {
    if (!backupStatusEl) {
      return;
    }
    backupStatusEl.textContent = "Checking backup status...";
    try {
      const response = await fetch("./api/backup-status", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`Status HTTP ${response.status}`);
      }
      const data = await response.json();
      if (data?.hasBackup && data?.meta?.capturedAt) {
        const captured = new Date(data.meta.capturedAt).toLocaleString();
        const bytes = formatBytes(Number(data.meta.bytes || 0));
        backupStatusEl.textContent = `Saved: ${captured} · ${bytes}`;
      } else {
        backupStatusEl.textContent = "No saved backup yet.";
      }
    } catch (error) {
      backupStatusEl.textContent = `Backup status unavailable: ${String(error.message || error)}`;
    }
  }

  async function saveCurrentImage() {
    if (!backupStatusEl || !backupCurrentButtonEl) {
      return;
    }

    const port = (backupPortEl?.value || "COM3").trim() || "COM3";
    const chip = (backupChipEl?.value || "esp32s3").trim() || "esp32s3";
    const baudRaw = Number.parseInt((backupBaudEl?.value || "460800").trim(), 10);
    const baud = Number.isFinite(baudRaw) && baudRaw > 0 ? baudRaw : 460800;

    backupCurrentButtonEl.disabled = true;
    backupStatusEl.textContent = `Saving image from ${port}... this can take a few minutes.`;

    try {
      const response = await fetch("./api/backup-current", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ port, chip, baud, sizeHex: "0x800000" })
      });

      const data = await response.json().catch(() => ({}));
      if (!response.ok || !data?.ok) {
        throw new Error(data?.error || `Backup failed (HTTP ${response.status})`);
      }

      const bytes = formatBytes(Number(data.bytes || 0));
      const when = data.capturedAt ? new Date(data.capturedAt).toLocaleString() : "just now";
      backupStatusEl.textContent = `Saved OK: ${bytes} · ${when}. Refreshing package list...`;
      setTimeout(() => window.location.reload(), 900);
    } catch (error) {
      backupStatusEl.textContent = `Backup failed: ${String(error.message || error)}`;
    } finally {
      backupCurrentButtonEl.disabled = false;
    }
  }
})();