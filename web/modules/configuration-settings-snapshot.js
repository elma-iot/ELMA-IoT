export function createConfigurationSettingsSnapshotModule({
  state,
  elements,
  isPlainObject,
  cloneSettingsObject,
  normalizeUiSettings,
  collectForm,
  normalizedPeripheralAudioProfiles,
  normalizedPeripheralAudioInProfiles,
  normalizedPeripheralDisplayProfiles,
  normalizedPeripheralSensorProfiles,
  normalizedPeripheralInputProfiles,
  normalizedPeripheralControlProfiles,
  normalizedPeripheralExpansionProfiles,
  normalizedPeripheralStorageProfiles,
  normalizedPeripheralCommunicationProfiles,
  normalizedPeripheralPowerProfiles,
}) {
  function mergeSettingsObjects(baseValue, overrideValue) {
    if (Array.isArray(overrideValue)) {
      return [...overrideValue];
    }
    if (!isPlainObject(overrideValue)) {
      return overrideValue;
    }

    const result = isPlainObject(baseValue) ? { ...baseValue } : {};
    for (const [key, value] of Object.entries(overrideValue)) {
      result[key] = isPlainObject(value)
        ? mergeSettingsObjects(result[key], value)
        : (Array.isArray(value) ? [...value] : value);
    }
    return result;
  }

  function applyPeripheralProfileSelections(snapshot) {
    snapshot.audio ||= {};
    snapshot.oled ||= {};
    snapshot.sd ||= {};
    const audioProfiles = normalizedPeripheralAudioProfiles();
    state.peripheralAudioProfiles = audioProfiles;

    const audioProfile = String(audioProfiles[0] || "none").trim().toLowerCase();
    snapshot.audio.enabled = audioProfile !== "none" && !audioProfile.includes("bluetooth") && !audioProfile.includes("buzzer");

    const displayProfiles = normalizedPeripheralDisplayProfiles();
    const displayProfile = String(displayProfiles[0] || "none").trim().toLowerCase();
    if (displayProfile === "none") {
      snapshot.oled.enabled = false;
    } else if (displayProfile === "waveshare-screen") {
      snapshot.oled.enabled = true;
      snapshot.oled.displayType = "wape";
    } else if (displayProfile === "i2c-oled") {
      snapshot.oled.enabled = true;
      snapshot.oled.displayType = "oled";
    }

    const storageProfiles = normalizedPeripheralStorageProfiles();
    snapshot.sd.enabled = String(storageProfiles[0] || "none").trim().toLowerCase() !== "none";
  }

  function currentSettingsSnapshot() {
    const baseSettings = cloneSettingsObject(state.settings || {}) || {};
    const snapshot = mergeSettingsObjects(baseSettings, collectForm());
    applyPeripheralProfileSelections(snapshot);
    const audioProfiles = normalizedPeripheralAudioProfiles();
    const audioInProfiles = normalizedPeripheralAudioInProfiles();
    const displayProfiles = normalizedPeripheralDisplayProfiles();
    const persistedUi = normalizeUiSettings(baseSettings.ui);
    snapshot.ui = normalizeUiSettings({
      gpioBoardAutodetect: Boolean(elements.gpioBoardAutodetect?.checked ?? true),
      gpioSafetyOverride: Boolean(elements.gpioSafetyOverride?.checked),
      gpioBoardSelection: String(elements.gpioBoardSelector?.value || ""),
      peripheralDiagramPositions: cloneSettingsObject(state.peripheralDiagramPositions || {}) || {},
      peripheralHelperBindings: cloneSettingsObject(state.peripheralHelperBindings || {}) || {},
      motorRuntimeConfig: cloneSettingsObject(persistedUi.motorRuntimeConfig) || {},
      peripheralProfiles: {
        audioProfile: String(audioProfiles[0] || "none"),
        audioProfiles: [...audioProfiles],
        audioInProfile: String(audioInProfiles[0] || "none"),
        audioInProfiles: [...audioInProfiles],
        displayProfile: String(displayProfiles[0] || "none"),
        displayProfiles: [...displayProfiles],
        sensors: [...normalizedPeripheralSensorProfiles()],
        inputs: [...normalizedPeripheralInputProfiles()],
        controls: [...normalizedPeripheralControlProfiles()],
        expansions: [...normalizedPeripheralExpansionProfiles()],
        storage: normalizedPeripheralStorageProfiles(),
        communication: [...normalizedPeripheralCommunicationProfiles()],
        power: [...normalizedPeripheralPowerProfiles()],
      },
    });
    return snapshot;
  }

  return {
    mergeSettingsObjects,
    applyPeripheralProfileSelections,
    currentSettingsSnapshot,
  };
}