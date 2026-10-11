export function createEffectsTab({
  state,
  elements,
  effectSelectConfig,
  effectFileSources,
  effectFilePageSize,
  effectFilesCacheStorageKey,
  request,
  delay,
  toast,
  setMessage,
  handleError,
  saveSettings,
  previewEffectFile,
  setEffectVolume,
  effectVolumeSetting,
  effectVolumePercentValue,
  shouldDeferSdReads,
  stopPlayback,
  pollStatusUntil,
  rebuildStorageIndexFromBrowser,
  formatBrowserReindexStatus,
  storageQueryParams,
  isSupportedAudioFilename,
  storageBaseName,
}) {
  function effectSelectElements() {
    return effectSelectConfig
      .map((item) => ({ ...item, element: elements[item.id], volumeElement: elements[item.volumeId] }))
      .filter((item) => item.element);
  }

  function effectFileLabelFromEntry(source, entry) {
    return `${source.prefix}: ${entry.name}`;
  }

  function effectFileLabelFromValue(value) {
    const normalized = String(value || "").trim();
    if (!normalized) {
      return "None";
    }
    const separatorIndex = normalized.indexOf(":");
    if (separatorIndex <= 0) {
      return normalized;
    }
    const target = normalized.slice(0, separatorIndex).toLowerCase();
    const path = normalized.slice(separatorIndex + 1);
    const prefix = target === "sd" ? "SD" : (target === "flash" ? "Flash" : target.toUpperCase());
    return `${prefix}: ${storageBaseName(path)}`;
  }

  function configuredEffectValue(settings, field, element) {
    const configuredValue = settings?.effects?.[field];
    if (configuredValue !== undefined && configuredValue !== null && String(configuredValue).trim()) {
      return String(configuredValue).trim();
    }
    const savedValue = String(element?.dataset?.savedEffectValue || "").trim();
    if (savedValue) {
      return savedValue;
    }
    return String(element?.value || "").trim();
  }

  function sortEffectFileOptions(options) {
    return [...options].sort((left, right) => left.label.localeCompare(right.label, undefined, { sensitivity: "base" }));
  }

  function effectFilesCacheKey(settings = state.settings) {
    const sd = settings?.sd || {};
    const flashEnabled = true;
    return JSON.stringify({
      sdEnabled: true,
      sdCsPin: Number(sd.csPin || 0),
      sdSckPin: Number(sd.sckPin || 0),
      sdMosiPin: Number(sd.mosiPin || 0),
      sdMisoPin: Number(sd.misoPin || 0),
      flashEnabled,
      sources: effectFileSources.map((source) => `${source.target}:${source.dir}`).join("|"),
    });
  }

  function persistEffectFileOptionsCache() {
    if (!state.effectFileOptionsLoaded || !state.effectFileOptions.length || !state.effectFileOptionsCacheKey) {
      return;
    }
    try {
      window.sessionStorage.setItem(effectFilesCacheStorageKey, JSON.stringify({
        key: state.effectFileOptionsCacheKey,
        options: state.effectFileOptions,
      }));
    } catch {
    }
  }

  function clearEffectFileOptionsCache() {
    state.effectFileOptions = [];
    state.effectFileOptionsLoaded = false;
    state.effectFileOptionsCacheKey = "";
    try {
      window.sessionStorage.removeItem(effectFilesCacheStorageKey);
    } catch {
    }
  }

  function effectFilesReadyMessage() {
    return "Changing a selection previews that audio file on the device.";
  }

  function effectFilesUnavailableMessage() {
    return "No supported audio files found in SD /media/wav or flash /wav.";
  }

  function restoreEffectFileOptionsFromCache(settings = state.settings) {
    const cacheKey = effectFilesCacheKey(settings);
    try {
      const raw = window.sessionStorage.getItem(effectFilesCacheStorageKey);
      if (!raw) {
        return false;
      }
      const parsed = JSON.parse(raw);
      if (parsed?.key !== cacheKey || !Array.isArray(parsed?.options)) {
        return false;
      }
      state.effectFileOptions = sortEffectFileOptions(parsed.options
        .filter((option) => option && typeof option.value === "string" && typeof option.label === "string"));
      state.effectFileOptionsLoaded = true;
      state.effectFileOptionsCacheKey = cacheKey;
      return state.effectFileOptions.length > 0;
    } catch {
      return false;
    }
  }

  function mergeEffectFileOptions(options) {
    const byValue = new Map();
    for (const option of state.effectFileOptions || []) {
      byValue.set(option.value, option);
    }
    for (const option of options || []) {
      byValue.set(option.value, option);
    }
    state.effectFileOptions = sortEffectFileOptions([...byValue.values()]);
  }

  function renderEffectFileOptions(settings = state.settings) {
    for (const { field, element } of effectSelectElements()) {
      const currentValue = configuredEffectValue(settings, field, element);
      const currentOptionExists = state.effectFileOptions.some((option) => option.value === currentValue);
      element.dataset.savedEffectValue = currentValue;
      element.innerHTML = "";

      const noneOption = document.createElement("option");
      noneOption.value = "";
      noneOption.textContent = field === "captureFile" ? "Built-in shutter" : "None";
      element.append(noneOption);

      for (const optionData of state.effectFileOptions) {
        if (field === "captureFile" && !/\.wav$/i.test(optionData.value)) continue;
        const option = document.createElement("option");
        option.value = optionData.value;
        option.textContent = optionData.label;
        element.append(option);
      }

      if (currentValue && !currentOptionExists) {
        const savedOption = document.createElement("option");
        savedOption.value = currentValue;
        savedOption.textContent = state.effectFileOptionsLoaded
          ? `${effectFileLabelFromValue(currentValue)} (unavailable)`
          : effectFileLabelFromValue(currentValue);
        savedOption.selected = true;
        element.append(savedOption);
      }

      element.value = [...element.options].some((option) => option.value === currentValue) ? currentValue : "";
    }
  }

  async function loadEffectFileOptions(options = {}) {
    const { reindex = false } = options;
    if (!state.settings) {
      return;
    }
    const cacheKey = effectFilesCacheKey(state.settings);
    if (!reindex && state.effectFileOptionsLoaded && state.effectFileOptionsCacheKey === cacheKey && state.effectFileOptions.length) {
      renderEffectFileOptions(state.settings);
      if (elements.effectsFileStatus) {
        elements.effectsFileStatus.textContent = effectFilesReadyMessage();
      }
      return;
    }
    if (!reindex && !state.effectFilesLoading && restoreEffectFileOptionsFromCache(state.settings)) {
      renderEffectFileOptions(state.settings);
      if (elements.effectsFileStatus) {
        elements.effectsFileStatus.textContent = "Loaded effect files from this browser session cache.";
      }
      return;
    }
    if (shouldDeferSdReads()) {
      state.deferredEffectsReload = true;
      renderEffectFileOptions(state.settings);
      if (elements.effectsFileStatus) {
        elements.effectsFileStatus.textContent = "Playback is active, so SD effect-file scanning is deferred to avoid audio interruptions.";
      }
      return;
    }

    state.effectFilesLoading = true;
    state.deferredEffectsReload = false;
    state.effectFileOptionsLoaded = false;
    state.effectFileOptionsCacheKey = cacheKey;
    if (elements.effectsFileStatus) {
      elements.effectsFileStatus.textContent = "Loading effect files...";
    }
    const previousOptions = Array.isArray(state.effectFileOptions) ? [...state.effectFileOptions] : [];
    let loadedAnySource = false;
    let loadedAnyOptions = false;
    let processedEntries = 0;
    let totalEntries = 0;
    state.effectFileOptions = [];
    try {
      for (const source of effectFileSources) {
        try {
          let offset = 0;
          let hasMore = true;
          let sourceTotalCounted = false;
          while (hasMore) {
            const payload = await request(`/api/storage?${storageQueryParams({
              target: source.target,
              dir: source.dir,
              offset,
              limit: effectFilePageSize,
              live: false,
              reindex: reindex && source.target === "sd" && offset === 0,
            })}`);
            loadedAnySource = true;
            if (!sourceTotalCounted) {
              totalEntries += Number(payload?.totalEntries || 0);
              sourceTotalCounted = true;
            }
            processedEntries += Number(payload?.returned || (payload?.entries?.length || 0));
            if (elements.effectsFileStatus) {
              elements.effectsFileStatus.textContent = `Loading effect files... ${formatLoadProgress(processedEntries, totalEntries)}`;
            }
            const pageOptions = [];
            for (const entry of payload.entries || []) {
              if (entry.isDirectory) {
                continue;
              }
              if (!isSupportedAudioFilename(entry.name || entry.path || "")) {
                continue;
              }
              pageOptions.push({
                value: `${source.target}:${entry.path}`,
                label: effectFileLabelFromEntry(source, entry),
              });
            }
            if (pageOptions.length) {
              loadedAnyOptions = true;
            }
            mergeEffectFileOptions(pageOptions);
            renderEffectFileOptions(state.settings);
            offset = Number(payload?.nextOffset || offset + (payload?.entries?.length || 0));
            hasMore = Boolean(payload?.hasMore);
            if (hasMore) {
              await delay(20);
            }
          }
        } catch (error) {
          console.warn(`Skipping effect file source ${source.target}:${source.dir}`, error);
        }
      }
    } finally {
      if (!loadedAnyOptions && previousOptions.length && !loadedAnySource) {
        state.effectFileOptions = previousOptions;
        state.effectFileOptionsLoaded = true;
        renderEffectFileOptions(state.settings);
        if (elements.effectsFileStatus) {
          elements.effectsFileStatus.textContent = "Keeping the cached effect-file list because storage could not be refreshed right now.";
        }
        state.effectFilesLoading = false;
        return;
      }
      if (elements.effectsFileStatus) {
        elements.effectsFileStatus.textContent = state.effectFileOptions.length
          ? effectFilesReadyMessage()
          : (loadedAnySource
            ? effectFilesUnavailableMessage()
            : "Unable to refresh effect files right now. Try again in a moment.");
      }
      state.effectFilesLoading = false;
      state.effectFileOptionsLoaded = true;
      if (state.effectFileOptions.length) {
        persistEffectFileOptionsCache();
      }
      renderEffectFileOptions(state.settings);
    }
  }

  async function reindexEffectsFiles() {
    if (state.effectReindexInProgress) {
      const message = "Effect-file reindex is already in progress.";
      if (elements.effectsFileStatus) {
        elements.effectsFileStatus.textContent = message;
      }
      toast(message);
      return;
    }
    if (shouldDeferSdReads()) {
      if (elements.effectsFileStatus) {
        elements.effectsFileStatus.textContent = "Stopping playback before reindexing effect files...";
      }
      await stopPlayback();
      const playbackStopped = !shouldDeferSdReads() || await pollStatusUntil(
        (status) => {
          const playbackState = String(status?.playback?.state || "idle");
          return playbackState !== "playing" && playbackState !== "buffering";
        },
        32,
        150,
      );
      if (playbackStopped && !shouldDeferSdReads()) {
        if (elements.effectsFileStatus) {
          elements.effectsFileStatus.textContent = "Playback stopped. Starting effect-file reindex...";
        }
      } else {
        const message = "Playback is still stopping. Try reindexing SD effect files again in a moment.";
        if (elements.effectsFileStatus) {
          elements.effectsFileStatus.textContent = message;
        }
        toast(message);
        return;
      }
    }
    state.effectReindexInProgress = true;
    if (elements.effectsReindexButton) {
      elements.effectsReindexButton.disabled = true;
    }
    clearEffectFileOptionsCache();
    try {
      if (elements.effectsFileStatus) {
        elements.effectsFileStatus.textContent = "Starting effect-file reindex...";
      }
      const source = effectFileSources.find((entry) => entry.target === "sd") || effectFileSources[0];
      await rebuildStorageIndexFromBrowser(source.target, source.dir, (progress) => {
        if (elements.effectsFileStatus) {
          elements.effectsFileStatus.textContent = formatBrowserReindexStatus(progress, "Reindexing effect files...");
        }
      });
      if (elements.effectsFileStatus) {
        elements.effectsFileStatus.textContent = "Reindex complete. Reloading effect files...";
      }
      await loadEffectFileOptions();
    } finally {
      state.effectReindexInProgress = false;
      if (elements.effectsReindexButton) {
        elements.effectsReindexButton.disabled = false;
      }
    }
  }

  function syncEffectsPage(settings = state.settings) {
    if (!state.effectFileOptionsLoaded) {
      restoreEffectFileOptionsFromCache(settings);
    }
    for (const config of effectSelectElements()) {
      if (!config.volumeElement) {
        continue;
      }
      config.volumeElement.value = String(effectVolumeSetting(config, settings));
    }
    renderEffectFileOptions(settings);
  }

  function bindEvents() {
    for (const config of effectSelectElements()) {
      const { label, element, volumeElement, source } = config;
      const ensureEffectOptionsLoaded = () => {
        if (state.effectFilesLoading || state.effectFileOptionsLoaded) {
          return;
        }
        loadEffectFileOptions().catch(handleError);
      };
      element?.addEventListener("pointerdown", ensureEffectOptionsLoaded);
      element?.addEventListener("focus", ensureEffectOptionsLoaded);
      element?.addEventListener("change", async () => {
        const selectedValue = String(element.value || "").trim();
        element.dataset.savedEffectValue = selectedValue;
        state.settingsDirty = true;
        try {
          await saveSettings({ silent: true });
          if (selectedValue) {
            const ambientWasPlaying = source === "effect-ambient"
              && String(state.status?.playback?.source || "") === "effect-ambient";
            if (ambientWasPlaying) {
              await stopPlayback();
              const playbackStopped = await pollStatusUntil(
                (status) => {
                  const playbackState = String(status?.playback?.state || "idle");
                  return playbackState !== "playing" && playbackState !== "buffering";
                },
                32,
                150,
              );
              if (!playbackStopped) {
                throw new Error("Ambient playback is still stopping. Try the change again in a moment.");
              }
            }
            await previewEffectFile(selectedValue, label, { source });
          }
        } catch (error) {
          handleError(error);
        }
      });
      volumeElement?.addEventListener("input", (event) => {
        const trimmedValue = String(event.target.value ?? "").trim();
        if (!trimmedValue) {
          state.settingsDirty = true;
          return;
        }
        const normalizedValue = effectVolumePercentValue(trimmedValue, effectVolumeSetting(config));
        if (String(normalizedValue) !== trimmedValue) {
          event.target.value = String(normalizedValue);
        }
        state.settingsDirty = true;
      });
      volumeElement?.addEventListener("change", (event) => {
        setEffectVolume(config, event.target.value).catch(handleError);
      });
    }
    elements.effectsReindexButton?.addEventListener("click", () => {
      reindexEffectsFiles().catch(handleError);
    });
  }

  return {
    mergeEffectFileOptions,
    clearEffectFileOptionsCache,
    renderEffectFileOptions,
    loadEffectFileOptions,
    reindexEffectsFiles,
    syncEffectsPage,
    bindEvents,
  };
}

function formatLoadProgress(processedEntries, totalEntries) {
  const total = Math.max(0, Number(totalEntries || 0));
  const processed = Math.max(0, Number(processedEntries || 0));
  if (total <= 0) {
    return `${processed} file${processed === 1 ? "" : "s"}`;
  }
  const percent = Math.max(0, Math.min(100, Math.round((processed / total) * 100)));
  return `${percent}% (${processed}/${total})`;
}