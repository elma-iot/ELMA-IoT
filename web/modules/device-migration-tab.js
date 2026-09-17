export function createDeviceMigrationTab({
  elements,
  request,
  currentSettingsSnapshot,
  mergeSettingsObjects,
  applyBackupUiState,
  applySettingsPayload,
  activateTabByName,
  setMessage,
  toast,
  handleError,
  isPcDesignerRuntime,
}) {
  let inspection = null;
  let yamlText = "";

  const boardOptions = {
    esp32s3: [
      ["esp32-s3-super-mini", "ESP32-S3 Super Mini"],
      ["esp32-s3-zero", "ESP32-S3 Zero"],
      ["esp32-s3-psram", "ESP32-S3-N16R8"],
      ["esp32-spk-n16r8", "ESP32-SPK-N16R8"],
      ["esp32-s3-devkit-c1", "ESP32-S3 DevKit C-1"],
      ["esp32-s3-cam-module", "ESP32-S3-CAM Module"],
    ],
    esp32: [
      ["esp32-wroom", "Classic ESP32-WROOM"],
      ["esp32-wrover", "ESP32-WROVER"],
      ["esp32-mini", "ESP32 Mini"],
      ["wemos-lolin32-mini", "Wemos Lolin32 Mini"],
      ["wemos-d1-mini-esp32", "Wemos D1 Mini ESP32"],
    ],
    esp32c3: [["esp32-c3", "ESP32-C3 Super Mini"]],
  };

  function escapeHtml(value) {
    return String(value ?? "")
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;")
      .replaceAll("'", "&#39;");
  }

  function setBusy(busy, label = "") {
    elements.migrationScanButton.disabled = busy;
    elements.migrationInspectButton.disabled = busy;
    elements.migrationApplyButton.disabled = busy || !inspection;
    if (label) elements.migrationStatus.textContent = label;
  }

  function selectedDevice() {
    const option = elements.migrationDeviceSelect.selectedOptions?.[0];
    if (!option?.dataset?.device) return null;
    try { return JSON.parse(option.dataset.device); } catch { return null; }
  }

  function renderDevices(devices) {
    const previousIp = String(elements.migrationIpAddress.value || "");
    elements.migrationDeviceSelect.innerHTML = '<option value="">Select a discovered Tasmota or ESPHome device</option>';
    for (const device of devices) {
      if (!["tasmota", "esphome"].includes(String(device.kind || "").toLowerCase())) continue;
      const option = document.createElement("option");
      option.value = String(device.ip || "");
      option.dataset.device = JSON.stringify(device);
      const chip = device.chip ? ` · ${String(device.chip).toUpperCase()}` : " · chip unverified";
      option.textContent = `${device.name || device.kind} · ${device.ip} · ${device.kind}${chip}`;
      elements.migrationDeviceSelect.appendChild(option);
    }
    const preferred = [...elements.migrationDeviceSelect.options].find((option) => option.value === previousIp);
    if (preferred) elements.migrationDeviceSelect.value = previousIp;
    elements.migrationStatus.textContent = elements.migrationDeviceSelect.options.length > 1
      ? `${elements.migrationDeviceSelect.options.length - 1} migratable device(s) discovered.`
      : "No Tasmota or ESPHome devices were found. You can enter an IP address manually.";
  }

  async function scan() {
    setBusy(true, "Scanning the local network without blocking the interface…");
    try {
      const payload = await request("/api/builder/network-devices/scan", {
        method: "POST",
        body: JSON.stringify({
          username: elements.migrationUsername.value,
          password: elements.migrationPassword.value,
        }),
      });
      renderDevices(Array.isArray(payload.devices) ? payload.devices : []);
    } finally {
      setBusy(false);
    }
  }

  function populateBoardSelect(chip, selectedBoard) {
    elements.migrationBoardSelect.innerHTML = "";
    for (const [value, label] of boardOptions[String(chip || "")] || []) {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = label;
      elements.migrationBoardSelect.appendChild(option);
    }
    if ([...elements.migrationBoardSelect.options].some((option) => option.value === selectedBoard)) {
      elements.migrationBoardSelect.value = selectedBoard;
    }
  }

  function renderInspection(result) {
    inspection = result;
    populateBoardSelect(result.chip, result.boardProfile);
    elements.migrationBoardReason.textContent = `${result.boardReason || "Board inferred from source."} Confidence: ${result.boardConfidence || "unknown"}.`;
    const mappings = Array.isArray(result.mappings) ? result.mappings : [];
    const unresolved = Array.isArray(result.unresolved) ? result.unresolved : [];
    const warnings = Array.isArray(result.warnings) ? result.warnings : [];
    elements.migrationPreview.innerHTML = `
      <div class="migration-summary-grid">
        <div><span>Firmware</span><strong>${escapeHtml(result.kind)}</strong></div>
        <div><span>Device</span><strong>${escapeHtml(result.name)}</strong></div>
        <div><span>Chip</span><strong>${escapeHtml(String(result.chip || "unknown").toUpperCase())}</strong></div>
        <div><span>GPIO mappings</span><strong>${mappings.length}</strong></div>
      </div>
      ${warnings.length ? `<div class="migration-warning-list">${warnings.map((item) => `<p>${escapeHtml(item)}</p>`).join("")}</div>` : ""}
      <div class="migration-mapping-list">
        ${mappings.length ? mappings.map((item) => `
          <div class="migration-mapping-row">
            <strong>GPIO${escapeHtml(item.pin)}</strong>
            <span>${escapeHtml(item.source)}</span>
            <span aria-hidden="true">→</span>
            <span>${escapeHtml(item.target)}</span>
            <small data-confidence="${escapeHtml(item.confidence)}">${escapeHtml(item.confidence)}</small>
          </div>`).join("") : '<p class="note">No physical GPIO mappings were recoverable from the device API.</p>'}
      </div>
      ${unresolved.length ? `<details class="migration-unresolved"><summary>${unresolved.length} assignment(s) need manual review</summary>${unresolved.map((item) => `<p>${escapeHtml(item)}</p>`).join("")}</details>` : ""}`;
    elements.migrationPreviewPanel.removeAttribute("hidden");
    elements.migrationApplyButton.disabled = false;
    elements.migrationDownloadButton.disabled = false;
  }

  async function inspect() {
    const ip = String(elements.migrationIpAddress.value || elements.migrationDeviceSelect.value || "").trim();
    if (!ip) throw new Error("Select a discovered device or enter its IP address.");
    setBusy(true, "Reading the source firmware configuration…");
    inspection = null;
    elements.migrationPreviewPanel.setAttribute("hidden", "");
    try {
      const result = await request("/api/builder/migration/import", {
        method: "POST",
        body: JSON.stringify({
          ip,
          username: elements.migrationUsername.value,
          password: elements.migrationPassword.value,
          yaml: yamlText,
        }),
      });
      renderInspection(result);
      elements.migrationStatus.textContent = `Configuration read from ${result.name || ip}. Review it before applying.`;
    } finally {
      setBusy(false);
    }
  }

  async function apply() {
    if (!inspection) return;
    const selectedBoard = String(elements.migrationBoardSelect.value || inspection.boardProfile || "");
    const settingsPatch = JSON.parse(JSON.stringify(inspection.settingsPatch || {}));
    settingsPatch.ui ||= {};
    settingsPatch.ui.gpioBoardAutodetect = false;
    settingsPatch.ui.gpioBoardSelection = selectedBoard;
    const uiState = JSON.parse(JSON.stringify(inspection.uiState || {}));
    uiState.gpioBoard = { autodetect: false, selectedBoard };

    setBusy(true, "Applying imported wiring to the Configuration tab…");
    try {
      applyBackupUiState(uiState, { persist: false });
      const merged = mergeSettingsObjects(currentSettingsSnapshot(), settingsPatch);
      await applySettingsPayload(merged, {
        silent: false,
        successMessage: `Imported ${inspection.kind} configuration`,
        toastMessage: "Source wiring applied to Configuration",
      });
      activateTabByName("gpio");
      setMessage(`Imported ${inspection.kind} GPIOs and selected ${selectedBoard}. Review low-confidence mappings before compiling.`);
    } finally {
      setBusy(false);
    }
  }

  function downloadSnapshot() {
    if (!inspection) return;
    const contents = JSON.stringify({
      exportedAt: new Date().toISOString(),
      source: { ip: inspection.ip, kind: inspection.kind, chip: inspection.chip, name: inspection.name },
      rawSnapshot: inspection.rawSnapshot,
      translated: inspection.settingsPatch,
      mappings: inspection.mappings,
      unresolved: inspection.unresolved,
    }, null, 2);
    const url = URL.createObjectURL(new Blob([contents], { type: "application/json" }));
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = `elma-migration-${String(inspection.name || inspection.ip || "device").toLowerCase().replace(/[^a-z0-9]+/g, "-")}.json`;
    anchor.click();
    URL.revokeObjectURL(url);
    toast("Source migration snapshot downloaded");
  }

  function setup() {
    if (!isPcDesignerRuntime || !elements.migrationTabButton) return;
    elements.migrationTabButton.removeAttribute("hidden");
    elements.migrationTab.removeAttribute("hidden");
    elements.migrationDeviceSelect.addEventListener("change", () => {
      const device = selectedDevice();
      if (device?.ip) elements.migrationIpAddress.value = device.ip;
      elements.migrationYamlRow.hidden = String(device?.kind || "").toLowerCase() !== "esphome";
    });
    elements.migrationScanButton.addEventListener("click", () => scan().catch(handleError));
    elements.migrationInspectButton.addEventListener("click", () => inspect().catch(handleError));
    elements.migrationApplyButton.addEventListener("click", () => apply().catch(handleError));
    elements.migrationDownloadButton.addEventListener("click", downloadSnapshot);
    elements.migrationYamlFile.addEventListener("change", async () => {
      const file = elements.migrationYamlFile.files?.[0];
      if (!file) {
        yamlText = "";
        elements.migrationYamlLabel.textContent = "No YAML selected";
        return;
      }
      if (file.size > 2 * 1024 * 1024) throw new Error("ESPHome YAML must be smaller than 2 MiB.");
      yamlText = await file.text();
      elements.migrationYamlLabel.textContent = `${file.name} · ${(file.size / 1024).toFixed(1)} KiB`;
    });
  }

  setup();
  return { scan, inspect, apply };
}
