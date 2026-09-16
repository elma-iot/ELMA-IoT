import {diagramRect} from './diagram-viewport.js';
export function createConfigurationBackupModule({
  state,
  elements,
  gpioBoardLayouts,
  isPlainObject,
  cloneSettingsObject,
  normalizeUiSettings,
  sanitizeStoredPeripheralProfiles,
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
  maxPeripheralAudioOutputs,
  maxPeripheralAudioInputs,
  maxPeripheralDisplays,
  maxPeripheralSensors,
  maxPeripheralInputs,
  maxPeripheralControls,
  maxPeripheralExpansions,
  maxPeripheralStorages,
  maxPeripheralCommunications,
  maxPeripheralPowers,
  savePeripheralHelperBindings,
  renderPeripheralAudioOutputControls,
  renderPeripheralAudioInControls,
  renderPeripheralDisplayControls,
  renderPeripheralSensorControls,
  renderPeripheralInputControls,
  renderPeripheralControlControls,
  renderPeripheralExpansionControls,
  renderPeripheralStorageControls,
  renderPeripheralCommunicationControls,
  renderPeripheralPowerControls,
  syncPeripheralBindingGroups,
  renderPeripheralDiagram,
  queueSettingsSave,
  updateGpioBoardSelectorMode,
  updateGpioBoardImage,
  isGpioUiInteracting,
  renderGpioOverview,
  currentSettingsSnapshot,
  mergeSettingsObjects,
  applySettingsPayload,
  setMessage,
  toast,
}) {
  function currentBackupUiState() {
    return {
      gpioBoard: {
        autodetect: Boolean(elements.gpioBoardAutodetect?.checked ?? true),
        selectedBoard: String(elements.gpioBoardSelector?.value || ""),
      },
      peripheralDiagramPositions: cloneSettingsObject(state.peripheralDiagramPositions || {}) || {},
      peripherals: {
        audioProfile: String(elements.peripheralAudioProfile?.value || "none"),
        audioProfiles: [...normalizedPeripheralAudioProfiles()],
        audioInProfile: String(elements.peripheralAudioInProfile?.value || "none"),
        audioInProfiles: [...normalizedPeripheralAudioInProfiles()],
        displayProfile: String(elements.peripheralDisplayProfile?.value || "none"),
        displayProfiles: [...normalizedPeripheralDisplayProfiles()],
        sensors: [...normalizedPeripheralSensorProfiles()],
        inputs: [...normalizedPeripheralInputProfiles()],
        controls: [...normalizedPeripheralControlProfiles()],
        expansions: [...normalizedPeripheralExpansionProfiles()],
        storage: [...normalizedPeripheralStorageProfiles()],
        communication: [...normalizedPeripheralCommunicationProfiles()],
        power: [...normalizedPeripheralPowerProfiles()],
        helperBindings: cloneSettingsObject(state.peripheralHelperBindings || {}) || {},
      },
    };
  }

  function cloneSettingSection(value) {
    return isPlainObject(value) ? (cloneSettingsObject(value) || {}) : undefined;
  }

  function cloneDeviceDiagramSettings(deviceSettings) {
    if (!isPlainObject(deviceSettings)) {
      return undefined;
    }

    const nextDeviceSettings = {};
    for (const [key, value] of Object.entries(deviceSettings)) {
      if (/pin/i.test(key)) {
        nextDeviceSettings[key] = value;
      }
    }
    return Object.keys(nextDeviceSettings).length > 0 ? nextDeviceSettings : undefined;
  }

  function diagramShareSettingsFromSnapshot(snapshot) {
    const shareSettings = {};
    const audio = cloneSettingSection(snapshot?.audio);
    const oled = cloneSettingSection(snapshot?.oled);
    const sd = cloneSettingSection(snapshot?.sd);
    const battery = cloneSettingSection(snapshot?.battery);
    const device = cloneDeviceDiagramSettings(snapshot?.device);
    const ui = normalizeUiSettings({
      gpioBoardAutodetect: snapshot?.ui?.gpioBoardAutodetect,
      gpioBoardSelection: snapshot?.ui?.gpioBoardSelection,
      peripheralDiagramPositions: snapshot?.ui?.peripheralDiagramPositions,
      peripheralHelperBindings: snapshot?.ui?.peripheralHelperBindings,
      peripheralProfiles: snapshot?.ui?.peripheralProfiles,
    });

    if (audio) {
      shareSettings.audio = audio;
    }
    if (oled) {
      shareSettings.oled = oled;
    }
    if (sd) {
      shareSettings.sd = sd;
    }
    if (battery) {
      shareSettings.battery = battery;
    }
    if (device) {
      shareSettings.device = device;
    }
    shareSettings.ui = ui;
    return shareSettings;
  }

  function currentPeripheralDiagramShareDocument() {
    const settingsSnapshot = currentSettingsSnapshot();
    const firmwareVersion = String(state.status?.firmware?.version || "unknown");
    const deviceName = String(settingsSnapshot?.device?.friendlyName || settingsSnapshot?.device?.deviceName || "ESP32 Notifier");
    return {
      meta: {
        format: "esp32-notifier-diagram-share",
        version: 1,
        exportedAt: new Date().toISOString(),
        firmwareVersion,
        deviceName,
      },
      settings: diagramShareSettingsFromSnapshot(settingsSnapshot),
      uiState: currentBackupUiState(),
    };
  }

  function applyBackupUiState(uiState, options = {}) {
    const { persist = false } = options;
    if (!isPlainObject(uiState)) {
      return;
    }

    const normalized = normalizeUiSettings({
      gpioBoardAutodetect: uiState?.gpioBoard?.autodetect,
      gpioBoardSelection: uiState?.gpioBoard?.selectedBoard,
      peripheralDiagramPositions: uiState?.peripheralDiagramPositions,
    });

    if (elements.gpioBoardAutodetect) {
      elements.gpioBoardAutodetect.checked = normalized.gpioBoardAutodetect;
    }
    if (elements.gpioBoardSelector && normalized.gpioBoardSelection && gpioBoardLayouts[normalized.gpioBoardSelection]) {
      elements.gpioBoardSelector.value = normalized.gpioBoardSelection;
    }
    if (state.settings) {
      state.settings.ui = normalizeUiSettings(mergeSettingsObjects(
        cloneSettingsObject(state.settings.ui || {}) || {},
        normalized,
      ));
    }
    updateGpioBoardSelectorMode(state.status, { force: true });
    updateGpioBoardImage();
    if (!isGpioUiInteracting()) {
      renderGpioOverview();
    }

    state.peripheralDiagramPositions = cloneSettingsObject(normalized.peripheralDiagramPositions) || {};
    if (isPlainObject(uiState.peripherals)) {
      if (elements.peripheralAudioProfile && [...elements.peripheralAudioProfile.options].some((option) => option.value === String(uiState.peripherals.audioProfile || "none"))) {
        elements.peripheralAudioProfile.value = String(uiState.peripherals.audioProfile || "none");
      }
      state.peripheralAudioProfiles = sanitizeStoredPeripheralProfiles(
        uiState.peripherals.audioProfiles,
        maxPeripheralAudioOutputs,
        [String(uiState.peripherals.audioProfile || elements.peripheralAudioProfile?.value || "none")],
      );
      state.peripheralAudioInProfiles = sanitizeStoredPeripheralProfiles(
        uiState.peripherals.audioInProfiles,
        maxPeripheralAudioInputs,
        [String(uiState.peripherals.audioInProfile || elements.peripheralAudioInProfile?.value || "none")],
      );
      if (elements.peripheralAudioInProfile && [...elements.peripheralAudioInProfile.options].some((option) => option.value === String(state.peripheralAudioInProfiles[0] || uiState.peripherals.audioInProfile || "none"))) {
        elements.peripheralAudioInProfile.value = String(state.peripheralAudioInProfiles[0] || uiState.peripherals.audioInProfile || "none");
      }
      state.peripheralDisplayProfiles = sanitizeStoredPeripheralProfiles(
        uiState.peripherals.displayProfiles,
        maxPeripheralDisplays,
        [String(uiState.peripherals.displayProfile || elements.peripheralDisplayProfile?.value || "none")],
      );
      if (elements.peripheralDisplayProfile && [...elements.peripheralDisplayProfile.options].some((option) => option.value === String(state.peripheralDisplayProfiles[0] || uiState.peripherals.displayProfile || "none"))) {
        elements.peripheralDisplayProfile.value = String(state.peripheralDisplayProfiles[0] || uiState.peripherals.displayProfile || "none");
      }
      state.peripheralSensorProfiles = sanitizeStoredPeripheralProfiles(uiState.peripherals.sensors, maxPeripheralSensors, ["none"]);
      state.peripheralInputProfiles = sanitizeStoredPeripheralProfiles(uiState.peripherals.inputs, maxPeripheralInputs, ["none"]);
      state.peripheralControlProfiles = sanitizeStoredPeripheralProfiles(uiState.peripherals.controls, maxPeripheralControls, ["none"]);
      state.peripheralExpansionProfiles = sanitizeStoredPeripheralProfiles(uiState.peripherals.expansions, maxPeripheralExpansions, ["none"]);
      state.peripheralStorageProfiles = sanitizeStoredPeripheralProfiles(uiState.peripherals.storage, maxPeripheralStorages, ["none"]);
      state.peripheralCommunicationProfiles = sanitizeStoredPeripheralProfiles(uiState.peripherals.communication, maxPeripheralCommunications, ["none"]);
      state.peripheralPowerProfiles = sanitizeStoredPeripheralProfiles(uiState.peripherals.power, maxPeripheralPowers, ["none"]);
      state.peripheralHelperBindings = isPlainObject(uiState.peripherals.helperBindings)
        ? (cloneSettingsObject(uiState.peripherals.helperBindings) || {})
        : {};
      savePeripheralHelperBindings();
      renderPeripheralAudioOutputControls();
      renderPeripheralAudioInControls();
      renderPeripheralDisplayControls();
      renderPeripheralSensorControls();
      renderPeripheralInputControls();
      renderPeripheralControlControls();
      renderPeripheralExpansionControls();
      renderPeripheralStorageControls();
      renderPeripheralCommunicationControls();
      renderPeripheralPowerControls();
      syncPeripheralBindingGroups();
    }
    renderPeripheralDiagram();

    if (persist && !state.settingsLoading) {
      queueSettingsSave(0);
    }
  }

  function backupTimestamp() {
    const now = new Date();
    const pad = (value) => String(value).padStart(2, "0");
    return `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}_${pad(now.getHours())}-${pad(now.getMinutes())}-${pad(now.getSeconds())}`;
  }

  function backupDeviceLabel(settings) {
    const friendly = String(settings?.device?.friendlyName || settings?.device?.deviceName || state.status?.device?.friendlyName || "esp32-notifier").trim();
    return friendly.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "") || "esp32-notifier";
  }

  function createConfigurationBackupMarkdown(settings) {
    const firmwareVersion = String(state.status?.firmware?.version || "unknown");
    const deviceName = String(settings?.device?.friendlyName || settings?.device?.deviceName || "ESP32 Notifier");
    const uiState = currentBackupUiState();
    const backupDocument = {
      meta: {
        format: "esp32-notifier-config-backup",
        version: 1,
        exportedAt: new Date().toISOString(),
        firmwareVersion,
        deviceName,
      },
      settings,
      uiState,
    };

    return [
      "# ESP32 Notifier Configuration Backup",
      "",
      `Device: ${deviceName}`,
      `Firmware: ${firmwareVersion}`,
      `Exported: ${backupDocument.meta.exportedAt}`,
      "",
      "Edit only the JSON block below if you want to adjust values on a PC before restoring.",
      "",
      "```json",
      JSON.stringify(backupDocument, null, 2),
      "```",
      "",
    ].join("\n");
  }

  function createPeripheralDiagramShareMarkdown() {
    const shareDocument = currentPeripheralDiagramShareDocument();

    return [
      "# ESP32 Notifier Peripheral Diagram Share",
      "",
      `Device: ${shareDocument.meta.deviceName}`,
      `Firmware: ${shareDocument.meta.firmwareVersion}`,
      `Exported: ${shareDocument.meta.exportedAt}`,
      "",
      "This share file only contains diagram layout data, peripheral profile selections, helper bindings, and wiring-relevant GPIO settings.",
      "",
      "```json",
      JSON.stringify(shareDocument, null, 2),
      "```",
      "",
    ].join("\n");
  }

  function filePickerUnavailableMessage() {
    return "This browser page is served over HTTP, so Windows save-location pickers are unavailable. Open the UI through HTTPS or localhost to choose a save location.";
  }

  function canUseSaveFilePicker() {
    return window.isSecureContext && typeof window.showSaveFilePicker === "function";
  }

  function canUseDirectoryPicker() {
    return window.isSecureContext && typeof window.showDirectoryPicker === "function";
  }

  function requireSaveFilePicker() {
    if (canUseSaveFilePicker()) {
      return;
    }
    throw new Error(filePickerUnavailableMessage());
  }

  function requireDirectoryPicker() {
    if (canUseDirectoryPicker()) {
      return;
    }
    throw new Error(filePickerUnavailableMessage());
  }

  async function writeFileHandle(handle, content) {
    const writable = await handle.createWritable();
    try {
      await writable.write(content);
    } finally {
      await writable.close();
    }
  }

  async function saveTextFileWithPicker(filename, content, description = "Markdown file") {
    requireSaveFilePicker();
    const extension = filename.toLowerCase().endsWith(".json")
      ? ".json"
      : (filename.toLowerCase().endsWith(".txt") ? ".txt" : ".md");
    const type = extension === ".json" ? "application/json" : "text/markdown";
    const handle = await window.showSaveFilePicker({
      suggestedName: filename,
      types: [{
        description,
        accept: {
          [type]: [extension],
        },
      }],
    });
    await writeFileHandle(handle, content);
    return handle;
  }

  async function saveBlobFileWithPicker(filename, blob, description = "File") {
    requireSaveFilePicker();
    const extension = filename.toLowerCase().endsWith(".png")
      ? ".png"
      : (filename.toLowerCase().endsWith(".svg") ? ".svg" : ".bin");
    const handle = await window.showSaveFilePicker({
      suggestedName: filename,
      types: [{
        description,
        accept: {
          [blob.type || "application/octet-stream"]: [extension],
        },
      }],
    });
    await writeFileHandle(handle, blob);
    return handle;
  }

  async function saveDiagramShareFolder(filenameBase, markdown, screenshotMarkup) {
    requireDirectoryPicker();
    const directoryHandle = await window.showDirectoryPicker({ mode: "readwrite" });
    const markdownHandle = await directoryHandle.getFileHandle(`${filenameBase}.md`, { create: true });
    const screenshotHandle = await directoryHandle.getFileHandle(`${filenameBase}.svg`, { create: true });
    await writeFileHandle(markdownHandle, markdown);
    await writeFileHandle(screenshotHandle, screenshotMarkup);
    return directoryHandle;
  }

  function triggerDownload(filename, href) {
    const anchor = document.createElement("a");
    anchor.href = href;
    anchor.download = filename;
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
  }

  function downloadTextFile(filename, content, mimeType = "text/markdown;charset=utf-8") {
    triggerDownload(filename, `data:${mimeType},${encodeURIComponent(content)}`);
  }

  function downloadBinaryFile(filename, bytes, mimeType = "application/octet-stream") {
    let binary = "";
    const chunkSize = 0x8000;
    for (let offset = 0; offset < bytes.length; offset += chunkSize) {
      const chunk = bytes.subarray(offset, offset + chunkSize);
      binary += String.fromCharCode(...chunk);
    }
    triggerDownload(filename, `data:${mimeType};base64,${btoa(binary)}`);
  }

  function crc32Table() {
    const table = new Uint32Array(256);
    for (let index = 0; index < 256; index += 1) {
      let value = index;
      for (let bit = 0; bit < 8; bit += 1) {
        value = (value & 1) ? (0xedb88320 ^ (value >>> 1)) : (value >>> 1);
      }
      table[index] = value >>> 0;
    }
    return table;
  }

  const ZIP_CRC32_TABLE = crc32Table();

  function crc32(bytes) {
    let value = 0xffffffff;
    for (const byte of bytes) {
      value = ZIP_CRC32_TABLE[(value ^ byte) & 0xff] ^ (value >>> 8);
    }
    return (value ^ 0xffffffff) >>> 0;
  }

  function zipDosTimestamp(date = new Date()) {
    const year = Math.max(1980, date.getFullYear());
    const dosTime = ((date.getHours() & 0x1f) << 11)
      | ((date.getMinutes() & 0x3f) << 5)
      | Math.floor((date.getSeconds() & 0x3e) / 2);
    const dosDate = (((year - 1980) & 0x7f) << 9)
      | (((date.getMonth() + 1) & 0x0f) << 5)
      | (date.getDate() & 0x1f);
    return { dosTime, dosDate };
  }

  function pushUint16(target, value) {
    target.push(value & 0xff, (value >>> 8) & 0xff);
  }

  function pushUint32(target, value) {
    target.push(value & 0xff, (value >>> 8) & 0xff, (value >>> 16) & 0xff, (value >>> 24) & 0xff);
  }

  function appendBytes(target, bytes) {
    for (const byte of bytes) {
      target.push(byte);
    }
  }

  function createStoredZipArchive(entries) {
    const encoder = new TextEncoder();
    const output = [];
    const centralDirectory = [];
    const timestamp = zipDosTimestamp();

    for (const entry of entries) {
      const nameBytes = encoder.encode(entry.name);
      const contentBytes = encoder.encode(entry.content);
      const checksum = crc32(contentBytes);
      const localOffset = output.length;

      pushUint32(output, 0x04034b50);
      pushUint16(output, 20);
      pushUint16(output, 0x0800);
      pushUint16(output, 0);
      pushUint16(output, timestamp.dosTime);
      pushUint16(output, timestamp.dosDate);
      pushUint32(output, checksum);
      pushUint32(output, contentBytes.length);
      pushUint32(output, contentBytes.length);
      pushUint16(output, nameBytes.length);
      pushUint16(output, 0);
      appendBytes(output, nameBytes);
      appendBytes(output, contentBytes);

      pushUint32(centralDirectory, 0x02014b50);
      pushUint16(centralDirectory, 20);
      pushUint16(centralDirectory, 20);
      pushUint16(centralDirectory, 0x0800);
      pushUint16(centralDirectory, 0);
      pushUint16(centralDirectory, timestamp.dosTime);
      pushUint16(centralDirectory, timestamp.dosDate);
      pushUint32(centralDirectory, checksum);
      pushUint32(centralDirectory, contentBytes.length);
      pushUint32(centralDirectory, contentBytes.length);
      pushUint16(centralDirectory, nameBytes.length);
      pushUint16(centralDirectory, 0);
      pushUint16(centralDirectory, 0);
      pushUint16(centralDirectory, 0);
      pushUint16(centralDirectory, 0);
      pushUint32(centralDirectory, 0);
      pushUint32(centralDirectory, localOffset);
      appendBytes(centralDirectory, nameBytes);
    }

    const centralDirectoryOffset = output.length;
    appendBytes(output, centralDirectory);
    const centralDirectorySize = output.length - centralDirectoryOffset;

    pushUint32(output, 0x06054b50);
    pushUint16(output, 0);
    pushUint16(output, 0);
    pushUint16(output, entries.length);
    pushUint16(output, entries.length);
    pushUint32(output, centralDirectorySize);
    pushUint32(output, centralDirectoryOffset);
    pushUint16(output, 0);

    return new Uint8Array(output);
  }

  function stylesheetText() {
    return Array.from(document.styleSheets)
      .map((sheet) => {
        try {
          return Array.from(sheet.cssRules || []).map((rule) => rule.cssText).join("\n");
        } catch {
          return "";
        }
      })
      .filter(Boolean)
      .join("\n");
  }

  async function blobToDataUrl(blob) {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(String(reader.result || ""));
      reader.onerror = () => reject(new Error("Unable to read image data for peripheral diagram screenshot."));
      reader.readAsDataURL(blob);
    });
  }

  async function inlineClonedImages(root) {
    const images = root.querySelectorAll("img");
    await Promise.all(Array.from(images).map(async (image) => {
      const source = image.currentSrc || image.src || image.getAttribute("src") || "";
      if (!source || source.startsWith("data:")) {
        return;
      }
      const response = await fetch(source);
      if (!response.ok) {
        throw new Error(`Unable to fetch diagram image asset: ${source}`);
      }
      const dataUrl = await blobToDataUrl(await response.blob());
      image.setAttribute("src", dataUrl);
    }));
  }

  async function capturePeripheralDiagramBlob() {
    const stage = elements.peripheralDiagramStage;
    if (!stage) {
      throw new Error("Dynamic peripheral diagram is not available.");
    }

    const rect = diagramRect(stage);
    const width = Math.max(1, Math.ceil(rect.width));
    const height = Math.max(1, Math.ceil(rect.height));
    if (width < 2 || height < 2) {
      throw new Error("Dynamic peripheral diagram is not ready for screenshot export.");
    }

    const clone = stage.cloneNode(true);
    clone.style.transform = "none";
    clone.style.width = `${width}px`;
    clone.style.height = `${height}px`;
    clone.style.margin = "0";
    await inlineClonedImages(clone);

    const svgRoot = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svgRoot.setAttribute("xmlns", "http://www.w3.org/2000/svg");
    svgRoot.setAttribute("width", String(width));
    svgRoot.setAttribute("height", String(height));
    svgRoot.setAttribute("viewBox", `0 0 ${width} ${height}`);

    const foreignObject = document.createElementNS("http://www.w3.org/2000/svg", "foreignObject");
    foreignObject.setAttribute("width", "100%");
    foreignObject.setAttribute("height", "100%");

    const wrapper = document.createElement("div");
    wrapper.setAttribute("xmlns", "http://www.w3.org/1999/xhtml");
    wrapper.style.margin = "0";
    wrapper.style.width = `${width}px`;
    wrapper.style.height = `${height}px`;
    wrapper.style.overflow = "hidden";
    wrapper.style.background = "linear-gradient(180deg,#f8fafc 0%,#eef2f7 100%)";

    const style = document.createElement("style");
    style.textContent = stylesheetText();
    wrapper.appendChild(style);
    wrapper.appendChild(clone);
    foreignObject.appendChild(wrapper);
    svgRoot.appendChild(foreignObject);

    return new XMLSerializer().serializeToString(svgRoot);
  }

  function parseConfigurationBackup(text) {
    const rawText = String(text || "").trim();
    if (!rawText) {
      throw new Error("Backup file is empty.");
    }

    const tryParseJson = (value) => {
      const parsed = JSON.parse(value);
      if (!isPlainObject(parsed)) {
        throw new Error("Backup file does not contain a settings object.");
      }
      return parsed;
    };

    try {
      const parsed = tryParseJson(rawText);
      return {
        settings: isPlainObject(parsed.settings) ? parsed.settings : parsed,
        uiState: isPlainObject(parsed.uiState) ? parsed.uiState : {},
        meta: isPlainObject(parsed.meta) ? parsed.meta : {},
      };
    } catch {
      const fencedMatch = rawText.match(/```(?:json)?\s*([\s\S]*?)```/i);
      if (!fencedMatch) {
        throw new Error("Backup file does not contain a readable JSON configuration block.");
      }
      const parsed = tryParseJson(fencedMatch[1].trim());
      return {
        settings: isPlainObject(parsed.settings) ? parsed.settings : parsed,
        uiState: isPlainObject(parsed.uiState) ? parsed.uiState : {},
        meta: isPlainObject(parsed.meta) ? parsed.meta : {},
      };
    }
  }

  function exportConfigurationBackup() {
    const settingsSnapshot = currentSettingsSnapshot();
    const filename = `${backupDeviceLabel(settingsSnapshot)}-config-backup-${backupTimestamp()}.md`;
    if (canUseSaveFilePicker()) {
      return saveTextFileWithPicker(filename, createConfigurationBackupMarkdown(settingsSnapshot), "ESP32 Notifier backup")
        .then(() => {
          setMessage("Configuration backup saved");
          toast("Configuration backup saved");
        });
    }

    downloadTextFile(filename, createConfigurationBackupMarkdown(settingsSnapshot));
    setMessage("Configuration backup downloaded");
    toast("Configuration backup downloaded");
    return Promise.resolve();
  }

  async function exportPeripheralDiagramShare() {
    const shareDocument = currentPeripheralDiagramShareDocument();
    const filenameBase = `${backupDeviceLabel(shareDocument.settings)}-peripheral-diagram-${backupTimestamp()}`;
    const markdown = createPeripheralDiagramShareMarkdown();
    const screenshot = await capturePeripheralDiagramBlob();
    if (canUseDirectoryPicker()) {
      await saveDiagramShareFolder(filenameBase, markdown, screenshot);
      setMessage("Peripheral diagram share saved to selected folder");
      toast("Peripheral diagram share saved to selected folder");
      return;
    }

    const archive = createStoredZipArchive([
      { name: `${filenameBase}.md`, content: markdown },
      { name: `${filenameBase}.svg`, content: screenshot },
    ]);
    downloadBinaryFile(`${filenameBase}.zip`, archive, "application/zip");
    setMessage("Peripheral diagram share downloaded as ZIP");
    toast("Peripheral diagram share downloaded as ZIP");
  }

  function parsePeripheralDiagramShare(text) {
    const parsed = parseConfigurationBackup(text);
    if (String(parsed.meta?.format || "") !== "esp32-notifier-diagram-share") {
      throw new Error("Selected file is not a peripheral diagram share export.");
    }
    if (!isPlainObject(parsed.settings) || (!Object.keys(parsed.settings).length && !isPlainObject(parsed.uiState))) {
      throw new Error("Peripheral diagram share file is missing diagram data.");
    }
    return parsed;
  }

  async function restorePeripheralDiagramShare(file) {
    if (!file) {
      return;
    }

    const text = await file.text();
    const importedShare = parsePeripheralDiagramShare(text);
    const importedSettings = diagramShareSettingsFromSnapshot(importedShare.settings);
    const restoredUi = normalizeUiSettings({
      gpioBoardAutodetect: importedSettings?.ui?.gpioBoardAutodetect ?? importedShare.uiState?.gpioBoard?.autodetect,
      gpioBoardSelection: importedSettings?.ui?.gpioBoardSelection ?? importedShare.uiState?.gpioBoard?.selectedBoard,
      peripheralDiagramPositions: importedSettings?.ui?.peripheralDiagramPositions ?? importedShare.uiState?.peripheralDiagramPositions,
      peripheralHelperBindings: importedSettings?.ui?.peripheralHelperBindings ?? importedShare.uiState?.peripherals?.helperBindings,
      peripheralProfiles: importedSettings?.ui?.peripheralProfiles,
    });
    const mergedSettings = mergeSettingsObjects(cloneSettingsObject(state.settings || {}) || {}, importedSettings);
    mergedSettings.ui = normalizeUiSettings(mergeSettingsObjects(
      cloneSettingsObject(mergedSettings.ui || {}) || {},
      restoredUi,
    ));
    await applySettingsPayload(mergedSettings, {
      silent: false,
      successMessage: "Peripheral diagram loaded",
      toastMessage: "Peripheral diagram loaded",
    });
    applyBackupUiState(importedShare.uiState, { persist: true });
  }

  async function restoreConfigurationBackup(file) {
    if (!file) {
      return;
    }

    const text = await file.text();
    const importedBackup = parseConfigurationBackup(text);
    const restoredUi = normalizeUiSettings({
      gpioBoardAutodetect: importedBackup.settings?.ui?.gpioBoardAutodetect ?? importedBackup.uiState?.gpioBoard?.autodetect,
      gpioBoardSelection: importedBackup.settings?.ui?.gpioBoardSelection ?? importedBackup.uiState?.gpioBoard?.selectedBoard,
      peripheralDiagramPositions: importedBackup.settings?.ui?.peripheralDiagramPositions ?? importedBackup.uiState?.peripheralDiagramPositions,
      peripheralHelperBindings: importedBackup.settings?.ui?.peripheralHelperBindings ?? importedBackup.uiState?.peripherals?.helperBindings,
      peripheralProfiles: importedBackup.settings?.ui?.peripheralProfiles,
    });
    const mergedSettings = mergeSettingsObjects(cloneSettingsObject(state.settings || {}) || {}, importedBackup.settings);
    mergedSettings.ui = normalizeUiSettings(mergeSettingsObjects(
      cloneSettingsObject(mergedSettings.ui || {}) || {},
      restoredUi,
    ));
    await applySettingsPayload(mergedSettings, {
      silent: false,
      successMessage: "Configuration restored",
      toastMessage: "Configuration restored",
    });
    applyBackupUiState(importedBackup.uiState, { persist: true });
  }

  return {
    currentBackupUiState,
    applyBackupUiState,
    backupTimestamp,
    backupDeviceLabel,
    createConfigurationBackupMarkdown,
    createPeripheralDiagramShareMarkdown,
    currentPeripheralDiagramShareDocument,
    capturePeripheralDiagramBlob,
    downloadTextFile,
    parseConfigurationBackup,
    parsePeripheralDiagramShare,
    exportConfigurationBackup,
    exportPeripheralDiagramShare,
    restoreConfigurationBackup,
    restorePeripheralDiagramShare,
  };
}