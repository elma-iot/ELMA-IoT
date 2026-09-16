export function createHardwareTab({
  state,
  elements,
  formatBytes,
  pinSummary,
  updateResourceCard,
}) {
  function renderHardwareSummary(status) {
    const hardware = status?.hardware || {};
    const system = status?.system || {};
    const settings = state.settings || {};
    const audio = settings.audio || {};
    const battery = settings.battery || {};
    const oled = settings.oled || {};
    const sd = settings.sd || {};
    const sdSystem = system.sd || {};
    const hardwareReady = Boolean(hardware.chipModel || hardware.flashSizeBytes || hardware.cpuFreqMHz);

    const boardLabel = hardwareReady ? [
      hardware.chipModel || "ESP32",
      hardware.chipRevision ? `rev ${hardware.chipRevision}` : "",
      hardware.cpuCores ? `${hardware.cpuCores} core${hardware.cpuCores === 1 ? "" : "s"}` : "",
      hardware.cpuFreqMHz ? `${hardware.cpuFreqMHz} MHz` : "",
    ].filter(Boolean).join(" • ") : "Waiting for live status";
    const cpuLabel = hardwareReady ? [
      hardware.cpuCores ? `${hardware.cpuCores} core${hardware.cpuCores === 1 ? "" : "s"}` : "-",
      hardware.cpuFreqMHz ? `${hardware.cpuFreqMHz} MHz` : "",
    ].filter(Boolean).join(" • ") : "Waiting for live status";
    const flashLabel = hardwareReady ? [
      hardware.flashSizeBytes ? formatBytes(hardware.flashSizeBytes) : "-",
      hardware.appPartitionSizeBytes ? `OTA slot ${formatBytes(hardware.appPartitionSizeBytes)}` : "",
      hardware.sketchSizeBytes ? `fw ${formatBytes(hardware.sketchSizeBytes)}` : "",
    ].filter(Boolean).join(" • ") : "Waiting for live status";
    const displayType = String(oled.displayType || "oled").toLowerCase();
    const displayLabel = displayType === "wape"
      ? `Wape • trigger ${pinSummary(oled.wapeTriggerPin || 0)}`
      : `${oled.enabled ? "OLED" : "OLED off"} • SDA ${pinSummary(oled.sdaPin)} • SCL ${pinSummary(oled.sclPin)}`;
    const audioLabel = audio.enabled === false
      ? "Disabled"
      : `WS ${pinSummary(audio.wsPin)} • BCLK ${pinSummary(audio.bclkPin)} • DIN ${pinSummary(audio.doutPin)}`;
    const batteryLabel = Number(battery.adcPin || 0) > 0 ? pinSummary(battery.adcPin) : "-";
    const sdLabel = !sd.enabled
      ? "Disabled"
      : sdSystem.mounted && Number(sdSystem.totalBytes || 0) > 0
        ? `${formatBytes(sdSystem.freeBytes || 0)} free of ${formatBytes(sdSystem.totalBytes || 0)} fs • ${Number(sdSystem.cardSizeBytes || 0) > 0 ? formatBytes(sdSystem.cardSizeBytes || 0) : "card size unknown"} card • GPIO${sd.csPin}/${sd.sckPin}/${sd.mosiPin}/${sd.misoPin}`
        : `Configured • GPIO${sd.csPin}/${sd.sckPin}/${sd.mosiPin}/${sd.misoPin}`;

    if (elements.deviceHardwareBoard) {
      elements.deviceHardwareBoard.textContent = boardLabel || "-";
    }
    if (elements.deviceHardwareCpu) {
      elements.deviceHardwareCpu.textContent = cpuLabel || "-";
    }
    if (elements.deviceHardwareFlash) {
      elements.deviceHardwareFlash.textContent = flashLabel || "-";
    }
    if (elements.deviceHardwareAudio) {
      elements.deviceHardwareAudio.textContent = audioLabel;
    }
    if (elements.deviceHardwareDisplay) {
      elements.deviceHardwareDisplay.textContent = displayLabel;
    }
    if (elements.deviceHardwareBattery) {
      elements.deviceHardwareBattery.textContent = batteryLabel;
    }
    if (elements.deviceHardwareSd) {
      elements.deviceHardwareSd.textContent = sdLabel;
    }
  }

  function renderDeviceResources(status) {
    const system = status?.system || {};
    const hardware = status?.hardware || {};
    const sram = system.sram || {};
    const psram = system.psram || {};
    const spiffs = system.spiffs || {};
    const sd = system.sd || {};
    const sdEnabled = true;
    const systemReady = Boolean(system.cpuLoadPercent || system.freeHeap || sram.totalBytes || psram.totalBytes || spiffs.totalBytes || sd.totalBytes || sdEnabled);

    if (!systemReady) {
      updateResourceCard(elements.deviceCpuLoadValue, elements.deviceCpuLoadBar, elements.deviceCpuLoadMeta, "--", 0, "Live metrics appear after the first status refresh.");
      updateResourceCard(elements.deviceCpuClockValue, elements.deviceCpuClockBar, elements.deviceCpuClockMeta, "--", 0, "Waiting for live CPU frequency telemetry.");
      updateResourceCard(elements.deviceSramValue, elements.deviceSramBar, elements.deviceSramMeta, "--", 0, "Waiting for SRAM usage from the device.");
      updateResourceCard(elements.devicePsramValue, elements.devicePsramBar, elements.devicePsramMeta, "--", 0, "Waiting for PSRAM status from the device.");
      updateResourceCard(elements.deviceSpiffsValue, elements.deviceSpiffsBar, elements.deviceSpiffsMeta, "--", 0, "Waiting for filesystem telemetry from the device.");
      updateResourceCard(elements.deviceSdValue, elements.deviceSdBar, elements.deviceSdMeta, "--", 0, "Waiting for SD card telemetry from the device.");
      updateResourceCard(elements.deviceChipTempValue, elements.deviceChipTempBar, elements.deviceChipTempMeta, "--", 0, "Waiting for chip temperature telemetry from the device.");
      updateResourceCard(elements.deviceFlashHeadroomValue, elements.deviceFlashHeadroomBar, elements.deviceFlashHeadroomMeta, "--", 0, "Waiting for flash layout telemetry from the device.");
      return;
    }

    const cpuLoadAvailable = system.cpuLoadAvailable !== false;
    const cpuLoadPercent = Math.max(0, Math.min(100, Number(system.cpuLoadPercent || 0)));
    const coreLoads = Array.isArray(system.cpuLoadCorePercent)
      ? system.cpuLoadCorePercent.map((value) => Math.max(0, Math.min(100, Number(value || 0))))
      : [];
    const cpuFrequencyMhz = Math.max(0, Number(hardware.cpuFreqMHz || 0));
    const cpuFrequencyPercent = Math.max(0, Math.min(100, (cpuFrequencyMhz / 240) * 100));
    const chipTemperatureAvailable = Boolean(system.chipTemperatureAvailable);
    const chipTemperatureC = Number(system.chipTemperatureC);
    const chipTemperaturePercent = chipTemperatureAvailable && Number.isFinite(chipTemperatureC)
      ? Math.max(0, Math.min(100, ((chipTemperatureC - 20) / 70) * 100))
      : 0;
    const flashSlotSizeBytes = Number(hardware.appPartitionSizeBytes || 0);
    const firmwareSizeBytes = Number(hardware.sketchSizeBytes || 0);
    const flashChipSizeBytes = Number(hardware.flashSizeBytes || 0);
    const flashHeadroomBytes = flashSlotSizeBytes > firmwareSizeBytes ? flashSlotSizeBytes - firmwareSizeBytes : 0;
    const flashUsedPercent = flashSlotSizeBytes > 0 ? (firmwareSizeBytes * 100) / flashSlotSizeBytes : 0;

    const coreLoadSummary = coreLoads.length
      ? coreLoads.map((value, index) => `Core ${index}: ${Math.round(value)}%`).join(" • ")
      : "Per-core sampling unavailable";
    updateResourceCard(
      elements.deviceCpuLoadValue,
      elements.deviceCpuLoadBar,
      elements.deviceCpuLoadMeta,
      cpuLoadAvailable ? `${Math.round(cpuLoadPercent)}%` : "Unavailable",
      cpuLoadAvailable ? cpuLoadPercent : 0,
      cpuLoadAvailable ? `${coreLoadSummary} • sampled from FreeRTOS scheduler ticks` : "CPU load sampling is unavailable on this target."
    );
    updateResourceCard(
      elements.deviceCpuClockValue,
      elements.deviceCpuClockBar,
      elements.deviceCpuClockMeta,
      cpuFrequencyMhz > 0 ? `${Math.round(cpuFrequencyMhz)} MHz` : "Unavailable",
      cpuFrequencyPercent,
      cpuFrequencyMhz > 0
        ? "Dynamic policy: 80 MHz idle • 160 MHz playback • 240 MHz buffering, OTA, or AP"
        : "Runtime CPU frequency telemetry is unavailable."
    );
    updateResourceCard(
      elements.deviceChipTempValue,
      elements.deviceChipTempBar,
      elements.deviceChipTempMeta,
      chipTemperatureAvailable && Number.isFinite(chipTemperatureC) ? `${chipTemperatureC.toFixed(1)} C` : "Unavailable",
      chipTemperaturePercent,
      chipTemperatureAvailable && Number.isFinite(chipTemperatureC)
        ? `${system.chipTemperatureEstimated ? "Estimated internal die temperature; classic ESP32 is not factory-calibrated by this firmware." : "Internal die temperature sensor reading."} Last valid sample ${Math.floor(Math.max(0, Number(system.chipTemperatureAgeMs) || 0) / 1000)} s ago.`
        : (system.chipTemperatureReason || "This build or target does not expose chip temperature telemetry.")
    );

    const sramUsedPercent = sram.totalBytes > 0 ? (Number(sram.usedBytes || 0) * 100) / Number(sram.totalBytes) : 0;
    updateResourceCard(
      elements.deviceSramValue,
      elements.deviceSramBar,
      elements.deviceSramMeta,
      formatBytes(sram.freeBytes || system.freeHeap || 0),
      sramUsedPercent,
      `${formatBytes(sram.usedBytes || 0)} used of ${formatBytes(sram.totalBytes || 0)} • min free ${formatBytes(system.minFreeHeapBytes || 0)}`
    );

    if (psram.available && Number(psram.totalBytes || 0) > 0) {
      const psramUsedPercent = (Number(psram.usedBytes || 0) * 100) / Number(psram.totalBytes || 0);
      updateResourceCard(
        elements.devicePsramValue,
        elements.devicePsramBar,
        elements.devicePsramMeta,
        formatBytes(psram.freeBytes || 0),
        psramUsedPercent,
        `${formatBytes(psram.usedBytes || 0)} used of ${formatBytes(psram.totalBytes || 0)}`
      );
    } else {
      updateResourceCard(elements.devicePsramValue, elements.devicePsramBar, elements.devicePsramMeta, "Not available", 0, "Firmware will keep using internal SRAM only.");
    }

    if (spiffs.available && Number(spiffs.totalBytes || 0) > 0) {
      const spiffsUsedPercent = (Number(spiffs.usedBytes || 0) * 100) / Number(spiffs.totalBytes || 0);
      const spiffsMeta = spiffs.mounted
        ? `${formatBytes(spiffs.usedBytes || 0)} used of ${formatBytes(spiffs.totalBytes || 0)}`
        : `${formatBytes(spiffs.totalBytes || 0)} filesystem partition reserved in flash • not mounted`;
      updateResourceCard(
        elements.deviceSpiffsValue,
        elements.deviceSpiffsBar,
        elements.deviceSpiffsMeta,
        spiffs.mounted ? formatBytes(spiffs.freeBytes || 0) : formatBytes(spiffs.totalBytes || 0),
        spiffs.mounted ? spiffsUsedPercent : 0,
        spiffs.mounted ? `${spiffsMeta} • click to manage files` : spiffsMeta
      );
    } else {
      updateResourceCard(elements.deviceSpiffsValue, elements.deviceSpiffsBar, elements.deviceSpiffsMeta, "Unavailable", 0, "No flash filesystem partition detected.");
    }

    if (flashSlotSizeBytes > 0) {
      updateResourceCard(
        elements.deviceFlashHeadroomValue,
        elements.deviceFlashHeadroomBar,
        elements.deviceFlashHeadroomMeta,
        formatBytes(flashHeadroomBytes),
        flashUsedPercent,
        `${formatBytes(firmwareSizeBytes)} firmware in ${formatBytes(flashSlotSizeBytes)} OTA slot • ${formatBytes(flashChipSizeBytes)} chip flash total`
      );
    } else {
      updateResourceCard(
        elements.deviceFlashHeadroomValue,
        elements.deviceFlashHeadroomBar,
        elements.deviceFlashHeadroomMeta,
        "Unavailable",
        0,
        flashChipSizeBytes > 0 ? `${formatBytes(flashChipSizeBytes)} chip flash detected • OTA slot size unknown` : "Flash layout telemetry is unavailable."
      );
    }

    if (sd.available && Number(sd.totalBytes || 0) > 0 && sd.mounted) {
      const sdUsedPercent = (Number(sd.usedBytes || 0) * 100) / Number(sd.totalBytes || 0);
      const cardLabel = Number(sd.cardSizeBytes || 0) > 0 ? ` • card ${formatBytes(sd.cardSizeBytes || 0)}` : "";
      updateResourceCard(
        elements.deviceSdValue,
        elements.deviceSdBar,
        elements.deviceSdMeta,
        formatBytes(sd.freeBytes || 0),
        sdUsedPercent,
        `${formatBytes(sd.usedBytes || 0)} used of ${formatBytes(sd.totalBytes || 0)} filesystem${cardLabel}`
      );
    } else if (sdEnabled) {
      const configuredPins = [state.settings?.sd?.csPin, state.settings?.sd?.sckPin, state.settings?.sd?.mosiPin, state.settings?.sd?.misoPin]
        .map((pin) => `GPIO${pin}`)
        .join(" / ");
      updateResourceCard(
        elements.deviceSdValue,
        elements.deviceSdBar,
        elements.deviceSdMeta,
        "Not mounted",
        0,
        `Configured for ${configuredPins} • use External Storage tab`
      );
    } else {
      updateResourceCard(elements.deviceSdValue, elements.deviceSdBar, elements.deviceSdMeta, "Disabled", 0, "SD card is not mounted or not wired.");
    }
  }

  return {
    renderHardwareSummary,
    renderDeviceResources,
  };
}
