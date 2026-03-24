(function () {
  const refreshButtonEl = document.getElementById("refreshSelections");
  const adminStatusEl = document.getElementById("adminStatus");
  const tableBodyEl = document.getElementById("selectionTableBody");
  const accountIdentityEl = document.getElementById("accountIdentity");
  const passwordNoticeEl = document.getElementById("passwordNotice");
  const changePasswordFormEl = document.getElementById("changePasswordForm");
  const currentPasswordEl = document.getElementById("currentPassword");
  const newPasswordEl = document.getElementById("newPassword");
  const confirmPasswordEl = document.getElementById("confirmPassword");
  const passwordStatusEl = document.getElementById("passwordStatus");

  let currentUser = null;

  refreshButtonEl?.addEventListener("click", () => {
    loadSelections();
  });

  changePasswordFormEl?.addEventListener("submit", async (event) => {
    event.preventDefault();
    await changePassword();
  });

  loadAccount();

  loadSelections();

  async function loadAccount() {
    if (!accountIdentityEl) {
      return;
    }

    accountIdentityEl.textContent = "Loading account...";
    try {
      const response = await fetch("/api/auth/me", {
        cache: "no-store",
        credentials: "same-origin"
      });

      if (!response.ok) {
        throw new Error(`Request failed (${response.status})`);
      }

      const payload = await response.json();
      currentUser = payload?.user || null;
      if (!currentUser) {
        throw new Error("No user in session.");
      }

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
    if (!currentPasswordEl || !newPasswordEl || !confirmPasswordEl || !passwordStatusEl) {
      return;
    }

    const currentPassword = currentPasswordEl.value || "";
    const newPassword = newPasswordEl.value || "";
    const confirmPassword = confirmPasswordEl.value || "";

    if (!currentPassword || !newPassword) {
      passwordStatusEl.textContent = "Enter current and new password.";
      return;
    }

    if (newPassword.length < 8) {
      passwordStatusEl.textContent = "New password must be at least 8 characters.";
      return;
    }

    if (newPassword !== confirmPassword) {
      passwordStatusEl.textContent = "New password and confirmation do not match.";
      return;
    }

    passwordStatusEl.textContent = "Updating password...";
    try {
      const response = await fetch("/api/auth/change-password", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        credentials: "same-origin",
        body: JSON.stringify({
          currentPassword,
          newPassword
        })
      });

      const payload = await response.json().catch(() => ({}));
      if (!response.ok || !payload?.ok) {
        throw new Error(payload?.error || `Request failed (${response.status})`);
      }

      currentPasswordEl.value = "";
      newPasswordEl.value = "";
      confirmPasswordEl.value = "";
      passwordStatusEl.textContent = "Password updated successfully.";
      if (passwordNoticeEl) {
        passwordNoticeEl.style.display = "none";
      }
      await loadAccount();
      await loadSelections();
    } catch (error) {
      passwordStatusEl.textContent = `Password update failed: ${String(error.message || error)}`;
    }
  }

  async function loadSelections() {
    if (!adminStatusEl || !tableBodyEl) {
      return;
    }

    adminStatusEl.textContent = "Loading selection data...";
    tableBodyEl.innerHTML = "";

    try {
      const response = await fetch("/api/admin/device-selections?limit=100", {
        cache: "no-store",
        credentials: "same-origin"
      });

      if (!response.ok) {
        throw new Error(`Request failed (${response.status})`);
      }

      const payload = await response.json();
      const selections = Array.isArray(payload?.selections) ? payload.selections : [];

      if (!selections.length) {
        adminStatusEl.textContent = "No selection data yet.";
        return;
      }

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

  function createCell(value) {
    const td = document.createElement("td");
    td.textContent = value;
    return td;
  }

  function formatDate(value) {
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) {
      return "-";
    }
    return date.toLocaleString();
  }
})();