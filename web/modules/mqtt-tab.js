export function createMqttTab({
  state,
  elements,
  namedField,
  request,
  saveSettings,
  pollStatusUntil,
  loadStatus,
  setMessage,
  handleError,
}) {
  const rediscoveryDefaultLabel = "Republish Discovery";
  const rediscoveryRunningLabel = "Republishing Discovery...";
  const rediscoveryCompletedLabel = "Republishing Completed!";
  let rediscoveryFeedbackTimer = 0;
  let rediscoveryFeedbackLabel = "";

  function setMqttConnectStatus(message, isError = false) {
    if (!elements.mqttConnectStatus) {
      return;
    }
    elements.mqttConnectStatus.textContent = message;
    elements.mqttConnectStatus.style.color = isError ? "#b42318" : "";
  }

  function clearRediscoveryFeedbackTimer() {
    if (rediscoveryFeedbackTimer) {
      window.clearTimeout(rediscoveryFeedbackTimer);
      rediscoveryFeedbackTimer = 0;
    }
  }

  function setRediscoveryButtonLabel(label, { resetAfterMs = 0 } = {}) {
    rediscoveryFeedbackLabel = label || "";
    if (elements.mqttRediscoveryButton) {
      elements.mqttRediscoveryButton.textContent = rediscoveryFeedbackLabel || rediscoveryDefaultLabel;
    }
    clearRediscoveryFeedbackTimer();
    if (resetAfterMs > 0) {
      rediscoveryFeedbackTimer = window.setTimeout(() => {
        rediscoveryFeedbackLabel = "";
        rediscoveryFeedbackTimer = 0;
        updateMqttActionButton();
      }, resetAfterMs);
    }
  }

  function updateMqttActionButton() {
    if (!elements.mqttConnectButton) {
      return;
    }

    const mqttConnected = Boolean(state.status?.network?.mqttConnected);
    const discoveryEnabled = Boolean(namedField("mqtt.discoveryEnabled")?.checked ?? state.settings?.mqtt?.discoveryEnabled);
    elements.mqttConnectButton.textContent = mqttConnected ? "Disconnect MQTT" : (document.body.classList.contains("android-designer") ? "Test Connection" : "Connect MQTT");
    elements.mqttConnectButton.classList.toggle("secondary", !mqttConnected);

    if (elements.mqttRediscoveryButton) {
      elements.mqttRediscoveryButton.disabled = !mqttConnected || !discoveryEnabled || state.mqttConnectInProgress;
      elements.mqttRediscoveryButton.textContent = state.mqttRediscoveryInProgress
        ? rediscoveryRunningLabel
        : (rediscoveryFeedbackLabel || rediscoveryDefaultLabel);
      elements.mqttRediscoveryButton.title = !discoveryEnabled
        ? "Enable Home Assistant discovery first"
        : (!mqttConnected ? "Connect MQTT first" : "Republish Home Assistant discovery topics");
    }
  }

  async function connectMqtt() {
    const mqttConnected = Boolean(state.status?.network?.mqttConnected);
    if (mqttConnected) {
      state.mqttConnectInProgress = true;
      state.mqttActionInProgress = "disconnect";
      elements.mqttConnectButton.disabled = true;
      setMqttConnectStatus("Disconnecting from MQTT broker...");

      try {
        await request("/api/mqtt", {
          method: "POST",
          body: JSON.stringify({ action: "disconnect" }),
        });

        const disconnected = await pollStatusUntil(
          (status) => !Boolean(status?.network?.mqttConnected),
          8,
          400,
        );

        if (disconnected) {
          setMqttConnectStatus("MQTT disconnected.");
          setMessage("MQTT disconnected");
        } else {
          setMqttConnectStatus("MQTT disconnect requested.");
          setMessage("MQTT disconnect requested");
        }
      } finally {
        state.mqttConnectInProgress = false;
        state.mqttActionInProgress = "";
        elements.mqttConnectButton.disabled = false;
        updateMqttActionButton();
      }
      return;
    }

    const host = String(namedField("mqtt.host")?.value || "").trim();
    if (!host) {
      setMqttConnectStatus("Enter an MQTT host first", true);
      return;
    }

    state.mqttConnectInProgress = true;
    state.mqttActionInProgress = "connect";
    elements.mqttConnectButton.disabled = true;
    setMqttConnectStatus(`Saving MQTT settings for ${host}...`);

    try {
      await saveSettings({ silent: true });
      setMessage(`MQTT settings saved for ${host}`);

      await request("/api/mqtt", {
        method: "POST",
        body: JSON.stringify({ action: "connect" }),
      });

      if (document.body.classList.contains("local-builder-mode") || document.body.classList.contains("android-designer")) {
        await loadStatus();
        setMqttConnectStatus(`MQTT Connected to ${host}. Credentials saved for the firmware build.`);
        setMessage(`MQTT Connected — ${host}`);
        return;
      }

      if (!state.status?.network?.wifiConnected) {
        setMqttConnectStatus("MQTT connect requested. Waiting for Wi-Fi first.");
        return;
      }

      setMqttConnectStatus(`Connecting to ${host}...`);
      const connected = await pollStatusUntil(
        (status) => Boolean(status?.network?.mqttConnected),
        15,
        1000,
      );

      if (connected) {
        setMqttConnectStatus(`Connected to ${host}`);
        setMessage(`MQTT connected to ${host}`);
      } else {
        setMqttConnectStatus("MQTT settings saved. Waiting for broker connection.");
        setMessage("MQTT settings saved. Waiting for broker connection.");
      }
    } catch (error) {
      setMqttConnectStatus(`Connection failed: ${error?.message || error}`, true);
      throw error;
    } finally {
      state.mqttConnectInProgress = false;
      state.mqttActionInProgress = "";
      elements.mqttConnectButton.disabled = false;
      updateMqttActionButton();
    }
  }

  async function republishMqttDiscovery() {
    const mqttConnected = Boolean(state.status?.network?.mqttConnected);
    if (!mqttConnected) {
      setMqttConnectStatus("Connect MQTT first.", true);
      return;
    }

    const discoveryEnabled = Boolean(namedField("mqtt.discoveryEnabled")?.checked ?? state.settings?.mqtt?.discoveryEnabled);
    if (!discoveryEnabled) {
      setMqttConnectStatus("Enable Home Assistant discovery first.", true);
      return;
    }

    state.mqttConnectInProgress = true;
    state.mqttRediscoveryInProgress = true;
    state.mqttActionInProgress = "rediscover";
    if (elements.mqttConnectButton) {
      elements.mqttConnectButton.disabled = true;
    }
    if (elements.mqttRediscoveryButton) {
      elements.mqttRediscoveryButton.disabled = true;
    }
    setRediscoveryButtonLabel(rediscoveryRunningLabel);

    let completionMessage = "";

    try {
      const response = await request("/api/mqtt", {
        method: "POST",
        body: JSON.stringify({ action: "rediscover" }),
      });
      completionMessage = response?.message || "MQTT discovery republished.";
      setMessage("MQTT discovery republished");
    } finally {
      state.mqttConnectInProgress = false;
      state.mqttRediscoveryInProgress = false;
      state.mqttActionInProgress = "";
      if (elements.mqttConnectButton) {
        elements.mqttConnectButton.disabled = false;
      }
      updateMqttActionButton();
    }

    if (completionMessage) {
      await loadStatus();
      setRediscoveryButtonLabel(rediscoveryCompletedLabel, { resetAfterMs: 2000 });
      setMqttConnectStatus("");
    }
  }

  function bindEvents() {
    elements.mqttConnectButton?.addEventListener("click", () => connectMqtt().catch(handleError));
    elements.mqttRediscoveryButton?.addEventListener("click", () => republishMqttDiscovery().catch(handleError));
  }

  return {
    setMqttConnectStatus,
    updateMqttActionButton,
    connectMqtt,
    republishMqttDiscovery,
    bindEvents,
  };
}
