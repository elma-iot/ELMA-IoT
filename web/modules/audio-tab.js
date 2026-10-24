export function createAudioTab({
  state,
  elements,
  hasDistinctI2sPins,
  waitForSettingsIdle,
  saveSettings,
  handleError,
  queueSettingsSave,
  populateSdPinOptions,
  populateBatteryAdcPinOptions,
  populateOledPinOptions,
  populateWapeTriggerPinOptions,
  updateBatteryUi,
}) {
  function updateAudioUiState() {
    const muted = Boolean(elements.audioMutedToggle?.checked);
    const audioEnabled = Boolean(state.status?.firmware?.audioEnabled ?? false);
    if (elements.audioMutedNote) {
      if (!hasDistinctI2sPins()) {
        elements.audioMutedNote.textContent = "Pick three different GPIOs for I2S LRC/WS, BCLK, and DIN. The mapping will be saved after the combination becomes valid.";
        return;
      }
      if (!audioEnabled) {
        elements.audioMutedNote.textContent = "Audio playback is disabled in this diagnostic firmware build, so Play requests will not produce sound until audio is re-enabled in firmware.";
        return;
      }
      elements.audioMutedNote.textContent = muted
        ? "Audio is muted by default in this build. Sound effects stay suppressed while muted."
        : "Audio mute is off and playback is enabled.";
    }
  }

  function updateAudioI2sUi() {
    const wsPin = Number(elements.audioWsPin?.value || state.settings?.audio?.wsPin || 0);
    const bclkPin = Number(elements.audioBclkPin?.value || state.settings?.audio?.bclkPin || 0);
    const doutPin = Number(elements.audioDoutPin?.value || state.settings?.audio?.doutPin || 0);

    if (elements.audioWsSummary) {
      elements.audioWsSummary.textContent = `GPIO${wsPin}`;
    }
    if (elements.audioBclkSummary) {
      elements.audioBclkSummary.textContent = `GPIO${bclkPin}`;
    }
    if (elements.audioDoutSummary) {
      elements.audioDoutSummary.textContent = `GPIO${doutPin}`;
    }
  }

  async function saveAudioPinMapping(options = {}) {
    const { silent = false } = options;
    if (!hasDistinctI2sPins()) {
      state.settingsDirty = true;
      if (!silent) {
        throw new Error("Pick three different GPIOs before saving the I2S mapping.");
      }
      return false;
    }

    if (state.settingsSaveTimer) {
      window.clearTimeout(state.settingsSaveTimer);
      state.settingsSaveTimer = null;
    }

    await waitForSettingsIdle();
    await saveSettings({ silent });
    return true;
  }

  function bindEvents() {
    for (const field of [elements.audioWsPin, elements.audioBclkPin, elements.audioDoutPin]) {
      field?.addEventListener("change", () => {
        updateAudioI2sUi();
        updateAudioUiState();
        populateSdPinOptions();
        populateBatteryAdcPinOptions();
        populateOledPinOptions();
        populateWapeTriggerPinOptions();
        updateBatteryUi();
        state.settingsDirty = true;
        if (hasDistinctI2sPins()) queueSettingsSave(150);
      });
    }

    elements.saveAudioButton?.addEventListener("click", () => {
      saveAudioPinMapping({ silent: false }).catch(handleError);
    });

    elements.audioMutedToggle?.addEventListener("change", () => {
      updateAudioUiState();
      queueSettingsSave(150);
    });
  }

  return {
    updateAudioUiState,
    updateAudioI2sUi,
    saveAudioPinMapping,
    bindEvents,
  };
}