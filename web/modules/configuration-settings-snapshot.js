import {ONBOARD_BOARDS} from './onboard-boards.js';
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
    const board=ONBOARD_BOARDS[snapshot.ui?.gpioBoardSelection||state.settings?.ui?.gpioBoardSelection];
    if(board?.audioProfile===audioProfile){const a=board.defaults.audio;Object.assign(snapshot.audio,{bclkPin:a.bclkPin,wsPin:a.wsPin,doutPin:a.doutPin});}

    const displayProfiles = normalizedPeripheralDisplayProfiles();
    const displayProfile = String(displayProfiles[0] || "none").trim().toLowerCase();
    if (displayProfile === "none") {
      snapshot.oled.enabled = false;
    } else if (displayProfile === "waveshare-screen") {
      snapshot.oled.enabled = true;
      snapshot.oled.displayType = "wape";
    } else if (displayProfile === "viewe-onboard-lcd" || snapshot.oled.displayType === "panel") {
      snapshot.oled.enabled=true;snapshot.oled.displayType="panel";snapshot.oled.width=Number(state.settings?.oled?.width)||Number(snapshot.oled.width)||240;snapshot.oled.height=Number(state.settings?.oled?.height)||Number(snapshot.oled.height)||320;
    } else if (displayProfile === "i2c-oled") {
      snapshot.oled.enabled = true;
      snapshot.oled.displayType = "oled";
    }

    const storageProfiles = normalizedPeripheralStorageProfiles();
    snapshot.sd.enabled = String(storageProfiles[0] || "none").trim().toLowerCase() !== "none";
    snapshot.sd.sdmmc=['viewe-sdmmc','camera-sdmmc'].includes(storageProfiles[0]);
    if(board?.defaults?.sd&&snapshot.sd.enabled){
      const fixed=board.bindings?.['storage:'+storageProfiles[0]];
      if(fixed)Object.assign(snapshot.sd,{csPin:fixed.CS??board.defaults.sd.csPin,sckPin:fixed.SCK??board.defaults.sd.sckPin,mosiPin:fixed.MOSI??board.defaults.sd.mosiPin,misoPin:fixed.MISO??board.defaults.sd.misoPin});
    }
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
      language: String(window.ElmaAndroidConfig?.language?.() || persistedUi.language || "en"),
      theme: String(persistedUi.theme || "automatic"),
      gpioBoardAutodetect: Boolean(elements.gpioBoardAutodetect?.checked ?? true),
      gpioSafetyOverride: Boolean(elements.gpioSafetyOverride?.checked),
      gpioBoardSelection: String(elements.gpioBoardSelector?.value || ""),
      customBoard: cloneSettingsObject(state.settings?.ui?.customBoard || persistedUi.customBoard) || {},
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
    const board=ONBOARD_BOARDS[snapshot.ui.gpioBoardSelection];
    // Preserve fixed onboard wiring even after editing the profile in the browser.
    for(const [group,profiles] of [['audio',audioProfiles],['audioIn',audioInProfiles],['storage',normalizedPeripheralStorageProfiles()]]){
      const fixed=board?.bindings?.[group+':'+profiles[0]];
      if(fixed)snapshot.ui.peripheralHelperBindings[group+':0']={...(snapshot.ui.peripheralHelperBindings[group+':0']||{}),...fixed};
    }
    return snapshot;
  }

  return {
    mergeSettingsObjects,
    applyPeripheralProfileSelections,
    currentSettingsSnapshot,
  };
}
