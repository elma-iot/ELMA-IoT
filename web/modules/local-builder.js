import { boardChipFamily, applyBoardFilter } from "./board-pin-policy.js";

export function createLocalBuilder({ elements, currentSettingsSnapshot, setMessage, toast, validatePins = () => {} }) {
  let active = false;
  let jobId = "";
  let timer = null;
  let cancelRequested = false;

  function usesIpTransport() {
    return elements.localBuilderTransport?.value === "ip";
  }

  function usesMinimalFirmware() {
    return elements.localBuilderFirmwareMode?.value === "minimal";
  }

  function idleActionLabel() {
    return usesIpTransport()
      ? `Compile ${usesMinimalFirmware() ? "Minimal" : "Full"} Firmware & Flash IP Device`
      : "Compile Firmware & Flash USB Device";
  }

  function formatBytes(value) {
    const bytes = Number(value || 0);
    if (!bytes) return "Pending firmware build";
    return bytes >= 1048576 ? `${(bytes / 1048576).toFixed(2)} MiB` : `${Math.round(bytes / 1024)} KiB`;
  }

  function renderBuildEstimate(build = {}) {
    const set = (id, value) => {
      const target = document.getElementById(id);
      if (target) target.textContent = value;
    };
    const application = Number(build.applicationBytes || 0);
    const capacity = Number(build.flashCapacityBytes || 0);
    const ramUsed = Number(build.ramUsedBytes || 0);
    const ramTotal = Number(build.ramTotalBytes || 0);
    set("pcBuildProfile", build.profile || "Resolved after USB chip detection");
    set("pcBuildFlash", application
      ? `${formatBytes(application)} confirmed${capacity ? ` · ${(application / capacity * 100).toFixed(1)}% of ${formatBytes(capacity)} chip flash` : ""}`
      : "Estimated during compile; confirmed from firmware.bin");
    set("pcBuildRam", ramUsed
      ? `${formatBytes(ramUsed)} linked static use · ${(ramUsed / ramTotal * 100).toFixed(1)}% of ${formatBytes(ramTotal)}`
      : "Estimated during compile; confirmed from linker report");
    set("pcBuildFile", build.firmwareFile || "Generated binary will be saved beside ELMA IoT – ESP32 Toolkit");
  }

  function adaptInterfaceForPc(version = "") {
    document.title = "ELMA IoT – ESP32 Toolkit — Device Designer";
    document.querySelector(".hero-actions")?.setAttribute("hidden", "");
    const title = document.getElementById("deviceTitle");
    if (title) title.textContent = "ELMA Device Designer";
    const firmwareLabel = document.querySelector(".hero-firmware-label");
    if (firmwareLabel) firmwareLabel.textContent = "ELMA IoT – ESP32 Toolkit";

    const hideTab = (name) => {
      document.querySelector(`.tab-button[data-tab="${name}"]`)?.setAttribute("hidden", "");
      document.getElementById(`tab-${name}`)?.setAttribute("hidden", "");
    };
    // Configured feature tabs are controlled dynamically by app.js. Runtime
    // storage browsers stay hidden because no device filesystem is mounted.
    ["storage-internal", "storage-external"].forEach(hideTab);
    document.getElementById("motorHeroStat")?.setAttribute("hidden", "");

    const deviceTab = document.querySelector('.tab-button[data-tab="device"]');
    if (deviceTab) {
      deviceTab.setAttribute("aria-label", "Identity & Startup");
      deviceTab.setAttribute("data-tooltip", "Identity & Startup");
      deviceTab.title = "Identity & Startup";
    }
    const deviceHeading = document.querySelector("#tab-device > h2");
    if (deviceHeading) deviceHeading.textContent = "Identity & Startup";
    document.querySelector("#tab-device .device-summary-grid")?.setAttribute("hidden", "");
    document.querySelector('[name="device.savedVolumePercent"]')?.closest("label")?.setAttribute("hidden", "");
    ["powerCycleFactoryResetToggle", "touchHoldFactoryResetToggle"].forEach((id) => {
      document.getElementById(id)?.closest("label")?.setAttribute("hidden", "");
    });
    document.getElementById("factoryResetButton")?.setAttribute("hidden", "");
    document.getElementById("mqttRediscoveryButton")?.closest(".inline-actions")?.setAttribute("hidden", "");

    const hardwareTab = document.querySelector('.tab-button[data-tab="hardware"]');
    if (hardwareTab) {
      hardwareTab.setAttribute("aria-label", "Build Memory Estimate");
      hardwareTab.setAttribute("data-tooltip", "Build Memory Estimate");
      hardwareTab.title = "Build Memory Estimate";
    }
    const hardware = document.getElementById("tab-hardware");
    if (hardware) {
      hardware.innerHTML = `
        <h2>Build Memory Estimate</h2>
        <p class="note">Values are estimates until compilation finishes. ELMA IoT – ESP32 Toolkit then replaces them with the exact application binary size and the compiler linker's static RAM report.</p>
        <div class="status-list">
          <div class="status-item"><span>Build profile</span><strong id="pcBuildProfile">Resolved after USB chip detection</strong></div>
          <div class="status-item"><span>Application flash</span><strong id="pcBuildFlash">Estimated during compile</strong></div>
          <div class="status-item"><span>Static RAM</span><strong id="pcBuildRam">Estimated during compile</strong></div>
          <div class="status-item"><span>Saved binary</span><strong id="pcBuildFile">Generated beside ELMA IoT – ESP32 Toolkit</strong></div>
        </div>
        <p class="note">Runtime heap and stack peaks depend on the configured peripherals and traffic; confirm those after flashing from the real device's Hardware Monitor.</p>`;
      renderBuildEstimate();
    }

    const wifiNote = document.querySelector("#tab-wifi .note");
    if (wifiNote) wifiNote.textContent = "Scan uses this PC's Wi-Fi adapter. Connect temporarily tests the selected SSID and password through Windows, then saves verified credentials into the future firmware configuration. If no PC Wi-Fi adapter is available, the application reports that directly.";

    const info = document.getElementById("tab-info");
    if (info) {
      info.innerHTML = `
        <h2>About ELMA IoT – ESP32 Toolkit</h2>
        <div class="status-list">
          <div class="status-item"><span>Application</span><strong>ELMA IoT – ESP32 Toolkit ${version ? `v${version}` : ""}</strong></div>
          <div class="status-item"><span>Purpose</span><strong>Design, compile, provision and flash ELMA ESP devices from a PC</strong></div>
          <div class="status-item"><span>Supported targets</span><strong>ESP32, ESP32-S3 and ESP32-C3</strong></div>
          <div class="status-item"><span>Identity safety</span><strong>Device and MQTT identity are regenerated from the target hardware ID</strong></div>
        </div>
        <h3>Capabilities</h3>
        <p>Configure GPIO wiring, peripherals, Wi-Fi, MQTT, AP fallback, identity, web access and firmware features. The embedded compiler selects the maximum compatible feature set for the chosen chip, then writes and verifies the connected USB target without browser HTTPS or Web Serial requirements.</p>
        <p><a class="button-link" href="https://github.com/elma-iot/ELMA-IoT">Open the ELMA IoT project on GitHub</a></p>
        <p class="note">This screen describes the PC application. Runtime health, storage, media playback, reboot, shutdown and factory-reset controls appear only in the web interface of a flashed device.</p>`;
    }
  }

  async function api(path, options = {}) {
    const response = await fetch(path, {
      headers: { "Content-Type": "application/json" },
      cache: "no-store",
      ...options,
    });
    let payload = {};
    try { payload = await response.json(); } catch {}
    if (!response.ok) throw new Error(payload.error || `Builder HTTP ${response.status}`);
    return payload;
  }

  function setProgress(percent, label) {
    const value = Math.max(0, Math.min(100, Number(percent || 0)));
    elements.localBuilderProgressFill.style.width = `${value}%`;
    elements.localBuilderProgressLabel.textContent = label || "Working...";
  }

  function setBusy(busy) {
    elements.localBuilderCompileFlash.disabled = busy && cancelRequested;
    elements.localBuilderCompileFlash.textContent = busy
      ? (cancelRequested ? "Cancelling Safely…" : "Cancel")
      : idleActionLabel();
    elements.localBuilderCompileFlash.classList.toggle("danger", busy);
    elements.localBuilderTransport.disabled = busy;
    elements.localBuilderChip.disabled = busy;
    elements.localBuilderFirmwareMode.disabled = busy;
    elements.localBuilderPort.disabled = busy;
    elements.localBuilderRefreshPorts.disabled = busy;
    elements.localBuilderIpDevice.disabled = busy;
    elements.localBuilderIpAddress.disabled = busy;
    elements.localBuilderIpUsername.disabled = busy;
    elements.localBuilderIpPassword.disabled = busy;
    elements.localBuilderScanIpDevices.disabled = busy;
    elements.localBuilderCancel.hidden = true;
  }

  function updateTransportUi() {
    const ip = usesIpTransport();
    elements.localBuilderUsbTarget.hidden = ip;
    elements.localBuilderIpTarget.hidden = !ip;
    elements.localBuilderErase.closest("label").hidden = ip;
    if (!jobId) elements.localBuilderCompileFlash.textContent = idleActionLabel();
    elements.localBuilderCompatibility.textContent = ip
      ? "IP flashing uses the device's inactive OTA partition. Cancelling an upload aborts that partition and leaves the currently running firmware bootable."
      : "USB flashing verifies the generated image. If cancellation is requested after erase/write begins, ELMA completes the critical write safely before stopping.";
  }

  function updateFirmwareModeUi() {
    const minimal = usesMinimalFirmware();
    if (minimal && !usesIpTransport()) {
      elements.localBuilderTransport.value = "ip";
      updateTransportUi();
    }
    elements.localBuilderMaximum.closest("fieldset").hidden = minimal;
    elements.localBuilderCompatibility.textContent = minimal
      ? "Minimal recovery firmware keeps the existing ELMA NVS configuration, removes peripherals and the full UI, disables Wi-Fi power saving, and provides only Wi-Fi plus a small OTA upload page. It is IP-flashed without erasing partitions."
      : "The compiler resolves selected wiring against chip GPIO limits and reports forced exclusions before flash is erased.";
    if (!jobId) elements.localBuilderCompileFlash.textContent = idleActionLabel();
  }

  async function refreshPorts() {
    const payload = await api("/api/builder/ports");
    const previous = elements.localBuilderPort.value;
    elements.localBuilderPort.innerHTML = "";
    for (const port of payload.ports || []) {
      const option = document.createElement("option");
      option.value = port.device;
      option.textContent = `${port.device} — ${port.description || "Serial device"}`;
      elements.localBuilderPort.appendChild(option);
    }
    if (previous && [...elements.localBuilderPort.options].some((item) => item.value === previous)) {
      elements.localBuilderPort.value = previous;
    }
    if (!elements.localBuilderPort.options.length) {
      elements.localBuilderPort.appendChild(new Option("No USB serial device detected", ""));
    }
  }

  async function scanIpDevices() {
    elements.localBuilderScanIpDevices.disabled = true;
    elements.localBuilderScanIpDevices.textContent = "Scanning…";
    try {
      const payload = await api("/api/builder/network-devices/scan", {
        method: "POST",
        body: JSON.stringify({
          hintIp: elements.localBuilderIpAddress.value.trim(),
          username: elements.localBuilderIpUsername.value,
          password: elements.localBuilderIpPassword.value,
        }),
      });
      const previous = elements.localBuilderIpDevice.value;
      elements.localBuilderIpDevice.innerHTML = "";
      elements.localBuilderIpDevice.appendChild(new Option("Select a discovered ELMA device", ""));
      for (const device of payload.devices || []) {
        const detail = [device.name, device.version ? `v${device.version}` : "", device.chip || ""].filter(Boolean).join(" · ");
        elements.localBuilderIpDevice.appendChild(new Option(`${device.ip}${detail ? ` — ${detail}` : ""}`, device.ip));
      }
      if (previous && [...elements.localBuilderIpDevice.options].some((option) => option.value === previous)) {
        elements.localBuilderIpDevice.value = previous;
      }
      setMessage(payload.devices?.length
        ? `Found ${payload.devices.length} compatible ELMA device${payload.devices.length === 1 ? "" : "s"} on this LAN.`
        : "No compatible ELMA devices answered the LAN scan. You can enter an IP address manually.");
    } finally {
      elements.localBuilderScanIpDevices.disabled = false;
      elements.localBuilderScanIpDevices.textContent = "Scan LAN";
    }
  }

  function applyTargetPolicy() {
    const chip = elements.localBuilderChip.value;
    const c3 = chip === "esp32c3";
    const maximum = elements.localBuilderMaximum.checked;
    elements.localBuilderWebUi.disabled = maximum;
    elements.localBuilderHacs.disabled = maximum;
    elements.localBuilderAudio.disabled = maximum || c3;
    if (maximum) {
      elements.localBuilderWebUi.checked = true;
      elements.localBuilderHacs.checked = true;
      elements.localBuilderAudio.checked = !c3;
    } else if (c3) {
      elements.localBuilderAudio.checked = false;
    }
    const board = document.getElementById("gpioBoardSelector");
    if (board) applyBoardFilter(board, chip, Boolean(document.getElementById("gpioBoardAutodetect")?.checked));
    elements.localBuilderCompatibility.textContent = c3
      ? "ESP32-C3 keeps the compatible web configurator, Wi-Fi, MQTT/HACS, OTA, GPIO/motor, displays, sensors, controls, storage and communications. Audio-only choices are unavailable."
      : "Maximum-fit mode retains every compatible feature that fits the detected chip and flash. Nothing is removed silently.";
  }

  async function poll() {
    if (!jobId) return;
    try {
      const state = await api(`/api/builder/jobs/${encodeURIComponent(jobId)}`);
      renderBuildEstimate(state);
      setProgress(state.progress, state.status);
      elements.localBuilderLog.textContent = (state.log || []).join("\n") || "Preparing build...";
      elements.localBuilderLog.scrollTop = elements.localBuilderLog.scrollHeight;
      elements.localBuilderCompatibility.textContent = state.compatibility || elements.localBuilderCompatibility.textContent;
      if (state.state === "complete") {
        setBusy(false);
        cancelRequested = false;
        jobId = "";
        setMessage(state.ipAddress
          ? `Device flashed at ${state.ipAddress}. Firmware saved as ${state.firmwareFile || "a standard release binary"}.`
          : `Device compiled, flashed and configured successfully. Firmware saved as ${state.firmwareFile || "a standard release binary"}.`);
        toast("Compile and flash complete");
        if (state.ipAddress && /^\d{1,3}(?:\.\d{1,3}){3}$/.test(state.ipAddress)) {
          const address = document.createElement("a");
          address.href = `http://${state.ipAddress}/`;
          address.textContent = `Open device: ${state.ipAddress}`;
          address.style.display = "block";
          elements.localBuilderLog.parentElement.append(address);
        }
        return;
      }
      if (state.state === "failed" || state.state === "cancelled") {
        setBusy(false);
        cancelRequested = false;
        jobId = "";
        setMessage(state.error || state.status, state.state === "failed");
        return;
      }
    } catch (error) {
      setBusy(false);
      jobId = "";
      setMessage(error.message, true);
      return;
    }
    timer = window.setTimeout(poll, 750);
  }

  async function compileAndFlash() {
    const transport = usesIpTransport() ? "ip" : "usb";
    const port = elements.localBuilderPort.value;
    const targetIp = elements.localBuilderIpAddress.value.trim() || elements.localBuilderIpDevice.value;
    if (transport === "usb" && !port) throw new Error("Connect and select a USB ESP device first.");
    if (transport === "ip" && !targetIp) throw new Error("Scan for a compatible device or enter its IP address first.");
    let networkTarget = null;
    if (transport === "ip") {
      networkTarget = await api("/api/builder/network-devices/probe", {
        method: "POST",
        body: JSON.stringify({
          ip: targetIp,
          username: elements.localBuilderIpUsername.value,
          password: elements.localBuilderIpPassword.value,
        }),
      });
      if (!networkTarget.chip) {
        window.alert("ELMA IoT – ESP32 Toolkit identified this device, but could not verify its ESP chip family. IP flashing has been stopped to prevent installing firmware for the wrong chip. Connect it by USB once or use firmware that reports its exact ESP family.");
        return;
      }
      const selectedChip = elements.localBuilderChip.value;
      if (selectedChip !== "auto" && selectedChip !== networkTarget.chip) {
        window.alert(`Destination chip mismatch. The device reports ${networkTarget.chip.toUpperCase()}, but the configured firmware target is ${selectedChip.toUpperCase()}. Nothing was compiled or uploaded.`);
        return;
      }
      const boardValue = document.getElementById("gpioBoardSelector")?.value || "";
      const boardChip = boardChipFamily(boardValue);
      if (boardChip !== networkTarget.chip) {
        window.alert(`Destination chip mismatch. The selected board (${boardValue}) targets ${boardChip.toUpperCase()}, while the device reports ${networkTarget.chip.toUpperCase()}. Select the correct board before flashing.`);
        return;
      }
      elements.localBuilderChip.value = networkTarget.chip;
      applyTargetPolicy();
      if (networkTarget.kind !== "elma") {
        const product = networkTarget.kind === "tasmota" ? "Tasmota" : "ESPHome";
        const confirmed = window.confirm(`${product} firmware was detected on ${targetIp}. Continuing will replace it with ELMA firmware. Device settings from ${product} will not be converted.\n\nReplace ${product} firmware on this device?`);
        if (!confirmed) return;
      }
    }
    // The standalone Flash tab deliberately reads the shared Designer backend
    // at click time so a second WebEngine view cannot flash a stale snapshot.
    const desktopFlashView = new URLSearchParams(window.location.search).get("elmaView") === "flash";
    const settings = desktopFlashView ? await api("/api/settings") : currentSettingsSnapshot();
    if (!usesMinimalFirmware()) validatePins(settings);
    const payload = {
      transport,
      port,
      targetIp,
      username: elements.localBuilderIpUsername.value,
      password: elements.localBuilderIpPassword.value,
      confirmedForeignFirmware: Boolean(networkTarget && networkTarget.kind !== "elma"),
      targetKind: networkTarget?.kind || "",
      chip: elements.localBuilderChip.value,
      erase: transport === "usb" && elements.localBuilderErase.checked,
      settings,
      capabilities: {
        maximum: elements.localBuilderMaximum.checked,
        webUi: elements.localBuilderWebUi.checked,
        hacs: elements.localBuilderHacs.checked,
        audio: elements.localBuilderAudio.checked,
      },
      firmwareMode: usesMinimalFirmware() ? "minimal" : "full",
    };
    const response = await api("/api/builder/jobs", { method: "POST", body: JSON.stringify(payload) });
    jobId = response.jobId;
    cancelRequested = false;
    setBusy(true);
    setProgress(1, "Build queued");
    poll();
  }

  async function cancel() {
    if (!jobId || cancelRequested) return;
    cancelRequested = true;
    setBusy(true);
    setProgress(Number.parseFloat(elements.localBuilderProgressFill.style.width) || 0, "Cancellation requested — stopping at a safe boundary");
    await api(`/api/builder/jobs/${encodeURIComponent(jobId)}/cancel`, { method: "POST", body: "{}" });
  }

  async function primaryAction() {
    if (jobId) {
      await cancel();
      return;
    }
    await compileAndFlash();
  }

  async function initialize() {
    const runtime = new URLSearchParams(window.location.search).get("elmaRuntime");
    if (runtime !== "pc-designer" && !["localhost", "127.0.0.1", "::1", "[::1]"].includes(window.location.hostname)) return;
    let status;
    try {
      status = await api("/api/builder/status");
      active = Boolean(status.active);
    } catch {
      return;
    }
    if (!active) return;
    document.body.classList.add("local-builder-mode");
    adaptInterfaceForPc(status.version || "");
    elements.localBuilderPanel.hidden = false;
    if (elements.runtimeFirmwarePanel) elements.runtimeFirmwarePanel.hidden = true;
    elements.localBuilderBadge.textContent = `Local compiler ${status.version || ""}`.trim();
    if (new URLSearchParams(window.location.search).get("elmaView") === "flash") {
      const heading = elements.localBuilderPanel.querySelector(".local-builder-heading h3");
      const copy = elements.localBuilderPanel.querySelector(".local-builder-heading p");
      if (heading) heading.textContent = "Flash USB Device";
      if (copy) copy.textContent = "Uses the complete configuration saved in Device Designer. Select the connected target, then compile and flash.";
    }
    await refreshPorts();
    elements.localBuilderRefreshPorts.addEventListener("click", () => refreshPorts().catch((error) => setMessage(error.message, true)));
    elements.localBuilderScanIpDevices.addEventListener("click", () => scanIpDevices().catch((error) => setMessage(error.message, true)));
    elements.localBuilderIpDevice.addEventListener("change", () => {
      if (elements.localBuilderIpDevice.value) elements.localBuilderIpAddress.value = elements.localBuilderIpDevice.value;
    });
    elements.localBuilderTransport.addEventListener("change", () => {
      if (usesMinimalFirmware() && !usesIpTransport()) {
        elements.localBuilderTransport.value = "ip";
        setMessage("Minimal firmware is OTA-only so the existing partition table and NVS configuration cannot be erased.");
      }
      updateTransportUi();
    });
    elements.localBuilderFirmwareMode.addEventListener("change", updateFirmwareModeUi);
    elements.localBuilderChip.addEventListener("change", applyTargetPolicy);
    const board = document.getElementById("gpioBoardSelector");
    const autodetect = document.getElementById("gpioBoardAutodetect");
    const syncManualBoard = () => {
      if (board && !autodetect?.checked) elements.localBuilderChip.value = boardChipFamily(board.value);
      applyTargetPolicy();
    };
    board?.addEventListener("change", syncManualBoard);
    autodetect?.addEventListener("change", syncManualBoard);
    elements.localBuilderMaximum.addEventListener("change", applyTargetPolicy);
    elements.localBuilderCompileFlash.addEventListener("click", () => primaryAction().catch((error) => {
      cancelRequested = false;
      if (!jobId) setBusy(false);
      setMessage(error.message, true);
    }));
    applyTargetPolicy();
    updateTransportUi();
    updateFirmwareModeUi();
  }

  return { initialize, refreshPorts };
}
