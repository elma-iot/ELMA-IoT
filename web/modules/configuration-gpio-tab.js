import {gpioRoleWireColor} from './peripheral-pin-model.js';
export function createConfigurationGpioTab({
  state,
  elements,
  gpioBoardLayouts,
  gpioBoardExtraLayouts,
  gpioBoardReservedPins,
  gpioBoardAssets,
  gpioBoardPresentation,
  ensureUiSettings,
  queueSettingsSave,
  detectGpioBoardProfile,
  activeGpioBoardProfile,
  gpioRoleMap,
  gpioConfigRoleState,
  gpioConfigOptions,
  gpioDynamicFieldId,
  gpioDynamicFieldName,
  escapeHtml,
  renderPeripheralDiagram,
  setMessage,
  syncGpioMappingControls,
  loadStatus,
  handleError,
  isPcDesignerRuntime = false,
  renderAssignmentWarnings = () => {},
}) {
  const GPIO_LEGEND_ITEMS = {
    free: {
      swatchClass: "gpio-board-legend-swatch-free",
      label: "Free GPIO",
      description: "Best for PWM outputs, buttons, triggers, and most user peripherals.",
    },
    occupied: {
      swatchClass: "gpio-board-legend-swatch-occupied",
      label: "Active assignment",
      description: "Already claimed by one of your current peripheral or system roles.",
    },
    camera: {
      swatchClass: "gpio-board-legend-swatch-camera",
      label: "Camera / board-routed",
      description: "Committed to onboard camera or board-specific routing. Avoid for add-on peripherals.",
    },
    strap: {
      swatchClass: "gpio-board-legend-swatch-strap",
      label: "Boot / strap pin",
      description: "Affects boot behavior. Use only when you understand the startup constraints.",
    },
    psram: {
      swatchClass: "gpio-board-legend-swatch-psram",
      label: "PSRAM reserved",
      description: "Wired to onboard PSRAM and not suitable for external peripherals.",
    },
    sd: {
      swatchClass: "gpio-board-legend-swatch-sd",
      label: "Storage reserved",
      description: "Used by onboard or board-routed SD storage lines.",
    },
    jtag: {
      swatchClass: "gpio-board-legend-swatch-jtag",
      label: "Debug / JTAG",
      description: "Usually better left alone unless you intentionally need debug wiring.",
    },
    usb: {
      swatchClass: "gpio-board-legend-swatch-usb",
      label: "USB data",
      description: "Reserved for native USB wiring on boards that expose USB D+ / D-.",
    },
    serial: {
      swatchClass: "gpio-board-legend-swatch-serial",
      label: "Serial / flashing",
      description: "Shared with UART logging, flashing, or onboard TX/RX activity.",
    },
    onboard: {
      swatchClass: "gpio-board-legend-swatch-onboard",
      label: "Onboard device",
      description: "Connected to built-in hardware such as the status RGB LED.",
    },
    v5: {
      swatchClass: "gpio-board-legend-swatch-5v",
      label: "5V rail",
      description: "Power only, not a GPIO signal pin.",
    },
    gnd: {
      swatchClass: "gpio-board-legend-swatch-gnd",
      label: "Ground",
      description: "Signal reference / return path for peripherals.",
    },
    v33: {
      swatchClass: "gpio-board-legend-swatch-3v3",
      label: "3.3V rail",
      description: "3.3V power output, not a GPIO signal pin.",
    },
  };
  const GPIO_RESERVED_KIND_ORDER = ["camera", "strap", "psram", "sd", "jtag", "usb", "serial", "onboard"];

  function saveGpioBoardPreferences() {
    const ui = ensureUiSettings();
    ui.gpioBoardAutodetect = Boolean(elements.gpioBoardAutodetect?.checked ?? true);
    ui.gpioBoardSelection = elements.gpioBoardSelector?.value && gpioBoardLayouts[elements.gpioBoardSelector.value]
      ? String(elements.gpioBoardSelector.value)
      : "";
    if (!state.settingsLoading) {
      queueSettingsSave(0);
    }
  }

  function restoreGpioBoardPreferences() {
    const ui = ensureUiSettings();
    if (elements.gpioBoardAutodetect) {
      elements.gpioBoardAutodetect.checked = ui.gpioBoardAutodetect;
    }
    if (elements.gpioBoardSelector && ui.gpioBoardSelection && gpioBoardLayouts[ui.gpioBoardSelection]) {
      elements.gpioBoardSelector.value = ui.gpioBoardSelection;
    }
  }

  function isGpioUiInteracting() {
    if (state.gpioUiInteractionDepth > 0 || state.gpioRoleMenuOpen) {
      return true;
    }
    const activeElement = document.activeElement;
    return activeElement === elements.gpioBoardSelector ||
      activeElement === elements.gpioBoardAutodetect ||
      (activeElement instanceof HTMLSelectElement && activeElement.matches("[data-gpio-role-select]"));
  }

  function updateGpioBoardSelectorMode(status = state.status, options = {}) {
    if (!elements.gpioBoardSelector) {
      return;
    }
    const { force = false } = options;
    const compiledBoard = String(status?.hardware?.boardProfile || "").trim();
    if (isPcDesignerRuntime) {
      // The EXE keeps the complete board catalog. Autodetect may temporarily
      // disable manual selection, but unticking it must immediately expose the
      // full list so one concrete board can be baked into the firmware.
      const autodetectEnabled = Boolean(elements.gpioBoardAutodetect?.checked);
      elements.gpioBoardSelector.disabled = autodetectEnabled;
      elements.gpioBoardSelector.title = autodetectEnabled
        ? "Board autodetect is on. Untick Board Autodetect to choose a board manually."
        : "Choose the concrete board to include in the generated firmware.";
      if (elements.gpioBoardAutodetect) {
        elements.gpioBoardAutodetect.disabled = false;
        elements.gpioBoardAutodetect.title = "Automatically choose a compatible board from the detected target chip.";
        const autodetectLabel = elements.gpioBoardAutodetect.closest("label");
        autodetectLabel?.removeAttribute("hidden");
        if (autodetectLabel) {
          autodetectLabel.title = "Automatically choose a compatible board from the detected target chip.";
        }
      }
      const selectedBoard = String(elements.gpioBoardSelector.value || "");
      if (!selectedBoard || !gpioBoardLayouts[selectedBoard]) {
        elements.gpioBoardSelector.value = "esp32-s3-super-mini";
      }
      return;
    }
    if (!isPcDesignerRuntime && compiledBoard && gpioBoardLayouts[compiledBoard]) {
      for (const option of [...elements.gpioBoardSelector.options]) {
        if (option.value !== compiledBoard) {
          option.remove();
        }
      }
      elements.gpioBoardSelector.value = compiledBoard;
      elements.gpioBoardSelector.disabled = true;
      if (elements.gpioBoardAutodetect) {
        elements.gpioBoardAutodetect.checked = false;
        elements.gpioBoardAutodetect.disabled = true;
        elements.gpioBoardAutodetect.closest("label")?.setAttribute("hidden", "");
      }
      const ui = ensureUiSettings();
      ui.gpioBoardAutodetect = false;
      ui.gpioBoardSelection = compiledBoard;
      return;
    }
    const autodetectEnabled = Boolean(elements.gpioBoardAutodetect?.checked ?? true);
    elements.gpioBoardSelector.disabled = autodetectEnabled;
    if (!autodetectEnabled) {
      const selectedBoard = String(elements.gpioBoardSelector.value || "");
      if (!selectedBoard || !gpioBoardLayouts[selectedBoard]) {
        elements.gpioBoardSelector.value = "esp32-s3-super-mini";
      }
      return;
    }
    if (!force && isGpioUiInteracting()) {
      return;
    }
    const detectedBoard = detectGpioBoardProfile(status);
    if ([...elements.gpioBoardSelector.options].some((option) => option.value === detectedBoard)) {
      elements.gpioBoardSelector.value = detectedBoard;
    }
  }

  function gpioReservedPinInfo(pin, boardProfile = activeGpioBoardProfile()) {
    return gpioBoardReservedPins[boardProfile]?.[pin] || null;
  }

  function inferBoardAdcHint(boardProfile) {
    const normalized = String(boardProfile || "").trim().toLowerCase();
    if (normalized.startsWith("esp32-s3") || normalized === "esp32-spk-n16r8") {
      return "ADC sensors: prefer free GPIO1-GPIO20 when you need analog input. PWM outputs can use any green free GPIO that is not already assigned.";
    }
    if (normalized === "esp32-wrover" || normalized === "esp32-wroom" || normalized === "esp32-mini" || normalized === "wemos-lolin32-mini" || normalized === "wemos-d1-mini-esp32") {
      return "ADC sensors: prefer free GPIO32, 33, 34, 35, 36, 39, 25, 26, 27, 14, 13, 12, 15, 4, 2, or 0. PWM outputs should go to green free GPIO rows.";
    }
    if (normalized === "esp32-s2-psram" || normalized === "esp32-s2-wemos-mini") {
      return "ADC and PWM support depends on the exposed ESP32-S2 pins here. Use green free GPIO rows first, then confirm analog-capable choices in the Battery ADC selector.";
    }
    if (normalized === "esp32-c6" || normalized === "esp32-c3") {
      return "Compact C-series boards have tighter analog and peripheral limits. Use green free GPIO rows first, then confirm ADC-capable choices in the Battery ADC selector.";
    }
    return "Use green free GPIO rows for most peripherals. Confirm analog-capable choices in the Battery ADC selector before wiring ADC sensors.";
  }

  function boardLegendEntries(boardProfile) {
    const entries = [GPIO_LEGEND_ITEMS.free, GPIO_LEGEND_ITEMS.occupied, GPIO_LEGEND_ITEMS.v33, GPIO_LEGEND_ITEMS.v5, GPIO_LEGEND_ITEMS.gnd];
    const reservedKinds = [...new Set(
      Object.values(gpioBoardReservedPins[boardProfile] || {})
        .map((info) => String(info?.kind || "").trim().toLowerCase())
        .filter(Boolean),
    )];
    for (const kind of GPIO_RESERVED_KIND_ORDER) {
      if (reservedKinds.includes(kind) && GPIO_LEGEND_ITEMS[kind]) {
        entries.push(GPIO_LEGEND_ITEMS[kind]);
      }
    }
    return entries;
  }

  function gpioBoardLegendMarkup(boardProfile) {
    const entries = boardLegendEntries(boardProfile);
    return `
      <div class="gpio-board-legend" aria-label="GPIO color legend">
        <div class="gpio-board-legend-title">GPIO color guide</div>
        ${entries.map((entry) => `
          <div class="gpio-board-legend-item">
            <span class="gpio-board-legend-swatch ${escapeHtml(entry.swatchClass)}" aria-hidden="true"></span>
            <span class="gpio-board-legend-copy"><strong>${escapeHtml(entry.label)}</strong> ${escapeHtml(entry.description)}</span>
          </div>
        `).join("")}
        <div class="gpio-board-legend-note">${escapeHtml(inferBoardAdcHint(boardProfile))}</div>
      </div>
    `;
  }

  function gpioReservedRowClass(reservedInfo) {
    switch (reservedInfo?.kind) {
      case "camera":
        return "gpio-pin-row gpio-pin-row-reserved-camera";
      case "strap":
        return "gpio-pin-row gpio-pin-row-reserved-strap";
      case "psram":
        return "gpio-pin-row gpio-pin-row-reserved-psram";
      case "sd":
        return "gpio-pin-row gpio-pin-row-reserved-sd";
      case "jtag":
        return "gpio-pin-row gpio-pin-row-reserved-jtag";
      case "usb":
        return "gpio-pin-row gpio-pin-row-reserved-usb";
      case "serial":
        return "gpio-pin-row gpio-pin-row-reserved-serial";
      case "onboard":
        return "gpio-pin-row gpio-pin-row-reserved-onboard";
      default:
        return "gpio-pin-row gpio-pin-row-occupied";
    }
  }

  function gpioDropdownMarkup(item, roleMap, roleState, identityScope = "") {
    const rawPin = item?.pin;
    const hasNumericPin = typeof rawPin === "number" && Number.isFinite(rawPin) && rawPin >= 0;
    const numericPin = hasNumericPin ? rawPin : NaN;
    const label = String(item?.label || (hasNumericPin ? `GPIO${numericPin}` : "Pin"));
    const selectIdentity = hasNumericPin
      ? ` id="${escapeHtml(gpioDynamicFieldId(numericPin, identityScope))}" name="${escapeHtml(gpioDynamicFieldName(numericPin, identityScope))}"`
      : "";
    if (!hasNumericPin) {
      const normalizedLabel = label.trim().toUpperCase();
      const rowClass = normalizedLabel === "5V" || normalizedLabel === "5V IN" || normalizedLabel === "VBUS"
        ? "gpio-pin-row gpio-pin-row-5v"
        : (normalizedLabel === "GND"
          ? "gpio-pin-row gpio-pin-row-gnd"
          : (normalizedLabel === "3V3" || normalizedLabel === "3.3V"
            ? "gpio-pin-row gpio-pin-row-3v3"
            : "gpio-pin-row"));
      return `
        <div class="${rowClass}">
          <span class="gpio-pin-label">${escapeHtml(label)}</span>
          <span class="gpio-pin-fixed" aria-label="${escapeHtml(label)} assignment">Fixed</span>
        </div>
      `;
    }

    const currentRoleKey = roleState.pinToRole.get(numericPin) || "";
    const currentDefinition = currentRoleKey ? roleState.byKey.get(currentRoleKey) : null;
    const activeRoles = roleMap.get(numericPin) || [];
    const reservedInfo = gpioReservedPinInfo(numericPin);
    const warningTitle = reservedInfo?.warning ? ` title="${escapeHtml(reservedInfo.warning)}"` : "";
    if (reservedInfo) {
      return `
        <label class="${gpioReservedRowClass(reservedInfo)}"${warningTitle}>
          <span class="gpio-pin-label">${escapeHtml(label)}</span>
          <select disabled${selectIdentity} aria-label="${escapeHtml(label)} assignment"${warningTitle}>
            <option selected>${escapeHtml(reservedInfo.label)}</option>
          </select>
        </label>
      `;
    }
    const selectedLabel = currentDefinition?.label || (activeRoles.length ? activeRoles.join(" + ") : "Unused");
    const occupiedStyle=currentRoleKey||activeRoles.length?` style="--gpio-wire-color:${escapeHtml(gpioRoleWireColor(currentRoleKey,selectedLabel))}"`:'';
    if (!currentDefinition && activeRoles.length) {
      return `
        <label class="gpio-pin-row gpio-pin-row-occupied"${occupiedStyle}>
          <span class="gpio-pin-label">${escapeHtml(label)}</span>
          <select disabled${selectIdentity} aria-label="${escapeHtml(label)} assignment">
            <option selected>${escapeHtml(selectedLabel)}</option>
          </select>
        </label>
      `;
    }

    const options = gpioConfigOptions(numericPin, currentRoleKey, roleState);
    const rowClass = currentRoleKey || activeRoles.length ? "gpio-pin-row gpio-pin-row-occupied" : "gpio-pin-row gpio-pin-row-unused";
    return `
      <label class="${rowClass}"${occupiedStyle}>
        <span class="gpio-pin-label">${escapeHtml(label)}</span>
        <select data-gpio-role-select="true" data-pin="${numericPin}"${selectIdentity} aria-label="${escapeHtml(label)} assignment">
          ${currentRoleKey ? "" : `<option value="" selected>${escapeHtml(selectedLabel)}</option>`}
          ${currentRoleKey ? `<option value="__unused__">Unused</option>` : ""}
          ${options.map((definition) => `<option value="${escapeHtml(definition.key)}"${definition.key === currentRoleKey ? " selected" : ""}>${escapeHtml(definition.label)}</option>`).join("")}
        </select>
      </label>
    `;
  }

  function renderGpioPinColumn(columnElement, items, roleMap, roleState, identityScope = "") {
    if (!columnElement) {
      return;
    }
    columnElement.innerHTML = (items || []).map((item, index) => gpioDropdownMarkup(item, roleMap, roleState, `${identityScope}-${index}`)).join("");
  }

  function setGpioExtraExpanded(expanded) {
    if (!elements.gpioExtraToggle || !elements.gpioExtraPanel) {
      return;
    }
    elements.gpioExtraToggle.setAttribute("aria-expanded", expanded ? "true" : "false");
    elements.gpioExtraPanel.classList.toggle("gpio-extra-panel-expanded", expanded);
    elements.gpioExtraPanel.setAttribute("aria-hidden", expanded ? "false" : "true");
  }

  function renderGpioOverview() {
    if (!elements.gpioLeftPins || !elements.gpioRightPins) {
      return;
    }
    const roleMap = gpioRoleMap(state.settings || {}, state.status || {});
    const roleState = gpioConfigRoleState(state.settings || {});
    const selectedBoard = String(elements.gpioBoardSelector?.value || "esp32-s3-super-mini");
    renderAssignmentWarnings();
    const layout = gpioBoardLayouts[selectedBoard] || gpioBoardLayouts["esp32-s3-super-mini"];
    renderGpioPinColumn(elements.gpioLeftPins, layout.left, roleMap, roleState, `${selectedBoard}-main-left`);
    renderGpioPinColumn(elements.gpioRightPins, layout.right, roleMap, roleState, `${selectedBoard}-main-right`);

    const extraLayout = gpioBoardExtraLayouts[selectedBoard];
    if (elements.gpioExtraSection) {
      elements.gpioExtraSection.hidden = !extraLayout;
    }
    if (extraLayout) {
      renderGpioPinColumn(elements.gpioExtraLeftPins, extraLayout.left, roleMap, roleState, `${selectedBoard}-extra-left`);
      renderGpioPinColumn(elements.gpioExtraRightPins, extraLayout.right, roleMap, roleState, `${selectedBoard}-extra-right`);
    } else {
      renderGpioPinColumn(elements.gpioExtraLeftPins, [], roleMap, roleState, `${selectedBoard}-extra-left`);
      renderGpioPinColumn(elements.gpioExtraRightPins, [], roleMap, roleState, `${selectedBoard}-extra-right`);
      setGpioExtraExpanded(false);
    }
  }

  function updateGpioBoardImage() {
    if (!elements.gpioBoardSelector || !elements.gpioBoardImage) {
      return;
    }
    const selectedBoard = String(elements.gpioBoardSelector.value || "esp32-s3-super-mini");
    const asset = gpioBoardAssets[selectedBoard] || gpioBoardAssets["esp32-s3-super-mini"];
    const presentation = gpioBoardPresentation[selectedBoard] || gpioBoardPresentation["esp32-s3-super-mini"];
    elements.gpioBoardImage.src = asset.src;
    elements.gpioBoardImage.alt = asset.alt;
    elements.gpioBoardImage.style.transform = presentation.rotation;
    if (elements.peripheralDiagramBoardImage) {
      elements.peripheralDiagramBoardImage.src = asset.src;
      elements.peripheralDiagramBoardImage.alt = `${asset.alt} in peripheral diagram`;
      elements.peripheralDiagramBoardImage.style.transform = presentation.rotation;
    }
    if (elements.gpioBoardRecommendations) {
      elements.gpioBoardRecommendations.className = `gpio-board-recommendations gpio-board-recommendations-${presentation.tone || "neutral"}`;
      elements.gpioBoardRecommendations.innerHTML = `
        <div><strong>${escapeHtml(presentation.rank)}</strong></div>
        <div>${escapeHtml(presentation.recommendation)}</div>
        ${gpioBoardLegendMarkup(selectedBoard)}
      `;
    }
    renderPeripheralDiagram();
    renderGpioOverview();
  }

  function applyGpioRoleSelection(pin, selectedRoleKey) {
    const numericPin = Number(pin);
    if (!Number.isFinite(numericPin)) {
      state.gpioRoleMenuOpen = false;
      renderGpioOverview();
      return;
    }

    const roleState = gpioConfigRoleState(state.settings || {});
    const currentRoleKey = roleState.pinToRole.get(numericPin) || "";
    const currentDefinition = currentRoleKey ? roleState.byKey.get(currentRoleKey) : null;
    const assignDefinitionValue = (definition, value) => {
      if (!definition) {
        return;
      }
      if (typeof definition.setValue === "function") {
        definition.setValue(value);
      } else if (definition.element) {
        definition.element.value = String(value ?? "");
        if (definition.key === "battery.adcPin") {
          definition.element.dispatchEvent(new Event("change", { bubbles: true }));
        }
      }
    };

    if (selectedRoleKey === "__unused__") {
      if (!currentDefinition?.element && typeof currentDefinition?.setValue !== "function") {
        state.gpioRoleMenuOpen = false;
        renderGpioOverview();
        return;
      }
      if (currentDefinition.unusedValue !== undefined) {
        assignDefinitionValue(currentDefinition, currentDefinition.unusedValue);
        syncGpioMappingControls();
        state.gpioRoleMenuOpen = false;
        queueSettingsSave(150);
        return;
      }
      assignDefinitionValue(currentDefinition, "");
      state.gpioRoleMenuOpen = false;
      setMessage(`${currentDefinition.label} marked Unused. Select another GPIO to reassign it.`);
      renderGpioOverview();
      return;
    }

    if (!selectedRoleKey) {
      state.gpioRoleMenuOpen = false;
      renderGpioOverview();
      return;
    }

    const selectedDefinition = roleState.byKey.get(selectedRoleKey);
    if (!selectedDefinition?.element && typeof selectedDefinition?.setValue !== "function") {
      state.gpioRoleMenuOpen = false;
      renderGpioOverview();
      return;
    }

    if (currentRoleKey === selectedRoleKey) {
      state.gpioRoleMenuOpen = false;
      renderGpioOverview();
      return;
    }

    const previousPinForSelectedRole = roleState.roleToPin.get(selectedRoleKey);
    if (currentRoleKey && previousPinForSelectedRole === null) {
      state.gpioRoleMenuOpen = false;
      renderGpioOverview();
      return;
    }

    assignDefinitionValue(selectedDefinition, numericPin);

    if (currentRoleKey && previousPinForSelectedRole !== null && previousPinForSelectedRole !== numericPin) {
      const displacedDefinition = roleState.byKey.get(currentRoleKey);
      assignDefinitionValue(displacedDefinition, previousPinForSelectedRole);
    }

    syncGpioMappingControls();
    state.gpioRoleMenuOpen = false;
    queueSettingsSave(150);
  }

  function bindEvents() {
    elements.gpioBoardSelector?.addEventListener("change", () => {
      const board=activeGpioBoardProfile();
      const onboardLed=Object.entries(gpioBoardReservedPins[board]||{}).find(([,meta])=>meta.kind==='onboard' && /LED|WS2812|RGB red/i.test(meta.label||''));
      const led=onboardLed?{[board]:Number(onboardLed[0])}:({"esp32-c3":8,"esp32-s3-super-mini":48,"esp32-s3-zero":48});
      // Explicit board changes choose its built-in LED; loading saved projects keeps overrides.
      if(elements.statusLedPin && led[board]!==undefined){
        const value=String(led[board]);
        if(![...elements.statusLedPin.options].some(option=>option.value===value))elements.statusLedPin.add(new Option(`GPIO${value}`,value));
        elements.statusLedPin.value=value;
        state.settings.device.statusLedPin=led[board];
      }
      saveGpioBoardPreferences();
      syncGpioMappingControls();
      updateGpioBoardImage();
      queueSettingsSave(150);
    });

    elements.gpioBoardAutodetect?.addEventListener("change", async () => {
      saveGpioBoardPreferences();
      if (elements.gpioBoardAutodetect?.checked) {
        try {
          const status = await loadStatus();
          updateGpioBoardSelectorMode(status, { force: true });
        } catch (error) {
          handleError(error);
          return;
        }
      } else {
        updateGpioBoardSelectorMode(state.status, { force: true });
      }
      const board=activeGpioBoardProfile();
      const led={"esp32-c3":8,"esp32-s3-super-mini":48,"esp32-s3-zero":48,"esp32-s3-devkit-c1":48,"wemos-lolin32-mini":22,"wemos-d1-mini-esp32":2,"esp32-wroom":2,"esp32-mini":2,"esp32-wrover":2};
      // Explicit board changes choose its built-in LED; loading saved projects keeps overrides.
      if(elements.statusLedPin && led[board]!==undefined){
        const value=String(led[board]);
        if(![...elements.statusLedPin.options].some(option=>option.value===value))elements.statusLedPin.add(new Option(`GPIO${value}`,value));
        elements.statusLedPin.value=value;
        state.settings.device.statusLedPin=led[board];
      }
      saveGpioBoardPreferences();
      syncGpioMappingControls();
      updateGpioBoardImage();
      queueSettingsSave(150);
    });

    elements.gpioExtraToggle?.addEventListener("click", () => {
      const expanded = elements.gpioExtraToggle?.getAttribute("aria-expanded") === "true";
      setGpioExtraExpanded(!expanded);
    });

    for (const gpioColumn of [elements.gpioLeftPins, elements.gpioRightPins, elements.gpioExtraLeftPins, elements.gpioExtraRightPins]) {
      gpioColumn?.addEventListener("pointerdown", (event) => {
        const target = event.target;
        if (target instanceof HTMLSelectElement && target.matches("[data-gpio-role-select]")) {
          state.gpioRoleMenuOpen = true;
        }
      });
      gpioColumn?.addEventListener("focusin", () => {
        state.gpioUiInteractionDepth += 1;
      });
      gpioColumn?.addEventListener("focusout", () => {
        state.gpioUiInteractionDepth = Math.max(0, state.gpioUiInteractionDepth - 1);
        window.setTimeout(() => {
          const activeElement = document.activeElement;
          if (!(activeElement instanceof HTMLSelectElement && activeElement.matches("[data-gpio-role-select]"))) {
            state.gpioRoleMenuOpen = false;
          }
        }, 0);
      });
      gpioColumn?.addEventListener("change", (event) => {
        const target = event.target;
        if (!(target instanceof HTMLSelectElement) || !target.matches("[data-gpio-role-select]")) {
          return;
        }
        applyGpioRoleSelection(target.dataset.pin, target.value);
      });
    }
  }

  return {
    saveGpioBoardPreferences,
    restoreGpioBoardPreferences,
    isGpioUiInteracting,
    updateGpioBoardSelectorMode,
    updateGpioBoardImage,
    setGpioExtraExpanded,
    renderGpioOverview,
    applyGpioRoleSelection,
    bindEvents,
  };
}
