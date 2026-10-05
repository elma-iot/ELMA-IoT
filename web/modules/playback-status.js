export function createPlaybackStatusModule({
  state,
  elements,
  normalizePlaybackTitle,
  isPlaybackActive,
  toast,
  applySelectedRadioStation,
  isFileManagerPlayback,
  canStepFileManagerPlayback,
}) {
  const playIcon = '<svg class="media-control-icon media-control-play" viewBox="0 0 24 24" aria-hidden="true"><path d="m7 4 14 8-14 8z"></path></svg>';
  const stopIcon = '<svg class="media-control-icon" viewBox="0 0 24 24" aria-hidden="true"><rect x="6" y="6" width="12" height="12" rx="1"></rect></svg>';

  function selectedRadioStation() {
    const selectedIndex = Number(elements.radioStationSelect?.value ?? -1);
    if (!Number.isInteger(selectedIndex) || selectedIndex < 0 || selectedIndex >= state.radioStations.length) {
      return null;
    }
    return state.radioStations[selectedIndex] || null;
  }

  function currentPlaybackHeroTitle(status = state.status) {
    const playingTitle = normalizePlaybackTitle(status?.playback?.title, status?.playback?.url);
    if (playingTitle) {
      return playingTitle;
    }

    const selectedStation = selectedRadioStation();
    if (selectedStation?.name) {
      return selectedStation.name;
    }

    const manualLabel = normalizePlaybackTitle(elements.playLabel?.value, elements.playUrl?.value);
    if (manualLabel) {
      return manualLabel;
    }

    return "No station selected";
  }

  function updatePlaybackHeroControls() {
    // LCD, web and remote commands share the same runtime player. Follow a
    // changed live station without overwriting a user's unplayed selection.
    const liveUrl = String(state.status?.playback?.url || "");
    if (liveUrl && liveUrl !== state.lastSyncedRadioUrl) {
      const index = state.radioStations.findIndex(station => station.url === liveUrl);
      if (index >= 0 && elements.radioStationSelect) elements.radioStationSelect.value = String(index);
      const editingSource = [elements.playUrl, elements.playLabel].some(field => field && globalThis.document?.activeElement === field);
      if (!editingSource) {
        if (elements.playUrl) elements.playUrl.value = liveUrl;
        if (elements.playLabel) elements.playLabel.value = state.status?.playback?.title || state.radioStations[index]?.name || "";
        state.lastSyncedRadioUrl = liveUrl;
      }
    }
    const audioEnabled = Boolean(state.status?.firmware?.audioEnabled);
    const playbackActive = isPlaybackActive();
    const busy = Boolean(state.playbackActionInProgress);
    const fileManagerActive = Boolean(isFileManagerPlayback?.());
    const stationStepReady = audioEnabled && state.radioStations.length > 0 && !elements.radioStationSelect?.disabled && !busy;
    const fileStepReady = audioEnabled && Boolean(canStepFileManagerPlayback?.()) && !busy;
    const stepReady = fileManagerActive ? fileStepReady : stationStepReady;
    const lastSourceWasFileManager = String(state.status?.playback?.source || "") === "file-manager";
    const hasSelection = playbackActive || lastSourceWasFileManager || Boolean(String(elements.playUrl?.value || "").trim());

    if (elements.playbackPrevButton) {
      elements.playbackPrevButton.disabled = !stepReady;
      elements.playbackPrevButton.title = fileManagerActive
        ? (stepReady ? "Previous song; hold to rewind" : "No previous song available")
        : (stepReady ? "Previous station" : "Load a station list first");
      elements.playbackPrevButton.setAttribute("aria-label", elements.playbackPrevButton.title);
    }

    if (elements.playbackNextButton) {
      elements.playbackNextButton.disabled = !stepReady;
      elements.playbackNextButton.title = fileManagerActive
        ? (stepReady ? "Next song; hold to fast-forward" : "No next song available")
        : (stepReady ? "Next station" : "Load a station list first");
      elements.playbackNextButton.setAttribute("aria-label", elements.playbackNextButton.title);
    }

    if (!elements.playbackHeroToggleButton) {
      return;
    }

    const button = elements.playbackHeroToggleButton;
    button.classList.toggle("playing", playbackActive && !busy);

    if (state.playbackActionInProgress === "play") {
      button.textContent = "…";
      button.disabled = true;
      button.title = "Starting playback";
      button.setAttribute("aria-label", "Starting playback");
      return;
    }

    if (state.playbackActionInProgress === "stop") {
      button.textContent = "…";
      button.disabled = true;
      button.title = "Stopping playback";
      button.setAttribute("aria-label", "Stopping playback");
      return;
    }

    button.innerHTML = playbackActive ? stopIcon : playIcon;
    button.disabled = !audioEnabled || (!playbackActive && !hasSelection);
    const playTitle = lastSourceWasFileManager ? "Play selected song" : "Play selected station";
    button.title = playbackActive ? "Stop playback" : playTitle;
    button.setAttribute("aria-label", playbackActive ? "Stop playback" : playTitle);
  }

  async function stepRadioStationSelection(delta) {
    if (state.playbackActionInProgress) {
      return;
    }

    if (!elements.radioStationSelect || !state.radioStations.length || elements.radioStationSelect.disabled) {
      toast("Load a station list first");
      return;
    }

    const stationCount = state.radioStations.length;
    const currentIndex = Number(elements.radioStationSelect.value ?? -1);
    const normalizedCurrentIndex = Number.isInteger(currentIndex) && currentIndex >= 0 && currentIndex < stationCount
      ? currentIndex
      : (delta >= 0 ? -1 : 0);
    const nextIndex = normalizedCurrentIndex < 0
      ? (delta >= 0 ? 0 : stationCount - 1)
      : (normalizedCurrentIndex + delta + stationCount) % stationCount;

    elements.radioStationSelect.value = String(nextIndex);
    await applySelectedRadioStation({ autoPlay: isPlaybackActive() });
    updatePlaybackHeroControls();
  }

  return {
    currentPlaybackHeroTitle,
    updatePlaybackHeroControls,
    stepRadioStationSelection,
  };
}
