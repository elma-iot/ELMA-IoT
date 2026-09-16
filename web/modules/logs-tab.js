export function createLogsTab({ request, document = globalThis.document, navigator = globalThis.navigator, timers = globalThis }) {
  const terminal = document.getElementById("deviceLogText");
  const status = document.getElementById("deviceLogStatus");
  const copy = document.getElementById("deviceLogCopy");
  let active = false;
  let inFlight = false;
  let timer = null;
  let revision = "";
  let text = "";

  function stop() {
    if (timer !== null) timers.clearTimeout(timer);
    timer = null;
  }

  async function refresh() {
    stop();
    if (!active || document.hidden || inFlight) return;
    inFlight = true;
    let designer = false;
    try {
      const result = await request(`/api/logs?since=${encodeURIComponent(revision)}`);
      if (!active || document.hidden) return;
      designer = result.source === "designer";
      status.textContent = [result.notice, result.storageError].filter(Boolean).join(" ");
      if (!result.unchanged) {
        const followTail = terminal.scrollHeight - terminal.clientHeight - terminal.scrollTop < 32;
        const previousScroll = terminal.scrollTop;
        text = String(result.text || "");
        terminal.textContent = text || (designer ? "Open the flashed device’s web interface to view its logs." : "No captured output yet.");
        terminal.scrollTop = followTail ? terminal.scrollHeight : previousScroll;
        revision = String(result.revision || "");
      }
      copy.disabled = !text;
    } catch (error) {
      if (active) status.textContent = `Logs unavailable: ${error.message || error}. Retrying...`;
    } finally {
      inFlight = false;
      if (active && !document.hidden && !designer) timer = timers.setTimeout(refresh, 2000);
    }
  }

  copy.addEventListener("click", async () => {
    try {
      if (navigator.clipboard?.writeText) {
        try { await navigator.clipboard.writeText(text); }
        catch { fallbackCopy(); }
      } else fallbackCopy();
      copy.textContent = "Copied";
    } catch {
      status.textContent = "Copy was blocked. Select the log text and copy it manually.";
    }
  });

  function fallbackCopy() {
    // Device pages commonly use HTTP, where the Clipboard API is unavailable.
    const field = document.createElement("textarea");
    field.value = text;
    field.style.cssText = "position:fixed;left:-9999px;top:0";
    document.body.append(field);
    try {
      field.select();
      if (!document.execCommand("copy")) throw new Error("Copy blocked");
    } finally { field.remove(); copy.focus(); }
  }

  document.addEventListener("visibilitychange", () => {
    if (document.hidden) stop();
    else if (active) refresh();
  });
  return {
    setActive(value) {
      active = value;
      stop();
      copy.textContent = "Copy";
      if (active) refresh();
    },
  };
}
