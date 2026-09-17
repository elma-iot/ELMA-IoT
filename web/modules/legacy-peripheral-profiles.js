// Older firmware stored operational pins/enabled flags without UI profiles.
// Recover only missing selections; explicit values (including None) win.
export function restoreLegacyPeripheralProfiles(settings) {
  const profiles = { ...(settings?.ui?.peripheralProfiles || {}) };
  function primary(arrayKey, scalarKey, fallback) {
    const saved = profiles[arrayKey];
    if (Array.isArray(saved) && saved.length) return;
    const scalar = String(profiles[scalarKey] || "").trim();
    profiles[arrayKey] = [scalar || fallback];
    profiles[scalarKey] = scalar || fallback;
  }
  primary("audioProfiles", "audioProfile", settings?.audio?.enabled === true ? "max98357a-i2s-amp" : "none");
  primary("displayProfiles", "displayProfile", settings?.oled?.enabled === true
    ? (settings.oled.displayType === "wape" ? "waveshare-screen" : "i2c-oled") : "none");
  if (!Array.isArray(profiles.storage) || !profiles.storage.length) {
    profiles.storage = [settings?.sd?.enabled === true ? "microsd-spi" : "none"];
  }
  if (!Array.isArray(profiles.sensors) || !profiles.sensors.length) {
    profiles.sensors = [Number(settings?.battery?.adcPin || 0) > 0 ? "battery-voltage-divider-220k" : "none"];
  }
  return profiles;
}
