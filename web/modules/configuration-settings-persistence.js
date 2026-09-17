import { normalizeWifiPower, renderWifiPowerValues } from "./wifi-power-controls.js";
import {voltageDividerSettings} from './voltage-divider.js';
import { restoreLegacyPeripheralProfiles } from "./legacy-peripheral-profiles.js";
import { applyMqttDefaults } from "./mqtt-defaults.js";

export function createConfigurationSettingsPersistenceModule({
  state,
  elements,
  defaultEsp32S3AudioPins,
  defaultSdGpioPins,
  effectSelectConfig,
  settingsAutosaveDelayMs,
  request,
  delay,
  handleError,
  setMessage,
  toast,
  normalizeDecimalField,
  parseDecimalFieldValue,
  currentBatteryCalibrationMultiplier,
  choosePreferredOledPins,
  effectVolumePercentValue,
  effectVolumeSetting,
  populateAudioI2sPinOptions,
  renderEqualizerPreset,
  populateSdPinOptions,
  populateStatusLedPinOptions,
  populateOledPinOptions,
  populateWapeTriggerPinOptions,
  populateButtonActionSelects,
  populateBatteryAdcPinOptions,
  syncPeripheralProfilesFromSettings,
  applyPeripheralProfileSelectionsState,
  syncPageSections,
  normalizeUiSettings,
  cloneSettingsObject,
  peripheralDiagramPositionsStorageKey,
  validateSettingsPayload,
  applyPeripheralProfileSelections,
  currentSettingsSnapshot,
  loadStatus,
  renderEffectFileOptions,
  clearEffectFileOptionsCache,
  sdSettingsChanged,
  activeTabName,
  refreshExternalStorageTab,
  rerenderStorageManager,
  refreshStorageManager,
  maybeRefreshVisibleStorageTab,
  activateTabByName,
  setFirmwareAuthorLink,
  updateGpioBoardImage,
  isGpioUiInteracting,
  renderGpioOverview,
  renderPeripheralAudioOutputControls,
  renderPeripheralAudioInControls,
  renderPeripheralDisplayControls,
  renderPeripheralSensorControls,
  renderPeripheralInputControls,
  renderPeripheralControlControls,
  renderPeripheralExpansionControls,
  renderPeripheralDiagram,
  renderPeripheralCommunicationControls,
  savePeripheralProfileSelections,
  savePeripheralHelperBindings,
  resetWifiNetworkList,
  restoreGpioBoardPreferences,
  updateGpioBoardSelectorMode,
  applyBackupUiState,
}) {
  function settingsSubsetMatches(actual, expected) {
    if (expected === null || typeof expected !== "object") {
      if (typeof expected === "number") {
        return Math.abs(Number(actual ?? 0) - expected) < 0.0005;
      }
      if (typeof expected === "boolean") {
        return Boolean(actual) === expected;
      }
      return String(actual ?? "") === String(expected ?? "");
    }

    return Object.entries(expected).every(([key, value]) => settingsSubsetMatches(actual?.[key], value));
  }

  async function refreshSettingsAfterSave(expectedSettings, attempts = 8, delayMs = 250) {
    for (let attempt = 0; attempt < attempts; attempt += 1) {
      const loadedSettings = await request("/api/settings");
      if (settingsSubsetMatches(loadedSettings, expectedSettings)) {
        state.settings = loadedSettings;
        fillForm(loadedSettings);
        return true;
      }

      await delay(delayMs);
    }

    return false;
  }

  async function saveDirtyBatteryMeasurement() {
    state.settingsDirty = true;
    await saveSettings({ silent: true });
  }

  function loadPeripheralDiagramPositions() {
    const saved=normalizeUiSettings(state.settings?.ui).peripheralDiagramPositions;
    if(Object.keys(saved||{}).length)return saved;
    try {
      const local = normalizeUiSettings({
        peripheralDiagramPositions: window.localStorage.getItem(peripheralDiagramPositionsStorageKey) || "{}",
      }).peripheralDiagramPositions;
      if (Object.keys(local).length) {
        return local;
      }
    } catch {
    }

    return normalizeUiSettings(state.settings?.ui).peripheralDiagramPositions;
  }

  function savePeripheralDiagramPositions(positions, options = {}) {
    const { persist = true, immediate = false } = options;
    const ui = normalizeUiSettings(state.settings?.ui);
    ui.peripheralDiagramPositions = cloneSettingsObject(positions || {}) || {};
    if (state.settings) {
      state.settings.ui = ui;
    }
    try {
      window.localStorage.setItem(
        peripheralDiagramPositionsStorageKey,
        JSON.stringify(ui.peripheralDiagramPositions),
      );
    } catch {
    }
    if (persist && !state.settingsLoading) {
      if (immediate) {
        return saveSettings({ silent: true });
      }
      queueSettingsSave(0);
    }
    return Promise.resolve();
  }

  function fillForm(data) {
    state.settingsLoading = true;
    applyMqttDefaults(data);
    data.wifi ||= {};
    data.wifi.staTxPowerDbm = normalizeWifiPower(data.wifi.staTxPowerDbm);
    data.wifi.apTxPowerDbm = normalizeWifiPower(data.wifi.apTxPowerDbm);
    data.audio ||= {};
    data.audio.lastPlayback ||= {};
    if (data.audio.rememberLastPlayed === undefined) {
      data.audio.rememberLastPlayed = true;
    }
    data.sd ||= {};
    data.ui = normalizeUiSettings(data.ui);
    data.ui.peripheralProfiles = restoreLegacyPeripheralProfiles(data);
    state.peripheralDiagramPositions = cloneSettingsObject(loadPeripheralDiagramPositions()) || {};
    applyPeripheralProfileSelectionsState(data.ui.peripheralProfiles);
    state.peripheralHelperBindings = cloneSettingsObject(data.ui.peripheralHelperBindings) || {};
    savePeripheralProfileSelections();
    savePeripheralHelperBindings();
    if (data.audio.enabled === undefined) {
      data.audio.enabled = true;
    }
    populateAudioI2sPinOptions(data);
    populateSdPinOptions(data);
    populateStatusLedPinOptions(data);
    populateOledPinOptions(data);
    populateWapeTriggerPinOptions(data);
    populateButtonActionSelects();
    for (const [section, sectionValue] of Object.entries(data)) {
      if (sectionValue === null || typeof sectionValue !== "object") {
        continue;
      }
      for (const [key, value] of Object.entries(sectionValue)) {
        const field = elements.settingsForm.elements.namedItem(`${section}.${key}`);
        if (!field || field === document.activeElement) {
          continue;
        }
        if (field.type === "checkbox") {
          field.checked = Boolean(value);
        } else {
          field.value = value ?? "";
        }
      }
    }
    if (elements.audioWsPin && data.audio?.wsPin !== undefined) {
      elements.audioWsPin.value = String(data.audio.wsPin);
    }
    if (elements.audioBclkPin && data.audio?.bclkPin !== undefined) {
      elements.audioBclkPin.value = String(data.audio.bclkPin);
    }
    if (elements.audioDoutPin && data.audio?.doutPin !== undefined) {
      elements.audioDoutPin.value = String(data.audio.doutPin);
    }
    renderEqualizerPreset(data.audio?.equalizerPreset || "flat", [
      data.audio?.equalizerLowDb ?? 0,
      data.audio?.equalizerPresenceDb ?? 0,
      data.audio?.equalizerHighDb ?? 0,
    ]);
    if (elements.statusLedPin && data.device?.statusLedPin !== undefined) {
      elements.statusLedPin.value = String(data.device.statusLedPin);
    }
    if (elements.statusLedGreenPin && data.device?.statusLedGreenPin !== undefined) {
      elements.statusLedGreenPin.value = String(data.device.statusLedGreenPin);
    }
    if (elements.statusLedBluePin && data.device?.statusLedBluePin !== undefined) {
      elements.statusLedBluePin.value = String(data.device.statusLedBluePin);
    }
    if (elements.oledSdaPin && data.oled?.sdaPin !== undefined && [...elements.oledSdaPin.options].some((option) => option.value === String(data.oled.sdaPin))) {
      elements.oledSdaPin.value = String(data.oled.sdaPin);
    }
    if (elements.oledSclPin && data.oled?.sclPin !== undefined && [...elements.oledSclPin.options].some((option) => option.value === String(data.oled.sclPin))) {
      elements.oledSclPin.value = String(data.oled.sclPin);
    }
    if (elements.oledResetPin && data.oled?.resetPin !== undefined && [...elements.oledResetPin.options].some((option) => option.value === String(data.oled.resetPin))) {
      elements.oledResetPin.value = String(data.oled.resetPin);
    }
    if (elements.sdEnabled && data.sd?.enabled !== undefined) {
      elements.sdEnabled.checked = Boolean(data.sd.enabled);
    }
    if (elements.sdCsPin && data.sd?.csPin !== undefined && [...elements.sdCsPin.options].some((option) => option.value === String(data.sd.csPin))) {
      elements.sdCsPin.value = String(data.sd.csPin);
    }
    if (elements.sdSckPin && data.sd?.sckPin !== undefined && [...elements.sdSckPin.options].some((option) => option.value === String(data.sd.sckPin))) {
      elements.sdSckPin.value = String(data.sd.sckPin);
    }
    if (elements.sdMosiPin && data.sd?.mosiPin !== undefined && [...elements.sdMosiPin.options].some((option) => option.value === String(data.sd.mosiPin))) {
      elements.sdMosiPin.value = String(data.sd.mosiPin);
    }
    if (elements.sdMisoPin && data.sd?.misoPin !== undefined && [...elements.sdMisoPin.options].some((option) => option.value === String(data.sd.misoPin))) {
      elements.sdMisoPin.value = String(data.sd.misoPin);
    }
    populateBatteryAdcPinOptions(data);
    if (elements.batteryAdcPin && data.battery?.adcPin !== undefined) {
      elements.batteryAdcPin.value = String(data.battery.adcPin);
    }
    if (elements.audioMutedToggle && data.device?.audioMuted !== undefined) {
      elements.audioMutedToggle.checked = Boolean(data.device.audioMuted);
    }
    if (elements.volumeSlider && data.device?.savedVolumePercent !== undefined) {
      elements.volumeSlider.value = String(data.device.savedVolumePercent);
    }
    if (elements.playUrl && document.activeElement !== elements.playUrl) {
      elements.playUrl.value = String(data.audio?.lastPlayback?.url || elements.playUrl.value || "");
    }
    if (elements.playLabel && document.activeElement !== elements.playLabel) {
      elements.playLabel.value = String(data.audio?.lastPlayback?.label || elements.playLabel.value || "");
    }
    if (elements.playType && document.activeElement !== elements.playType) {
      const savedPlayType = String(data.audio?.lastPlayback?.type || "").trim().toLowerCase();
      if (savedPlayType && [...elements.playType.options].some((option) => option.value === savedPlayType)) {
        elements.playType.value = savedPlayType;
      }
    }
    populateButtonActionSelects();
    syncPeripheralProfilesFromSettings(data);
    syncPageSections(data);
    renderWifiPowerValues();
    state.settingsDirty = false;
    state.settingsLoading = false;
  }

  function collectForm() {
    const payload = {};
    for (const field of elements.settingsForm.elements) {
      if (!field.name) {
        continue;
      }
      normalizeDecimalField(field);
      const [section, key] = field.name.split(".");
      payload[section] ||= {};
      payload[section][key] = field.type === "checkbox" ? field.checked : field.value;
    }

    payload.device ||= {};
    payload.audio ||= {};
    payload.mqtt ||= {};
    payload.battery ||= {};
    payload.oled ||= {};
    payload.sd ||= {};
    payload.effects ||= {};
    payload.wifi ||= {};
    payload.wifi.staTxPowerDbm = normalizeWifiPower(payload.wifi.staTxPowerDbm);
    payload.wifi.apTxPowerDbm = normalizeWifiPower(payload.wifi.apTxPowerDbm);

    payload.mqtt.port = Number(payload.mqtt.port || 1883);
    payload.device.savedVolumePercent = Number(elements.volumeSlider?.value || payload.device.savedVolumePercent || 5);
    payload.device.statusLedPin = Number(elements.statusLedPin?.value || payload.device.statusLedPin || state.settings?.device?.statusLedPin || 0);
    payload.device.statusLedGreenPin = Number(elements.statusLedGreenPin?.value || payload.device.statusLedGreenPin || state.settings?.device?.statusLedGreenPin || 0);
    payload.device.statusLedBluePin = Number(elements.statusLedBluePin?.value || payload.device.statusLedBluePin || state.settings?.device?.statusLedBluePin || 0);
    payload.device.audioMuted = Boolean(elements.audioMutedToggle?.checked ?? payload.device.audioMuted ?? true);
    payload.device.lowBatterySleepThresholdPercent = Number(payload.device.lowBatterySleepThresholdPercent || 20);
    payload.device.lowBatteryWakeIntervalMinutes = Number(payload.device.lowBatteryWakeIntervalMinutes || 0);
    payload.audio.enabled = Boolean(payload.audio.enabled ?? state.settings?.audio?.enabled ?? true);
    payload.audio.doutPin = Number(elements.audioDoutPin?.value || payload.audio.doutPin || defaultEsp32S3AudioPins.dout);
    payload.audio.wsPin = Number(elements.audioWsPin?.value || payload.audio.wsPin || defaultEsp32S3AudioPins.ws);
    payload.audio.bclkPin = Number(elements.audioBclkPin?.value || payload.audio.bclkPin || defaultEsp32S3AudioPins.bclk);
    const batteryDividerSelected = Array.isArray(state.peripheralSensorProfiles)
      && state.peripheralSensorProfiles.some((profile) => String(profile || "").trim().toLowerCase() === "battery-voltage-divider-220k");
    payload.battery.adcPin = batteryDividerSelected
      ? Number(elements.batteryAdcPin?.value || payload.battery.adcPin || state.settings?.battery?.adcPin || 0)
      : 0;
    payload.battery.measuredVoltage = parseDecimalFieldValue(elements.batteryMeasuredVoltage, payload.battery.measuredVoltage || 0);
    payload.battery.calibrationMultiplier = currentBatteryCalibrationMultiplier();
    Object.assign(payload.battery,voltageDividerSettings({...state.settings?.battery,...payload.battery}));
    payload.battery.updateIntervalMs = Number(payload.battery.updateIntervalMs || 10000);
    payload.battery.movingAverageWindowSize = Number(payload.battery.movingAverageWindowSize || 10);
    payload.oled.i2cAddress = Number(payload.oled.i2cAddress || 60);
    payload.oled.width = Number(payload.oled.width || 128);
    payload.oled.height = Number(payload.oled.height || 64);
    payload.oled.rotation = Number(payload.oled.rotation || 0);
    const preferredOledPins = choosePreferredOledPins(state.settings);
    payload.oled.sdaPin = Number(elements.oledSdaPin?.value || payload.oled.sdaPin || preferredOledPins.sda);
    payload.oled.sclPin = Number(elements.oledSclPin?.value || payload.oled.sclPin || preferredOledPins.scl);
    payload.oled.resetPin = Number(elements.oledResetPin?.value || payload.oled.resetPin || -1);
    payload.oled.dimTimeoutSeconds = Number(payload.oled.dimTimeoutSeconds || 0);
    payload.oled.wapeTriggerPin = Number(elements.wapeTriggerPin?.value || payload.oled.wapeTriggerPin || 0);
    payload.oled.displayType = String(elements.displayType?.value || payload.oled.displayType || "oled");
    payload.oled.wapeTriggerEvent = String(elements.wapeTriggerEvent?.value || payload.oled.wapeTriggerEvent || "play_start");
    payload.sd.enabled = Boolean(elements.sdEnabled?.checked ?? payload.sd.enabled ?? state.settings?.sd?.enabled ?? true);
    payload.sd.csPin = Number(elements.sdCsPin?.value || payload.sd.csPin || defaultSdGpioPins.cs);
    payload.sd.sckPin = Number(elements.sdSckPin?.value || payload.sd.sckPin || defaultSdGpioPins.sck);
    payload.sd.mosiPin = Number(elements.sdMosiPin?.value || payload.sd.mosiPin || defaultSdGpioPins.mosi);
    payload.sd.misoPin = Number(elements.sdMisoPin?.value || payload.sd.misoPin || defaultSdGpioPins.miso);
    for (const config of effectSelectConfig) {
      const effectElement = elements[config.id];
      const volumeElement = elements[config.volumeId];
      payload.effects[config.field] = String(effectElement?.value || effectElement?.dataset?.savedEffectValue || payload.effects[config.field] || "");
      payload.effects[config.volumeField] = effectVolumePercentValue(volumeElement?.value, effectVolumeSetting(config));
    }
    return payload;
  }

  function queueSettingsSave(delayMs = settingsAutosaveDelayMs) {
    if (state.settingsLoading) {
      return;
    }
    state.settingsDirty = true;
    state.settingsEditRevision = Number(state.settingsEditRevision || 0) + 1;
    // Android may suspend WebView timers immediately when the app backgrounds.
    // Commit the complete peripheral snapshot before the deferred UI autosave.
    if (document.body.classList.contains("android-designer") && window.elmaPersistAndroidDraft) {
      try { window.elmaPersistAndroidDraft(currentSettingsSnapshot()); }
      catch (error) { handleError(error); }
    }
    if (state.settingsSaveTimer) {
      window.clearTimeout(state.settingsSaveTimer);
    }
    state.settingsSaveTimer = window.setTimeout(() => {
      saveSettings({ silent: true }).catch(handleError);
    }, delayMs);
  }

  async function awaitPendingSettingsSave() {
    if (!state.settingsSavePromise) {
      return;
    }
    await state.settingsSavePromise;
  }

  async function applySettingsPayload(submittedSettings, options = {}) {
    const {
      silent = false,
      successMessage = silent ? "Settings auto-saved" : "Settings saved",
      toastMessage = silent ? "" : "Settings saved",
    } = options;

    const previousSettings = state.settings;
    const submittedRevision = Number(state.settingsEditRevision || 0);
    // The PC Designer already owns the authoritative in-memory form state.
    // Rebuilding every control and polling the loopback backend after each
    // keystroke/change interrupts open selects and makes clicks feel lost.
    const lightweightAutosave = silent && document.body.classList.contains("local-builder-mode");
    submittedSettings.sd ||= {};
    applyPeripheralProfileSelections(submittedSettings);
    submittedSettings.ui = normalizeUiSettings(submittedSettings.ui);
    validateSettingsPayload(submittedSettings);

    if (!silent) {
      setMessage("Saving settings...");
    }

    state.settingsSaving = true;
    const savePromise = (async () => {
      try {
        await request("/api/settings", {
          method: "POST",
          body: JSON.stringify(submittedSettings),
        });

        state.settings = submittedSettings;
        state.batteryMeasuredVoltageInput = submittedSettings.battery?.measuredVoltage > 0
          ? Number(submittedSettings.battery.measuredVoltage).toFixed(3)
          : "";
        if (Number(state.settingsEditRevision || 0) === submittedRevision) {
          state.settingsDirty = false;
        }
        if (!lightweightAutosave) {
          fillForm(submittedSettings);
          applyBackupUiState({
            gpioBoard: {
              autodetect: submittedSettings.ui.gpioBoardAutodetect,
              selectedBoard: submittedSettings.ui.gpioBoardSelection,
            },
            peripheralDiagramPositions: submittedSettings.ui.peripheralDiagramPositions,
          });
          renderEffectFileOptions(submittedSettings);
          setMessage(successMessage);
          if (toastMessage) {
            toast(toastMessage);
          }

          loadStatus().catch((error) => console.error(error));
          refreshSettingsAfterSave(submittedSettings).catch((error) => console.error(error));
          if (sdSettingsChanged(previousSettings, submittedSettings)) {
            clearEffectFileOptionsCache();
            renderEffectFileOptions(submittedSettings);
          }
          if (activeTabName() === "storage-external") {
            if (state.activeStorageTarget === "sd") {
              refreshExternalStorageTab(state.currentStoragePathByTarget.sd || "/").catch((error) => console.error(error));
            } else {
              rerenderStorageManager("sd");
            }
          }
        }
      } finally {
        state.settingsSaving = false;
        state.settingsSavePromise = null;
      }
    })();

    state.settingsSavePromise = savePromise;
    return savePromise;
  }

  async function loadSettings() {
    const loadedSettings = await request("/api/settings");
    loadedSettings.audio ||= {};
    loadedSettings.sd ||= {};
    if (loadedSettings.audio.enabled === undefined) {
      loadedSettings.audio.enabled = true;
    }
    if (loadedSettings.audio.rememberLastPlayed === undefined) {
      loadedSettings.audio.rememberLastPlayed = true;
    }
    loadedSettings.ui = normalizeUiSettings(loadedSettings.ui);
    if (state.settingsDirty || state.settingsSaving) {
      return;
    }
    state.settings = loadedSettings;
    state.peripheralDiagramPositions = cloneSettingsObject(state.settings.ui.peripheralDiagramPositions) || {};
    restoreGpioBoardPreferences();
    updateGpioBoardSelectorMode(state.status, { force: true });
    state.batteryMeasuredVoltageInput = Number(state.settings?.battery?.measuredVoltage || 0) > 0
      ? Number(state.settings.battery.measuredVoltage).toFixed(3)
      : "";
    fillForm(state.settings);
    renderPeripheralAudioOutputControls();
    renderPeripheralAudioInControls();
    renderPeripheralDisplayControls();
    renderPeripheralSensorControls();
    renderPeripheralInputControls();
    renderPeripheralControlControls();
    renderPeripheralExpansionControls();
    renderPeripheralCommunicationControls();
    if (state.settings?.usingSavedSettings === false) {
      activateTabByName("gpio");
    }
    setFirmwareAuthorLink(state.settings);
    updateGpioBoardImage();
    renderPeripheralDiagram();
    if (!isGpioUiInteracting()) {
      renderGpioOverview();
    }
    resetWifiNetworkList();
    maybeRefreshVisibleStorageTab();
  }

  async function saveSettings(options = {}) {
    const { silent = false } = options;
    if (state.settingsLoading) {
      return;
    }
    if (state.settingsSaving) {
      await awaitPendingSettingsSave();
      if (state.settingsDirty) {
        return saveSettings(options);
      }
      return;
    }
    if (state.settingsSaveTimer) {
      window.clearTimeout(state.settingsSaveTimer);
      state.settingsSaveTimer = null;
    }
    normalizeDecimalField(elements.batteryMeasuredVoltage);
    const submittedSettings = currentSettingsSnapshot();
    await applySettingsPayload(submittedSettings, {
      silent,
      successMessage: silent ? "Settings auto-saved" : "Settings saved",
      toastMessage: silent ? "" : "Settings saved",
    });
  }

  return {
    settingsSubsetMatches,
    refreshSettingsAfterSave,
    saveDirtyBatteryMeasurement,
    loadPeripheralDiagramPositions,
    savePeripheralDiagramPositions,
    fillForm,
    collectForm,
    queueSettingsSave,
    awaitPendingSettingsSave,
    applySettingsPayload,
    loadSettings,
    saveSettings,
  };
}
