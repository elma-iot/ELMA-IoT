import {verifiedStorageDownload} from './storage-integrity.js';
export function createStorageTab({
  state,
  elements,
  activeStorageEntries,
  activeStorageMeta,
  activeTabName,
  activateTabByName,
  clearStorageSelection,
  closeStoragePreview,
  createStorageFolder,
  deleteSelectedStorageItems,
  flashStorageAvailable,
  handleError,
  loadMoreStorageEntries,
  openStoragePreview,
  queueStoragePlayback,
  refreshStorageManager,
  reindexStorageDirectory,
  remountStorageDirectory,
  renderStorageManager,
  resolveStorageTarget,
  setStorageSelectionMode,
  setStorageStatus,
  selectedStoragePlaybackEntry,
  currentStoragePlayingEntry,
  storageEntryIsPlaying,
  shouldDeferSdReads,
  storageParentPath,
  uploadStorageFiles,
  toggleStoragePreviewPlayback,
  toggleStorageSelection,
  selectAllStorageEntries,
  updateStoragePreviewPlaybackControls,
  updateStorageMeter,
  updateStorageVolumeMeter,
  advanceStoragePreviewTrack,
  stopStoragePreviewPlayback,
  setVolume,
  normalizeStorageDirectoryPath,
  request,
  formatPlaybackClock,
}) {
  const rowAudio=new Audio();state.storageRowAudio=rowAudio;state.storageRowPlaybackTargets={};
  const redrawRows=()=>renderStorageManager({target:state.activeStorageTarget,storage:state.storageInfoByTarget[state.activeStorageTarget]||{},currentPath:state.currentStoragePathByTarget[state.activeStorageTarget]||'/',entries:activeStorageEntries(),...activeStorageMeta()});
  const stopBrowserRow=()=>{rowAudio.pause();rowAudio.removeAttribute('src');rowAudio.load();state.storageRowBrowserPath=null;};
  rowAudio.addEventListener('ended',()=>{state.storageRowBrowserPath=null;redrawRows();});
  rowAudio.addEventListener('error',()=>{state.storageRowBrowserPath=null;redrawRows();setStorageStatus('Browser could not play this file format; try the board.',true);});
  async function refreshExternalStorageTab(directoryPath = state.currentStoragePathByTarget.sd || "/", options = {}) {
    const normalizedDirectoryPath = normalizeStorageDirectoryPath(directoryPath);
    const canReuseCachedDirectory = shouldDeferSdReads()
      && activeStorageEntries("sd").length
      && normalizedDirectoryPath === normalizeStorageDirectoryPath(state.currentStoragePathByTarget.sd || "/");
    state.storageInitialLoadRequested = true;
    state.activeStorageTarget = "sd";
    state.currentStoragePathByTarget.sd = normalizedDirectoryPath;
    if (canReuseCachedDirectory) {
      state.deferredStorageReload = true;
      setStorageStatus("Using cached folder view during playback. Stop playback or scroll later to refresh from SD.");
      renderStorageManager({
        target: "sd",
        storage: state.storageInfoByTarget.sd || {},
        currentPath: state.currentStoragePathByTarget.sd || "/",
        entries: activeStorageEntries("sd"),
        ...activeStorageMeta("sd"),
      });
      return {
        storage: state.storageInfoByTarget.sd || {},
        entries: activeStorageEntries("sd"),
        hasMore: Boolean(activeStorageMeta("sd").hasMore),
      };
    }
    state.deferredStorageReload = false;
    setStorageStatus("Loading files...");
    const payload = await refreshStorageManager("sd", normalizedDirectoryPath, options);
    if (elements.storageProgressFill) {
      elements.storageProgressFill.style.width = "0%";
    }
    if (elements.storageProgressLabel) {
      elements.storageProgressLabel.textContent = "Storage operation";
    }
    if (elements.storageProgressWrap) elements.storageProgressWrap.hidden = true;
    if (!payload?.hasMore) {
      setStorageStatus("Ready");
    }
    return payload;
  }

  function updateStorageAvailabilityUi(status = state.status) {
    const flashAvailable = flashStorageAvailable(status);
    const internalStorageTabButton = document.querySelector('.tab-button[data-tab="storage-internal"]');
    const internalStoragePanel = document.getElementById("tab-storage-internal");

    if (internalStorageTabButton) {
      internalStorageTabButton.hidden = false;
      internalStorageTabButton.disabled = false;
    }

    if (internalStoragePanel) {
      internalStoragePanel.hidden = false;
    }

    if (elements.storageFlashButton) {
      elements.storageFlashButton.hidden = !flashAvailable;
      elements.storageFlashButton.disabled = !flashAvailable;
    }

    if (!flashAvailable && state.activeStorageTarget === "flash") {
      state.activeStorageTarget = "sd";
    }

    const activeTabButton = document.querySelector('.tab-button[aria-selected="true"]');
    if (activeTabButton && (activeTabButton.hidden || activeTabButton.disabled)) {
      const fallbackTabButton = [...document.querySelectorAll(".tab-button")]
        .find((button) => !button.hidden && !button.disabled);
      fallbackTabButton?.click();
    }
  }

  function maybeRefreshVisibleStorageTab(force = false) {
    if (activeTabName() !== "storage-external") {
      return;
    }
    const storage = state.storageInfoByTarget.sd || {};
    const entries = activeStorageEntries("sd");
    const meta = activeStorageMeta("sd");
    const needsRefresh = force
      || (!state.storageInitialLoadRequested && !meta.loadingMore)
      || (!entries.length && !storage.mounted && !meta.loadingMore);
    if (!needsRefresh) {
      return;
    }
    refreshExternalStorageTab(state.currentStoragePathByTarget.sd || "/").catch(handleError);
  }

  async function openStorageManager(target = "flash", directoryPath = state.currentStoragePathByTarget[target] || "/") {
    if (!elements.storageFileList) {
      return;
    }
    const resolvedTarget = resolveStorageTarget(target);
    if (resolvedTarget !== "sd") {
      activateTabByName(resolvedTarget === "flash" ? "storage-internal" : "storage-external");
      return;
    }
    activateTabByName("storage-external");
    let candidatePath = normalizeStorageDirectoryPath(directoryPath);
    while (true) {
      try {
        await refreshExternalStorageTab(candidatePath);
        return;
      } catch (error) {
        if (candidatePath === "/") {
          throw error;
        }
        candidatePath = storageParentPath(candidatePath) || "/";
        state.currentStoragePathByTarget.sd = candidatePath;
      }
    }
  }

  function bindEvents() {
    elements.storageUpButton?.addEventListener("click", () => {
      const parentPath = storageParentPath(state.currentStoragePathByTarget[state.activeStorageTarget] || "/");
      if (parentPath) {
        openStorageManager(state.activeStorageTarget, parentPath).catch(handleError);
      }
    });
    elements.storageReindexButton?.addEventListener("click", () => {
      reindexStorageDirectory(
        state.activeStorageTarget,
        state.currentStoragePathByTarget[state.activeStorageTarget] || "/",
      ).catch(handleError);
    });
    elements.storageRemountButton?.addEventListener("click", () => {
      remountStorageDirectory(
        state.activeStorageTarget,
        state.currentStoragePathByTarget[state.activeStorageTarget] || "/",
      ).catch(handleError);
    });
    elements.storageFormatButton?.addEventListener("click", async () => {
      try{await request('/api/storage/format?action=prompt',{method:'POST'});setStorageStatus('Confirm formatting in the SD card dialog.');}
      catch(error){handleError(error);}
    });
    elements.storageEjectButton?.addEventListener("click", async () => {
      try{await request('/api/storage/eject',{method:'POST'});setStorageStatus('Eject requested. Wait for the card to become unavailable before removing it.');}
      catch(error){handleError(error);}
    });
    elements.storageSelectModeButton?.addEventListener("click", () => {
      setStorageSelectionMode(!state.storageSelectionMode);
    });
    elements.storageSelectAllButton?.addEventListener("click", () => {
      if (!state.storageSelectionMode) {
        setStorageSelectionMode(true);
      }
      selectAllStorageEntries();
    });
    elements.storageDeleteButton?.addEventListener("click", () => {
      deleteSelectedStorageItems().catch(handleError);
    });
    elements.storagePlayButton?.addEventListener("click", () => {
      const entry = selectedStoragePlaybackEntry();
      if (!entry) {
        setStorageStatus("Select one audio file first.", true);
        return;
      }
      (storageEntryIsPlaying(entry)
        ? stopStoragePreviewPlayback()
        : queueStoragePlayback(entry, state.activeStorageTarget)).catch(handleError);
    });
    elements.storageUploadButton?.addEventListener("click", () => {
      if (!elements.storageFileInput || elements.storageUploadButton.disabled || state.storageUploadInProgress) {
        return;
      }
      elements.storageFileInput.value = "";
      elements.storageFileInput.click();
    });
    elements.storageFileInput?.addEventListener("change", () => {
      const files = [...(elements.storageFileInput?.files || [])];
      if (!files.length) {
        return;
      }
      uploadStorageFiles(files).catch(handleError);
    });
    elements.storageNewFolderButton?.addEventListener("click", () => {
      createStorageFolder().catch(handleError);
    });
    elements.storageFileList?.addEventListener("click", (event) => {
      const action=event.target.closest('[data-storage-file-action]');
      if(action){
        event.preventDefault();event.stopPropagation();clearTimeout(state.storageClickTimer);state.storageClickTimer=null;
        const path=action.closest('.storage-file-row')?.dataset.storagePath;if(!path)return;
        const entry=activeStorageEntries().find(item=>item.path===path),key=state.activeStorageTarget+'|'+path;
        if(action.dataset.storageFileAction==='play'||action.dataset.storageFileAction==='target'){
          action.disabled=true;
          (async()=>{
            const browserPlaying=state.storageRowBrowserPath===key&&!rowAudio.paused;
            const devicePlaying=entry&&storageEntryIsPlaying(entry);
            if(action.dataset.storageFileAction==='target'){
              if(browserPlaying)stopBrowserRow();if(devicePlaying)await stopStoragePreviewPlayback();
              state.storageRowPlaybackTargets[key]=!state.storageRowPlaybackTargets[key];
            }else if(browserPlaying||devicePlaying){if(browserPlaying)stopBrowserRow();if(devicePlaying)await stopStoragePreviewPlayback();}
            else if(state.storageRowPlaybackTargets[key]){stopBrowserRow();await queueStoragePlayback(entry,state.activeStorageTarget);}
            else{stopBrowserRow();state.storageRowBrowserPath=key;rowAudio.src='/api/storage/file?target='+encodeURIComponent(state.activeStorageTarget)+'&path='+encodeURIComponent(path);try{await rowAudio.play();}catch(error){state.storageRowBrowserPath=null;throw error;}}
            redrawRows();
          })().catch(error=>{redrawRows();handleError(error);}).finally(()=>{action.disabled=false;});return;
        }
        if(action.dataset.storageFileAction==='download'){
          action.disabled=true;verifiedStorageDownload(state.activeStorageTarget,path,(done,total,attempt)=>setStorageStatus(attempt?`Download retry ${attempt}/3`:total?`Downloading and verifying: ${Math.round(done*100/total)}%`:'Verifying download')).then(result=>setStorageStatus(result?.native?'Android download started':result?.cancelled?'Download cancelled':'Download verified')).catch(handleError).finally(()=>{action.disabled=false;});
        }else if(confirm('Delete '+path+'?')){
          if(state.storageRowBrowserPath===key)stopBrowserRow();
          action.disabled=true;
          (async()=>{if(entry&&storageEntryIsPlaying(entry))await stopStoragePreviewPlayback();await request('/api/storage/delete?target='+encodeURIComponent(state.activeStorageTarget)+'&dir='+encodeURIComponent(state.currentStoragePathByTarget[state.activeStorageTarget]||'/'),{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path})});clearStorageSelection();await refreshStorageManager(state.activeStorageTarget);setStorageStatus('Deleted '+path);})().catch(handleError).finally(()=>{action.disabled=false;});
        }
        return;
      }
      const checkbox = event.target.closest("[data-storage-checkbox]");
      if (checkbox) {
        return;
      }

      const row = event.target.closest(".storage-file-row");
      if (!row) {
        return;
      }

      const path = row.dataset.storagePath || "";
      if (!path) {
        return;
      }

      if (state.storageClickTimer) {
        window.clearTimeout(state.storageClickTimer);
        state.storageClickTimer = null;
      }

      state.storageClickTimer = window.setTimeout(() => {
        toggleStorageSelection(path, { additive: state.storageSelectionMode });
        state.storageClickTimer = null;
      }, state.storageSelectionMode ? 0 : 380);
    });
    elements.storageFileList?.addEventListener("change", (event) => {
      const checkbox = event.target.closest("[data-storage-checkbox]");
      if (!checkbox) {
        return;
      }
      toggleStorageSelection(checkbox.dataset.storageCheckbox, { additive: true });
    });
    elements.storageFileList?.addEventListener("dblclick", (event) => {
      if(event.target.closest('[data-storage-file-action]'))return;
      const row = event.target.closest(".storage-file-row");
      if (!row) {
        return;
      }

      const path = row.dataset.storagePath || "";
      const kind = row.dataset.storageKind || "file";
      const entry = activeStorageEntries().find((candidate) => candidate.path === path);
      if (!entry) {
        return;
      }

      if (state.storageClickTimer) {
        window.clearTimeout(state.storageClickTimer);
        state.storageClickTimer = null;
      }

      if (kind === "folder") {
        clearStorageSelection();
        openStorageManager(state.activeStorageTarget, path).catch(handleError);
        return;
      }

      openStoragePreview(entry).catch(handleError);
    });
    elements.storageFileList?.addEventListener("scroll", () => {
      const node = elements.storageFileList;
      if (!node) {
        return;
      }
      if (node.scrollTop + node.clientHeight >= node.scrollHeight - 160) {
        loadMoreStorageEntries(state.activeStorageTarget).catch(handleError);
      }
    });
    elements.storageBreadcrumbs?.addEventListener("click", (event) => {
      const crumbButton = event.target.closest("[data-storage-nav]");
      if (!crumbButton) {
        return;
      }
      clearStorageSelection();
      openStorageManager(state.activeStorageTarget, crumbButton.dataset.storageNav).catch(handleError);
    });
    elements.storagePreviewPlayButton?.addEventListener("click", () => {
      toggleStoragePreviewPlayback().catch(handleError);
    });
    elements.storagePreviewPrevButton?.addEventListener("click", () => {
      advanceStoragePreviewTrack(-1, {
        autoplayDevice: state.storagePreviewPlaybackMode.deviceActive,
        respectModes: false,
      }).catch(handleError);
    });
    elements.storagePreviewNextButton?.addEventListener("click", () => {
      advanceStoragePreviewTrack(1, {
        autoplayDevice: state.storagePreviewPlaybackMode.deviceActive,
        respectModes: false,
      }).catch(handleError);
    });
    elements.storagePreviewLoopButton?.addEventListener("click", () => {
      state.storagePreviewPlaybackMode.loop = !state.storagePreviewPlaybackMode.loop;
      updateStoragePreviewPlaybackControls();
    });
    elements.storagePreviewShuffleButton?.addEventListener("click", () => {
      state.storagePreviewPlaybackMode.shuffle = !state.storagePreviewPlaybackMode.shuffle;
      updateStoragePreviewPlaybackControls();
    });
    elements.storageInlinePlayButton?.addEventListener("click", () => {
      const playingEntry = currentStoragePlayingEntry();
      const entry = playingEntry || selectedStoragePlaybackEntry() || state.storagePreviewItem;
      if (!entry) {
        setStorageStatus("Select a song from this folder first.", true);
        return;
      }
      (playingEntry
        ? stopStoragePreviewPlayback()
        : queueStoragePlayback(entry, state.activeStorageTarget)).catch(handleError);
    });
    elements.storageInlineShuffleButton?.addEventListener("click", () => {
      state.storagePreviewPlaybackMode.shuffle = !state.storagePreviewPlaybackMode.shuffle;
      updateStoragePreviewPlaybackControls();
    });
    elements.storageInlineRepeatButton?.addEventListener("click", () => {
      state.storagePreviewPlaybackMode.loop = !state.storagePreviewPlaybackMode.loop;
      updateStoragePreviewPlaybackControls();
    });
    elements.storageInlineAutoplayButton?.addEventListener("click", () => {
      state.storagePreviewPlaybackMode.autoplay = !state.storagePreviewPlaybackMode.autoplay;
      updateStoragePreviewPlaybackControls();
    });
    elements.storageInlineVolumeSlider?.addEventListener("input", (event) => {
      elements.storageInlineVolumeMeter?.classList.add("is-dragging");
      updateStorageVolumeMeter(event.target.value);
    });
    elements.storageInlineVolumeSlider?.addEventListener("change", (event) => {
      elements.storageInlineVolumeMeter?.classList.remove("is-dragging");
      setVolume(Number(event.target.value)).catch(handleError);
    });
    const bindSeekSlider = (slider, label) => {
      slider?.addEventListener("input", (event) => {
        state.storagePreviewPlaybackMode.seeking = true;
        const position = Math.max(0, Number(event.target.value || 0));
        const duration = Math.max(0, Number(event.target.max || 0));
        if (slider === elements.storageInlineSeekSlider) {
          elements.storageInlineTrackMeter?.classList.add("is-dragging");
          const trackName = currentStoragePlayingEntry()?.name || selectedStoragePlaybackEntry()?.name || state.storagePreviewItem?.name || "Select a song";
          updateStorageMeter(elements.storageInlineTrackMeter, "[data-storage-track-text]", `${trackName} · ${formatPlaybackClock(position)} / ${formatPlaybackClock(duration)}`, duration > 0 ? (position / duration) * 100 : 0);
        } else if (label) {
          label.textContent = `${formatPlaybackClock(position)} / ${formatPlaybackClock(duration)}`;
        }
      });
      slider?.addEventListener("change", async (event) => {
        try {
          const positionSeconds = Math.max(0, Math.round(Number(event.target.value || 0)));
          await request("/api/playback/seek", { method: "POST", body: JSON.stringify({ positionSeconds }) });
          setStorageStatus(`Moved to ${formatPlaybackClock(positionSeconds)}`);
        } finally {
          state.storagePreviewPlaybackMode.seeking = false;
          if (slider === elements.storageInlineSeekSlider) elements.storageInlineTrackMeter?.classList.remove("is-dragging");
        }
      });
    };
    bindSeekSlider(elements.storageInlineSeekSlider, null);
    bindSeekSlider(elements.storagePreviewSeekSlider, elements.storagePreviewProgressLabel);
    elements.storagePreviewCloseButton?.addEventListener("click", () => {
      closeStoragePreview();
    });
    elements.storagePreviewModal?.addEventListener("cancel", (event) => {
      event.preventDefault();
      closeStoragePreview();
    });
  }

  return {
    bindEvents,
    maybeRefreshVisibleStorageTab,
    openStorageManager,
    refreshExternalStorageTab,
    updateStorageAvailabilityUi,
  };
}
