export function createDisplayTab({
  state,
  elements,
  maxPeripheralDisplays,
  peripheralDisplayProfileOptions,
  oledPreviewScrollIntervalMs,
  normalizedPeripheralDisplayProfiles,
  updatePrimaryPeripheralIndexLabel,
  appendPeripheralOptions,
  buildPeripheralProfileComposite,
  peripheralProfileInstanceLabel,
  buildPeripheralActionButton,
  syncPeripheralBindingGroups,
  renderPeripheralDiagram,
  syncGpioMappingControls,
  savePeripheralProfileSelections,
  removePeripheralHelperBindingsForIndex,
  populateOledPinOptions,
  populateWapeTriggerPinOptions,
  renderGpioOverview,
  namedField,
  oledDimensions,
  charsForWidth,
  oledTopDividerY,
  oledBottomDividerY,
  truncateOledText,
  oledScrollWindow,
  normalizePlaybackTitle,
  postSimple,
  queueSettingsSave,
  handleError,
}) {
  function renderPeripheralDisplayControls() {
    state.peripheralDisplayProfiles = normalizedPeripheralDisplayProfiles();
    updatePrimaryPeripheralIndexLabel(elements.peripheralDisplayPrimaryIndexLabel, "Display", state.peripheralDisplayProfiles.length);
    if (elements.peripheralDisplayAddButton) {
      const total = state.peripheralDisplayProfiles.length;
      elements.peripheralDisplayAddButton.disabled = total >= maxPeripheralDisplays;
      elements.peripheralDisplayAddButton.title = total >= maxPeripheralDisplays ? `Maximum of ${maxPeripheralDisplays} displays reached` : "Add another display";
      elements.peripheralDisplayAddButton.setAttribute("aria-label", elements.peripheralDisplayAddButton.title);
    }
    if (!elements.peripheralDisplayList) {
      return;
    }

    elements.peripheralDisplayList.innerHTML = "";
    const total = state.peripheralDisplayProfiles.length;
    state.peripheralDisplayProfiles.slice(1).forEach((selectedValue, offset) => {
      const index = offset + 1;
      const row = document.createElement("div");
      row.className = "peripheral-profile-row";

      const select = document.createElement("select");
      select.dataset.peripheralDisplayIndex = String(index);
      select.setAttribute("aria-label", `Display ${index + 1} peripheral profile`);
      appendPeripheralOptions(select, peripheralDisplayProfileOptions, selectedValue);
      row.appendChild(buildPeripheralProfileComposite("display", selectedValue, index, select, peripheralProfileInstanceLabel("Display", index, total)));
      row.appendChild(buildPeripheralActionButton({
        addDatasetKey: "peripheralDisplayAdd",
        removeDatasetKey: "peripheralDisplayRemove",
        index,
        total,
        maxCount: maxPeripheralDisplays,
        singularLabel: "display",
      }));
      elements.peripheralDisplayList.appendChild(row);
    });
  }

  function oledPreviewNode(selector) {
    return elements.oledPreview?.querySelector(selector) || null;
  }

  function updateDisplayModeUi() {
    const displayType = String(elements.displayType?.value || state.settings?.oled?.displayType || "oled").toLowerCase();
    const oledSelected = displayType !== "wape";
    const oledEnabledField = namedField("oled.enabled");
    if (elements.oledModeSection) {
      elements.oledModeSection.hidden = !oledSelected;
    }
    if (elements.wapeModeSection) {
      elements.wapeModeSection.hidden = oledSelected;
    }
    if (oledEnabledField) {
      oledEnabledField.disabled = !oledSelected;
    }
  }

  function stopOledPreviewScroll(centerNode) {
    if (state.oledPreviewScrollTimer) {
      clearInterval(state.oledPreviewScrollTimer);
      state.oledPreviewScrollTimer = null;
    }
    state.oledPreviewScrollSignature = "";
    state.oledPreviewScrollOffset = 0;
    if (centerNode) {
      centerNode.dataset.scrollText = "";
      centerNode.dataset.scrollChars = "";
    }
  }

  function renderOledPreviewCenter(centerNode, text, maxChars, hidden) {
    if (!centerNode) {
      stopOledPreviewScroll(null);
      return;
    }

    centerNode.hidden = hidden;
    if (hidden) {
      centerNode.textContent = "";
      stopOledPreviewScroll(centerNode);
      return;
    }

    const value = String(text || "");
    if (value.length <= maxChars) {
      centerNode.textContent = truncateOledText(value, maxChars);
      stopOledPreviewScroll(centerNode);
      return;
    }

    const signature = `${value}\n${maxChars}`;
    if (state.oledPreviewScrollSignature !== signature) {
      stopOledPreviewScroll(centerNode);
      state.oledPreviewScrollSignature = signature;
    }

    centerNode.dataset.scrollText = value;
    centerNode.dataset.scrollChars = String(maxChars);

    const drawFrame = () => {
      if (document.hidden || !centerNode.closest(".tab-panel")?.classList.contains("active")) return;
      centerNode.textContent = oledScrollWindow(value, maxChars, state.oledPreviewScrollOffset);
      state.oledPreviewScrollOffset = (state.oledPreviewScrollOffset + 1) % (`${value}   `.length);
    };

    drawFrame();
    if (!state.oledPreviewScrollTimer) {
      state.oledPreviewScrollTimer = setInterval(drawFrame, oledPreviewScrollIntervalMs);
    }
  }

  function oledCenterText(status) {
    if (!status) {
      return "Idle";
    }
    if (status.ota?.busy) {
      return status.ota.phase || "OTA updating";
    }
    if (status.system?.lastError) {
      return status.system.lastError;
    }
    if (status.playback?.state === "playing") {
      return normalizePlaybackTitle(status.playback.title, status.playback.url) || "Playing";
    }
    if (status.network?.apMode && !status.network?.wifiConnected) {
      return "AP setup mode";
    }
    if (!status.network?.wifiConnected) {
      return "Connecting Wi-Fi";
    }
    return "Idle";
  }

  function renderOledPreview() {
    if (!elements.oledPreview) {
      return;
    }

    if (String(elements.displayType?.value || state.settings?.oled?.displayType || "oled").toLowerCase() === "wape") {
      if (elements.oledPreviewCard) {
        elements.oledPreviewCard.hidden = true;
      }
      return;
    }

    if (elements.oledPreviewCard) {
      elements.oledPreviewCard.hidden = false;
    }

    const status = state.status;
    const enabled = Boolean(namedField("oled.enabled")?.checked ?? state.settings?.oled?.enabled ?? true);
    const { configuredWidth, configuredHeight, rotation, effectiveWidth, effectiveHeight } = oledDimensions();
    const topChars = charsForWidth(effectiveWidth, 1);
    const centerChars = charsForWidth(effectiveWidth, 2);
    const bottomChars = charsForWidth(effectiveWidth, 1);

    const top = status?.network?.wifiConnected
      ? status.network.ip
      : (status?.network?.apMode ? status.network.apSsid : "Booting");
    const center = oledCenterText(status);
    const bottom = `${status?.network?.wifiConnected ? "WiFi" : "AP"} ${Number(status?.battery?.voltage || 0).toFixed(2)}V ${status?.network?.mqttConnected ? "MQTT" : "noMQTT"}`;
    const isUpdating = Boolean(status?.ota?.busy);
    const progress = Number(status?.ota?.progressPercent || 0);
    const topDivider = oledTopDividerY(effectiveHeight);
    const bottomDivider = oledBottomDividerY(effectiveHeight);
    const centerTop = topDivider + 6;
    const centerBottom = bottomDivider - 5;
    const centerHeight = Math.max(18, centerBottom - centerTop);
    const labelY = centerTop;
    const progressBarHeight = 12;
    const progressBarY = Math.min(centerBottom - progressBarHeight, labelY + 12);

    const topNode = oledPreviewNode(".oled-preview-top");
    const centerNode = oledPreviewNode(".oled-preview-center");
    const bottomNode = oledPreviewNode(".oled-preview-bottom");
    const topDividerNode = oledPreviewNode(".oled-preview-divider-top");
    const bottomDividerNode = oledPreviewNode(".oled-preview-divider-bottom");
    if (topNode) {
      topNode.textContent = truncateOledText(top, topChars);
    }
    if (centerNode) {
      renderOledPreviewCenter(centerNode, center, centerChars, isUpdating);
      centerNode.style.top = `${(centerTop / effectiveHeight) * 100}%`;
      centerNode.style.height = `${(centerHeight / effectiveHeight) * 100}%`;
    }
    if (bottomNode) {
      bottomNode.textContent = truncateOledText(bottom, bottomChars);
    }
    if (topDividerNode) {
      topDividerNode.style.top = `${(topDivider / effectiveHeight) * 100}%`;
    }
    if (bottomDividerNode) {
      bottomDividerNode.style.top = `${(bottomDivider / effectiveHeight) * 100}%`;
    }

    if (elements.oledPreviewProgress) {
      elements.oledPreviewProgress.hidden = !isUpdating;
      elements.oledPreviewProgress.style.top = `${(centerTop / effectiveHeight) * 100}%`;
      elements.oledPreviewProgress.style.height = `${(centerHeight / effectiveHeight) * 100}%`;
    }
    if (elements.oledPreviewProgressLabel) {
      elements.oledPreviewProgressLabel.textContent = `${status?.ota?.phase || "Updating"} ${progress}%`;
      elements.oledPreviewProgressLabel.style.minHeight = `${(12 / effectiveHeight) * 100}%`;
      elements.oledPreviewProgressLabel.style.top = `${(labelY / effectiveHeight) * 100}%`;
    }
    if (elements.oledPreviewProgressFill) {
      elements.oledPreviewProgressFill.style.width = `${Math.max(0, Math.min(100, progress))}%`;
      elements.oledPreviewProgressFill.style.top = `${(progressBarY / effectiveHeight) * 100}%`;
      elements.oledPreviewProgressFill.style.height = `${(progressBarHeight / effectiveHeight) * 100}%`;
    }
    if (elements.oledPreviewDisabled) {
      elements.oledPreviewDisabled.hidden = enabled;
    }

    elements.oledPreview.style.aspectRatio = `${effectiveWidth} / ${effectiveHeight}`;
    if (elements.oledPreviewMeta) {
      const orientation = rotation === 90 || rotation === 270 ? "portrait" : "landscape";
      elements.oledPreviewMeta.textContent = `${configuredWidth} x ${configuredHeight} • ${rotation} deg • ${effectiveWidth} x ${effectiveHeight} effective ${orientation}`;
    }
  }

  async function triggerDisplay() {
    await postSimple("/api/display-trigger", "Display trigger queued");
  }

  function bindEvents() {
    elements.displayTriggerButton?.addEventListener("click", () => triggerDisplay().catch(handleError));

    elements.peripheralDisplayAddButton?.addEventListener("click", () => {
      state.peripheralDisplayProfiles = normalizedPeripheralDisplayProfiles();
      if (state.peripheralDisplayProfiles.length >= maxPeripheralDisplays) {
        return;
      }
      state.peripheralDisplayProfiles.push("none");
      renderPeripheralDisplayControls();
      renderPeripheralDiagram();
      savePeripheralProfileSelections();
      elements.peripheralDisplayList?.querySelector(`select[data-peripheral-display-index="${state.peripheralDisplayProfiles.length - 1}"]`)?.focus();
    });

    elements.peripheralDisplayProfile?.addEventListener("change", () => {
      state.peripheralDisplayProfiles = normalizedPeripheralDisplayProfiles();
      state.peripheralDisplayProfiles[0] = String(elements.peripheralDisplayProfile?.value || "none");
      renderPeripheralDisplayControls();
      // The profile select still owns focus during this event. Force the GPIO
      // binding refresh so I2C OLED fields appear immediately.
      syncPeripheralBindingGroups({ force: true });
      renderPeripheralDiagram();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
      queueSettingsSave(150);
    });

    elements.peripheralDisplayList?.addEventListener("change", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLSelectElement)) {
        return;
      }
      if (target.matches("[data-peripheral-helper-signal]")) {
        syncGpioMappingControls();
        renderPeripheralDiagram();
        return;
      }
      const displayIndex = Number(target.dataset.peripheralDisplayIndex);
      if (!Number.isInteger(displayIndex) || displayIndex <= 0) {
        return;
      }
      state.peripheralDisplayProfiles = normalizedPeripheralDisplayProfiles();
      if (displayIndex >= state.peripheralDisplayProfiles.length) {
        return;
      }
      state.peripheralDisplayProfiles[displayIndex] = String(target.value || "none");
      renderPeripheralDisplayControls();
      syncPeripheralBindingGroups();
      renderPeripheralDiagram();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
    });

    elements.peripheralDisplayList?.addEventListener("click", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLButtonElement)) {
        return;
      }

      if (target.dataset.peripheralDisplayAdd === "true") {
        state.peripheralDisplayProfiles = normalizedPeripheralDisplayProfiles();
        if (state.peripheralDisplayProfiles.length >= maxPeripheralDisplays) {
          return;
        }
        state.peripheralDisplayProfiles.push("none");
        renderPeripheralDisplayControls();
        savePeripheralProfileSelections();
        elements.peripheralDisplayList?.querySelector(`select[data-peripheral-display-index="${state.peripheralDisplayProfiles.length - 1}"]`)?.focus();
        return;
      }

      const removeIndex = Number(target.dataset.peripheralDisplayRemove);
      if (!Number.isInteger(removeIndex) || removeIndex <= 0 || removeIndex >= state.peripheralDisplayProfiles.length) {
        return;
      }
      state.peripheralDisplayProfiles.splice(removeIndex, 1);
      removePeripheralHelperBindingsForIndex("display", removeIndex);
      renderPeripheralDisplayControls();
      syncPeripheralBindingGroups();
      renderPeripheralDiagram();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
    });

    elements.displayType?.addEventListener("change", () => {
      updateDisplayModeUi();
      populateOledPinOptions();
      populateWapeTriggerPinOptions();
      renderOledPreview();
      renderGpioOverview();
    });
  }

  return {
    bindEvents,
    renderPeripheralDisplayControls,
    updateDisplayModeUi,
    renderOledPreview,
    triggerDisplay,
  };
}
