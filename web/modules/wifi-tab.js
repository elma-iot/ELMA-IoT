import { createWifiPowerControls } from "./wifi-power-controls.js";

export function createWifiTab({
  state,
  elements,
  namedField,
  request,
  saveSettings,
  pollStatusUntil,
  waitForSettingsIdle,
  usableStationIp,
  maybeRedirectToStationIp,
  setMessage,
  handleError,
}) {
  const powerControls = createWifiPowerControls({ state, request, saveSettings, waitForSettingsIdle, setMessage, handleError });
  function aggregateWifiNetworks(networks) {
    const bySsid = new Map();

    for (const network of Array.isArray(networks) ? networks : []) {
      const ssid = String(network?.ssid || "").trim();
      if (!ssid) {
        continue;
      }

      const rssi = Number(network?.rssi ?? -1000);
      const encrypted = Boolean(network?.encrypted);
      const current = bySsid.get(ssid);
      if (!current) {
        bySsid.set(ssid, {
          ssid,
          rssi,
          encrypted,
          authentication: String(network?.authentication || ""),
          nodeCount: 1,
        });
        continue;
      }

      current.nodeCount += 1;
      current.encrypted = current.encrypted || encrypted;
      if (rssi > current.rssi) {
        current.rssi = rssi;
      }
    }

    return [...bySsid.values()].sort((left, right) => right.rssi - left.rssi || left.ssid.localeCompare(right.ssid));
  }

  function setScanStatus(message, isError = false) {
    elements.scanStatus.textContent = message;
    elements.scanStatus.style.color = isError ? "#b42318" : "";
  }

  function renderWifiNetworks(networks) {
    state.wifiSelectionPending = false;
    elements.wifiNetworkList.innerHTML = "";
    const aggregatedNetworks = aggregateWifiNetworks(networks);

    const placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = aggregatedNetworks.length ? "Select a scanned SSID" : "No networks found";
    elements.wifiNetworkList.appendChild(placeholder);

    for (const network of aggregatedNetworks) {
      const option = document.createElement("option");
      option.value = network.ssid;
      option.dataset.encrypted = network.encrypted ? "true" : "false";
      option.dataset.nodeCount = String(network.nodeCount);
      option.dataset.rssi = String(network.rssi);
      option.dataset.authentication = network.authentication || "";
      option.textContent = `${network.ssid} (${network.rssi} dBm${network.nodeCount > 1 ? `, ${network.nodeCount} mesh nodes` : ""}${network.encrypted ? ", locked" : ", open"})`;
      elements.wifiNetworkList.appendChild(option);
    }

    updateWifiActionButton();
  }

  function resetWifiNetworkList(message = "Select a scanned SSID") {
    state.wifiSelectionPending = false;
    renderWifiNetworks([]);
    const placeholder = elements.wifiNetworkList.firstElementChild;
    if (placeholder) {
      placeholder.textContent = message;
    }
  }

  function updateWifiActionButton() {
    if (!elements.scanWifiButton && !elements.wifiConnectButton) {
      return;
    }

    const ssid = String(namedField("wifi.ssid")?.value || "").trim();
    const canConnect = Boolean(ssid) && !state.wifiConnectInProgress;

    if (elements.scanWifiButton) {
      elements.scanWifiButton.textContent = "Scan Network";
      elements.scanWifiButton.classList.add("secondary");
      elements.scanWifiButton.disabled = Boolean(state.wifiConnectInProgress);
    }
    if (elements.wifiConnectButton) {
      elements.wifiConnectButton.disabled = !canConnect;
      elements.wifiConnectButton.textContent = state.wifiConnectInProgress ? "Connecting..." : "Connect";
      elements.wifiConnectButton.classList.toggle("secondary", !canConnect);
    }
  }

  function stopWifiScanPolling() {
    if (state.wifiScanPollTimer) {
      window.clearTimeout(state.wifiScanPollTimer);
      state.wifiScanPollTimer = null;
    }
  }

  async function scanWifiNetworks() {
    const button = elements.scanWifiButton;
    const requestId = state.wifiScanRequestId + 1;
    state.wifiScanRequestId = requestId;
    stopWifiScanPolling();
    button.disabled = true;
    setScanStatus("Searching...");
    resetWifiNetworkList("Searching for networks...");

    try {
      const startResult = await request("/api/wifi/scan?start=1");
      if (!startResult.started && !startResult.scanning) {
        button.disabled = false;
        resetWifiNetworkList("No scan in progress");
        setScanStatus("Wi-Fi scan could not start", true);
        return;
      }

      const pollScan = async () => {
        try {
          if (state.wifiScanRequestId !== requestId) {
            return;
          }

          const result = await request("/api/wifi/scan");
          if (state.wifiScanRequestId !== requestId) {
            return;
          }

          if (result.scanning) {
            setScanStatus("Searching...");
            state.wifiScanPollTimer = window.setTimeout(() => {
              pollScan().catch(handleError);
            }, 800);
            return;
          }

          stopWifiScanPolling();
          button.disabled = false;

          if (result.failed) {
            resetWifiNetworkList("Wi-Fi scan failed");
            setScanStatus("Wi-Fi scan failed", true);
            return;
          }

          const networks = Array.isArray(result.networks) ? result.networks : [];
          renderWifiNetworks(networks);
          setScanStatus(networks.length ? `Found ${networks.length} network(s)` : "No networks found");
        } catch (error) {
          if (state.wifiScanRequestId !== requestId) {
            return;
          }
          stopWifiScanPolling();
          button.disabled = false;
          setScanStatus(error.message, true);
        }
      };

      await pollScan();
    } catch (error) {
      resetWifiNetworkList("Wi-Fi scan failed");
      setScanStatus(error.message, true);
      button.disabled = false;
      throw error;
    }
  }

  async function connectWifi() {
    const ssid = String(namedField("wifi.ssid")?.value || "").trim();
    if (!ssid) {
      setScanStatus("Select or enter a Wi-Fi SSID first", true);
      return;
    }

    state.wifiConnectInProgress = true;
    if (elements.scanWifiButton) {
      elements.scanWifiButton.disabled = true;
    }
    if (elements.wifiConnectButton) {
      elements.wifiConnectButton.disabled = true;
    }
    updateWifiActionButton();
    setScanStatus(`Saving Wi-Fi settings for ${ssid}...`);

    try {
      if (state.settingsSaveTimer) {
        window.clearTimeout(state.settingsSaveTimer);
        state.settingsSaveTimer = null;
      }

      await waitForSettingsIdle();
      await saveSettings({ silent: true });
      setMessage(`Wi-Fi settings saved for ${ssid}`);

      if (document.body.classList.contains("android-designer")) {
        window.ElmaAndroidConfig?.openWifiSettings();
        setScanStatus(`Settings saved for ${ssid}. Select that network in Android Wi-Fi settings to connect the phone.`);
        return;
      }

      if (document.body.classList.contains("local-builder-mode")) {
        setScanStatus(`Testing ${ssid} through this PC's Wi-Fi adapter...`);
        const selectedOption = elements.wifiNetworkList?.selectedOptions?.[0];
        const result = await request("/api/pc/wifi/test", {
          method: "POST",
          body: JSON.stringify({
            ssid,
            password: String(namedField("wifi.password")?.value || ""),
            authentication: selectedOption?.dataset.authentication || "WPA2-Personal",
          }),
        });
        if (!result?.connected) {
          throw new Error(`Could not verify Wi-Fi credentials for ${ssid}.`);
        }
        setScanStatus(`Wi-Fi Connected to ${ssid}. Credentials saved for the firmware build.`);
        setMessage(`Wi-Fi Connected — ${ssid}`);
        return;
      }

      setScanStatus(`Connecting to ${ssid}...`);

      const connected = await pollStatusUntil(
        (status) => Boolean(usableStationIp(status)),
        30,
        1000,
      );

      if (connected) {
        const stationIp = usableStationIp(state.status);
        setScanStatus(`Connected to ${state.status?.network?.ssid || ssid}${stationIp ? ` at ${stationIp}` : ""}`);
        setMessage(`Wi-Fi connected to ${state.status?.network?.ssid || ssid}${stationIp ? ` at ${stationIp}` : ""}`);
        maybeRedirectToStationIp(state.status, { force: true });
      } else {
        setScanStatus("Wi-Fi settings saved. Connection is still in progress.");
        setMessage("Wi-Fi settings saved. Waiting for connection.");
      }
    } finally {
      state.wifiConnectInProgress = false;
      if (elements.scanWifiButton) {
        elements.scanWifiButton.disabled = false;
      }
      if (elements.wifiConnectButton) {
        elements.wifiConnectButton.disabled = false;
      }
      updateWifiActionButton();
    }
  }

  function bindEvents() {
    powerControls.bindEvents();
    elements.scanWifiButton?.addEventListener("click", () => scanWifiNetworks().catch(handleError));
    elements.wifiConnectButton?.addEventListener("click", () => connectWifi().catch(handleError));
    elements.wifiNetworkList?.addEventListener("change", (event) => {
      if (!event.target.value) {
        state.wifiSelectionPending = false;
        setScanStatus("");
        updateWifiActionButton();
        return;
      }
      const field = namedField("wifi.ssid");
      if (field) {
        field.value = event.target.value;
        state.wifiSelectionPending = true;
        const selectedOption = event.target.selectedOptions?.[0];
        const nodeCount = Number(selectedOption?.dataset.nodeCount || 1);
        const encrypted = selectedOption?.dataset.encrypted === "true";
        setScanStatus(
          encrypted
            ? `Selected ${event.target.value}${nodeCount > 1 ? ` (${nodeCount} mesh nodes, strongest signal shown)` : ""}. Enter the password if needed, then connect.`
            : `Selected open network ${event.target.value}${nodeCount > 1 ? ` (${nodeCount} mesh nodes, strongest signal shown)` : ""}. Connect when ready.`,
        );
        updateWifiActionButton();
      }
    });
    namedField("wifi.ssid")?.addEventListener("input", () => {
      state.wifiSelectionPending = false;
      updateWifiActionButton();
    });
    namedField("wifi.password")?.addEventListener("input", updateWifiActionButton);
  }

  return {
    renderPowerStatus: powerControls.renderStatus,
    setScanStatus,
    renderWifiNetworks,
    resetWifiNetworkList,
    updateWifiActionButton,
    stopWifiScanPolling,
    scanWifiNetworks,
    connectWifi,
    bindEvents,
  };
}
