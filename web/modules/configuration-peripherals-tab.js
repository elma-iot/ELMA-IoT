import {arrayControls} from './led-array.js';
export function createConfigurationPeripheralsTab({
  state,
  elements,
  gpioTabElement,
  batteryDividerSensorProfile,
  maxPeripheralAudioOutputs,
  maxPeripheralAudioInputs,
  maxPeripheralSensors,
  maxPeripheralInputs,
  maxPeripheralStorages,
  maxPeripheralPowers,
  maxPeripheralControls,
  maxPeripheralExpansions,
  maxPeripheralCommunications,
  peripheralAudioProfileOptions,
  peripheralAudioInProfileOptions,
  peripheralSensorProfileOptions,
  peripheralInputProfileOptions,
  peripheralStorageProfileOptions,
  peripheralControlProfileOptions,
  peripheralExpansionProfileOptions,
  peripheralCommunicationProfileOptions,
  peripheralPowerProfileOptions,
  normalizedPeripheralAudioProfiles,
  normalizedPeripheralAudioInProfiles,
  normalizedPeripheralSensorProfiles,
  normalizedPeripheralInputProfiles,
  normalizedPeripheralStorageProfiles,
  normalizedPeripheralControlProfiles,
  normalizedPeripheralExpansionProfiles,
  normalizedPeripheralCommunicationProfiles,
  normalizedPeripheralPowerProfiles,
  sanitizeStoredPeripheralProfiles,
  updatePrimaryPeripheralIndexLabel,
  appendPeripheralOptions,
  buildPeripheralProfileComposite,
  peripheralProfileInstanceLabel,
  buildPeripheralActionButton,
  renderPeripheralDiagram,
  syncPeripheralBindingGroups,
  syncGpioMappingControls,
  savePeripheralProfileSelections,
  queueSettingsSave,
  onPeripheralConfigurationChange,
  gpioConfigRoleState,
  setPeripheralHelperBindingValue,
  removePeripheralHelperBindingsForIndex,
  isTopPeripheralSelect,
}) {
  function syncBatteryAdcSelection() {
    const batteryDividerSelected = state.peripheralSensorProfiles
      .some((profile) => String(profile || "").trim().toLowerCase() === batteryDividerSensorProfile);
    if (!batteryDividerSelected && elements.batteryAdcPin && Number(elements.batteryAdcPin.value || 0) > 0) {
      elements.batteryAdcPin.value = "0";
    }
  }

  function renderPeripheralAudioOutputControls() {
    state.peripheralAudioProfiles = normalizedPeripheralAudioProfiles();
    updatePrimaryPeripheralIndexLabel(elements.peripheralAudioPrimaryIndexLabel, "Audio Out", state.peripheralAudioProfiles.length);
    if (elements.peripheralAudioAddButton) {
      const total = state.peripheralAudioProfiles.length;
      elements.peripheralAudioAddButton.disabled = total >= maxPeripheralAudioOutputs;
      elements.peripheralAudioAddButton.title = total >= maxPeripheralAudioOutputs ? `Maximum of ${maxPeripheralAudioOutputs} audio outs reached` : "Add another audio out";
      elements.peripheralAudioAddButton.setAttribute("aria-label", elements.peripheralAudioAddButton.title);
    }
    if (!elements.peripheralAudioOutputsList) {
      return;
    }

    elements.peripheralAudioOutputsList.innerHTML = "";
    const total = state.peripheralAudioProfiles.length;
    state.peripheralAudioProfiles.slice(1).forEach((selectedValue, offset) => {
      const index = offset + 1;
      const row = document.createElement("div");
      row.className = "peripheral-profile-row";

      const select = document.createElement("select");
      select.dataset.peripheralAudioIndex = String(index);
      select.setAttribute("aria-label", `Audio out ${index + 1} peripheral profile`);
      appendPeripheralOptions(select, peripheralAudioProfileOptions, selectedValue);
      row.appendChild(buildPeripheralProfileComposite("audio", selectedValue, index, select, peripheralProfileInstanceLabel("Audio Out", index, total)));
      row.appendChild(buildPeripheralActionButton({
        addDatasetKey: "peripheralAudioAdd",
        removeDatasetKey: "peripheralAudioRemove",
        index,
        total,
        maxCount: maxPeripheralAudioOutputs,
        singularLabel: "audio out",
      }));
      elements.peripheralAudioOutputsList.appendChild(row);
    });
  }

  function renderPeripheralAudioInControls() {
    state.peripheralAudioInProfiles = normalizedPeripheralAudioInProfiles();
    updatePrimaryPeripheralIndexLabel(elements.peripheralAudioInPrimaryIndexLabel, "Audio In", state.peripheralAudioInProfiles.length);
    if (elements.peripheralAudioInAddButton) {
      const total = state.peripheralAudioInProfiles.length;
      elements.peripheralAudioInAddButton.disabled = total >= maxPeripheralAudioInputs;
      elements.peripheralAudioInAddButton.title = total >= maxPeripheralAudioInputs ? `Maximum of ${maxPeripheralAudioInputs} audio inputs reached` : "Add another audio input";
      elements.peripheralAudioInAddButton.setAttribute("aria-label", elements.peripheralAudioInAddButton.title);
    }
    if (!elements.peripheralAudioInList) {
      return;
    }

    elements.peripheralAudioInList.innerHTML = "";
    const total = state.peripheralAudioInProfiles.length;
    state.peripheralAudioInProfiles.slice(1).forEach((selectedValue, offset) => {
      const index = offset + 1;
      const row = document.createElement("div");
      row.className = "peripheral-profile-row";

      const select = document.createElement("select");
      select.dataset.peripheralAudioInIndex = String(index);
      select.setAttribute("aria-label", `Audio in ${index + 1} peripheral profile`);
      appendPeripheralOptions(select, peripheralAudioInProfileOptions, selectedValue);
      row.appendChild(buildPeripheralProfileComposite("audioIn", selectedValue, index, select, peripheralProfileInstanceLabel("Audio In", index, total)));
      row.appendChild(buildPeripheralActionButton({
        addDatasetKey: "peripheralAudioInAdd",
        removeDatasetKey: "peripheralAudioInRemove",
        index,
        total,
        maxCount: maxPeripheralAudioInputs,
        singularLabel: "audio input",
      }));
      elements.peripheralAudioInList.appendChild(row);
    });
  }

  function renderPeripheralSensorControls() {
    if (!elements.peripheralSensorsList) {
      return;
    }

    state.peripheralSensorProfiles = sanitizeStoredPeripheralProfiles(state.peripheralSensorProfiles, maxPeripheralSensors, ["none"]);
    syncBatteryAdcSelection();
    const total = state.peripheralSensorProfiles.length;
    elements.peripheralSensorsList.innerHTML = "";

    state.peripheralSensorProfiles.forEach((selectedValue, index) => {
      const row = document.createElement("div");
      row.className = "peripheral-profile-row";

      const select = document.createElement("select");
      select.dataset.peripheralSensorIndex = String(index);
      select.setAttribute("aria-label", `Sensor ${index + 1} peripheral profile`);
      appendPeripheralOptions(select, peripheralSensorProfileOptions, selectedValue);
      row.appendChild(buildPeripheralProfileComposite("sensor", selectedValue, index, select, peripheralProfileInstanceLabel("Sensor", index, total)));
      row.appendChild(buildPeripheralActionButton({
        addDatasetKey: "peripheralSensorAdd",
        removeDatasetKey: "peripheralSensorRemove",
        index,
        total,
        maxCount: maxPeripheralSensors,
        singularLabel: "sensor",
      }));
      elements.peripheralSensorsList.appendChild(row);
    });
    renderPeripheralDiagram();
  }

  function renderPeripheralInputControls() {
    if (!elements.peripheralInputsList) {
      return;
    }

    state.peripheralInputProfiles = normalizedPeripheralInputProfiles();
    const total = state.peripheralInputProfiles.length;
    elements.peripheralInputsList.innerHTML = "";

    state.peripheralInputProfiles.forEach((selectedValue, index) => {
      const row = document.createElement("div");
      row.className = "peripheral-profile-row";

      const select = document.createElement("select");
      select.dataset.peripheralInputIndex = String(index);
      select.setAttribute("aria-label", `Input ${index + 1} peripheral profile`);
      appendPeripheralOptions(select, peripheralInputProfileOptions, selectedValue);
      row.appendChild(buildPeripheralProfileComposite("input", selectedValue, index, select, peripheralProfileInstanceLabel("Input", index, total)));
      row.appendChild(buildPeripheralActionButton({
        addDatasetKey: "peripheralInputAdd",
        removeDatasetKey: "peripheralInputRemove",
        index,
        total,
        maxCount: maxPeripheralInputs,
        singularLabel: "input",
      }));
      elements.peripheralInputsList.appendChild(row);
    });
    renderPeripheralDiagram();
  }

  function renderPeripheralStorageControls() {
    if (!elements.peripheralStorageList) {
      return;
    }

    state.peripheralStorageProfiles = sanitizeStoredPeripheralProfiles(
      state.peripheralStorageProfiles,
      maxPeripheralStorages,
      normalizedPeripheralStorageProfiles(),
    );
    const total = state.peripheralStorageProfiles.length;
    elements.peripheralStorageList.innerHTML = "";

    state.peripheralStorageProfiles.forEach((selectedValue, index) => {
      const row = document.createElement("div");
      row.className = "peripheral-profile-row";

      const select = document.createElement("select");
      select.dataset.peripheralStorageIndex = String(index);
      select.setAttribute("aria-label", `Storage ${index + 1} peripheral profile`);
      appendPeripheralOptions(select, peripheralStorageProfileOptions, selectedValue);
      select.addEventListener("change", (event) => {
        event.stopPropagation();
        applyPeripheralStorageProfileSelection(index, String(select.value || "none"));
      });
      row.appendChild(buildPeripheralProfileComposite("storage", selectedValue, index, select, peripheralProfileInstanceLabel("Storage", index, total)));
      row.appendChild(buildPeripheralActionButton({
        addDatasetKey: "peripheralStorageAdd",
        removeDatasetKey: "peripheralStorageRemove",
        index,
        total,
        maxCount: maxPeripheralStorages,
        singularLabel: "storage option",
      }));
      elements.peripheralStorageList.appendChild(row);
    });
    syncPeripheralBindingGroups();
    renderPeripheralDiagram();
  }

  function renderPeripheralPowerControls() {
    if (!elements.peripheralPowerList) {
      return;
    }

    state.peripheralPowerProfiles = sanitizeStoredPeripheralProfiles(
      state.peripheralPowerProfiles,
      maxPeripheralPowers,
      normalizedPeripheralPowerProfiles(),
    );
    const total = state.peripheralPowerProfiles.length;
    elements.peripheralPowerList.innerHTML = "";

    state.peripheralPowerProfiles.forEach((selectedValue, index) => {
      const row = document.createElement("div");
      row.className = "peripheral-profile-row";

      const select = document.createElement("select");
      select.dataset.peripheralPowerIndex = String(index);
      select.setAttribute("aria-label", `Power ${index + 1} peripheral profile`);
      appendPeripheralOptions(select, peripheralPowerProfileOptions, selectedValue);
      row.appendChild(buildPeripheralProfileComposite("power", selectedValue, index, select, peripheralProfileInstanceLabel("Power", index, total)));
      row.appendChild(buildPeripheralActionButton({
        addDatasetKey: "peripheralPowerAdd",
        removeDatasetKey: "peripheralPowerRemove",
        index,
        total,
        maxCount: maxPeripheralPowers,
        singularLabel: "power converter",
      }));
      elements.peripheralPowerList.appendChild(row);
    });
    syncPeripheralBindingGroups();
    renderPeripheralDiagram();
  }

  function applyPeripheralStorageProfileSelection(storageIndex, value) {
    state.peripheralStorageProfiles = sanitizeStoredPeripheralProfiles(
      state.peripheralStorageProfiles,
      maxPeripheralStorages,
      normalizedPeripheralStorageProfiles(),
    );
    if (!Number.isInteger(storageIndex) || storageIndex < 0 || storageIndex >= state.peripheralStorageProfiles.length) {
      return;
    }
    state.peripheralStorageProfiles[storageIndex] = String(value || "none");
    renderPeripheralStorageControls();
    syncGpioMappingControls();
    savePeripheralProfileSelections();
    if (storageIndex === 0) {
      queueSettingsSave(150);
    }
  }

  function renderPeripheralControlControls() {
    if (!elements.peripheralControlsList) {
      return;
    }

    state.peripheralControlProfiles = normalizedPeripheralControlProfiles();
    const total = state.peripheralControlProfiles.length;
    const hasArray=state.peripheralControlProfiles.includes('ws2812-neopixel-led-strip');
    const wledTab=document.querySelector('[data-tab="wled"]');
    if(wledTab){
      const wasActive=wledTab.getAttribute('aria-selected')==='true';
      wledTab.hidden=!hasArray;
      if(!hasArray&&wasActive)document.querySelector('[data-tab="gpio"]')?.click();
    }

    elements.peripheralControlsList.innerHTML = "";
    const ledList=document.getElementById('wledControlsList');
    if(ledList)ledList.replaceChildren();

    state.peripheralControlProfiles.forEach((selectedValue, index) => {
      const ledProfile=['ws2812-neopixel-led-strip','led-pwm-dimmer'].includes(selectedValue);
      for(const targetList of [elements.peripheralControlsList,...(ledList&&ledProfile?[ledList]:[])]) {
      const row = document.createElement("div");
      row.className = "peripheral-profile-row";

      const select = document.createElement("select");
      select.dataset.peripheralControlIndex = String(index);
      select.setAttribute("aria-label", `Controls ${index + 1} peripheral profile`);
      const isLed=['ws2812-neopixel-led-strip','led-pwm-dimmer'].includes(selectedValue);
      appendPeripheralOptions(select, isLed ? peripheralControlProfileOptions.filter(option=>['none','ws2812-neopixel-led-strip','led-pwm-dimmer'].includes(option.value)) : peripheralControlProfileOptions, selectedValue);
      row.appendChild(buildPeripheralProfileComposite("control", selectedValue, index, select, peripheralProfileInstanceLabel("Control", index, total)));
      row.appendChild(buildPeripheralActionButton({
        addDatasetKey: "peripheralControlAdd",
        removeDatasetKey: "peripheralControlRemove",
        index,
        total,
        maxCount: maxPeripheralControls,
        singularLabel: "control",
      }));
      if(targetList===ledList){
        const ids=new Map();
        for(const field of row.querySelectorAll('[id]')){ids.set(field.id,'wled-'+field.id);field.id='wled-'+field.id;}
        for(const field of row.querySelectorAll('[for],[aria-labelledby]'))for(const attribute of ['for','aria-labelledby']){
          if(field.hasAttribute(attribute))field.setAttribute(attribute,field.getAttribute(attribute).split(' ').map(id=>ids.get(id)||id).join(' '));
        }
      }
      targetList.appendChild(row);
      if(selectedValue==='ws2812-neopixel-led-strip'){
        state.peripheralHelperBindings ||= {};const key=`control:${index}`;const values=state.peripheralHelperBindings[key]||{};
        const capabilities=state.status?.ledCapabilities;
        targetList.append(arrayControls(values,capabilities?.limits?.[key]||256,next=>{state.peripheralHelperBindings[key]=next;queueSettingsSave(150);renderPeripheralControlControls();renderPeripheralDiagram();},{showPreview:false,compact:targetList===elements.peripheralControlsList,...(capabilities?.effects?{effects:capabilities.effects}:{})}));
      }
      }
    });
  }

  function renderPeripheralExpansionControls() {
    if (!elements.peripheralExpansionsList) {
      return;
    }

    state.peripheralExpansionProfiles = normalizedPeripheralExpansionProfiles();
    const total = state.peripheralExpansionProfiles.length;
    elements.peripheralExpansionsList.innerHTML = "";

    state.peripheralExpansionProfiles.forEach((selectedValue, index) => {
      const row = document.createElement("div");
      row.className = "peripheral-profile-row";

      const select = document.createElement("select");
      select.dataset.peripheralExpansionIndex = String(index);
      select.setAttribute("aria-label", `Expansion ${index + 1} peripheral profile`);
      appendPeripheralOptions(select, peripheralExpansionProfileOptions, selectedValue);
      row.appendChild(buildPeripheralProfileComposite("expansion", selectedValue, index, select, peripheralProfileInstanceLabel("Expansion", index, total)));
      row.appendChild(buildPeripheralActionButton({
        addDatasetKey: "peripheralExpansionAdd",
        removeDatasetKey: "peripheralExpansionRemove",
        index,
        total,
        maxCount: maxPeripheralExpansions,
        singularLabel: "expansion",
      }));
      elements.peripheralExpansionsList.appendChild(row);
    });
    renderPeripheralDiagram();
  }

  function renderPeripheralCommunicationControls() {
    if (!elements.peripheralCommunicationList) {
      return;
    }

    state.peripheralCommunicationProfiles = normalizedPeripheralCommunicationProfiles();
    const total = state.peripheralCommunicationProfiles.length;
    elements.peripheralCommunicationList.innerHTML = "";

    state.peripheralCommunicationProfiles.forEach((selectedValue, index) => {
      const row = document.createElement("div");
      row.className = "peripheral-profile-row";

      const select = document.createElement("select");
      select.dataset.peripheralCommunicationIndex = String(index);
      select.setAttribute("aria-label", `Communication ${index + 1} peripheral profile`);
      appendPeripheralOptions(select, peripheralCommunicationProfileOptions, selectedValue);
      row.appendChild(buildPeripheralProfileComposite("communication", selectedValue, index, select, peripheralProfileInstanceLabel("Communication", index, total)));
      row.appendChild(buildPeripheralActionButton({
        addDatasetKey: "peripheralCommunicationAdd",
        removeDatasetKey: "peripheralCommunicationRemove",
        index,
        total,
        maxCount: maxPeripheralCommunications,
        singularLabel: "communication option",
      }));
      elements.peripheralCommunicationList.appendChild(row);
    });
    renderPeripheralDiagram();
  }

  function bindEvents() {
    elements.peripheralAudioAddButton?.addEventListener("click", () => {
      state.peripheralAudioProfiles = normalizedPeripheralAudioProfiles();
      if (state.peripheralAudioProfiles.length >= maxPeripheralAudioOutputs) {
        return;
      }
      state.peripheralAudioProfiles.push("none");
      renderPeripheralAudioOutputControls();
      renderPeripheralDiagram();
      savePeripheralProfileSelections();
      elements.peripheralAudioOutputsList?.querySelector(`select[data-peripheral-audio-index="${state.peripheralAudioProfiles.length - 1}"]`)?.focus();
    });

    elements.peripheralAudioInAddButton?.addEventListener("click", () => {
      state.peripheralAudioInProfiles = normalizedPeripheralAudioInProfiles();
      if (state.peripheralAudioInProfiles.length >= maxPeripheralAudioInputs) {
        return;
      }
      state.peripheralAudioInProfiles.push("none");
      renderPeripheralAudioInControls();
      renderPeripheralDiagram();
      savePeripheralProfileSelections();
      elements.peripheralAudioInList?.querySelector(`select[data-peripheral-audio-in-index="${state.peripheralAudioInProfiles.length - 1}"]`)?.focus();
    });

    for (const field of [elements.peripheralAudioProfile, elements.peripheralAudioInProfile]) {
      field?.addEventListener("change", () => {
        if (field === elements.peripheralAudioProfile) {
          state.peripheralAudioProfiles = normalizedPeripheralAudioProfiles();
          state.peripheralAudioProfiles[0] = String(elements.peripheralAudioProfile?.value || "none");
          renderPeripheralAudioOutputControls();
        } else if (field === elements.peripheralAudioInProfile) {
          state.peripheralAudioInProfiles = normalizedPeripheralAudioInProfiles();
          state.peripheralAudioInProfiles[0] = String(elements.peripheralAudioInProfile?.value || "none");
          renderPeripheralAudioInControls();
        }
        // This change fires while the profile select still owns focus. Force the
        // binding refresh so an I2S profile immediately exposes WS/BCLK/DOUT.
        syncPeripheralBindingGroups({ force: true });
        renderPeripheralDiagram();
        syncGpioMappingControls();
        savePeripheralProfileSelections();
        if (field === elements.peripheralAudioProfile) {
          queueSettingsSave(150);
        }
      });
    }

    gpioTabElement?.addEventListener("change", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLSelectElement) || !target.matches("[data-peripheral-helper-signal]")) {
        return;
      }
      const helperGroup = String(target.dataset.peripheralHelperGroup || "custom");
      const helperIndex = Number(target.dataset.peripheralHelperIndex || 0);
      const helperSignal = String(target.dataset.peripheralHelperSignal || "SIG");
      const helperProfile = String(state.peripheralSensorProfiles?.[helperIndex] || "none").trim().toLowerCase();
      setPeripheralHelperBindingValue(
        helperGroup,
        helperIndex,
        helperSignal,
        String(target.value || ""),
      );
      syncGpioMappingControls();
      renderPeripheralDiagram();
      if (helperGroup === "sensor" && helperSignal === "GPIO" && helperProfile === batteryDividerSensorProfile) {
        queueSettingsSave(0);
        return;
      }
      queueSettingsSave(0);
    });

    elements.peripheralAudioInList?.addEventListener("change", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLSelectElement)) {
        return;
      }
      if (target.matches("[data-peripheral-helper-signal]")) {
        syncGpioMappingControls();
        renderPeripheralDiagram();
        return;
      }
      const audioInIndex = Number(target.dataset.peripheralAudioInIndex);
      if (!Number.isInteger(audioInIndex) || audioInIndex <= 0) {
        return;
      }
      state.peripheralAudioInProfiles = normalizedPeripheralAudioInProfiles();
      if (audioInIndex >= state.peripheralAudioInProfiles.length) {
        return;
      }
      state.peripheralAudioInProfiles[audioInIndex] = String(target.value || "none");
      renderPeripheralAudioInControls();
      syncPeripheralBindingGroups();
      renderPeripheralDiagram();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
    });

    elements.peripheralAudioInList?.addEventListener("click", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLButtonElement)) {
        return;
      }

      if (target.dataset.peripheralAudioInAdd === "true") {
        state.peripheralAudioInProfiles = normalizedPeripheralAudioInProfiles();
        if (state.peripheralAudioInProfiles.length >= maxPeripheralAudioInputs) {
          return;
        }
        state.peripheralAudioInProfiles.push("none");
        renderPeripheralAudioInControls();
        savePeripheralProfileSelections();
        elements.peripheralAudioInList?.querySelector(`select[data-peripheral-audio-in-index="${state.peripheralAudioInProfiles.length - 1}"]`)?.focus();
        return;
      }

      const removeIndex = Number(target.dataset.peripheralAudioInRemove);
      if (!Number.isInteger(removeIndex) || removeIndex <= 0 || removeIndex >= state.peripheralAudioInProfiles.length) {
        return;
      }
      state.peripheralAudioInProfiles.splice(removeIndex, 1);
      removePeripheralHelperBindingsForIndex("audioIn", removeIndex);
      renderPeripheralAudioInControls();
      syncPeripheralBindingGroups();
      renderPeripheralDiagram();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
    });

    elements.peripheralAudioOutputsList?.addEventListener("change", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLSelectElement)) {
        return;
      }
      if (target.matches("[data-peripheral-helper-signal]")) {
        syncGpioMappingControls();
        savePeripheralProfileSelections();
        renderPeripheralDiagram();
        return;
      }
      const audioIndex = Number(target.dataset.peripheralAudioIndex);
      if (!Number.isInteger(audioIndex) || audioIndex <= 0) {
        return;
      }
      state.peripheralAudioProfiles = normalizedPeripheralAudioProfiles();
      if (audioIndex >= state.peripheralAudioProfiles.length) {
        return;
      }
      state.peripheralAudioProfiles[audioIndex] = String(target.value || "none");
      renderPeripheralAudioOutputControls();
      syncPeripheralBindingGroups();
      renderPeripheralDiagram();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
    });

    elements.peripheralAudioOutputsList?.addEventListener("click", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLButtonElement)) {
        return;
      }

      if (target.dataset.peripheralAudioAdd === "true") {
        state.peripheralAudioProfiles = normalizedPeripheralAudioProfiles();
        if (state.peripheralAudioProfiles.length >= maxPeripheralAudioOutputs) {
          return;
        }
        state.peripheralAudioProfiles.push("none");
        renderPeripheralAudioOutputControls();
        savePeripheralProfileSelections();
        elements.peripheralAudioOutputsList?.querySelector(`select[data-peripheral-audio-index="${state.peripheralAudioProfiles.length - 1}"]`)?.focus();
        return;
      }

      const removeIndex = Number(target.dataset.peripheralAudioRemove);
      if (!Number.isInteger(removeIndex) || removeIndex <= 0 || removeIndex >= state.peripheralAudioProfiles.length) {
        return;
      }
      state.peripheralAudioProfiles.splice(removeIndex, 1);
      removePeripheralHelperBindingsForIndex("audio", removeIndex);
      renderPeripheralAudioOutputControls();
      syncPeripheralBindingGroups();
      renderPeripheralDiagram();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
    });

    elements.peripheralSensorsList?.addEventListener("change", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLSelectElement)) {
        return;
      }
      if (target.matches("[data-peripheral-helper-signal]")) {
        return;
      }
      const sensorIndex = Number(target.dataset.peripheralSensorIndex);
      if (!Number.isInteger(sensorIndex) || sensorIndex < 0 || sensorIndex >= state.peripheralSensorProfiles.length) {
        return;
      }
      state.peripheralSensorProfiles[sensorIndex] = String(target.value || "none");
      syncBatteryAdcSelection();
      renderPeripheralSensorControls();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
      queueSettingsSave(150);
    });

    elements.peripheralSensorsList?.addEventListener("click", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLButtonElement)) {
        return;
      }

      if (target.dataset.peripheralSensorAdd === "true") {
        state.peripheralSensorProfiles = normalizedPeripheralSensorProfiles();
        if (state.peripheralSensorProfiles.length >= maxPeripheralSensors) {
          return;
        }
        state.peripheralSensorProfiles.push("none");
        renderPeripheralSensorControls();
        syncGpioMappingControls();
        savePeripheralProfileSelections();
        queueSettingsSave(150);
        elements.peripheralSensorsList?.querySelector(`select[data-peripheral-sensor-index="${state.peripheralSensorProfiles.length - 1}"]`)?.focus();
        return;
      }

      const removeIndex = Number(target.dataset.peripheralSensorRemove);
      if (!Number.isInteger(removeIndex) || removeIndex <= 0 || removeIndex >= state.peripheralSensorProfiles.length) {
        return;
      }
      state.peripheralSensorProfiles.splice(removeIndex, 1);
      removePeripheralHelperBindingsForIndex("sensor", removeIndex);
      syncBatteryAdcSelection();
      renderPeripheralSensorControls();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
      queueSettingsSave(150);
    });

    elements.peripheralInputsList?.addEventListener("change", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLSelectElement)) {
        return;
      }
      if (target.matches("[data-peripheral-helper-signal]")) {
        setPeripheralHelperBindingValue(
          String(target.dataset.peripheralHelperGroup || "input"),
          Number(target.dataset.peripheralHelperIndex || 0),
          String(target.dataset.peripheralHelperSignal || "SIG"),
          String(target.value || ""),
        );
        syncGpioMappingControls();
        renderPeripheralDiagram();
        queueSettingsSave(0);
        onPeripheralConfigurationChange?.();
        return;
      }
      const inputIndex = Number(target.dataset.peripheralInputIndex);
      if (!Number.isInteger(inputIndex) || inputIndex < 0 || inputIndex >= state.peripheralInputProfiles.length) {
        return;
      }
      state.peripheralInputProfiles[inputIndex] = String(target.value || "none");
      renderPeripheralInputControls();
      syncGpioMappingControls();
      renderPeripheralDiagram();
      savePeripheralProfileSelections();
      queueSettingsSave(150);
      onPeripheralConfigurationChange?.();
    });

    elements.peripheralInputsList?.addEventListener("click", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLButtonElement)) {
        return;
      }

      if (target.dataset.peripheralInputAdd === "true") {
        state.peripheralInputProfiles = normalizedPeripheralInputProfiles();
        if (state.peripheralInputProfiles.length >= maxPeripheralInputs) {
          return;
        }
        state.peripheralInputProfiles.push("none");
        renderPeripheralInputControls();
        syncGpioMappingControls();
        savePeripheralProfileSelections();
        onPeripheralConfigurationChange?.();
        elements.peripheralInputsList?.querySelector(`select[data-peripheral-input-index="${state.peripheralInputProfiles.length - 1}"]`)?.focus();
        return;
      }

      const removeIndex = Number(target.dataset.peripheralInputRemove);
      if (!Number.isInteger(removeIndex) || removeIndex <= 0 || removeIndex >= state.peripheralInputProfiles.length) {
        return;
      }
      state.peripheralInputProfiles.splice(removeIndex, 1);
      removePeripheralHelperBindingsForIndex("input", removeIndex);
      renderPeripheralInputControls();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
      queueSettingsSave(150);
      onPeripheralConfigurationChange?.();
    });

    for(const controlList of [elements.peripheralControlsList,document.getElementById('wledControlsList')]) {
    controlList?.addEventListener("change", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLSelectElement)) {
        return;
      }
      if (target.matches("[data-peripheral-helper-signal]")) {
        setPeripheralHelperBindingValue(
          String(target.dataset.peripheralHelperGroup || "control"),
          Number(target.dataset.peripheralHelperIndex || 0),
          String(target.dataset.peripheralHelperSignal || "IN1"),
          String(target.value || ""),
        );
        renderPeripheralControlControls();
        syncGpioMappingControls();
        renderPeripheralDiagram();
        queueSettingsSave(0);
        onPeripheralConfigurationChange?.();
        return;
      }
      const controlIndex = Number(target.dataset.peripheralControlIndex);
      if (!Number.isInteger(controlIndex) || controlIndex < 0 || controlIndex >= state.peripheralControlProfiles.length) {
        return;
      }
      state.peripheralControlProfiles[controlIndex] = String(target.value || "none");
      renderPeripheralControlControls();
      syncGpioMappingControls();
      renderPeripheralDiagram();
      savePeripheralProfileSelections();
      queueSettingsSave(150);
      onPeripheralConfigurationChange?.();
    });

    controlList?.addEventListener("click", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLButtonElement)) {
        return;
      }

      if (target.dataset.peripheralControlAdd === "true") {
        state.peripheralControlProfiles = normalizedPeripheralControlProfiles();
        if (state.peripheralControlProfiles.length >= maxPeripheralControls) {
          return;
        }
        state.peripheralControlProfiles.push(controlList?.id==='wledControlsList'?'ws2812-neopixel-led-strip':"none");
        renderPeripheralControlControls();
        syncGpioMappingControls();
        savePeripheralProfileSelections();
        onPeripheralConfigurationChange?.();
        elements.peripheralControlsList?.querySelector(`select[data-peripheral-control-index="${state.peripheralControlProfiles.length - 1}"]`)?.focus();
        return;
      }

      const removeIndex = Number(target.dataset.peripheralControlRemove);
      if (!Number.isInteger(removeIndex) || removeIndex <= 0 || removeIndex >= state.peripheralControlProfiles.length) {
        return;
      }
      state.peripheralControlProfiles.splice(removeIndex, 1);
      removePeripheralHelperBindingsForIndex("control", removeIndex);
      renderPeripheralControlControls();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
      onPeripheralConfigurationChange?.();
    });

    }
    document.getElementById('wledAddArray')?.addEventListener('click',()=>{
      let index=state.peripheralControlProfiles.indexOf('none');
      if(index<0){
        if(state.peripheralControlProfiles.length>=maxPeripheralControls)return;
        index=state.peripheralControlProfiles.length;state.peripheralControlProfiles.push('none');
      }
      state.peripheralControlProfiles[index]='ws2812-neopixel-led-strip';
      renderPeripheralControlControls();syncGpioMappingControls();renderPeripheralDiagram();
      savePeripheralProfileSelections();queueSettingsSave(150);onPeripheralConfigurationChange?.();
    });

    elements.peripheralExpansionsList?.addEventListener("change", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLSelectElement)) {
        return;
      }
      if (target.matches("[data-peripheral-helper-signal]")) {
        return;
      }
      const expansionIndex = Number(target.dataset.peripheralExpansionIndex);
      if (!Number.isInteger(expansionIndex) || expansionIndex < 0 || expansionIndex >= state.peripheralExpansionProfiles.length) {
        return;
      }
      state.peripheralExpansionProfiles[expansionIndex] = String(target.value || "none");
      renderPeripheralExpansionControls();
      savePeripheralProfileSelections();
    });

    elements.peripheralExpansionsList?.addEventListener("click", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLButtonElement)) {
        return;
      }

      if (target.dataset.peripheralExpansionAdd === "true") {
        state.peripheralExpansionProfiles = normalizedPeripheralExpansionProfiles();
        if (state.peripheralExpansionProfiles.length >= maxPeripheralExpansions) {
          return;
        }
        state.peripheralExpansionProfiles.push("none");
        renderPeripheralExpansionControls();
        savePeripheralProfileSelections();
        elements.peripheralExpansionsList?.querySelector(`select[data-peripheral-expansion-index="${state.peripheralExpansionProfiles.length - 1}"]`)?.focus();
        return;
      }

      const removeIndex = Number(target.dataset.peripheralExpansionRemove);
      if (!Number.isInteger(removeIndex) || removeIndex <= 0 || removeIndex >= state.peripheralExpansionProfiles.length) {
        return;
      }
      state.peripheralExpansionProfiles.splice(removeIndex, 1);
      removePeripheralHelperBindingsForIndex("expansion", removeIndex);
      renderPeripheralExpansionControls();
      savePeripheralProfileSelections();
    });

    elements.peripheralStorageList?.addEventListener("change", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLSelectElement)) {
        return;
      }
      if (target.matches("[data-peripheral-helper-signal]")) {
        return;
      }
      const roleKey = String(target.dataset.peripheralBindingKey || "");
      if (roleKey) {
        const definition = gpioConfigRoleState(state.settings || {}).byKey.get(roleKey);
        if (!definition?.element) {
          return;
        }
        definition.element.value = String(target.value || definition.element.value || "");
        syncGpioMappingControls();
        queueSettingsSave(150);
        return;
      }
      const storageIndex = Number(target.dataset.peripheralStorageIndex);
      if (!Number.isInteger(storageIndex) || storageIndex < 0 || storageIndex >= state.peripheralStorageProfiles.length) {
        return;
      }
      state.peripheralStorageProfiles[storageIndex] = String(target.value || "none");
      renderPeripheralStorageControls();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
      if (storageIndex === 0) {
        queueSettingsSave(150);
      }
    });

    elements.peripheralStorageList?.addEventListener("click", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLButtonElement)) {
        return;
      }

      if (target.dataset.peripheralStorageAdd === "true") {
        state.peripheralStorageProfiles = normalizedPeripheralStorageProfiles();
        if (state.peripheralStorageProfiles.length >= maxPeripheralStorages) {
          return;
        }
        state.peripheralStorageProfiles.push("none");
        renderPeripheralStorageControls();
        savePeripheralProfileSelections();
        elements.peripheralStorageList?.querySelector(`select[data-peripheral-storage-index="${state.peripheralStorageProfiles.length - 1}"]`)?.focus();
        return;
      }

      const removeIndex = Number(target.dataset.peripheralStorageRemove);
      if (!Number.isInteger(removeIndex) || removeIndex <= 0 || removeIndex >= state.peripheralStorageProfiles.length) {
        return;
      }
      state.peripheralStorageProfiles.splice(removeIndex, 1);
      removePeripheralHelperBindingsForIndex("storage", removeIndex);
      renderPeripheralStorageControls();
      savePeripheralProfileSelections();
    });

    elements.peripheralCommunicationList?.addEventListener("change", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLSelectElement)) {
        return;
      }
      if (target.matches("[data-peripheral-helper-signal]")) {
        return;
      }
      const communicationIndex = Number(target.dataset.peripheralCommunicationIndex);
      if (!Number.isInteger(communicationIndex) || communicationIndex < 0 || communicationIndex >= state.peripheralCommunicationProfiles.length) {
        return;
      }
      state.peripheralCommunicationProfiles[communicationIndex] = String(target.value || "none");
      renderPeripheralCommunicationControls();
      savePeripheralProfileSelections();
    });

    elements.peripheralCommunicationList?.addEventListener("click", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLButtonElement)) {
        return;
      }

      if (target.dataset.peripheralCommunicationAdd === "true") {
        state.peripheralCommunicationProfiles = normalizedPeripheralCommunicationProfiles();
        if (state.peripheralCommunicationProfiles.length >= maxPeripheralCommunications) {
          return;
        }
        state.peripheralCommunicationProfiles.push("none");
        renderPeripheralCommunicationControls();
        savePeripheralProfileSelections();
        elements.peripheralCommunicationList?.querySelector(`select[data-peripheral-communication-index="${state.peripheralCommunicationProfiles.length - 1}"]`)?.focus();
        return;
      }

      const removeIndex = Number(target.dataset.peripheralCommunicationRemove);
      if (!Number.isInteger(removeIndex) || removeIndex <= 0 || removeIndex >= state.peripheralCommunicationProfiles.length) {
        return;
      }
      state.peripheralCommunicationProfiles.splice(removeIndex, 1);
      removePeripheralHelperBindingsForIndex("communication", removeIndex);
      renderPeripheralCommunicationControls();
      savePeripheralProfileSelections();
    });

    elements.peripheralPowerList?.addEventListener("change", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLSelectElement)) {
        return;
      }
      if (target.matches("[data-peripheral-helper-signal]")) {
        setPeripheralHelperBindingValue(
          String(target.dataset.peripheralHelperGroup || "power"),
          Number(target.dataset.peripheralHelperIndex || 0),
          String(target.dataset.peripheralHelperSignal || "INPUT_VOLTAGE"),
          String(target.value || ""),
        );
        renderPeripheralPowerControls();
        syncGpioMappingControls();
        queueSettingsSave(0);
        return;
      }
      const powerIndex = Number(target.dataset.peripheralPowerIndex);
      if (!Number.isInteger(powerIndex) || powerIndex < 0 || powerIndex >= state.peripheralPowerProfiles.length) {
        return;
      }
      state.peripheralPowerProfiles[powerIndex] = String(target.value || "none");
      setPeripheralHelperBindingValue("power", powerIndex, "INPUT_VOLTAGE", "");
      setPeripheralHelperBindingValue("power", powerIndex, "OUTPUT_VOLTAGE", "");
      renderPeripheralPowerControls();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
      queueSettingsSave(0);
    });

    elements.peripheralPowerList?.addEventListener("click", (event) => {
      const target = event.target;
      if (!(target instanceof HTMLButtonElement)) {
        return;
      }

      if (target.dataset.peripheralPowerAdd === "true") {
        state.peripheralPowerProfiles = normalizedPeripheralPowerProfiles();
        if (state.peripheralPowerProfiles.length >= maxPeripheralPowers) {
          return;
        }
        state.peripheralPowerProfiles.push("none");
        renderPeripheralPowerControls();
        savePeripheralProfileSelections();
        elements.peripheralPowerList?.querySelector(`select[data-peripheral-power-index="${state.peripheralPowerProfiles.length - 1}"]`)?.focus();
        return;
      }

      const removeIndex = Number(target.dataset.peripheralPowerRemove);
      if (!Number.isInteger(removeIndex) || removeIndex <= 0 || removeIndex >= state.peripheralPowerProfiles.length) {
        return;
      }
      state.peripheralPowerProfiles.splice(removeIndex, 1);
      removePeripheralHelperBindingsForIndex("power", removeIndex);
      renderPeripheralPowerControls();
      syncGpioMappingControls();
      savePeripheralProfileSelections();
      queueSettingsSave(0);
    });

    for (const bindingContainer of [elements.peripheralAudioPins, elements.peripheralDisplayPins]) {
      bindingContainer?.addEventListener("change", (event) => {
        const target = event.target;
        if (!(target instanceof HTMLSelectElement)) {
          return;
        }
        const roleKey = String(target.dataset.peripheralBindingKey || "");
        if (!roleKey) {
          return;
        }
        const definition = gpioConfigRoleState(state.settings || {}).byKey.get(roleKey);
        if (!definition?.element) {
          return;
        }
        definition.element.value = String(target.value || definition.element.value || "");
        syncGpioMappingControls();
        syncPeripheralBindingGroups({ force: true });
        renderPeripheralDiagram();
        queueSettingsSave(150);
      });
    }

    const peripheralInteractionContainers = [
      gpioTabElement,
      elements.peripheralAudioPins,
      elements.peripheralAudioOutputsList,
      elements.peripheralDisplayPins,
      elements.peripheralSensorsList,
      elements.peripheralInputsList,
      elements.peripheralPowerList,
      elements.peripheralStorageList,
      elements.peripheralCommunicationList,
      elements.peripheralControlsList,
      document.getElementById('wledControlsList'),
      elements.peripheralExpansionsList,
    ].filter(Boolean);
    for (const container of peripheralInteractionContainers) {
      container.addEventListener("pointerdown", (event) => {
        if (isTopPeripheralSelect(event.target)) {
          state.peripheralMenuOpen = true;
        }
      });
      container.addEventListener("focusin", (event) => {
        if (isTopPeripheralSelect(event.target)) {
          state.peripheralUiInteractionDepth += 1;
        }
      });
      container.addEventListener("focusout", (event) => {
        if (!isTopPeripheralSelect(event.target)) {
          return;
        }
        state.peripheralUiInteractionDepth = Math.max(0, state.peripheralUiInteractionDepth - 1);
        window.setTimeout(() => {
          if (!isTopPeripheralSelect(document.activeElement)) {
            state.peripheralMenuOpen = false;
          }
        }, 0);
      });
      container.addEventListener("change", (event) => {
        if (isTopPeripheralSelect(event.target)) {
          state.peripheralMenuOpen = false;
        }
      });
    }
  }

  return {
    bindEvents,
    renderPeripheralAudioOutputControls,
    renderPeripheralAudioInControls,
    renderPeripheralSensorControls,
    renderPeripheralInputControls,
    renderPeripheralStorageControls,
    renderPeripheralPowerControls,
    applyPeripheralStorageProfileSelection,
    renderPeripheralControlControls,
    renderPeripheralExpansionControls,
    renderPeripheralCommunicationControls,
  };
}
