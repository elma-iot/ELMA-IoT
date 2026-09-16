import {voltageDividerSettings,resistorLabel} from './voltage-divider.js';
export function createBatteryTab({
  state,
  elements,
  batteryDividerSensorProfile,
  parseDecimalFieldValue,
  normalizeDecimalField,
  saveSettings,
  handleError,
  queueSettingsSave,
  populateSdPinOptions,
  populateOledPinOptions,
  populateWapeTriggerPinOptions,
  ensureBatteryDividerSensorSelection,
  renderPeripheralSensorControls,
  syncGpioMappingControls,
  savePeripheralProfileSelections,
}) {
  function syncBatteryDividerSensorSelection() {
    const adcPin = Number(elements.batteryAdcPin?.value || state.settings?.battery?.adcPin || 0);
    const liveProfiles = elements.peripheralSensorsList
      ? Array.from(elements.peripheralSensorsList.querySelectorAll("select[data-peripheral-sensor-index]"))
          .map((select) => String(select.value || "none").trim() || "none")
      : [];
    const profiles = liveProfiles.length
      ? liveProfiles
      : (Array.isArray(state.peripheralSensorProfiles) ? [...state.peripheralSensorProfiles] : ["none"]);
    const normalizedProfiles = profiles.length ? profiles : ["none"];
    const hasBatteryDivider = normalizedProfiles.some((profile) => String(profile || "").trim().toLowerCase() === batteryDividerSensorProfile);

    if (adcPin <= 0) {
      if (!hasBatteryDivider) {
        state.peripheralSensorProfiles = normalizedProfiles;
        return false;
      }
      state.peripheralSensorProfiles = normalizedProfiles
        .map((profile) => String(profile || "").trim().toLowerCase() === batteryDividerSensorProfile ? "none" : profile);
      renderPeripheralSensorControls();
      savePeripheralProfileSelections();
      return true;
    }

    if (hasBatteryDivider) {
      state.peripheralSensorProfiles = normalizedProfiles;
      return false;
    }
    const nextProfiles = [...normalizedProfiles];
    const availableIndex = nextProfiles.findIndex((profile) => String(profile || "none").trim().toLowerCase() === "none");
    nextProfiles[availableIndex >= 0 ? availableIndex : 0] = batteryDividerSensorProfile;
    state.peripheralSensorProfiles = nextProfiles;
    renderPeripheralSensorControls();
    savePeripheralProfileSelections();
    return true;
  }

  function currentBatteryCalibrationMultiplier() {
    const measuredVoltage = parseDecimalFieldValue(elements.batteryMeasuredVoltage, state.settings?.battery?.measuredVoltage || 0);
    const rawAdcVoltage = Number(state.status?.battery?.rawAdcVoltage || 0);
    const savedMultiplierField = elements.settingsForm.elements.namedItem("battery.calibrationMultiplier");
    const savedMultiplier = Number(savedMultiplierField?.value || 0);

    if (measuredVoltage > 0 && rawAdcVoltage > 0) {
      return measuredVoltage / rawAdcVoltage;
    }
    return savedMultiplier || Number(state.settings?.battery?.calibrationMultiplier || 0) || 2.0;
  }

  function updateDerivedBatteryCalibration() {
    if (!elements.batteryDerivedMultiplier) {
      return;
    }
    const rawAdcVoltage = Number(state.status?.battery?.rawAdcVoltage || 0);
    const measuredVoltage = parseDecimalFieldValue(elements.batteryMeasuredVoltage, state.settings?.battery?.measuredVoltage || 0);
    if (measuredVoltage > 0 && rawAdcVoltage > 0) {
      elements.batteryDerivedMultiplier.textContent = currentBatteryCalibrationMultiplier().toFixed(3);
      return;
    }
    const savedMultiplierField = elements.settingsForm.elements.namedItem("battery.calibrationMultiplier");
    const savedMultiplier = Number(savedMultiplierField?.value || 0);
    elements.batteryDerivedMultiplier.textContent = savedMultiplier > 0 ? savedMultiplier.toFixed(3) : "-";
  }

  function updateLowBatterySleepUi() {
    const enabled = Boolean(elements.lowBatterySleepToggle?.checked);
    const threshold = Number(elements.lowBatterySleepThreshold?.value || state.settings?.device?.lowBatterySleepThresholdPercent || 20);
    if (elements.lowBatterySleepThresholdValue) {
      elements.lowBatterySleepThresholdValue.textContent = `${threshold}%`;
    }
    if (elements.lowBatterySleepThreshold) {
      elements.lowBatterySleepThreshold.disabled = !enabled;
    }
    if (elements.lowBatteryWakeIntervalMinutes) {
      elements.lowBatteryWakeIntervalMinutes.disabled = !enabled;
    }
  }

  function updateBatteryUi() {
    const adcPin = Number(elements.batteryAdcPin?.value || state.settings?.battery?.adcPin || 0);
    const chargingSensePin = Number(state.settings?.battery?.chargingSensePin || 0);
    const divider = voltageDividerSettings(state.settings?.battery);
    const exampleSuffix = adcPin > 0
      ? ` Example Li-ion divider for GPIO${adcPin}: BAT+ --- ${resistorLabel(divider.dividerR1Ohms)} - GPIO${adcPin} - ${resistorLabel(divider.dividerR2Ohms)} ---- GND.`
      : " Select the Battery Voltage Divider sensor and choose an ADC-capable GPIO to enable battery reading.";
    if (elements.batteryPinSummary) {
      elements.batteryPinSummary.textContent = adcPin > 0 ? `GPIO${adcPin}` : "-";
    }
    if (elements.chargingSenseSummary) {
      const chargingState = state.status?.battery?.charging ? "Charging" : "Idle";
      elements.chargingSenseSummary.textContent = chargingSensePin > 0
        ? `GPIO${chargingSensePin} sense • ${chargingState}`
        : (adcPin > 0 ? `GPIO${adcPin} trend • ${chargingState}` : chargingState);
    }
    if (elements.batteryNote) {
      elements.batteryNote.textContent = adcPin > 0
        ? `Measure the real voltage with a multimeter, enter it here, and save. The UI converts that value into the stored calibration multiplier using the live raw ADC voltage on GPIO${adcPin}.${exampleSuffix}`
        : `Battery voltage reading is disabled until a Battery Voltage Divider sensor is selected and an ADC-capable GPIO is assigned.${exampleSuffix}`;
    }
  }

  function syncBatteryPage(settings = state.settings) {
    const derivedMeasuredVoltage = settings?.battery?.calibrationMultiplier && state.status?.battery?.rawAdcVoltage
      ? settings.battery.calibrationMultiplier * state.status.battery.rawAdcVoltage
      : "";
    const savedMeasuredVoltage = Number(settings?.battery?.measuredVoltage || 0);
    const measuredVoltage = state.batteryMeasuredVoltageInput
      || (savedMeasuredVoltage > 0 ? savedMeasuredVoltage.toFixed(3) : "")
      || "4.2";

    state.batteryMeasuredVoltageInput = measuredVoltage;
    if (elements.batteryMeasuredVoltage) {
      elements.batteryMeasuredVoltage.value = measuredVoltage;
    }

    updateDerivedBatteryCalibration();
    updateLowBatterySleepUi();
    updateBatteryUi();
  }

  function bindEvents() {
    elements.batteryAdcPin?.addEventListener("change", () => {
      if (Number(elements.batteryAdcPin?.value || 0) > 0) {
        ensureBatteryDividerSensorSelection();
      }
      syncBatteryDividerSensorSelection();
      populateSdPinOptions();
      populateOledPinOptions();
      populateWapeTriggerPinOptions();
      updateBatteryUi();
      syncGpioMappingControls();
      queueSettingsSave(0);
    });

    elements.lowBatterySleepToggle?.addEventListener("change", () => {
      updateLowBatterySleepUi();
      queueSettingsSave(150);
    });

    elements.lowBatterySleepThreshold?.addEventListener("input", () => {
      updateLowBatterySleepUi();
    });

    elements.batteryMeasuredVoltage?.addEventListener("input", (event) => {
      normalizeDecimalField(event.target);
      state.batteryMeasuredVoltageInput = event.target.value;
      updateDerivedBatteryCalibration();
      queueSettingsSave();
    });

    elements.batteryMeasuredVoltage?.addEventListener("blur", (event) => {
      normalizeDecimalField(event.target);
      state.batteryMeasuredVoltageInput = event.target.value;
      updateDerivedBatteryCalibration();
      if (state.settingsDirty) {
        saveSettings({ silent: true }).catch(handleError);
      }
    });

    elements.batteryMeasuredVoltage?.addEventListener("keydown", (event) => {
      if (event.key !== "Enter") {
        return;
      }
      event.preventDefault();
      normalizeDecimalField(event.target);
      state.batteryMeasuredVoltageInput = event.target.value;
      updateDerivedBatteryCalibration();
      state.settingsDirty = true;
      saveSettings({ silent: true }).catch(handleError);
    });
  }

  return {
    syncBatteryPage,
    updateLowBatterySleepUi,
    updateBatteryUi,
    currentBatteryCalibrationMultiplier,
    updateDerivedBatteryCalibration,
    bindEvents,
  };
}
