export function createStatusRenderModule({
  state,
  elements,
  escapeHtml,
  isPlaybackActive,
  currentPlaybackHeroTitle,
  updatePlaybackHeroControls,
  storagePlaybackRef,
  advanceStoragePreviewTrack,
  handleError,
  activeTabName,
  refreshExternalStorageTab,
  loadEffectFileOptions,
  updateGpioBoardSelectorMode,
  updateStorageAvailabilityUi,
  populateStatusLedPinOptions,
  populateSdPinOptions,
  populateBatteryAdcPinOptions,
  populateWapeTriggerPinOptions,
  maybeRedirectToStationIp,
  renderInfoStatus,
  setCurrentFirmwareVersion,
  setFirmwareAuthorLink,
  updateDerivedBatteryCalibration,
  updatePlaybackActionButton,
  updateAudioUiState,
  setPill,
  renderHardwareSummary,
  renderDeviceResources,
  maybeRefreshVisibleStorageTab,
  isGpioUiInteracting,
  isPeripheralUiInteracting,
  renderGpioOverview,
  renderPeripheralDiagram,
  renderMotorTab,
  updateStoragePreviewProgressUi,
  showUpdateAvailablePopup,
  startFirmwareProgressPolling,
  stopFirmwareProgressPolling,
  beginFirmwareReconnectReload,
  setMqttConnectStatus,
  namedField,
  setScanStatus,
  updateWifiActionButton,
  updateMqttActionButton,
  updateStoragePreviewPlaybackControls,
  updateStorageFolderPlaybackStatus,
  syncStoragePlaybackFromStatus,
  populateButtonActionSelects,
  renderOledPreview,
  updateStorageVolumeMeter,
  updateStorageMeter,
  formatPlaybackClock,
}) {
  let batteryHeroSignature = "";
  let wifiHeroSignature = "";
  let pinOptionsSignature = "";
  function estimateBatteryPercent(voltage) {
    const numericVoltage = Number(voltage || 0);
    if (!Number.isFinite(numericVoltage) || numericVoltage <= 0) {
      return 0;
    }
    const percent = Math.round(((numericVoltage - 3.2) / (4.2 - 3.2)) * 100);
    return Math.max(0, Math.min(100, percent));
  }

  function batteryLevelClass(percent) {
    if (percent >= 75) {
      return "high";
    }
    if (percent >= 45) {
      return "mid";
    }
    if (percent >= 20) {
      return "low";
    }
    return "critical";
  }

  function wifiSignalState(rssi, connected) {
    if (!connected) {
      return { level: 0, label: "Offline", tone: "weak" };
    }

    const numericRssi = Number(rssi || 0);
    if (numericRssi >= -55) {
      return { level: 4, label: "Excellent", tone: "excellent" };
    }
    if (numericRssi >= -67) {
      return { level: 3, label: "Good", tone: "good" };
    }
    if (numericRssi >= -75) {
      return { level: 2, label: "Fair", tone: "fair" };
    }
    return { level: 1, label: "Weak", tone: "weak" };
  }

  function renderBatteryHero(voltage, charging = false) {
    if (!elements.batteryHero) {
      return;
    }

    const numericVoltage = Number(voltage || 0);
    const isCharging = Boolean(charging);
    const signature = `${numericVoltage}|${isCharging}`;
    if (signature === batteryHeroSignature) return;
    batteryHeroSignature = signature;
    const usbPowered = numericVoltage > 4.5;
    const percent = estimateBatteryPercent(numericVoltage);
    const levelClass = batteryLevelClass(percent);
    const fillWidth = Math.max(8, percent);

    elements.batteryHero.className = "stat-value stat-value-battery";
    if (usbPowered) {
      elements.batteryHero.innerHTML = `
        <div class="battery-hero-widget battery-usb" aria-label="${isCharging ? "USB charging" : "USB power connected"}">
          <div class="usb-hero-icon" aria-hidden="true">
            <svg viewBox="0 0 24 24" focusable="false">
              <path d="M4.7 19.3 19 5"></path>
              <path d="m21 3-3 1 2 2Z"></path>
              <path d="M9.26 7.68 5 12l2 5"></path>
              <path d="m10 14 5 2 3.5-3.5"></path>
              <path d="m18 12 1-1 1 1-1 1Z"></path>
            </svg>
          </div>
          <div class="wifi-quality">${isCharging ? "USB Charging" : "USB Power"}</div>
          <div class="battery-meta">${numericVoltage.toFixed(2)} V</div>
        </div>
      `;
      return;
    }

    elements.batteryHero.innerHTML = `
      <div class="battery-hero-widget battery-${levelClass} ${isCharging ? "battery-charging" : ""}" aria-label="Battery ${percent}%${isCharging ? ", charging" : ""}">
        <div class="battery-shell">
          <div class="battery-body">
            <div class="battery-fill" style="width: ${fillWidth}%;"></div>
            <div class="battery-percent">${percent}%</div>
          </div>
          <div class="battery-terminal"></div>
        </div>
        <div class="battery-meta">${numericVoltage.toFixed(2)} V${isCharging ? " - Charging" : ""}</div>
      </div>
    `;
  }

  function renderWifiHero(connected, ipAddress, rssi) {
    if (!elements.wifiPill) {
      return;
    }

    const signal = wifiSignalState(rssi, connected);
    const signature = `${connected}|${ipAddress}|${rssi}`;
    if (signature === wifiHeroSignature) return;
    wifiHeroSignature = signature;
    const bars = Array.from({ length: 4 }, (_, index) => {
      const active = index < signal.level;
      return `<span class="wifi-bar ${active ? `active ${signal.tone}` : ""}"></span>`;
    }).join("");

    elements.wifiPill.className = "stat-value stat-value-wifi";
    elements.wifiPill.innerHTML = connected
      ? `
        <div class="wifi-hero-widget wifi-${signal.tone}">
          <div class="wifi-icon" aria-hidden="true">${bars}</div>
          <div class="wifi-quality">${signal.label}</div>
          <div class="hero-meta hero-meta-compact">${escapeHtml(ipAddress || "No IP")} - ${Number(rssi || 0)} dBm</div>
        </div>
      `
      : `
        <div class="wifi-hero-widget wifi-weak">
          <div class="wifi-icon" aria-hidden="true">${bars}</div>
          <div class="wifi-quality">AP Mode</div>
          <div class="hero-meta hero-meta-compact">${escapeHtml(ipAddress || "No IP")}</div>
        </div>
      `;
  }

  function renderPlaybackHero(status, audioMuted) {
    if (!elements.audioPill || !elements.playbackHeroMeter) {
      return;
    }

    const playback = status?.playback || {};
    const playbackState = String(playback.state || "idle").toLowerCase();
    const active = !audioMuted && isPlaybackActive(status);
    const fileManagerActive = active && String(playback.source || "") === "file-manager";
    const duration = fileManagerActive ? Math.max(0, Number(playback.durationSeconds || 0)) : 0;
    const position = fileManagerActive ? Math.min(duration, Math.max(0, Number(playback.positionSeconds || 0))) : 0;
    const title = audioMuted ? "Muted" : currentPlaybackHeroTitle(status);
    const label = fileManagerActive
      ? `${title} · ${formatPlaybackClock(position)} / ${formatPlaybackClock(duration)}`
      : title;
    const buffering = !audioMuted && playbackState === "buffering";
    const progress = fileManagerActive && duration > 0
      ? (position / duration) * 100
      : (buffering ? 38 : (active ? 100 : 0));

    elements.audioPill.className = "stat-value stat-value-playback";
    elements.playbackHeroMeter.classList.toggle("is-buffering", buffering);
    elements.playbackHeroMeter.setAttribute("aria-valuenow", String(Math.round(progress)));
    elements.playbackHeroMeter.setAttribute("aria-label", label);
    elements.playbackHeroMeter.title = label;
    updateStorageMeter(elements.playbackHeroMeter, "[data-playback-hero-text]", label, progress);
    updatePlaybackHeroControls();
  }

  function renderStatus(status) {
    const previousStatus = state.status;
    state.status = status;
    syncStoragePlaybackFromStatus(status);
    const previewRef = state.storagePreviewItem ? storagePlaybackRef(state.storagePreviewItem.path, state.storagePreviewTarget) : "";
    const currentPlaybackUrl = String(status?.playback?.url || "");
    const wasDeviceActive = Boolean(previewRef &&
      String(previousStatus?.playback?.url || "") === previewRef &&
      isPlaybackActive(previousStatus));
    const isDeviceActive = Boolean(previewRef && currentPlaybackUrl === previewRef && isPlaybackActive(status));
    state.storagePreviewPlaybackMode.previousDeviceActive = wasDeviceActive;
    state.storagePreviewPlaybackMode.deviceActive = isDeviceActive;
    const completionSequence = Number(status?.playback?.completionSequence || 0);
    const previousCompletionSequence = Number(state.storagePreviewPlaybackMode.lastCompletionSequence || 0);
    const fileManagerTrackCompleted = completionSequence > previousCompletionSequence &&
      String(status?.playback?.completedSource || "") === "file-manager" &&
      String(status?.playback?.completedUrl || "") === previewRef;
    state.storagePreviewPlaybackMode.lastCompletionSequence = Math.max(previousCompletionSequence, completionSequence);
    if (fileManagerTrackCompleted && !state.storagePreviewPlaybackMode.suppressAutoAdvance && state.storagePreviewPlaybackMode.autoplay) {
      advanceStoragePreviewTrack(1, { autoplayDevice: true, respectModes: true }).catch(handleError);
    }
    if (!isDeviceActive && state.storagePreviewPlaybackMode.suppressAutoAdvance) {
      state.storagePreviewPlaybackMode.suppressAutoAdvance = false;
    }

    const playbackWasActive = isPlaybackActive(previousStatus);
    const playbackIsActive = isPlaybackActive(status);
    if (playbackWasActive && !playbackIsActive) {
      if (state.deferredStorageReload && activeTabName() === "storage-external") {
        window.setTimeout(() => {
          refreshExternalStorageTab().catch(handleError);
        }, 50);
      }
      if (state.deferredEffectsReload && activeTabName() === "effects") {
        window.setTimeout(() => {
          loadEffectFileOptions().catch(handleError);
        }, 50);
      }
    }

    updateGpioBoardSelectorMode(status);
    updateStorageAvailabilityUi(status);
    const device = status?.device || {};
    const network = status?.network || {};
    const playback = status?.playback || {};
    const battery = status?.battery || {};
    const firmware = status?.firmware || {};
    const settings = status?.settings || {};
    const nextPinOptionsSignature = JSON.stringify([firmware, state.settings, elements.gpioBoardSelector?.value]);
    const configurationInteracting = isGpioUiInteracting() || isPeripheralUiInteracting();
    if (nextPinOptionsSignature !== pinOptionsSignature && !configurationInteracting) {
      populateStatusLedPinOptions();
      populateSdPinOptions();
      populateBatteryAdcPinOptions();
      populateWapeTriggerPinOptions();
      pinOptionsSignature = nextPinOptionsSignature;
    }
    maybeRedirectToStationIp(status);
    const ota = status.otaManager || status.ota || {};
    const wifiConnected = Boolean(network.wifiConnected);
    const mqttConnected = Boolean(network.mqttConnected);
    const savedVolumePercent = Number(state.settings?.device?.savedVolumePercent ?? playback.volumePercent ?? 0);

    elements.deviceTitle.textContent = device.friendlyName || "ESP32 Notifier";
    renderInfoStatus(status);
    setCurrentFirmwareVersion(firmware.version);
    setFirmwareAuthorLink(settings);
    elements.batteryVoltage.textContent = `${Number(battery.voltage || 0).toFixed(3)} V`;
    elements.batteryRaw.textContent = `${battery.rawAdc ?? "-"} / ${Number(battery.rawAdcVoltage || 0).toFixed(3)} V`;
    updateDerivedBatteryCalibration();
    elements.currentUrl.value = playback.url || "";
    elements.currentUrl.title = playback.url || "";
    if (document.activeElement !== elements.volumeSlider) {
      elements.volumeSlider.value = savedVolumePercent;
    }
    if (document.activeElement !== elements.storagePreviewVolumeSlider && elements.storagePreviewVolumeSlider) {
      elements.storagePreviewVolumeSlider.value = savedVolumePercent;
    }
    elements.volumeValue.textContent = `${document.activeElement === elements.volumeSlider ? elements.volumeSlider.value : savedVolumePercent}%`;
    if (elements.storagePreviewVolumeValue) {
      elements.storagePreviewVolumeValue.textContent = `${document.activeElement === elements.storagePreviewVolumeSlider ? elements.storagePreviewVolumeSlider.value : savedVolumePercent}%`;
    }
    if (document.activeElement !== elements.storageInlineVolumeSlider && elements.storageInlineVolumeSlider) {
      elements.storageInlineVolumeSlider.value = savedVolumePercent;
    }
    updateStorageVolumeMeter(document.activeElement === elements.storageInlineVolumeSlider ? elements.storageInlineVolumeSlider.value : savedVolumePercent);
    const audioMuted = Boolean(elements.audioMutedToggle?.checked);

    updatePlaybackActionButton();
    updateAudioUiState();
    updateStorageFolderPlaybackStatus(status);

    renderWifiHero(wifiConnected, network.ip || (network.apMode ? "192.168.4.1" : "No IP"), network.wifiRssi);
    setPill(elements.mqttPill, mqttConnected ? "MQTT Connected" : "MQTT Offline", mqttConnected ? "ok" : "warn");
    renderPlaybackHero(status, audioMuted);
    renderBatteryHero(battery.voltage || 0, battery.charging);
    renderHardwareSummary(status);
    renderDeviceResources(status);
    maybeRefreshVisibleStorageTab();
    if (activeTabName() === "gpio" && !configurationInteracting) {
      renderGpioOverview();
      if (!state.peripheralDiagramDrag) {
        renderPeripheralDiagram();
      }
    }
    if (activeTabName() === "motor") renderMotorTab?.();
    updateStoragePreviewProgressUi();

    const previousUpdateVersion = String(previousStatus?.ota?.latestVersion || previousStatus?.otaManager?.latestVersion || "");
    const currentUpdateVersion = String(status?.ota?.latestVersion || ota.latestVersion || "");
    if (Boolean(status?.ota?.updateAvailable) && currentUpdateVersion && currentUpdateVersion !== previousUpdateVersion) {
      showUpdateAvailablePopup(status);
    }

    elements.otaStatusLabel.textContent = ota.message || ota.lastResult || "Idle";
    elements.latestVersion.textContent = ota.latestVersion || status.ota.latestVersion || "-";
    elements.otaStatus.textContent = JSON.stringify({ ota, snapshot: status.ota }, null, 2);
    const progress = Number(ota.updateProgress || 0);
    const bytes = Number(ota.updateBytes || 0);
    const totalBytes = Number(ota.updateTotalBytes || 0);
    const phase = ota.updatePhase || "";
    elements.otaProgressFill.style.width = `${Math.max(0, Math.min(100, progress))}%`;
    if (ota.busy || progress > 0) {
      const byteLabel = totalBytes > 0 ? ` (${bytes}/${totalBytes} bytes)` : "";
      elements.otaProgressLabel.textContent = `${phase || "Update"} ${progress}%${byteLabel}`;
    } else {
      elements.otaProgressLabel.textContent = ota.updateAvailable ? "Update available" : "No pending update";
    }

    if (ota.busy) {
      startFirmwareProgressPolling();
    } else if (progress === 0) {
      stopFirmwareProgressPolling();
    }

    if (state.awaitingFirmwareReboot && !state.firmwareReloadPending) {
      const installed = status.ota?.lastResult === "installed" || /installed|restarting/i.test(String(ota.message || ""));
      if (installed) {
        beginFirmwareReconnectReload();
      }
    }

    if (state.mqttConnectInProgress) {
      if (state.mqttActionInProgress === "disconnect") {
        if (!mqttConnected) {
          setMqttConnectStatus("MQTT disconnected.");
        } else {
          setMqttConnectStatus("Disconnecting from MQTT broker...");
        }
      } else if (state.mqttActionInProgress === "rediscover") {
        setMqttConnectStatus("Republishing Home Assistant discovery...");
      } else if (mqttConnected) {
        setMqttConnectStatus(`Connected to ${state.settings?.mqtt?.host || namedField("mqtt.host")?.value || "broker"}`);
      } else if (!wifiConnected) {
        setMqttConnectStatus("Waiting for Wi-Fi before MQTT can connect...");
      } else {
        setMqttConnectStatus("Connecting to MQTT broker...");
      }
    }

    if (state.wifiConnectInProgress) {
      if (wifiConnected) {
        setScanStatus(`Connected to ${network.ssid || namedField("wifi.ssid")?.value || "Wi-Fi"}`);
      } else {
        setScanStatus("Connecting to Wi-Fi...");
      }
    }

    updateWifiActionButton();
    updateMqttActionButton();
    updateStoragePreviewPlaybackControls();
    if (activeTabName() === "oled") renderOledPreview();
  }

  return {
    renderStatus,
  };
}
