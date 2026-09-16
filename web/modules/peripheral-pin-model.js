// Shared electrical model for the diagram. UI labels may be edited or
// translated, but connection safety is decided from these canonical roles.
const GROUND_ALIASES = new Set(["GND", "GROUND", "GND IN", "GND OUT"]);
const FIVE_VOLT_ALIASES = new Set(["VIN", "VIN / INPUT", "INPUT", "VBUS", "5V", "5V IN"]);
const THREE_VOLT_ALIASES = new Set(["VCC", "PWR", "3V3", "3.3V", "3VO"]);
const EXTERNAL_POWER_ALIASES = new Set(["12V", "24V"]);

export function canonicalSignalKey(value) {
  return String(value || "")
    .replace(/^OLED\s+/i, "")
    .replace(/^I2S\s+/i, "")
    .replace(/^SD\s+/i, "")
    .replace(/^TX\s*\/\s*/i, "TX ")
    .replace(/^RX\s*\/\s*/i, "RX ")
    .replace(/\s+/g, " ")
    .trim()
    .toUpperCase();
}

export function isBoardGpio(entry){
  return entry?.pin!==null&&entry?.pin!==undefined&&entry?.pin!==''
    &&Number.isInteger(Number(entry.pin))&&Number(entry.pin)>=0;
}

const SIGNAL_COLORS = {
  SDA:'#0284c7', SCL:'#b77900', WS:'#9333ea', BCLK:'#db2777',
  DIN:'#08916a', DOUT:'#4f46e5', SCK:'#9a5b18', MOSI:'#006b5b',
  MISO:'#a21caf', CS:'#507b16', RX:'#1750a8', TX:'#bf4c00',
  SIGNAL:'#0f766e', SIG:'#306d8c', OUT:'#6563c6', IN:'#8c3c64',
  RST:'#627c35', RESET:'#627c35', DC:'#a83291', BL:'#867000',
  INT:'#5b21b6', EN:'#437650', PWM:'#7c3aed', PWM1:'#7040aa',
  PWM2:'#aa406f', CLK:'#8c631b', DATA:'#207ca4', DQ:'#26714b',
  A:'#ab4960', B:'#4966ab', SW:'#577b4c', TRIG:'#7b541d',
};
export function signalWireColor(signal) {
  const key=canonicalSignalKey(signal);
  if(SIGNAL_COLORS[key])return SIGNAL_COLORS[key];
  let hash=0;for(const character of key)hash=(hash*31+character.charCodeAt(0))>>>0;
  return `hsl(${hash%360}, 65%, 38%)`;
}

export function gpioRoleWireColor(key,label='') {
  const signals={'audio.wsPin':'WS','audio.bclkPin':'BCLK','audio.doutPin':'DIN','oled.sdaPin':'SDA','oled.sclPin':'SCL','oled.resetPin':'RST','battery.adcPin':'SIGNAL','sd.csPin':'CS','sd.sckPin':'SCK','sd.mosiPin':'MOSI','sd.misoPin':'MISO'};
  if(signals[key])return signalWireColor(signals[key]);
  const signal=canonicalSignalKey(label).match(/\b(SDA|SCL|WS|BCLK|DIN|DOUT|MOSI|MISO|CS|SCK|RX|TX|SIGNAL|SIG|PWM\d*|IN\d*|OUT|RST|INT|EN)\b/);
  return signalWireColor(signal?.[1]||label||key);
}

export function signalRole(value) {
  const key = canonicalSignalKey(value);
  if (GROUND_ALIASES.has(key) || key.startsWith("GND ") || key.startsWith("GROUND ")) return "ground";
  if (FIVE_VOLT_ALIASES.has(key) || key.startsWith("VIN ") || key.startsWith("5V ")) return "power-5v";
  if (THREE_VOLT_ALIASES.has(key) || key.startsWith("VCC ") || key.startsWith("3V3 ") || key.startsWith("3.3V ")) return "power-3v3";
  if (EXTERNAL_POWER_ALIASES.has(key) || key.startsWith("12V ") || key.startsWith("24V ")) return "power-external";
  if (["SIGNAL", "SIG", "OUT", "ADC", "GPIO"].includes(key)) return "signal";
  return "digital";
}

export function isPositivePowerSignal(value) {
  return signalRole(value).startsWith("power-");
}

export function isGroundSignal(value) {
  return signalRole(value) === "ground";
}

export function boardRailForPeripheral(groupKey, value) {
  const role = signalRole(value);
  if (role === "ground") return "GND";
  if (role === "power-5v") return "5V";
  if (role === "power-external") return canonicalSignalKey(value).split(" ")[0];
  if (role !== "power-3v3") return null;
  if (["3V3", "3.3V", "3VO"].includes(canonicalSignalKey(value))) return "3V3";
  return ["audio", "control", "power"].includes(String(groupKey || "")) ? "5V" : "3V3";
}

export function validateElectricalConnection(connection) {
  if (String(connection?.type || "") === "gpio") {
    const pin = Number(connection?.pin);
    return Number.isInteger(pin) && pin >= 0
      ? { valid: true, reason: "" }
      : { valid: false, reason: "GPIO connection has no valid board pin" };
  }
  const sourceRole = signalRole(connection?.signalLabel);
  const targetRole = signalRole(connection?.boardLabel);
  if (sourceRole === "ground" && targetRole !== "ground") {
    return { valid: false, reason: "Ground cannot connect to a positive supply" };
  }
  if (sourceRole.startsWith("power-") && !targetRole.startsWith("power-")) {
    return { valid: false, reason: "Positive supply cannot connect to ground" };
  }
  if (sourceRole === "power-5v" && targetRole !== "power-5v") {
    return { valid: false, reason: "VIN/5V requires a VIN/5V board rail" };
  }
  if (["3V3", "3.3V", "3VO"].includes(canonicalSignalKey(connection?.signalLabel)) && targetRole !== "power-3v3") {
    return { valid: false, reason: "3.3V requires a 3V3 board rail" };
  }
  return { valid: true, reason: "" };
}

export function requiredBoardRails(boardProfile) {
  const profile = String(boardProfile || "").toLowerCase();
  const bareThreeVoltModule = profile.startsWith("esp8266-esp01")
    || profile.startsWith("esp8266-esp12")
    || profile === "esp8285-generic";
  return bareThreeVoltModule ? ["GND", "3V3"] : ["GND", "3V3", "5V"];
}

export function normalizeBoardRails(boardProfile, layout = { left: [], right: [] }) {
  const normalized = {
    left: [...(layout.left || [])],
    right: [...(layout.right || [])],
  };
  const present = new Set([...normalized.left, ...normalized.right]
    .map((entry) => canonicalSignalKey(entry?.label))
    .filter(Boolean));
  for (const rail of requiredBoardRails(boardProfile)) {
    const role = signalRole(rail);
    if ([...present].some((label) => signalRole(label) === role)) continue;
    normalized.left.push({ pin: null, label: rail, synthesizedRail: true });
    present.add(rail);
  }
  return normalized;
}

export function shouldShowBoardLabel({ targetKey, labelId, usedTargets, savedLabelIds, isCustom = false }) {
  return Boolean(
    isCustom
    || usedTargets?.has(String(targetKey || ""))
    || savedLabelIds?.has(String(labelId || "")),
  );
}

export function defaultPeripheralPins(groupKey, profileValue, bindingPins = []) {
  const group = String(groupKey || "");
  const profile = String(profileValue || "none").toLowerCase();
  if (bindingPins.length && !(group === "audio" && (profile.includes("buzzer") || profile.includes("bluetooth")))) {
    const pins = [...bindingPins];
    if (!pins.some(isPositivePowerSignal)) pins.push("VCC");
    if (!pins.some(isGroundSignal)) pins.push("GND");
    return pins;
  }
  if (group === "sensor" && profile.includes("battery-voltage-divider")) return ["VIN", "SIGNAL", "GND"];
  if (group === "sensor" && profile.includes("ds18b20")) return ["DQ", "VCC", "GND"];
  if (group === "sensor" && (profile.includes("bno") || profile.includes("mpu"))) return ["SDA", "SCL", "INT", "VCC", "GND"];
  if (group === "display" && profile.includes("spi-tft")) return ["SCK", "MOSI", "MISO", "CS", "DC", "RST", "BL", "VCC", "GND"];
  if (group === "display") return profile.includes("waveshare") ? ["CTRL", "VCC", "GND"] : ["SDA", "SCL", "RST", "VCC", "GND"];
  if (group === "storage") return profile.includes("sdmmc")
    ? ["CLK", "CMD", "D0", "D1", "D2", "D3", "VCC", "GND"]
    : ["CS", "SCK", "MOSI", "MISO", "VCC", "GND"];
  if (group === "communication") {
    if (profile.includes("uart")) return ["TX", "RX", "VCC", "GND"];
    if (profile.includes("rs485")) return ["TX", "RX", "DE", "RE", "VCC", "GND"];
    if (profile.includes("lora")) return ["TX", "RX", "AUX", "M0", "M1", "VCC", "GND"];
    if (profile.includes("i2c")) return ["SDA", "SCL", "VCC", "GND"];
    if (profile.includes("spi")) return ["SCK", "MOSI", "MISO", "CS", "VCC", "GND"];
  }
  if (group === "audioIn") {
    if (profile.includes("pdm")) return ["CLK", "DATA", "VCC", "GND"];
    if (profile.includes("adc") || profile.includes("line-in") || profile.includes("electret") || profile.includes("max9814") || profile.includes("max4466")) return ["OUT", "VCC", "GND"];
    if (profile.includes("bluetooth")) return ["BT", "PWR"];
    if (profile.includes("codec") || profile.includes("external-i2s-adc") || profile.includes("es7243") || profile.includes("es7210")) return ["WS", "BCLK", "DOUT", "SDA", "SCL", "VCC", "GND"];
    return ["WS", "SCK", "SD", "VCC", "GND"];
  }
  if (group === "audio") {
    if (profile.includes("buzzer")) return ["SIG", "VCC", "GND"];
    if (profile.includes("bluetooth")) return ["BT", "PWR"];
    return ["WS", "BCLK", "DOUT", "VCC", "GND"];
  }
  if (group === "input") {
    if (profile.includes("limit-switch")) return ["COM", "NO", "NC"];
    if (profile.includes("rotary-encoder")) return ["A", "B", "SW", "VCC", "GND"];
    if (profile.includes("joystick")) return ["VRX", "VRY", "SW", "VCC", "GND"];
    if (profile.includes("keypad")) return ["R1", "R2", "R3", "R4", "C1", "C2", "C3", "C4"];
    if (profile.includes("esp32-native-touch-pad")) return ["TOUCH"];
    return [(profile.includes("analog") || profile.includes("receiver") || profile.includes("sensor")) ? "OUT" : "SIG", "VCC", "GND"];
  }
  if (group === "control") {
    if (profile.includes("dual-servo")) return ["PWM1", "PWM2", "5V", "GND"];
    if (profile.includes("drv8833")) return ["IN1", "IN2", "IN3", "IN4", "VCC", "GND"];
    if (profile.includes("servo") || profile.includes("buzzer") || profile.includes("vibration")) return ["SIG", "VCC", "GND"];
    if (profile.includes("led-pwm-dimmer")) return ["PWM", "VCC", "GND"];
    if (profile.includes("fan")) return ["PWM", "TACH", "VCC", "GND"];
    if (profile.includes("tb6612") || profile.includes("l298n") || profile.includes("dc-motor-driver-generic")) return ["AIN1", "AIN2", "BIN1", "BIN2", "PWMA", "PWMB", "STBY", "VCC", "GND"];
    if (profile.includes("bts7960")) return ["RPWM", "LPWM", "REN", "LEN", "VCC", "GND"];
    if (profile.includes("stepper")) return ["STEP", "DIR", "EN", "VIO", "GND"];
    if (profile.includes("ws2812")) return ["DIN", "5V", "GND"];
    if (profile.includes("relay") || profile.includes("mosfet") || profile.includes("solenoid") || profile.includes("pump")) return ["IN", "VCC", "GND"];
  }
  if (group === "expansion") {
    if (profile.includes("74hc595")) return ["SER", "SRCLK", "RCLK", "OE", "VCC", "GND"];
    if (profile.includes("74hc165")) return ["PL", "CP", "Q7", "CE", "VCC", "GND"];
    if (profile.includes("4067")) return ["S0", "S1", "S2", "S3", "SIG", "EN", "VCC", "GND"];
    if (profile.includes("4051")) return ["S0", "S1", "S2", "SIG", "EN", "VCC", "GND"];
    if (profile.includes("mcp3008")) return ["SCK", "MOSI", "MISO", "CS", "VCC", "GND"];
    if (profile.includes("i2c") || profile.includes("mcp23017") || profile.includes("pcf857") || profile.includes("ads") || profile.includes("mcp4725") || profile.includes("pca9685")) return ["SDA", "SCL", "VCC", "GND"];
  }
  return null;
}

export function automaticPinLabelRotation(ownerRect, destinationRect) {
  const ownerX = Number(ownerRect?.left || 0) + (Number(ownerRect?.width || 0) / 2);
  const ownerY = Number(ownerRect?.top || 0) + (Number(ownerRect?.height || 0) / 2);
  const destinationX = Number(destinationRect?.left || 0) + (Number(destinationRect?.width || 0) / 2);
  const destinationY = Number(destinationRect?.top || 0) + (Number(destinationRect?.height || 0) / 2);
  let angle = Math.atan2(destinationY - ownerY, destinationX - ownerX) * 180 / Math.PI;
  if (angle > 90) angle -= 180;
  if (angle < -90) angle += 180;
  if (Math.abs(angle) < 22.5) return 0;
  if (Math.abs(angle) > 67.5) return 90;
  return angle < 0 ? -45 : 45;
}
