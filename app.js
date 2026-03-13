(async function () {
  const selectEl = document.getElementById("deviceSelect");
  const metaEl = document.getElementById("deviceMeta");
  const variantListEl = document.getElementById("variantList");
  const findLocationBtn = document.getElementById("findLocationBtn");
  const copyLocationBtn = document.getElementById("copyLocationBtn");
  const locationStatusEl = document.getElementById("locationStatus");
  const locationResultEl = document.getElementById("locationResult");
  const wifiSsidEl = document.getElementById("wifiSsid");
  const wifiPasswordEl = document.getElementById("wifiPassword");
  const copyWifiSsidBtn = document.getElementById("copyWifiSsidBtn");
  const copyWifiPasswordBtn = document.getElementById("copyWifiPasswordBtn");
  const clearWifiCacheBtn = document.getElementById("clearWifiCacheBtn");
  const wifiStatusEl = document.getElementById("wifiStatus");

  let locationLatLon = "";
  const WIFI_CACHE_KEY = "esp32InstallerLastWifiV1";

  let packages = [];

  try {
    const response = await fetch("./packages/catalog.json", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`Could not load package catalog: ${response.status}`);
    }
    packages = await response.json();
  } catch (error) {
    selectEl.innerHTML = "<option>Catalog load failed</option>";
    metaEl.textContent = String(error.message || error);
    return;
  }

  if (!Array.isArray(packages) || !packages.length) {
    selectEl.innerHTML = "<option>No packages configured</option>";
    metaEl.textContent = "Add entries in packages/catalog.json";
    return;
  }

  for (const item of packages) {
    const option = document.createElement("option");
    option.value = item.id;
    option.textContent = item.name;
    selectEl.appendChild(option);
  }

  selectEl.addEventListener("change", renderPackage);
  renderPackage();

  if (findLocationBtn) {
    findLocationBtn.addEventListener("click", findLocationByIp);
  }

  if (copyLocationBtn) {
    copyLocationBtn.addEventListener("click", async () => {
      if (!locationLatLon) {
        return;
      }
      try {
        await navigator.clipboard.writeText(locationLatLon);
        copyLocationBtn.textContent = "Copied";
        setTimeout(() => {
          copyLocationBtn.textContent = "Copy lat,lon";
        }, 1200);
      } catch (_error) {
        copyLocationBtn.textContent = "Copy failed";
      }
    });
  }

  initializeWifiCache();

  function renderPackage() {
    const selectedId = selectEl.value;
    const pkg = packages.find((item) => item.id === selectedId) || packages[0];

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

      const actions = document.createElement("div");
      actions.className = "variant-actions";

      const installButton = document.createElement("esp-web-install-button");
      installButton.setAttribute("manifest", variant.manifest);
      actions.appendChild(installButton);

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

      card.appendChild(actions);
      variantListEl.appendChild(card);
    }
  }

  async function findLocationByIp() {
    locationStatusEl.textContent = "Looking up location...";
    locationResultEl.innerHTML = "";
    locationLatLon = "";
    if (copyLocationBtn) {
      copyLocationBtn.disabled = true;
    }

    const providers = [
      {
        name: "ipapi.co",
        url: "https://ipapi.co/json/",
        parse: (data) => ({
          city: data.city,
          region: data.region,
          country: data.country_name,
          timezone: data.timezone,
          latitude: data.latitude,
          longitude: data.longitude,
          ip: data.ip
        })
      },
      {
        name: "ipwho.is",
        url: "https://ipwho.is/",
        parse: (data) => ({
          city: data.city,
          region: data.region,
          country: data.country,
          timezone: data.timezone && data.timezone.id,
          latitude: data.latitude,
          longitude: data.longitude,
          ip: data.ip
        })
      }
    ];

    let location = null;
    let usedProvider = "";

    for (const provider of providers) {
      try {
        const response = await fetch(provider.url, { cache: "no-store" });
        if (!response.ok) {
          continue;
        }
        const raw = await response.json();
        const parsed = provider.parse(raw);
        if (Number.isFinite(Number(parsed.latitude)) && Number.isFinite(Number(parsed.longitude))) {
          location = parsed;
          usedProvider = provider.name;
          break;
        }
      } catch (_error) {
      }
    }

    if (!location) {
      locationStatusEl.textContent = "Could not determine location from IP. Check internet access and try again.";
      return;
    }

    const latitude = Number(location.latitude);
    const longitude = Number(location.longitude);
    locationLatLon = `${latitude.toFixed(6)},${longitude.toFixed(6)}`;

    locationStatusEl.textContent = `Location found via ${usedProvider}.`;
    locationResultEl.innerHTML = `
      <div><strong>City:</strong> ${safeText(location.city)}</div>
      <div><strong>Region:</strong> ${safeText(location.region)}</div>
      <div><strong>Country:</strong> ${safeText(location.country)}</div>
      <div><strong>Timezone:</strong> ${safeText(location.timezone)}</div>
      <div><strong>IP:</strong> ${safeText(location.ip)}</div>
      <div><strong>Lat/Lon:</strong> <span class="latlon-value">${safeText(locationLatLon)}</span></div>
    `;

    if (copyLocationBtn) {
      copyLocationBtn.disabled = false;
    }
  }

  function initializeWifiCache() {
    if (!wifiSsidEl || !wifiPasswordEl) {
      return;
    }

    const cached = readWifiCache();
    if (cached) {
      wifiSsidEl.value = cached.ssid || "";
      wifiPasswordEl.value = cached.password || "";
      setWifiStatus("Loaded saved Wi-Fi credentials from this browser.");
    }

    const persist = () => {
      const ssid = wifiSsidEl.value.trim();
      const password = wifiPasswordEl.value;
      if (!ssid && !password) {
        localStorage.removeItem(WIFI_CACHE_KEY);
        setWifiStatus("No saved Wi-Fi credentials yet.");
        return;
      }
      const payload = {
        ssid,
        password,
        updatedAt: Date.now()
      };
      localStorage.setItem(WIFI_CACHE_KEY, JSON.stringify(payload));
      setWifiStatus("Saved in browser cache for next visit.");
    };

    wifiSsidEl.addEventListener("input", persist);
    wifiPasswordEl.addEventListener("input", persist);

    if (copyWifiSsidBtn) {
      copyWifiSsidBtn.addEventListener("click", async () => {
        await copyTextWithFeedback(wifiSsidEl.value, copyWifiSsidBtn, "Copy SSID");
      });
    }

    if (copyWifiPasswordBtn) {
      copyWifiPasswordBtn.addEventListener("click", async () => {
        await copyTextWithFeedback(wifiPasswordEl.value, copyWifiPasswordBtn, "Copy password");
      });
    }

    if (clearWifiCacheBtn) {
      clearWifiCacheBtn.addEventListener("click", () => {
        localStorage.removeItem(WIFI_CACHE_KEY);
        wifiSsidEl.value = "";
        wifiPasswordEl.value = "";
        setWifiStatus("Saved Wi-Fi credentials cleared.");
      });
    }
  }

  function readWifiCache() {
    try {
      const raw = localStorage.getItem(WIFI_CACHE_KEY);
      if (!raw) {
        return null;
      }
      const parsed = JSON.parse(raw);
      if (!parsed || typeof parsed !== "object") {
        return null;
      }
      return {
        ssid: typeof parsed.ssid === "string" ? parsed.ssid : "",
        password: typeof parsed.password === "string" ? parsed.password : ""
      };
    } catch (_error) {
      return null;
    }
  }

  function setWifiStatus(message) {
    if (!wifiStatusEl) {
      return;
    }
    wifiStatusEl.textContent = message;
  }

  async function copyTextWithFeedback(value, buttonEl, originalText) {
    if (!buttonEl) {
      return;
    }
    if (!value) {
      buttonEl.textContent = "Nothing to copy";
      setTimeout(() => {
        buttonEl.textContent = originalText;
      }, 1200);
      return;
    }

    try {
      await navigator.clipboard.writeText(value);
      buttonEl.textContent = "Copied";
      setTimeout(() => {
        buttonEl.textContent = originalText;
      }, 1200);
    } catch (_error) {
      buttonEl.textContent = "Copy failed";
      setTimeout(() => {
        buttonEl.textContent = originalText;
      }, 1200);
    }
  }

  function safeText(value) {
    if (value === null || value === undefined || value === "") {
      return "-";
    }
    return String(value)
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;")
      .replaceAll("'", "&#39;");
  }
})();