import {freePeripheralPin,occupiedPeripheralPins} from "./modules/peripheral-gpio-defaults.js";
import {createVoltageDividerControls,voltageDividerMaximum,resistorLabel,resistorBands} from './modules/voltage-divider.js';
import { createAudioTab } from "./modules/audio-tab.js";
import { createLogsTab } from "./modules/logs-tab.js";
import { boardChipFamily, chipPins, pinChoices } from "./modules/board-pin-policy.js";
import { createBatteryTab } from "./modules/battery-tab.js";
import { createConfigurationBackupModule } from "./modules/configuration-backup.js";
import { createConfigurationGpioTab } from "./modules/configuration-gpio-tab.js";
import { createConfigurationPeripheralsTab } from "./modules/configuration-peripherals-tab.js";
import { createConfigurationSettingsPersistenceModule } from "./modules/configuration-settings-persistence.js";
import { createConfigurationSettingsSnapshotModule } from "./modules/configuration-settings-snapshot.js";
import { createDeviceTab } from "./modules/device-tab.js";
import { createDeviceMigrationTab } from "./modules/device-migration-tab.js";
import { createDisplayTab } from "./modules/display-tab.js";
import { createFirmwareTab } from "./modules/firmware-tab.js";
import { createUsbFlasher } from "./modules/usb-flasher.js";
import { createEffectsTab } from "./modules/effects-tab.js";
import { createHardwareTab } from "./modules/hardware-tab.js";
import { createInfoTab } from "./modules/info-tab.js";
import { createMotorTab } from "./modules/motor-tab.js";
import { createLocalBuilder } from "./modules/local-builder.js";
import { createMqttTab } from "./modules/mqtt-tab.js";
import { normalizeMotorRuntimeConfig } from "./modules/motor-runtime-config.js";
import {
  createPeripheralDiagramLabelEditorModule,
  peripheralDiagramLabelId,
  peripheralDiagramBoardNodeId,
  peripheralDiagramBoardLabelEntryId,
  peripheralDiagramBoardLabelDefaultLayout,
  rotatePeripheralDiagramNodeLabels,
} from "./modules/peripheral-diagram-label-editor.js";
import { createPlaybackStatusModule } from "./modules/playback-status.js";
import { createPeripheralDiagramWiringModule } from "./modules/peripheral-diagram-wiring.js";
import { createRadioBrowserModule } from "./modules/radio-browser.js";
import { createStatusRenderModule } from "./modules/status-render.js";
import { createStorageTab } from "./modules/storage-tab.js";
import { initTabNavigation } from "./modules/tab-navigation.js";
import { createUiHistoryModule } from "./modules/ui-history.js";
import { createWifiTab } from "./modules/wifi-tab.js";

const PC_DESIGNER_RUNTIME = new URLSearchParams(window.location.search).get("elmaRuntime") === "pc-designer";
if (PC_DESIGNER_RUNTIME) {
  // Apply this before any status response can render. The PC application owns
  // the full board catalog; only a firmware compiled for one board may prune
  // and lock the selector.
  document.body.classList.add("local-builder-mode");
}

const state = {
  status: null,
  settings: null,
  deviceReachable: null,
  offlineNoticeShown: false,
  storageInfoByTarget: {},
  currentStorageEntriesByTarget: { flash: [], sd: [] },
  currentStoragePathByTarget: { flash: "/", sd: "/" },
  currentStorageMetaByTarget: { flash: {}, sd: {} },
  activeStorageTarget: "flash",
  storageSelectionMode: false,
  storageSelectedPathsByTarget: { flash: [], sd: [] },
  storagePreviewItem: null,
  storagePreviewTarget: "flash",
  storagePreviewAudio: null,
  storagePreviewObjectUrl: "",
  storagePreviewArtworkUrl: "",
  storagePreviewRequestId: 0,
  storageListRequestId: 0,
  storagePreviewPlaybackMode: {
    deviceActive: false,
    previousDeviceActive: false,
    suppressAutoAdvance: false,
    loop: false,
    shuffle: false,
    autoplay: true,
    lastCompletionSequence: 0,
    seeking: false,
  },
  storageClickTimer: null,
  storagePlaybackFocusUrl: "",
  storageInitialLoadRequested: false,
  effectReindexInProgress: false,
  storageReindexInProgressByTarget: { flash: false, sd: false },
  headerActionsMenuOpen: false,
  rebootOverlayArmed: false,
  rebootOverlayTimer: null,
  rebootOverlayPollTimer: null,
  rebootOverlayCountdownRemaining: 0,
  rebootOverlayStartedAt: 0,
  rebootOverlayReconnectAllowedAt: 0,
  rebootOverlaySawDisconnect: false,
  stationRedirectProbeTimer: null,
  effectFileOptions: [],
  effectFilesLoading: false,
  effectFileOptionsLoaded: false,
  effectFileOptionsCacheKey: "",
  batteryMeasuredVoltageInput: "",
  recentPlayback: loadRecentPlayback(),
  radioCountries: [],
  radioStations: [],
  radioCountriesLoading: false,
  radioStationsLoading: false,
  wifiScanPollTimer: null,
  firmwareProgressPollTimer: null,
  statusPollTimer: null,
  touchLivePollTimer: null,
  firmwareReloadTimer: null,
  statusRequestInFlight: false,
  settingsSaveTimer: null,
  settingsSavePromise: null,
  settingsDirty: false,
  settingsLoading: false,
  settingsSaving: false,
  wifiScanRequestId: 0,
  firmwareReleasesLoaded: false,
  firmwareReleasesLoading: false,
  firmwareReleases: [],
  firmwareLatestVersion: "",
  firmwareSelectedVersion: "",
  updatePopupShownVersion: "",
  deferredEffectsReload: false,
  deferredStorageReload: false,
  awaitingFirmwareReboot: false,
  firmwareReloadPending: false,
  wifiSelectionPending: false,
  gpioUiInteractionDepth: 0,
  gpioRoleMenuOpen: false,
  peripheralUiInteractionDepth: 0,
  peripheralMenuOpen: false,
  peripheralDiagramNodeMap: {},
  peripheralDiagramAssetFailures: {},
  peripheralDiagramPositions: {},
  peripheralDiagramDrag: null,
  peripheralAudioProfiles: ["none"],
  peripheralAudioInProfiles: ["none"],
  peripheralDisplayProfiles: ["none"],
  peripheralSensorProfiles: ["none"],
  peripheralInputProfiles: ["none", "none"],
  peripheralStorageProfiles: ["none"],
  peripheralCommunicationProfiles: ["none"],
  peripheralPowerProfiles: ["none"],
  peripheralControlProfiles: ["none"],
  peripheralExpansionProfiles: ["none"],
  peripheralHelperBindings: loadPeripheralHelperBindings(),
  wifiConnectInProgress: false,
  mqttConnectInProgress: false,
  mqttActionInProgress: "",
  mqttRediscoveryInProgress: false,
  playbackActionInProgress: "",
  storageUploadInProgress: false,
  stationRedirectInProgress: false,
  oledPreviewScrollTimer: null,
  oledPreviewScrollSignature: "",
  oledPreviewScrollOffset: 0,
};

let tabNavigation = null;
let logsTab = null;
let audioTab = null;
let batteryTab = null;
let configurationBackupModule = null;
let configurationGpioTab = null;
let configurationPeripheralsTab = null;
let configurationSettingsPersistenceModule = null;
let configurationSettingsSnapshotModule = null;
let deviceTab = null;
let deviceMigrationTab = null;
let displayTab = null;
let effectsTab = null;
let firmwareTab = null;
let usbFlasher = null;
let hardwareTab = null;
let infoTab = null;
let motorTab = null;
let mqttTab = null;
let peripheralDiagramLabelEditorModule = null;
let peripheralDiagramWiringModule = null;
let playbackStatusModule = null;
let radioBrowserModule = null;
let statusRenderModule = null;
let storageTab = null;
let uiHistoryModule = null;
let wifiTab = null;
const peripheralDiagramControlHideTimers = new WeakMap();

const SETTINGS_AUTOSAVE_DELAY_MS = 900;
const ACTIVE_TAB_STORAGE_KEY = "notifierActiveTab";
const RADIO_SELECTION_STORAGE_KEY = "notifierRadioSelection";
const EFFECT_FILES_CACHE_STORAGE_KEY = "notifierEffectFilesCache";
const STORAGE_LOCATION_STORAGE_KEY = "notifierStorageLocation";
const GPIO_BOARD_SELECTION_STORAGE_KEY = "notifierGpioBoardSelection";
const GPIO_BOARD_AUTODETECT_STORAGE_KEY = "notifierGpioBoardAutodetect";
const PERIPHERAL_DIAGRAM_POSITIONS_STORAGE_KEY = "notifierPeripheralDiagramPositions";
const PERIPHERAL_HELPER_BINDINGS_STORAGE_KEY = "notifierPeripheralHelperBindings";
const PERIPHERAL_PROFILE_SELECTIONS_STORAGE_KEY = "notifierPeripheralProfileSelections";
const STORAGE_INITIAL_PAGE_SIZE = 20;
const STORAGE_SCROLL_PAGE_SIZE = 20;
const STORAGE_AUDIO_EXTENSIONS = new Set(["mp3", "wav", "aac", "m4a", "ogg", "opus", "flac"]);
const DEFAULT_RADIO_SELECTION = {
  country: "Azerbaijan",
  stationName: "AvtoFM",
  stationUrl: "",
};
const EFFECT_FILE_SOURCES = [
  { target: "sd", dir: "/media/wav", prefix: "SD" },
  { target: "flash", dir: "/wav", prefix: "Flash" },
];
const EFFECT_FILE_PAGE_SIZE = 20;
const PERIPHERAL_DIAGRAM_CONTROL_HIDE_DELAY_MS = 2000;
const MAX_PERIPHERAL_AUDIO_OUTPUTS = 3;
const MAX_PERIPHERAL_AUDIO_INPUTS = 3;
const MAX_PERIPHERAL_DISPLAYS = 2;
const MAX_PERIPHERAL_SENSORS = 10;
const MAX_PERIPHERAL_INPUTS = 10;
const MAX_PERIPHERAL_STORAGES = 3;
const MAX_PERIPHERAL_COMMUNICATIONS = 4;
const MAX_PERIPHERAL_POWERS = 3;
const MAX_PERIPHERAL_CONTROLS = 16;
const MAX_PERIPHERAL_EXPANSIONS = 4;
const PERIPHERAL_AUDIO_PROFILE_OPTIONS = [
  { value: "none", label: "None" },
  { value: "max98357a-i2s-amp", label: "MAX98357A I2S Amp" },
  { value: "pcm5102-i2s-dac", label: "PCM5102 I2S DAC" },
  { value: "uda1334a-i2s-dac", label: "UDA1334A I2S DAC" },
  { value: "es9023-i2s-dac", label: "ES9023 I2S DAC" },
  { value: "pt8211-i2s-dac", label: "PT8211 I2S DAC" },
  { value: "cs4344-i2s-dac", label: "CS4344 I2S DAC" },
  { value: "internal-dac-gpio25-26", label: "Internal DAC GPIO25/26" },
  { value: "pwm-class-d-amp", label: "PWM / Class-D Amp" },
  { value: "analog-line-out-via-dac", label: "Analog Line-Out via DAC" },
  { value: "pam8403-analog-amp", label: "PAM8403 Analog Amp" },
  { value: "tpa3110-tpa3116-analog-amp", label: "TPA3110 / TPA3116 Analog Amp" },
  { value: "buzzer", label: "Buzzer" },
  { value: "wm8960-audio-codec", label: "WM8960 Audio Codec" },
  { value: "es8388-audio-codec", label: "ES8388 Audio Codec" },
  { value: "bluetooth-audio-source", label: "Bluetooth Audio Source" },
  { value: "custom", label: "Custom" },
];
const PERIPHERAL_AUDIO_IN_PROFILE_OPTIONS = [
  { value: "none", label: "None" },
  { value: "i2s-microphone-generic", label: "I2S Microphone Generic" },
  { value: "inmp441-i2s-mic", label: "INMP441 I2S Mic" },
  { value: "sph0645-ics43434-i2s-mic", label: "SPH0645 / ICS-43434 I2S Mic" },
  { value: "msm2615-i2s-mic", label: "MSM2615 I2S Mic" },
  { value: "pdm-microphone", label: "PDM Microphone" },
  { value: "analog-electret-mic-adc", label: "Analog Electret Mic ADC" },
  { value: "max9814-mic-adc", label: "MAX9814 Mic ADC" },
  { value: "max4466-mic-adc", label: "MAX4466 Mic ADC" },
  { value: "line-in-adc", label: "Line-In ADC" },
  { value: "external-i2s-adc", label: "External I2S ADC" },
  { value: "es7243-es7210-i2s-adc", label: "ES7243 / ES7210 I2S ADC" },
  { value: "wm8960-audio-codec", label: "WM8960 Audio Codec" },
  { value: "es8388-audio-codec", label: "ES8388 Audio Codec" },
  { value: "bluetooth-audio-sink", label: "Bluetooth Audio Sink" },
  { value: "custom", label: "Custom" },
];
const PERIPHERAL_DISPLAY_PROFILE_OPTIONS = [
  { value: "none", label: "None" },
  { value: "i2c-oled", label: "I2C OLED" },
  { value: "spi-tft", label: "SPI TFT" },
  { value: "waveshare-screen", label: "Waveshare Screen" },
  { value: "custom", label: "Custom" },
];
const PERIPHERAL_SENSOR_PROFILE_OPTIONS = [
  { value: "none", label: "None" },
  { value: "bno055", label: "BNO055" },
  { value: "bno085-bno080", label: "BNO085 / BNO080" },
  { value: "mpu6050", label: "MPU6050" },
  { value: "ds18b20", label: "DS18B20" },
  { value: "battery-voltage-divider-220k", label: "Battery Voltage Divider (2x 220kOhms)" },
  { value: "custom", label: "Custom" },
];
const BATTERY_DIVIDER_SENSOR_PROFILE = "battery-voltage-divider-220k";
const PERIPHERAL_INPUT_PROFILE_OPTIONS = [
  { value: "none", label: "None" },
  { value: "ttp223-touch-button", label: "TTP223 Touch Button" },
  { value: "physical-button", label: "Physical Button" },
  { value: "toggle-switch", label: "Toggle Switch" },
  { value: "rotary-encoder", label: "Rotary Encoder" },
  { value: "ir-receiver", label: "IR Receiver" },
  { value: "pir-motion-sensor", label: "PIR Motion Sensor" },
  { value: "reed-switch", label: "Reed Switch" },
  { value: "limit-switch", label: "Limit Switch" },
  { value: "joystick-analog", label: "Joystick Analog" },
  { value: "analog-potentiometer", label: "Analog Potentiometer" },
  { value: "esp32-native-touch-pad", label: "Touch Button (ESP32 Native)" },
  { value: "water-leak-rain-sensor", label: "Water Leak / Rain Sensor" },
  { value: "vibration-shock-sensor", label: "Vibration / Shock Sensor" },
  { value: "hall-sensor", label: "Hall Sensor" },
  { value: "flow-meter-pulse-sensor", label: "Flow Meter Pulse Sensor" },
  { value: "keypad-matrix", label: "Keypad Matrix" },
  { value: "rf-433mhz-receiver", label: "RF 433MHz Receiver" },
  { value: "wake-button", label: "Wake Button" },
  { value: "custom", label: "Custom" },
];
const PERIPHERAL_STORAGE_PROFILE_OPTIONS = [
  { value: "none", label: "None" },
  { value: "microsd-spi", label: "MicroSD SPI" },
  { value: "microsd-sdmmc", label: "MicroSD SDMMC" },
  { value: "custom", label: "Custom" },
];
const PERIPHERAL_COMMUNICATION_PROFILE_OPTIONS = [
  { value: "none", label: "None" },
  { value: "uart", label: "UART" },
  { value: "rs485", label: "RS485" },
  { value: "lora-e22-e220", label: "LoRa E22/E220" },
  { value: "i2c", label: "I2C" },
  { value: "spi", label: "SPI" },
  { value: "custom", label: "Custom" },
];
const PERIPHERAL_POWER_PROFILE_OPTIONS = [
  { value: "none", label: "None" },
  { value: "dc-dc-step-down", label: "DC-DC Step-Down" },
  { value: "dc-dc-step-up", label: "DC-DC Step-Up" },
  { value: "custom", label: "Custom" },
];
const PERIPHERAL_DIAGRAM_ASSET_MAP = {
  audio: {
    "max98357a-i2s-amp": { src: "/max98357a-breadboard.svg", label: "Audio Out" },
    "pcm5102-i2s-dac": { src: "/pcm5102a-breadboard.svg", label: "Audio Out" },
  },
  audioIn: {
    "inmp441-i2s-mic": { src: "/inmp441-breadboard.svg", label: "Audio In" },
  },
  display: {
    "i2c-oled": { src: "/i2c-oled-breadboard.svg", label: "Display" },
  },
  storage: {
    "microsd-spi": { src: "/microsd-spi-module.svg", label: "Storage" },
  },
  power: {
    "dc-dc-step-down": { src: "/dc-dc-step-down-breadboard.svg", label: "Power" },
    "dc-dc-step-up": { src: "/dc-dc-step-up-breadboard.svg", label: "Power" },
  },
  input: {
    "limit-switch": { src: "/limit-switch-module-breadboard.svg", label: "Input" },
    "ttp223-touch-button": { src: "/ttp223-touch-module.svg", label: "Input" },
  },
  control: {
    "drv8833-dual-motor-driver": {
      src: "/drv8833-motor-driver-breadboard.svg",
      className: "peripheral-diagram-node peripheral-diagram-node-control peripheral-diagram-node-drv8833",
    },
  },
};
const PERIPHERAL_CONTROL_PROFILE_OPTIONS = [
  { value: "none", label: "None" },
  { value: "servo", label: "Servo" },
  { value: "dual-servo", label: "Dual Servo" },
  { value: "pwm-fan", label: "PWM Fan" },
  { value: "dc-motor-driver-generic", label: "DC Motor Driver Generic" },
  { value: "drv8833-dual-motor-driver", label: "DRV8833 Dual Motor Driver" },
  { value: "tb6612fng-dual-motor-driver", label: "TB6612FNG Dual Motor Driver" },
  { value: "l298n-dual-motor-driver", label: "L298N Dual Motor Driver" },
  { value: "bts7960-high-power-motor-driver", label: "BTS7960 High Power Motor Driver" },
  { value: "stepper-driver-a4988-drv8825", label: "Stepper Driver A4988 / DRV8825" },
  { value: "stepper-driver-tmc2208-tmc2209", label: "Stepper Driver TMC2208 / TMC2209" },
  { value: "relay-module", label: "Relay Module" },
  { value: "mosfet-switch", label: "MOSFET Switch" },
  { value: "solenoid-valve-driver", label: "Solenoid / Valve Driver" },
  { value: "led-pwm-dimmer", label: "LED / PWM Dimmer" },
  { value: "ws2812-neopixel-led-strip", label: "WS2812 / NeoPixel LED Strip" },
  { value: "buzzer", label: "Buzzer" },
  { value: "vibration-motor", label: "Vibration Motor" },
  { value: "pump-driver", label: "Pump Driver" },
  { value: "custom", label: "Custom" },
];
const PERIPHERAL_EXPANSION_PROFILE_OPTIONS = [
  { value: "none", label: "None" },
  { value: "i2c-gpio-expander", label: "I2C GPIO Expander" },
  { value: "mcp23017-16-bit-io-expander", label: "MCP23017 16-bit I/O Expander" },
  { value: "pcf8574-8-bit-io-expander", label: "PCF8574 8-bit I/O Expander" },
  { value: "pcf8575-16-bit-io-expander", label: "PCF8575 16-bit I/O Expander" },
  { value: "74hc595-output-expander", label: "Shift Register 74HC595 Output Expander" },
  { value: "74hc165-input-expander", label: "Shift Register 74HC165 Input Expander" },
  { value: "cd4051-74hc4051-8-channel", label: "Analog Multiplexer CD4051 / 74HC4051 8-channel" },
  { value: "cd74hc4067-16-channel", label: "Analog Multiplexer CD74HC4067 16-channel" },
  { value: "ads1115-16-bit-i2c", label: "External ADC ADS1115 16-bit I2C" },
  { value: "ads1015-12-bit-i2c", label: "External ADC ADS1015 12-bit I2C" },
  { value: "mcp3008-spi", label: "External ADC MCP3008 SPI" },
  { value: "mcp4725-i2c", label: "External DAC MCP4725 I2C" },
  { value: "pca9685", label: "PWM Expander PCA9685" },
  { value: "custom", label: "Custom" },
];
const POWER_STEP_DOWN_INPUT_OPTIONS = ["4.5V", "5V", "6V", "7.4V", "9V", "12V", "15V", "18V", "20V", "24V", "28V"];
const POWER_STEP_DOWN_OUTPUT_OPTIONS = ["0.8V", "1.2V", "1.5V", "1.8V", "2.5V", "3.3V", "5V", "6V", "9V", "12V", "15V", "20V"];
const POWER_STEP_UP_INPUT_OPTIONS = ["2.5V", "3V", "3.3V", "3.7V", "4.2V", "5V"];
const POWER_STEP_UP_OUTPUT_OPTIONS = ["5V", "8V", "9V", "12V"];

function peripheralDiagramOptionLabel(options, value, fallbackLabel = "Peripheral") {
  const normalizedValue = String(value || "").trim();
  if (!normalizedValue || normalizedValue === "none") {
    return fallbackLabel;
  }
  const match = Array.isArray(options)
    ? options.find((entry) => String(entry?.value || "").trim() === normalizedValue)
    : null;
  return String(match?.label || fallbackLabel).trim() || fallbackLabel;
}

const EFFECT_SELECT_CONFIG = [
  { id: "effectStartupFile", field: "startupFile", label: "Startup", source: "effect-startup", volumeId: "effectStartupVolumePercent", volumeField: "startupVolumePercent" },
  { id: "effectAlarmFile", field: "alarmFile", label: "Alarm", source: "effect-alarm", volumeId: "effectAlarmVolumePercent", volumeField: "alarmVolumePercent" },
  { id: "effectNotificationFile", field: "notificationFile", label: "Notification", source: "effect-notification", volumeId: "effectNotificationVolumePercent", volumeField: "notificationVolumePercent" },
  { id: "effectAmbientSoundFile", field: "ambientSoundFile", label: "Ambient Sound", source: "effect-ambient", volumeId: "effectAmbientVolumePercent", volumeField: "ambientVolumePercent" },
  { id: "effectLowBatteryFile", field: "lowBatteryFile", label: "Low Battery", source: "effect-low-battery", volumeId: "effectLowBatteryVolumePercent", volumeField: "lowBatteryVolumePercent" },
  { id: "effectShutDownFile", field: "shutDownFile", label: "Shut Down", source: "effect-shutdown", volumeId: "effectShutDownVolumePercent", volumeField: "shutDownVolumePercent" },
  { id: "effectUpdateAvailableFile", field: "updateAvailableFile", label: "Updates Available", source: "effect-update-available", volumeId: "effectUpdateAvailableVolumePercent", volumeField: "updateAvailableVolumePercent" },
  { id: "effectUpdateSuccessFile", field: "updateSuccessFile", label: "Update Success", source: "effect-update-success", volumeId: "effectUpdateSuccessVolumePercent", volumeField: "updateSuccessVolumePercent" },
];
const STORAGE_PREVIEW_EMBEDDED_SCAN_MAX_BYTES = 256 * 1024;
const GPIO_BOARD_PRESENTATION = {
  "esp32-s3-super-mini": {
    rotation: "rotate(90deg)",
    rank: "Current board",
    recommendation: "Compact ESP32-S3 board. Good for speaker builds, but with tighter pin breakout than larger S3 boards.",
    tone: "featured",
  },
  "esp32-s3-zero": {
    rotation: "rotate(90deg)",
    rank: "Compact S3 option",
    recommendation: "Very small ESP32-S3 board. Good when you need S3 features in a minimal footprint.",
    tone: "good",
  },
  "esp32-s3-psram": {
    rotation: "none",
    rank: "S3 PSRAM variant",
    recommendation: "ESP32-S3-N16R8 layout with PSRAM and larger breakout coverage.",
    tone: "good",
  },
  "esp32-spk-n16r8": {
    rotation: "none",
    rank: "Speaker board variant",
    recommendation: "ESP32-S3 speaker/camera board with wide breakout access, onboard peripherals, and several already-committed pins.",
    tone: "warn",
  },
  "esp32-s3-devkit-c1": {
    rotation: "none",
    rank: "S3 DevKit variant",
    recommendation: "ESP32-S3 DevKit C-1 breakout with the N8R8-style pin arrangement.",
    tone: "good",
  },
  "esp32-s3-cam-module": {
    rotation: "none",
    rank: "S3 camera module",
    recommendation: "ESP32-S3-CAM module layout with the compact N16R8 module pin breakout.",
    tone: "warn",
  },
  "esp32-wrover": {
    rotation: "none",
    rank: "2. Very good",
    recommendation: "Strong classic ESP32 choice with PSRAM. A solid fallback when S3 boards are not available.",
    tone: "good",
  },
  "esp32-wroom": {
    rotation: "rotate(90deg)",
    rank: "3. Good",
    recommendation: "Good for speaker projects, but with less memory headroom than PSRAM-equipped boards.",
    tone: "good",
  },
  "esp32-mini": {
    rotation: "none",
    rank: "4. Compact fallback",
    recommendation: "Compact board for lighter builds. Works, but GPIO and memory headroom are tighter.",
    tone: "neutral",
  },
  "wemos-lolin32-mini": {
    rotation: "rotate(90deg)",
    rank: "ESP32 compact variant",
    recommendation: "Wemos Lolin32 Mini layout with the board-specific narrow pinout and VP/VN analog inputs.",
    tone: "neutral",
  },
  "esp32-s2-psram": {
    rotation: "none",
    rank: "5. Acceptable",
    recommendation: "Acceptable for simpler audio use, but it is not as strong as S3 or WROVER boards for this project.",
    tone: "neutral",
  },
  "esp32-c6": {
    rotation: "none",
    rank: "6. Works",
    recommendation: "Works, but it is not audio-focused. Choose it only if you specifically need the C6 platform.",
    tone: "neutral",
  },
  "esp32-c3": {
    rotation: "none",
    rank: "7. Basic only",
    recommendation: "ESP32-C3 Super Mini layout. Usable for simple builds, but still the most limited option here for speaker-oriented use.",
    tone: "basic",
  },
};
const GPIO_BOARD_ASSETS = {
  "esp32-s3-super-mini": {
    src: "/esp32-s3-supermini-breadboard.svg",
    alt: "ESP32-S3 Super Mini board",
  },
  "esp32-s3-zero": {
    src: "/esp32-s3-zero-breadboard.svg",
    alt: "ESP32-S3 Zero board",
  },
  "esp32-s3-psram": {
    src: "/esp32-s3-psram-breadboard.svg",
    alt: "ESP32-S3-N16R8 board",
  },
  "esp32-spk-n16r8": {
    src: "/esp32-spk-n16r8-breadboard.svg",
    alt: "ESP32-SPK-N16R8 board",
  },
  "esp32-s3-devkit-c1": {
    src: "/esp32-s3-devkit-c1-n8r8-v1-breadboard.svg",
    alt: "ESP32-S3 DevKit C-1 board",
  },
  "esp32-s3-cam-module": {
    src: "/esp32-s3-cam-module-breadboard.svg",
    alt: "ESP32-S3-CAM module board",
  },
  "esp32-wrover": {
    src: "/esp32-wrover-breadboard.svg",
    alt: "ESP32-WROVER board",
  },
  "esp32-wroom": {
    src: "/esp32-wroom-breadboard.svg",
    alt: "Classic ESP32-WROOM board",
  },
  "esp32-mini": {
    src: "/esp32-mini-breadboard.svg",
    alt: "ESP32 Mini board",
  },
  "wemos-lolin32-mini": {
    src: "/wemos-lolin32-mini-breadboard.svg",
    alt: "Wemos Lolin32 Mini board",
  },
  "esp32-s2-psram": {
    src: "/esp32-s2-mini-breadboard.svg",
    alt: "ESP32-S2 with PSRAM board",
  },
  "esp32-c6": {
    src: "/esp32-c6-mini-breadboard.svg",
    alt: "ESP32-C6 board",
  },
  "esp32-c3": {
    src: "/esp32-c3-breadboard.svg",
    alt: "ESP32-C3 Super Mini board",
  },
};
const OLED_PREVIEW_SCROLL_INTERVAL_MS = 300;
const DEFAULT_ESP32S3_AUDIO_PINS = {
  ws: 12,
  bclk: 11,
  dout: 9,
};
const DEFAULT_ESP32S3_OLED_PREFERRED_PINS = {
  sda: 4,
  scl: 5,
};
const DEFAULT_SD_GPIO_PINS = {
  cs: 4,
  sck: 5,
  mosi: 6,
  miso: 7,
};
const DOCUMENTED_BUZZER_PIN = 7;
const GPIO_BOARD_LAYOUTS = {
  "esp32-s3-super-mini": {
    left: [
      { pin: 43, label: "TX / GPIO43" },
      { pin: 44, label: "RX / GPIO44" },
      ...[1, 2, 3, 4, 5, 6, 7].map((pin) => ({ pin, label: `GPIO${pin}` })),
    ],
    right: [
      { pin: null, label: "5V" },
      { pin: null, label: "GND" },
      { pin: null, label: "3V3" },
      ...[13, 12, 11, 10, 9, 8].map((pin) => ({ pin, label: `GPIO${pin}` })),
    ],
  },
  "esp32-s3-zero": {
    left: [
      { pin: null, label: "5V" },
      { pin: null, label: "GND" },
      { pin: null, label: "3V3" },
      ...[1, 2, 3, 4, 5, 6].map((pin) => ({ pin, label: `GPIO${pin}` })),
    ],
    right: [
      { pin: 43, label: "TX / GPIO43" },
      { pin: 44, label: "RX / GPIO44" },
      ...[13, 12, 11, 10, 9, 8, 7, 16, 15, 14].map((pin) => ({ pin, label: `GPIO${pin}` })),
    ],
  },
  "esp32-s3-psram": {
    left: [
      { pin: null, label: "GND" },
      { pin: null, label: "3V3" },
      { pin: null, label: "EN" },
      ...[4, 5, 6, 7, 15, 16, 17, 18, 8, 19, 20, 3, 46, 9, 10, 11, 12].map((pin) => ({ pin, label: `GPIO${pin}` })),
      { pin: null, label: "3V3" },
    ],
    right: [
      { pin: null, label: "GND" },
      { pin: 1, label: "GPIO1" },
      { pin: 2, label: "GPIO2" },
      { pin: 43, label: "TX0 / GPIO43" },
      { pin: 44, label: "RX0 / GPIO44" },
      ...[42, 41, 40, 38, 37, 36, 35, 0, 45, 48, 47, 21, 14, 13].map((pin) => ({ pin, label: `GPIO${pin}` })),
      { pin: null, label: "5V IN" },
    ],
  },
  "esp32-spk-n16r8": {
    left: [
      { pin: null, label: "5V" },
      { pin: null, label: "GND" },
      { pin: null, label: "3.3V" },
      { pin: null, label: "GND" },
      ...[20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6].map((pin) => ({ pin, label: `GPIO${pin}` })),
    ],
    right: [
      { pin: null, label: "5V" },
      { pin: null, label: "GND" },
      { pin: null, label: "3.3V" },
      { pin: null, label: "GND" },
      { pin: null, label: "NC" },
      { pin: null, label: "NC" },
      { pin: 21, label: "GPIO21" },
      { pin: 38, label: "GPIO38" },
      { pin: 39, label: "GPIO39" },
      { pin: 40, label: "GPIO40" },
      { pin: 43, label: "GPIO43" },
      { pin: 44, label: "GPIO44" },
      { pin: 46, label: "GPIO46" },
      { pin: 0, label: "GPIO0" },
      { pin: 2, label: "GPIO2" },
      { pin: null, label: "EN" },
      { pin: 1, label: "GPIO1" },
      { pin: 3, label: "GPIO3" },
    ],
  },
  "esp32-s3-devkit-c1": {
    left: [
      { pin: null, label: "3V3" },
      { pin: null, label: "3V3" },
      { pin: null, label: "RST" },
      ...[4, 5, 6, 7, 15, 16, 17, 18, 8, 3, 46, 9, 10, 11, 12, 13, 14].map((pin) => ({ pin, label: `GPIO${pin}` })),
      { pin: null, label: "5V" },
      { pin: null, label: "GND" },
    ],
    right: [
      { pin: null, label: "GND" },
      { pin: 43, label: "TX / GPIO43" },
      { pin: 44, label: "RX / GPIO44" },
      { pin: 1, label: "GPIO1" },
      { pin: 2, label: "GPIO2" },
      ...[42, 41, 40, 39, 38, 37, 36, 35, 0, 45, 48, 47, 21, 20, 19].map((pin) => ({ pin, label: `GPIO${pin}` })),
      { pin: null, label: "GND" },
      { pin: null, label: "GND" },
    ],
  },
  "esp32-s3-cam-module": {
    left: [
      { pin: null, label: "3V3" },
      { pin: null, label: "RST" },
      ...[4, 5, 6, 7, 15, 16, 17, 18, 8, 3, 46, 9, 10, 11, 12, 13, 14].map((pin) => ({ pin, label: `GPIO${pin}` })),
      { pin: null, label: "5V" },
    ],
    right: [
      { pin: 43, label: "TX / GPIO43" },
      { pin: 44, label: "RX / GPIO44" },
      { pin: 1, label: "GPIO1" },
      { pin: 2, label: "GPIO2" },
      ...[42, 41, 40, 39, 38, 37, 36, 35, 0, 45, 48, 47, 21, 20, 19].map((pin) => ({ pin, label: `GPIO${pin}` })),
      { pin: null, label: "GND" },
    ],
  },
  "esp32-wrover": {
    left: [36, 39, 34, 35, 32, 33, 25, 26, 27, 14, 12, 13].map((pin) => ({ pin, label: `GPIO${pin}` })),
    right: [23, 22, 1, 3, 21, 19, 18, 5, 17, 16, 4, 0, 2, 15].map((pin) => ({
      pin,
      label: pin === 1 ? "TX / GPIO1" : (pin === 3 ? "RX / GPIO3" : `GPIO${pin}`),
    })),
  },
  "esp32-wroom": {
    left: [36, 39, 34, 35, 32, 33, 25, 26, 27, 14, 12, 13].map((pin) => ({ pin, label: `GPIO${pin}` })),
    right: [23, 22, 1, 3, 21, 19, 18, 5, 17, 16, 4, 0, 2, 15].map((pin) => ({
      pin,
      label: pin === 1 ? "TX / GPIO1" : (pin === 3 ? "RX / GPIO3" : `GPIO${pin}`),
    })),
  },
  "esp32-mini": {
    left: [
      { pin: null, label: "RST" },
      { pin: null, label: "A0" },
      { pin: 16, label: "D0 / GPIO16" },
      { pin: 14, label: "D5 / GPIO14" },
      { pin: 12, label: "D6 / GPIO12" },
      { pin: 13, label: "D7 / GPIO13" },
      { pin: 15, label: "D8 / GPIO15" },
      { pin: null, label: "3V3" },
    ],
    right: [
      { pin: 1, label: "TX / GPIO1" },
      { pin: 3, label: "RX / GPIO3" },
      { pin: 5, label: "D1 / GPIO5" },
      { pin: 4, label: "D2 / GPIO4" },
      { pin: 0, label: "D3 / GPIO0" },
      { pin: 2, label: "D4 / GPIO2" },
      { pin: null, label: "GND" },
      { pin: null, label: "5V" },
    ],
  },
  "wemos-lolin32-mini": {
    left: [
      { pin: 36, label: "VP / GPIO36" },
      { pin: 39, label: "VN / GPIO39" },
      { pin: null, label: "EN" },
      ...[34, 35, 32, 33, 25, 26, 27, 14, 12].map((pin) => ({ pin, label: `GPIO${pin}` })),
      { pin: null, label: "GND" },
    ],
    right: [
      { pin: null, label: "3V3" },
      ...[22, 19, 23, 18, 5, 17, 16, 4, 0, 2, 15, 13].map((pin) => ({ pin, label: `GPIO${pin}` })),
    ],
  },
  "esp32-s2-psram": {
    left: [
      { pin: 1, label: "GPIO1" },
      { pin: null, label: "EN" },
      { pin: 2, label: "GPIO2" },
      { pin: 3, label: "GPIO3" },
      { pin: 4, label: "GPIO4" },
      { pin: 5, label: "GPIO5" },
      { pin: 6, label: "GPIO6" },
      { pin: 7, label: "GPIO7" },
      { pin: 8, label: "GPIO8" },
      { pin: 9, label: "GPIO9" },
      { pin: 10, label: "GPIO10" },
      { pin: 11, label: "GPIO11" },
      { pin: 13, label: "GPIO13" },
      { pin: 12, label: "GPIO12" },
      { pin: 14, label: "GPIO14" },
      { pin: null, label: "3.3V" },
    ],
    right: [
      { pin: 39, label: "GPIO39" },
      { pin: 40, label: "GPIO40" },
      { pin: 37, label: "GPIO37" },
      { pin: 38, label: "GPIO38" },
      { pin: 35, label: "GPIO35" },
      { pin: 36, label: "GPIO36" },
      { pin: 33, label: "GPIO33" },
      { pin: 34, label: "GPIO34" },
      { pin: 18, label: "GPIO18" },
      { pin: 21, label: "GPIO21" },
      { pin: 16, label: "GPIO16" },
      { pin: 17, label: "GPIO17" },
      { pin: null, label: "GND" },
      { pin: null, label: "GND" },
      { pin: null, label: "VBUS" },
      { pin: 15, label: "GPIO15" },
    ],
  },
  "esp32-c6": {
    left: [
      { pin: 16, label: "TX / GPIO16" },
      { pin: 17, label: "RX / GPIO17" },
      ...[0, 1, 2, 3, 4, 5, 6, 7].map((pin) => ({ pin, label: `GPIO${pin}` })),
    ],
    right: [
      { pin: null, label: "5V" },
      { pin: null, label: "GND" },
      { pin: null, label: "3.3V" },
      ...[20, 19, 18, 15, 14, 9, 8].map((pin) => ({ pin, label: `GPIO${pin}` })),
    ],
  },
  "esp32-c3": {
    // Physical top-to-bottom order on the supplied ESP32-C3 Super Mini
    // breadboard artwork (USB-C at the bottom).
    left: [5, 6, 7, 8, 9, 10, 20, 21].map((pin) => ({
      pin,
      label: `GPIO${pin}`,
    })),
    right: [
      { pin: null, label: "5V" },
      { pin: null, label: "GND" },
      { pin: null, label: "3.3V" },
      { pin: 4, label: "GPIO4" },
      { pin: 3, label: "GPIO3" },
      { pin: 2, label: "GPIO2" },
      { pin: 1, label: "GPIO1" },
      { pin: 0, label: "GPIO0" },
    ],
  },
};
const GPIO_BOARD_EXTRA_LAYOUTS = {
  "esp32-s3-super-mini": {
    left: [48, 47, 46, 45, 42, 41, 40, 39].map((pin) => ({ pin, label: `GPIO${pin}` })),
    right: [34, 33, 21, 18, 17, 16, 15, 14].map((pin) => ({ pin, label: `GPIO${pin}` })),
  },
  "esp32-s3-zero": {
    left: [39, 40, 41, 42, 43, 44, 45, 46, 47, 48].map((pin) => ({ pin, label: `GPIO${pin}` })),
    right: [38, 37, 36, 35, 34, 33, 21, 18, 17].map((pin) => ({ pin, label: `GPIO${pin}` })),
  },
  "esp32-c6": {
    left: [15, 8, 23].map((pin) => ({ pin, label: `GPIO${pin}` })),
    right: [12, 13, 21, 22].map((pin) => ({ pin, label: `GPIO${pin}` })),
  },
};
const GPIO_BOARD_RESERVED_PINS = {
  "esp32-c6": {
    8: { label: "WS2812", warning: "Reserved onboard LED pin: GPIO8 drives the built-in WS2812 RGB LED (DIN).", kind: "onboard" },
    9: { label: "BOOT", warning: "Reserved strap pin: GPIO9 is tied to the BOOT function on this ESP32-C6 board.", kind: "strap" },
    15: { label: "LED", warning: "Board-tied LED pin: GPIO15 drives the onboard LED on this ESP32-C6 board.", kind: "onboard" },
  },
  "esp32-s2-psram": {
    15: { label: "LED", warning: "Board-tied LED pin: GPIO15 drives the onboard LED on this ESP32-S2 PSRAM board.", kind: "onboard" },
    34: { label: "SPI CS", warning: "Reserved SPI/PSRAM pin: GPIO34 is tied to the onboard CS signal on this board.", kind: "psram" },
    35: { label: "SPI MOSI", warning: "Reserved SPI/PSRAM pin: GPIO35 is tied to the onboard MOSI signal on this board.", kind: "psram" },
    36: { label: "SPI SCK", warning: "Reserved SPI/PSRAM pin: GPIO36 is tied to the onboard clock signal on this board.", kind: "psram" },
    37: { label: "SPI MISO", warning: "Reserved SPI/PSRAM pin: GPIO37 is tied to the onboard MISO signal on this board.", kind: "psram" },
    39: { label: "JTAG MTCK", warning: "Board-tied JTAG pin: GPIO39 is routed to MTCK on this ESP32-S2 board.", kind: "jtag" },
    40: { label: "JTAG MTDO", warning: "Board-tied JTAG pin: GPIO40 is routed to MTDO on this ESP32-S2 board.", kind: "jtag" },
  },
  "esp32-s3-cam-module": {
    0: { label: "BOOT", warning: "Reserved strap pin: GPIO0 is used for BOOT mode.", kind: "strap" },
    2: { label: "LED ON", warning: "Board-tied LED pin: GPIO2 drives the onboard LED.", kind: "onboard" },
    3: { label: "JTAG EN", warning: "Reserved strap pin: GPIO3 is used for JTAG enable/strap behavior on this module.", kind: "strap" },
    4: { label: "CAM SIOD", warning: "Reserved camera pin: GPIO4 is used for camera SIOD.", kind: "camera" },
    5: { label: "CAM SIOC", warning: "Reserved camera pin: GPIO5 is used for camera SIOC.", kind: "camera" },
    6: { label: "CAM VSYNC", warning: "Reserved camera pin: GPIO6 is used for camera VSYNC.", kind: "camera" },
    7: { label: "CAM HREF", warning: "Reserved camera pin: GPIO7 is used for camera HREF.", kind: "camera" },
    8: { label: "CAM Y4", warning: "Reserved camera pin: GPIO8 is used for camera Y4.", kind: "camera" },
    9: { label: "CAM Y3", warning: "Reserved camera pin: GPIO9 is used for camera Y3.", kind: "camera" },
    10: { label: "CAM Y5", warning: "Reserved camera pin: GPIO10 is used for camera Y5.", kind: "camera" },
    11: { label: "CAM Y2", warning: "Reserved camera pin: GPIO11 is used for camera Y2.", kind: "camera" },
    12: { label: "CAM Y6", warning: "Reserved camera pin: GPIO12 is used for camera Y6.", kind: "camera" },
    13: { label: "CAM PCLK", warning: "Reserved camera pin: GPIO13 is used for camera PCLK.", kind: "camera" },
    15: { label: "CAM XCLK", warning: "Reserved camera pin: GPIO15 is used for camera XCLK.", kind: "camera" },
    16: { label: "CAM Y9", warning: "Reserved camera pin: GPIO16 is used for camera Y9.", kind: "camera" },
    17: { label: "CAM Y8", warning: "Reserved camera pin: GPIO17 is used for camera Y8.", kind: "camera" },
    18: { label: "CAM Y7", warning: "Reserved camera pin: GPIO18 is used for camera Y7.", kind: "camera" },
    19: { label: "USB D+", warning: "Board-tied USB pin: GPIO19 is used for USB D+ and auxiliary serial functions.", kind: "usb" },
    20: { label: "USB D-", warning: "Board-tied USB pin: GPIO20 is used for USB D- and auxiliary serial functions.", kind: "usb" },
    35: { label: "PSRAM", warning: "Reserved PSRAM pin: GPIO35 is used for onboard PSRAM.", kind: "psram" },
    36: { label: "PSRAM", warning: "Reserved PSRAM pin: GPIO36 is used for onboard PSRAM.", kind: "psram" },
    37: { label: "PSRAM", warning: "Reserved PSRAM pin: GPIO37 is used for onboard PSRAM.", kind: "psram" },
    38: { label: "SD CMD", warning: "Reserved SD pin: GPIO38 is used for the built-in SD CMD line.", kind: "sd" },
    39: { label: "SD CLK", warning: "Reserved SD pin: GPIO39 is used for the built-in SD clock line.", kind: "sd" },
    40: { label: "SD DATA", warning: "Reserved SD pin: GPIO40 is used for built-in SD data.", kind: "sd" },
    41: { label: "MTDI", warning: "Board-tied JTAG pin: GPIO41 is routed to MTDI.", kind: "jtag" },
    42: { label: "MTMS", warning: "Board-tied JTAG pin: GPIO42 is routed to MTMS.", kind: "jtag" },
    43: { label: "U0TXD / LED TX", warning: "Board-tied serial pin: GPIO43 is used for U0TXD and the onboard TX LED.", kind: "serial" },
    44: { label: "U0RXD / LED RX", warning: "Board-tied serial pin: GPIO44 is used for U0RXD and the onboard RX LED.", kind: "serial" },
    45: { label: "VSPI", warning: "Reserved strap pin: GPIO45 is used for VSPI/boot strap behavior.", kind: "strap" },
    46: { label: "LOG", warning: "Reserved strap pin: GPIO46 is used for LOG/strap behavior.", kind: "strap" },
    48: { label: "WS2812", warning: "Reserved onboard LED pin: GPIO48 drives the built-in WS2812 status LED.", kind: "onboard" },
  },
  "esp32-spk-n16r8": {
    0: { label: "BOOT", warning: "Reserved strap pin: GPIO0 is tied to BOOT mode behavior on this speaker board.", kind: "strap" },
    1: { label: "U0TXD", warning: "Board-tied serial pin: GPIO1 is routed to the primary UART on this board.", kind: "serial" },
    2: { label: "STRAP", warning: "Reserved strap pin: GPIO2 participates in boot strapping on this speaker board.", kind: "strap" },
    3: { label: "U0RXD", warning: "Board-tied serial pin: GPIO3 is routed to the primary UART on this board.", kind: "serial" },
    19: { label: "USB D+", warning: "Board-tied USB pin: GPIO19 is used for USB D+ on this ESP32-SPK-N16R8 board.", kind: "usb" },
    20: { label: "USB D-", warning: "Board-tied USB pin: GPIO20 is used for USB D- on this ESP32-SPK-N16R8 board.", kind: "usb" },
    38: { label: "CAM/SD", warning: "Reserved board pin: GPIO38 is committed to onboard camera or storage routing on this board.", kind: "camera" },
    39: { label: "CAM/SD", warning: "Reserved board pin: GPIO39 is committed to onboard camera or storage routing on this board.", kind: "camera" },
    40: { label: "CAM/SD", warning: "Reserved board pin: GPIO40 is committed to onboard camera or storage routing on this board.", kind: "camera" },
    43: { label: "U0TXD / LED TX", warning: "Board-tied serial pin: GPIO43 is routed to U0TXD and board serial activity.", kind: "serial" },
    44: { label: "U0RXD / LED RX", warning: "Board-tied serial pin: GPIO44 is routed to U0RXD and board serial activity.", kind: "serial" },
    46: { label: "LOG", warning: "Reserved strap pin: GPIO46 is tied to strap/log behavior on this board.", kind: "strap" },
  },
};
const GPIO_ROLE_OPTIONS = [
  "Unused",
  "Battery ADC",
  "Charge Sense",
  "Status LED",
  "Button 1",
  "Button 2",
  "I2S DIN",
  "I2S WS",
  "I2S BCLK",
  "OLED SDA",
  "OLED SCL",
  "OLED RESET",
  "Wape Trigger",
  "SD CS",
  "SD SCK",
  "SD MOSI",
  "SD MISO",
  "Builtin RGB",
  "Buzzer Reserved",
];
const WS_STATUS_LED_BOARD_PROFILES = new Set(["esp32-s3-super-mini", "esp32-s3-zero"]);

const elements = {
  deviceTitle: document.getElementById("deviceTitle"),
  migrationTabButton: document.getElementById("migrationTabButton"),
  migrationTab: document.getElementById("tab-migration"),
  migrationDeviceSelect: document.getElementById("migrationDeviceSelect"),
  migrationScanButton: document.getElementById("migrationScanButton"),
  migrationIpAddress: document.getElementById("migrationIpAddress"),
  migrationUsername: document.getElementById("migrationUsername"),
  migrationPassword: document.getElementById("migrationPassword"),
  migrationYamlRow: document.getElementById("migrationYamlRow"),
  migrationYamlFile: document.getElementById("migrationYamlFile"),
  migrationYamlLabel: document.getElementById("migrationYamlLabel"),
  migrationInspectButton: document.getElementById("migrationInspectButton"),
  migrationDownloadButton: document.getElementById("migrationDownloadButton"),
  migrationStatus: document.getElementById("migrationStatus"),
  migrationPreview: document.getElementById("migrationPreviewContent"),
  migrationPreviewPanel: document.getElementById("migrationPreview"),
  migrationBoardSelect: document.getElementById("migrationBoardSelect"),
  migrationBoardReason: document.getElementById("migrationBoardReason"),
  migrationApplyButton: document.getElementById("migrationApplyButton"),
  heroFirmwareVersion: document.getElementById("heroFirmwareVersion"),
  heroFirmwareChannel: document.getElementById("heroFirmwareChannel"),
  heroFirmwareAuthorLink: document.getElementById("heroFirmwareAuthorLink"),
  gpioBoardRecommendations: document.getElementById("gpioBoardRecommendations"),
  deviceNameValue: document.getElementById("deviceNameValue"),
  connectionState: document.getElementById("connectionState"),
  ipAddress: document.getElementById("ipAddress"),
  apInfo: document.getElementById("apInfo"),
  wifiRssi: document.getElementById("wifiRssi"),
  mqttStatus: document.getElementById("mqttStatus"),
  firmwareVersion: document.getElementById("firmwareVersion"),
  firmwareVersionCard: document.getElementById("firmwareVersionCard"),
  batteryVoltage: document.getElementById("batteryVoltage"),
  batteryRaw: document.getElementById("batteryRaw"),
  batteryAdcPin: document.getElementById("batteryAdcPin"),
  batteryMeasuredVoltage: document.getElementById("batteryMeasuredVoltage"),
  batteryDerivedMultiplier: document.getElementById("batteryDerivedMultiplier"),
  batteryPinSummary: document.getElementById("batteryPinSummary"),
  chargingSenseSummary: document.getElementById("chargingSenseSummary"),
  batteryNote: document.getElementById("batteryNote"),
  audioWsPin: document.getElementById("audioWsPin"),
  audioBclkPin: document.getElementById("audioBclkPin"),
  audioDoutPin: document.getElementById("audioDoutPin"),
  audioEqualizerPreset: document.getElementById("audioEqualizerPreset"),
  equalizerLowSlider: document.getElementById("equalizerLowSlider"),
  equalizerLowValue: document.getElementById("equalizerLowValue"),
  equalizerPresenceSlider: document.getElementById("equalizerPresenceSlider"),
  equalizerPresenceValue: document.getElementById("equalizerPresenceValue"),
  equalizerHighSlider: document.getElementById("equalizerHighSlider"),
  equalizerHighValue: document.getElementById("equalizerHighValue"),
  effectStartupFile: document.getElementById("effectStartupFile"),
  effectStartupVolumePercent: document.getElementById("effectStartupVolumePercent"),
  effectAlarmFile: document.getElementById("effectAlarmFile"),
  effectAlarmVolumePercent: document.getElementById("effectAlarmVolumePercent"),
  effectNotificationFile: document.getElementById("effectNotificationFile"),
  effectNotificationVolumePercent: document.getElementById("effectNotificationVolumePercent"),
  effectAmbientSoundFile: document.getElementById("effectAmbientSoundFile"),
  effectAmbientVolumePercent: document.getElementById("effectAmbientVolumePercent"),
  effectLowBatteryFile: document.getElementById("effectLowBatteryFile"),
  effectLowBatteryVolumePercent: document.getElementById("effectLowBatteryVolumePercent"),
  effectShutDownFile: document.getElementById("effectShutDownFile"),
  effectShutDownVolumePercent: document.getElementById("effectShutDownVolumePercent"),
  effectUpdateAvailableFile: document.getElementById("effectUpdateAvailableFile"),
  effectUpdateAvailableVolumePercent: document.getElementById("effectUpdateAvailableVolumePercent"),
  effectUpdateSuccessFile: document.getElementById("effectUpdateSuccessFile"),
  effectUpdateSuccessVolumePercent: document.getElementById("effectUpdateSuccessVolumePercent"),
  effectsFileStatus: document.getElementById("effectsFileStatus"),
  audioWsSummary: document.getElementById("audioWsSummary"),
  audioBclkSummary: document.getElementById("audioBclkSummary"),
  audioDoutSummary: document.getElementById("audioDoutSummary"),
  saveAudioButton: document.getElementById("saveAudioButton"),
  batteryHero: document.getElementById("batteryHero"),
  freeHeap: document.getElementById("freeHeap"),
  deviceHardwareBoard: document.getElementById("deviceHardwareBoard"),
  deviceHardwareCpu: document.getElementById("deviceHardwareCpu"),
  deviceHardwareFlash: document.getElementById("deviceHardwareFlash"),
  deviceHardwareAudio: document.getElementById("deviceHardwareAudio"),
  deviceHardwareDisplay: document.getElementById("deviceHardwareDisplay"),
  deviceHardwareBattery: document.getElementById("deviceHardwareBattery"),
  deviceHardwareSd: document.getElementById("deviceHardwareSd"),
  deviceCpuLoadValue: document.getElementById("deviceCpuLoadValue"),
  deviceCpuLoadBar: document.getElementById("deviceCpuLoadBar"),
  deviceCpuLoadMeta: document.getElementById("deviceCpuLoadMeta"),
  deviceCpuClockValue: document.getElementById("deviceCpuClockValue"),
  deviceCpuClockBar: document.getElementById("deviceCpuClockBar"),
  deviceCpuClockMeta: document.getElementById("deviceCpuClockMeta"),
  deviceChipTempValue: document.getElementById("deviceChipTempValue"),
  deviceChipTempBar: document.getElementById("deviceChipTempBar"),
  deviceChipTempMeta: document.getElementById("deviceChipTempMeta"),
  deviceSramValue: document.getElementById("deviceSramValue"),
  deviceSramBar: document.getElementById("deviceSramBar"),
  deviceSramMeta: document.getElementById("deviceSramMeta"),
  devicePsramValue: document.getElementById("devicePsramValue"),
  devicePsramBar: document.getElementById("devicePsramBar"),
  devicePsramMeta: document.getElementById("devicePsramMeta"),
  deviceSpiffsValue: document.getElementById("deviceSpiffsValue"),
  deviceSpiffsBar: document.getElementById("deviceSpiffsBar"),
  deviceSpiffsMeta: document.getElementById("deviceSpiffsMeta"),
  deviceSpiffsCard: document.getElementById("deviceSpiffsCard"),
  deviceFlashHeadroomValue: document.getElementById("deviceFlashHeadroomValue"),
  deviceFlashHeadroomBar: document.getElementById("deviceFlashHeadroomBar"),
  deviceFlashHeadroomMeta: document.getElementById("deviceFlashHeadroomMeta"),
  deviceSdValue: document.getElementById("deviceSdValue"),
  deviceSdBar: document.getElementById("deviceSdBar"),
  deviceSdMeta: document.getElementById("deviceSdMeta"),
  deviceSdCard: document.getElementById("deviceSdCard"),
  gpioLeftPins: document.getElementById("gpioLeftPins"),
  gpioRightPins: document.getElementById("gpioRightPins"),
  gpioExtraSection: document.getElementById("gpioExtraSection"),
  gpioExtraToggle: document.getElementById("gpioExtraToggle"),
  gpioExtraPanel: document.getElementById("gpioExtraPanel"),
  gpioExtraLeftPins: document.getElementById("gpioExtraLeftPins"),
  gpioExtraRightPins: document.getElementById("gpioExtraRightPins"),
  gpioBoardAutodetect: document.getElementById("gpioBoardAutodetect"),
  peripheralSensorsList: document.getElementById("peripheralSensorsList"),
  peripheralInputsList: document.getElementById("peripheralInputsList"),
  peripheralPowerList: document.getElementById("peripheralPowerList"),
  peripheralControlsList: document.getElementById("peripheralControlsList"),
  peripheralExpansionsList: document.getElementById("peripheralExpansionsList"),
  peripheralStorageList: document.getElementById("peripheralStorageList"),
  peripheralCommunicationList: document.getElementById("peripheralCommunicationList"),
  peripheralAudioOutputsList: document.getElementById("peripheralAudioOutputsList"),
  peripheralAudioAddButton: document.getElementById("peripheralAudioAddButton"),
  peripheralAudioPrimaryIndexLabel: document.getElementById("peripheralAudioPrimaryIndexLabel"),
  peripheralAudioPins: document.getElementById("peripheralAudioPins"),
  peripheralAudioInList: document.getElementById("peripheralAudioInList"),
  peripheralAudioInAddButton: document.getElementById("peripheralAudioInAddButton"),
  peripheralAudioInPrimaryIndexLabel: document.getElementById("peripheralAudioInPrimaryIndexLabel"),
  peripheralDisplayPins: document.getElementById("peripheralDisplayPins"),
  peripheralDisplayList: document.getElementById("peripheralDisplayList"),
  peripheralDisplayAddButton: document.getElementById("peripheralDisplayAddButton"),
  peripheralDisplayPrimaryIndexLabel: document.getElementById("peripheralDisplayPrimaryIndexLabel"),
  settingsSource: document.getElementById("settingsSource"),
  playbackState: document.getElementById("playbackState"),
  currentTitle: document.getElementById("currentTitle"),
  currentUrl: document.getElementById("currentUrl"),
  wifiPill: document.getElementById("wifiPill"),
  mqttPill: document.getElementById("mqttPill"),
  motorHeroStat: document.getElementById("motorHeroStat"),
  motorHero: document.getElementById("motorHero"),
  motorHeroState: document.getElementById("motorHeroState"),
  motorHeroMeta: document.getElementById("motorHeroMeta"),
  motorHeroControls: document.getElementById("motorHeroControls"),
  audioPill: document.getElementById("audioPill"),
  playbackHeroMeter: document.getElementById("playbackHeroMeter"),
  playbackPrevButton: document.getElementById("playbackPrevButton"),
  playbackHeroToggleButton: document.getElementById("playbackHeroToggleButton"),
  playbackNextButton: document.getElementById("playbackNextButton"),
  volumeSlider: document.getElementById("volumeSlider"),
  volumeValue: document.getElementById("volumeValue"),
  statusLedPin: document.getElementById("statusLedPin"),
  audioMutedToggle: document.getElementById("audioMutedToggle"),
  lowBatterySleepToggle: document.getElementById("lowBatterySleepToggle"),
  powerCycleFactoryResetToggle: document.getElementById("powerCycleFactoryResetToggle"),
  touchHoldFactoryResetToggle: document.getElementById("touchHoldFactoryResetToggle"),
  lowBatterySleepThreshold: document.getElementById("lowBatterySleepThreshold"),
  lowBatterySleepThresholdValue: document.getElementById("lowBatterySleepThresholdValue"),
  lowBatteryWakeIntervalMinutes: document.getElementById("lowBatteryWakeIntervalMinutes"),
  audioMutedNote: document.getElementById("audioMutedNote"),
  otaStatus: document.getElementById("otaStatus"),
  otaStatusLabel: document.getElementById("otaStatusLabel"),
  latestVersion: document.getElementById("latestVersion"),
  otaProgressFill: document.getElementById("otaProgressFill"),
  otaProgressLabel: document.getElementById("otaProgressLabel"),
  applyOtaButton: document.getElementById("applyOtaButton"),
  firmwareList: document.getElementById("firmwareList"),
  firmwareSelectionLabel: document.getElementById("firmwareSelectionLabel"),
  firmwareRollbackAlert: document.getElementById("firmwareRollbackAlert"),
  firmwareRollbackTitle: document.getElementById("firmwareRollbackTitle"),
  firmwareRollbackSummary: document.getElementById("firmwareRollbackSummary"),
  firmwareRollbackReason: document.getElementById("firmwareRollbackReason"),
  effectsReindexButton: document.getElementById("effectsReindexButton"),
  headerActionsButton: document.getElementById("headerActionsButton"),
  headerActionsMenu: document.getElementById("headerActionsMenu"),
  headerRefreshButton: document.getElementById("headerRefreshButton"),
  headerRebootButton: document.getElementById("headerRebootButton"),
  headerShutdownButton: document.getElementById("headerShutdownButton"),
  rebootOverlay: document.getElementById("rebootOverlay"),
  rebootOverlayProgress: document.getElementById("rebootOverlayProgress"),
  rebootOverlayCountdown: document.getElementById("rebootOverlayCountdown"),
  rebootOverlayTitle: document.getElementById("rebootOverlayTitle"),
  rebootOverlayStatus: document.getElementById("rebootOverlayStatus"),
  wifiHandoffOverlay: document.getElementById("wifiHandoffOverlay"),
  wifiHandoffLink: document.getElementById("wifiHandoffLink"),
  wifiHandoffStatus: document.getElementById("wifiHandoffStatus"),
  wifiHandoffOpen: document.getElementById("wifiHandoffOpen"),
  updateAvailableDialog: document.getElementById("updateAvailableDialog"),
  updateAvailableBody: document.getElementById("updateAvailableBody"),
  updateAvailableCloseButton: document.getElementById("updateAvailableCloseButton"),
  uploadFirmwareButton: document.getElementById("uploadFirmwareButton"),
  localFirmwareFile: document.getElementById("localFirmwareFile"),
  localFirmwareLabel: document.getElementById("localFirmwareLabel"),
  usbFlashAnotherDeviceButton: document.getElementById("usbFlashAnotherDeviceButton"),
  usbFlasherDialog: document.getElementById("usbFlasherDialog"),
  usbFlasherCloseButton: document.getElementById("usbFlasherCloseButton"),
  usbFlasherCompatibility: document.getElementById("usbFlasherCompatibility"),
  usbFlasherOptions: document.getElementById("usbFlasherOptions"),
  usbFlasherFile: document.getElementById("usbFlasherFile"),
  usbFlasherChooseFileButton: document.getElementById("usbFlasherChooseFileButton"),
  usbFlasherFileLabel: document.getElementById("usbFlasherFileLabel"),
  usbFlasherProgressCircle: document.getElementById("usbFlasherProgressCircle"),
  usbFlasherProgressValue: document.getElementById("usbFlasherProgressValue"),
  usbFlasherStatus: document.getElementById("usbFlasherStatus"),
  usbFlasherDetail: document.getElementById("usbFlasherDetail"),
  usbFlasherTargetLink: document.getElementById("usbFlasherTargetLink"),
  usbFlasherLog: document.getElementById("usbFlasherLog"),
  usbFlasherStartButton: document.getElementById("usbFlasherStartButton"),
  usbFlasherCancelButton: document.getElementById("usbFlasherCancelButton"),
  usbFlasherOpenTargetButton: document.getElementById("usbFlasherOpenTargetButton"),
  localBuilderPanel: document.getElementById("localBuilderPanel"),
  runtimeFirmwarePanel: document.getElementById("runtimeFirmwarePanel"),
  localBuilderBadge: document.getElementById("localBuilderBadge"),
  localBuilderTransport: document.getElementById("localBuilderTransport"),
  localBuilderChip: document.getElementById("localBuilderChip"),
  localBuilderFirmwareMode: document.getElementById("localBuilderFirmwareMode"),
  localBuilderPort: document.getElementById("localBuilderPort"),
  localBuilderRefreshPorts: document.getElementById("localBuilderRefreshPorts"),
  localBuilderUsbTarget: document.getElementById("localBuilderUsbTarget"),
  localBuilderIpTarget: document.getElementById("localBuilderIpTarget"),
  localBuilderIpDevice: document.getElementById("localBuilderIpDevice"),
  localBuilderScanIpDevices: document.getElementById("localBuilderScanIpDevices"),
  localBuilderIpAddress: document.getElementById("localBuilderIpAddress"),
  localBuilderIpUsername: document.getElementById("localBuilderIpUsername"),
  localBuilderIpPassword: document.getElementById("localBuilderIpPassword"),
  localBuilderMaximum: document.getElementById("localBuilderMaximum"),
  localBuilderWebUi: document.getElementById("localBuilderWebUi"),
  localBuilderHacs: document.getElementById("localBuilderHacs"),
  localBuilderAudio: document.getElementById("localBuilderAudio"),
  localBuilderErase: document.getElementById("localBuilderErase"),
  localBuilderCompatibility: document.getElementById("localBuilderCompatibility"),
  localBuilderProgressFill: document.getElementById("localBuilderProgressFill"),
  localBuilderProgressLabel: document.getElementById("localBuilderProgressLabel"),
  localBuilderLog: document.getElementById("localBuilderLog"),
  localBuilderCompileFlash: document.getElementById("localBuilderCompileFlash"),
  localBuilderCancel: document.getElementById("localBuilderCancel"),
  message: document.getElementById("message"),
  playForm: document.getElementById("playForm"),
  playbackActionButton: document.getElementById("playbackActionButton"),
  playUrl: document.getElementById("playUrl"),
  playLabel: document.getElementById("playLabel"),
  playType: document.getElementById("playType"),
  radioCountrySelect: document.getElementById("radioCountrySelect"),
  radioStationSelect: document.getElementById("radioStationSelect"),
  radioBrowserStatus: document.getElementById("radioBrowserStatus"),
  motorTabButton: document.getElementById("motorTabButton"),
  motorTab: document.getElementById("tab-motor"),
  motorSummary: document.getElementById("motorSummary"),
  motorChannelAForwardButton: document.getElementById("motorChannelAForwardButton"),
  motorChannelABackwardButton: document.getElementById("motorChannelABackwardButton"),
  motorChannelAForwardDuration: document.getElementById("motorChannelAForwardDuration"),
  motorChannelAForwardRole: document.getElementById("motorChannelAForwardRole"),
  motorChannelAForwardLimit: document.getElementById("motorChannelAForwardLimit"),
  motorChannelABackwardDuration: document.getElementById("motorChannelABackwardDuration"),
  motorChannelABackwardRole: document.getElementById("motorChannelABackwardRole"),
  motorChannelABackwardLimit: document.getElementById("motorChannelABackwardLimit"),
  motorChannelAStatus: document.getElementById("motorChannelAStatus"),
  motorChannelBForwardButton: document.getElementById("motorChannelBForwardButton"),
  motorChannelBBackwardButton: document.getElementById("motorChannelBBackwardButton"),
  motorChannelBForwardDuration: document.getElementById("motorChannelBForwardDuration"),
  motorChannelBForwardRole: document.getElementById("motorChannelBForwardRole"),
  motorChannelBForwardLimit: document.getElementById("motorChannelBForwardLimit"),
  motorChannelBBackwardDuration: document.getElementById("motorChannelBBackwardDuration"),
  motorChannelBBackwardRole: document.getElementById("motorChannelBBackwardRole"),
  motorChannelBBackwardLimit: document.getElementById("motorChannelBBackwardLimit"),
  motorChannelBStatus: document.getElementById("motorChannelBStatus"),
  motorTouchSection: document.getElementById("motorTouchSection"),
  motorTouchList: document.getElementById("motorTouchList"),
  motorTouchSummary: document.getElementById("motorTouchSummary"),
  gpioBoardSelector: document.getElementById("gpioBoardSelector"),
  gpioBoardImage: document.getElementById("gpioBoardImage"),
  peripheralDiagramStage: document.getElementById("peripheralDiagramStage"),
  peripheralDiagramBoardShell: document.getElementById("peripheralDiagramBoardShell"),
  peripheralDiagramBoardEdit: document.getElementById("peripheralDiagramBoardEdit"),
  peripheralDiagramBoardImage: document.getElementById("peripheralDiagramBoardImage"),
  peripheralDiagramItems: document.getElementById("peripheralDiagramItems"),
  peripheralDiagramUndoButton: document.getElementById("peripheralDiagramUndoButton"),
  peripheralDiagramRedoButton: document.getElementById("peripheralDiagramRedoButton"),
  peripheralDiagramResetWiringButton: document.getElementById("peripheralDiagramResetWiringButton"),
  peripheralDiagramRewireButton: document.getElementById("peripheralDiagramRewireButton"),
  peripheralDiagramSaveButton: document.getElementById("peripheralDiagramSaveButton"),
  peripheralDiagramLoadButton: document.getElementById("peripheralDiagramLoadButton"),
  peripheralDiagramLoadFile: document.getElementById("peripheralDiagramLoadFile"),
  peripheralDiagramPlaceholderText: document.querySelector(".peripheral-diagram-placeholder-text"),
  peripheralDiagramLabelEditorModal: document.getElementById("peripheralDiagramLabelEditorModal"),
  peripheralDiagramLabelEditorTitle: document.getElementById("peripheralDiagramLabelEditorTitle"),
  peripheralDiagramLabelEditorSubtitle: document.getElementById("peripheralDiagramLabelEditorSubtitle"),
  peripheralDiagramLabelEditorClose: document.getElementById("peripheralDiagramLabelEditorClose"),
  peripheralDiagramLabelEditorAdd: document.getElementById("peripheralDiagramLabelEditorAdd"),
  peripheralDiagramLabelEditorStage: document.getElementById("peripheralDiagramLabelEditorStage"),
  peripheralDiagramLabelEditorVisual: document.getElementById("peripheralDiagramLabelEditorVisual"),
  peripheralDiagramLabelEditorLabels: document.getElementById("peripheralDiagramLabelEditorLabels"),
  peripheralDiagramLabelEditorHint: document.getElementById("peripheralDiagramLabelEditorHint"),
  peripheralDiagramLabelEditorInspector: document.getElementById("peripheralDiagramLabelEditorInspector"),
  peripheralDiagramLabelEditorPreset: document.getElementById("peripheralDiagramLabelEditorPreset"),
  peripheralDiagramLabelEditorName: document.getElementById("peripheralDiagramLabelEditorName"),
  peripheralDiagramLabelEditorRemove: document.getElementById("peripheralDiagramLabelEditorRemove"),
  peripheralAudioProfile: document.getElementById("peripheralAudioProfile"),
  peripheralAudioInProfile: document.getElementById("peripheralAudioInProfile"),
  peripheralAudioInPins: document.getElementById("peripheralAudioInPins"),
  peripheralDisplayProfile: document.getElementById("peripheralDisplayProfile"),
  settingsForm: document.getElementById("settingsForm"),
  recentPlaybackList: document.getElementById("recentPlaybackList"),
  useStaticIpToggle: document.getElementById("useStaticIpToggle"),
  scanWifiButton: document.getElementById("scanWifiButton"),
  wifiConnectButton: document.getElementById("wifiConnectButton"),
  scanStatus: document.getElementById("scanStatus"),
  wifiNetworkList: document.getElementById("wifiNetworkList"),
  mqttConnectButton: document.getElementById("mqttConnectButton"),
  mqttRediscoveryButton: document.getElementById("mqttRediscoveryButton"),
  backupConfigButton: document.getElementById("backupConfigButton"),
  restoreConfigButton: document.getElementById("restoreConfigButton"),
  restoreConfigFile: document.getElementById("restoreConfigFile"),
  saveDeviceButton: document.getElementById("saveDeviceButton"),
  rebootButton: document.getElementById("rebootButton"),
  factoryResetButton: document.getElementById("factoryResetButton"),
  mqttConnectStatus: document.getElementById("mqttConnectStatus"),
  displayType: document.getElementById("displayType"),
  oledSdaPin: document.getElementById("oledSdaPin"),
  oledSclPin: document.getElementById("oledSclPin"),
  oledResetPin: document.getElementById("oledResetPin"),
  oledModeSection: document.getElementById("oledModeSection"),
  wapeModeSection: document.getElementById("wapeModeSection"),
  displayTriggerButton: document.getElementById("displayTriggerButton"),
  wapeTriggerPin: document.getElementById("wapeTriggerPin"),
  wapeTriggerEvent: document.getElementById("wapeTriggerEvent"),
  oledPreviewCard: document.getElementById("oledPreviewCard"),
  oledPreview: document.getElementById("oledPreview"),
  oledPreviewMeta: document.getElementById("oledPreviewMeta"),
  oledPreviewProgress: document.getElementById("oledPreviewProgress"),
  oledPreviewProgressLabel: document.getElementById("oledPreviewProgressLabel"),
  oledPreviewProgressFill: document.getElementById("oledPreviewProgressFill"),
  oledPreviewDisabled: document.getElementById("oledPreviewDisabled"),
  storageModal: document.getElementById("storageModal"),
  storageCloseButton: document.getElementById("storageCloseButton"),
  storageTitle: document.getElementById("storageTitle"),
  storageFlashButton: document.getElementById("storageFlashButton"),
  storageSdButton: document.getElementById("storageSdButton"),
  storageSdConfig: document.getElementById("storageSdConfig"),
  storageSummary: document.getElementById("storageSummary"),
  storageStatus: document.getElementById("storageStatus"),
  storageLimit: document.getElementById("storageLimit"),
  storageUpButton: document.getElementById("storageUpButton"),
  storageBreadcrumbs: document.getElementById("storageBreadcrumbs"),
  storageNewFolderButton: document.getElementById("storageNewFolderButton"),
  storageRemountButton: document.getElementById("storageRemountButton"),
  storageReindexButton: document.getElementById("storageReindexButton"),
  storageSelectModeButton: document.getElementById("storageSelectModeButton"),
  storageSelectAllButton: document.getElementById("storageSelectAllButton"),
  storageDeleteButton: document.getElementById("storageDeleteButton"),
  storagePlayButton: document.getElementById("storagePlayButton"),
  storageUploadButton: document.getElementById("storageUploadButton"),
  storageFileInput: document.getElementById("storageFileInput"),
  storageProgressWrap: document.getElementById("storageProgressWrap"),
  storageProgressFill: document.getElementById("storageProgressFill"),
  storageProgressLabel: document.getElementById("storageProgressLabel"),
  storageInlinePrevButton: document.getElementById("storageInlinePrevButton"),
  storageInlinePlayButton: document.getElementById("storageInlinePlayButton"),
  storageInlineNextButton: document.getElementById("storageInlineNextButton"),
  storageInlineShuffleButton: document.getElementById("storageInlineShuffleButton"),
  storageInlineRepeatButton: document.getElementById("storageInlineRepeatButton"),
  storageInlineAutoplayButton: document.getElementById("storageInlineAutoplayButton"),
  storageInlineVolumeSlider: document.getElementById("storageInlineVolumeSlider"),
  storageInlineVolumeMeter: document.getElementById("storageInlineVolumeMeter"),
  storageInlineSeekSlider: document.getElementById("storageInlineSeekSlider"),
  storageInlineTrackMeter: document.getElementById("storageInlineTrackMeter"),
  storageFileList: document.getElementById("storageFileList"),
  storagePreviewModal: document.getElementById("storagePreviewModal"),
  storagePreviewCloseButton: document.getElementById("storagePreviewCloseButton"),
  storagePreviewTitle: document.getElementById("storagePreviewTitle"),
  storagePreviewMeta: document.getElementById("storagePreviewMeta"),
  storagePreviewPath: document.getElementById("storagePreviewPath"),
  storagePreviewAlbum: document.getElementById("storagePreviewAlbum"),
  storagePreviewArtwork: document.getElementById("storagePreviewArtwork"),
  storagePreviewArtworkFallback: document.getElementById("storagePreviewArtworkFallback"),
  storagePreviewArtworkStatus: document.getElementById("storagePreviewArtworkStatus"),
  storagePreviewSeekSlider: document.getElementById("storagePreviewSeekSlider"),
  storagePreviewProgressLabel: document.getElementById("storagePreviewProgressLabel"),
  storagePreviewVolumeSlider: document.getElementById("storagePreviewVolumeSlider"),
  storagePreviewVolumeValue: document.getElementById("storagePreviewVolumeValue"),
  storagePreviewPrevButton: document.getElementById("storagePreviewPrevButton"),
  storagePreviewPlayButton: document.getElementById("storagePreviewPlayButton"),
  storagePreviewNextButton: document.getElementById("storagePreviewNextButton"),
  storagePreviewLoopButton: document.getElementById("storagePreviewLoopButton"),
  storagePreviewShuffleButton: document.getElementById("storagePreviewShuffleButton"),
  sdEnabled: document.getElementById("sdEnabled"),
  sdCsPin: document.getElementById("sdCsPin"),
  sdSckPin: document.getElementById("sdSckPin"),
  sdMosiPin: document.getElementById("sdMosiPin"),
  sdMisoPin: document.getElementById("sdMisoPin"),
};

deviceTab = createDeviceTab({
  elements,
  activateTabByName,
  flashStorageAvailable,
  saveSettings,
  exportConfigurationBackup,
  restoreConfigurationBackup,
  requestDeviceRestart,
  handleError,
  confirmFactoryReset: () => window.confirm("Factory reset will erase all saved settings and credentials, GPIO and peripheral assignments, diagram positions and rotations, and other persisted device preferences. Continue?"),
});

firmwareTab = createFirmwareTab({
  state,
  elements,
  request,
  loadStatus,
  setMessage,
  beginFirmwareReconnectReload,
  setCurrentFirmwareVersion,
});

const localBuilder = createLocalBuilder({
  elements,
  currentSettingsSnapshot,
  validatePins: validateBoardPinAssignments,
  setMessage,
  toast,
});

usbFlasher = createUsbFlasher({
  elements,
  request,
  setMessage,
});

infoTab = createInfoTab({
  elements,
  formatBytes,
  normalizePlaybackTitle,
});

audioTab = createAudioTab({
  state,
  elements,
  hasDistinctI2sPins,
  waitForSettingsIdle,
  saveSettings,
  handleError,
  queueSettingsSave,
  populateSdPinOptions,
  populateBatteryAdcPinOptions,
  populateOledPinOptions,
  populateWapeTriggerPinOptions,
  updateBatteryUi,
});

batteryTab = createBatteryTab({
  state,
  elements,
  batteryDividerSensorProfile: BATTERY_DIVIDER_SENSOR_PROFILE,
  parseDecimalFieldValue,
  normalizeDecimalField,
  saveSettings,
  handleError,
  queueSettingsSave,
  populateSdPinOptions,
  populateOledPinOptions,
  populateWapeTriggerPinOptions,
  ensureBatteryDividerSensorSelection,
  renderPeripheralSensorControls,
  syncGpioMappingControls,
  savePeripheralProfileSelections,
});

displayTab = createDisplayTab({
  state,
  elements,
  maxPeripheralDisplays: MAX_PERIPHERAL_DISPLAYS,
  peripheralDisplayProfileOptions: PERIPHERAL_DISPLAY_PROFILE_OPTIONS,
  oledPreviewScrollIntervalMs: OLED_PREVIEW_SCROLL_INTERVAL_MS,
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
});

configurationPeripheralsTab = createConfigurationPeripheralsTab({
  state,
  elements,
  gpioTabElement: document.getElementById("tab-gpio"),
  batteryDividerSensorProfile: BATTERY_DIVIDER_SENSOR_PROFILE,
  maxPeripheralAudioOutputs: MAX_PERIPHERAL_AUDIO_OUTPUTS,
  maxPeripheralAudioInputs: MAX_PERIPHERAL_AUDIO_INPUTS,
  maxPeripheralSensors: MAX_PERIPHERAL_SENSORS,
  maxPeripheralInputs: MAX_PERIPHERAL_INPUTS,
  maxPeripheralStorages: MAX_PERIPHERAL_STORAGES,
  maxPeripheralControls: MAX_PERIPHERAL_CONTROLS,
  maxPeripheralExpansions: MAX_PERIPHERAL_EXPANSIONS,
  maxPeripheralCommunications: MAX_PERIPHERAL_COMMUNICATIONS,
  maxPeripheralPowers: MAX_PERIPHERAL_POWERS,
  peripheralAudioProfileOptions: PERIPHERAL_AUDIO_PROFILE_OPTIONS,
  peripheralAudioInProfileOptions: PERIPHERAL_AUDIO_IN_PROFILE_OPTIONS,
  peripheralSensorProfileOptions: PERIPHERAL_SENSOR_PROFILE_OPTIONS,
  peripheralInputProfileOptions: PERIPHERAL_INPUT_PROFILE_OPTIONS,
  peripheralStorageProfileOptions: PERIPHERAL_STORAGE_PROFILE_OPTIONS,
  peripheralControlProfileOptions: PERIPHERAL_CONTROL_PROFILE_OPTIONS,
  peripheralExpansionProfileOptions: PERIPHERAL_EXPANSION_PROFILE_OPTIONS,
  peripheralCommunicationProfileOptions: PERIPHERAL_COMMUNICATION_PROFILE_OPTIONS,
  peripheralPowerProfileOptions: PERIPHERAL_POWER_PROFILE_OPTIONS,
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
  onPeripheralConfigurationChange: () => {
    updateConfiguredFeatureVisibility();
  },
  queueSettingsSave,
  gpioConfigRoleState,
  setPeripheralHelperBindingValue,
  removePeripheralHelperBindingsForIndex,
  isTopPeripheralSelect,
});

configurationGpioTab = createConfigurationGpioTab({
  state,
  elements,
  gpioBoardLayouts: GPIO_BOARD_LAYOUTS,
  gpioBoardExtraLayouts: GPIO_BOARD_EXTRA_LAYOUTS,
  gpioBoardReservedPins: GPIO_BOARD_RESERVED_PINS,
  gpioBoardAssets: GPIO_BOARD_ASSETS,
  gpioBoardPresentation: GPIO_BOARD_PRESENTATION,
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
  isPcDesignerRuntime: PC_DESIGNER_RUNTIME,
  renderAssignmentWarnings: renderBoardPinWarnings,
});

peripheralDiagramWiringModule = createPeripheralDiagramWiringModule({
  state,
  elements,
  gpioBoardLayouts: GPIO_BOARD_LAYOUTS,
  gpioBoardExtraLayouts: GPIO_BOARD_EXTRA_LAYOUTS,
  activeGpioBoardProfile,
  realPeripheralBindingDefinitions,
  helperSignalLabels,
  peripheralHelperBindingValue,
  setPeripheralHelperBindingValue,
  savePeripheralDiagramPositions,
  syncGpioMappingControls,
  queueSettingsSave,
});

peripheralDiagramLabelEditorModule = createPeripheralDiagramLabelEditorModule({
  state,
  elements,
  renderPeripheralDiagram,
  savePeripheralDiagramPositions,
  buildEditablePeripheralLabels,
});

uiHistoryModule = createUiHistoryModule({
  state,
  elements,
  currentSettingsSnapshot,
  fillForm,
  activeTabName,
  activateTabByName,
  queueSettingsSave,
  renderPeripheralDiagram,
  syncGpioMappingControls,
});

configurationBackupModule = createConfigurationBackupModule({
  state,
  elements,
  gpioBoardLayouts: GPIO_BOARD_LAYOUTS,
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
  maxPeripheralAudioOutputs: MAX_PERIPHERAL_AUDIO_OUTPUTS,
  maxPeripheralAudioInputs: MAX_PERIPHERAL_AUDIO_INPUTS,
  maxPeripheralDisplays: MAX_PERIPHERAL_DISPLAYS,
  maxPeripheralSensors: MAX_PERIPHERAL_SENSORS,
  maxPeripheralInputs: MAX_PERIPHERAL_INPUTS,
  maxPeripheralControls: MAX_PERIPHERAL_CONTROLS,
  maxPeripheralExpansions: MAX_PERIPHERAL_EXPANSIONS,
  maxPeripheralStorages: MAX_PERIPHERAL_STORAGES,
  maxPeripheralCommunications: MAX_PERIPHERAL_COMMUNICATIONS,
  maxPeripheralPowers: MAX_PERIPHERAL_POWERS,
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
});

configurationSettingsPersistenceModule = createConfigurationSettingsPersistenceModule({
  state,
  elements,
  defaultEsp32S3AudioPins: DEFAULT_ESP32S3_AUDIO_PINS,
  defaultSdGpioPins: DEFAULT_SD_GPIO_PINS,
  effectSelectConfig: EFFECT_SELECT_CONFIG,
  settingsAutosaveDelayMs: SETTINGS_AUTOSAVE_DELAY_MS,
  request,
  delay,
  handleError,
  setMessage,
  toast,
  normalizeDecimalField,
  parseDecimalFieldValue,
  currentBatteryCalibrationMultiplier,
  choosePreferredOledPins,
  effectVolumePercentValue,
  effectVolumeSetting,
  populateAudioI2sPinOptions,
  renderEqualizerPreset,
  populateSdPinOptions,
  populateStatusLedPinOptions,
  populateOledPinOptions,
  populateWapeTriggerPinOptions,
  populateButtonActionSelects,
  populateBatteryAdcPinOptions,
  syncPeripheralProfilesFromSettings,
  applyPeripheralProfileSelectionsState,
  syncPageSections,
  normalizeUiSettings,
  cloneSettingsObject,
  peripheralDiagramPositionsStorageKey: PERIPHERAL_DIAGRAM_POSITIONS_STORAGE_KEY,
  validateSettingsPayload,
  applyPeripheralProfileSelections,
  currentSettingsSnapshot,
  loadStatus,
  renderEffectFileOptions,
  clearEffectFileOptionsCache,
  sdSettingsChanged,
  activeTabName,
  refreshExternalStorageTab,
  rerenderStorageManager,
  refreshStorageManager,
  maybeRefreshVisibleStorageTab,
  activateTabByName,
  setFirmwareAuthorLink,
  updateGpioBoardImage,
  isGpioUiInteracting,
  renderGpioOverview,
  renderPeripheralAudioOutputControls,
  renderPeripheralAudioInControls,
  renderPeripheralDisplayControls,
  renderPeripheralSensorControls,
  renderPeripheralInputControls,
  renderPeripheralControlControls,
  renderPeripheralExpansionControls,
  renderPeripheralDiagram,
  renderPeripheralCommunicationControls,
  renderPeripheralPowerControls,
  savePeripheralProfileSelections,
  savePeripheralHelperBindings,
  resetWifiNetworkList,
  restoreGpioBoardPreferences,
  updateGpioBoardSelectorMode,
  applyBackupUiState,
});

configurationSettingsSnapshotModule = createConfigurationSettingsSnapshotModule({
  state,
  elements,
  isPlainObject,
  cloneSettingsObject,
  normalizeUiSettings,
  collectForm,
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
});

deviceMigrationTab = createDeviceMigrationTab({
  elements,
  request,
  currentSettingsSnapshot,
  mergeSettingsObjects,
  applyBackupUiState,
  applySettingsPayload,
  activateTabByName,
  setMessage,
  toast,
  handleError,
  isPcDesignerRuntime: PC_DESIGNER_RUNTIME,
});

hardwareTab = createHardwareTab({
  state,
  elements,
  formatBytes,
  pinSummary,
  updateResourceCard,
});

motorTab = createMotorTab({
  state,
  elements,
  normalizedPeripheralControlProfiles,
  normalizedPeripheralInputProfiles,
  peripheralHelperBindingValue,
  inputAssignedPin,
  activeTabName,
  activateTabByName,
  request,
  saveSettings,
  queueSettingsSave,
  awaitPendingSettingsSave,
  loadStatus,
  setMessage,
  toast,
});

effectsTab = createEffectsTab({
  state,
  elements,
  effectSelectConfig: EFFECT_SELECT_CONFIG,
  effectFileSources: EFFECT_FILE_SOURCES,
  effectFilePageSize: EFFECT_FILE_PAGE_SIZE,
  effectFilesCacheStorageKey: EFFECT_FILES_CACHE_STORAGE_KEY,
  request,
  delay,
  toast,
  setMessage,
  handleError,
  saveSettings,
  previewEffectFile,
  setEffectVolume,
  effectVolumeSetting,
  effectVolumePercentValue,
  shouldDeferSdReads,
  stopPlayback,
  pollStatusUntil,
  rebuildStorageIndexFromBrowser,
  formatBrowserReindexStatus,
  storageQueryParams,
  isSupportedAudioFilename,
  storageBaseName,
});

mqttTab = createMqttTab({
  state,
  elements,
  namedField,
  request,
  saveSettings,
  pollStatusUntil,
  loadStatus,
  setMessage,
  handleError,
});

storageTab = createStorageTab({
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
});

wifiTab = createWifiTab({
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
});

radioBrowserModule = createRadioBrowserModule({
  state,
  elements,
  radioSelectionStorageKey: RADIO_SELECTION_STORAGE_KEY,
  defaultRadioSelection: DEFAULT_RADIO_SELECTION,
  updatePlaybackHeroControls: () => playbackStatusModule?.updatePlaybackHeroControls(),
  isPlaybackActive,
  submitPlay,
});

playbackStatusModule = createPlaybackStatusModule({
  state,
  elements,
  normalizePlaybackTitle,
  isPlaybackActive: isForegroundPlaybackActive,
  toast,
  applySelectedRadioStation: (options = {}) => radioBrowserModule?.applySelectedRadioStation(options),
  isFileManagerPlayback: () => isFileManagerPlayback(),
  canStepFileManagerPlayback: () => activeStorageAudioEntries().length > 1 && Boolean(currentStoragePlayingEntry() || state.storagePreviewItem),
});

statusRenderModule = createStatusRenderModule({
  state,
  elements,
  escapeHtml,
  isPlaybackActive,
  currentPlaybackHeroTitle: (status) => playbackStatusModule?.currentPlaybackHeroTitle(status) || "No station selected",
  updatePlaybackHeroControls: () => playbackStatusModule?.updatePlaybackHeroControls(),
  storagePlaybackRef,
  advanceStoragePreviewTrack,
  handleError,
  activeTabName,
  refreshExternalStorageTab,
  loadEffectFileOptions,
  updateGpioBoardSelectorMode,
  updateStorageAvailabilityUi,
  populateStatusLedPinOptions,
  populateSdPinOptions,
  populateBatteryAdcPinOptions,
  populateWapeTriggerPinOptions,
  maybeRedirectToStationIp,
  renderInfoStatus: (status) => infoTab?.renderStatus(status),
  setCurrentFirmwareVersion,
  setFirmwareAuthorLink,
  updateDerivedBatteryCalibration,
  updatePlaybackActionButton,
  updateAudioUiState,
  setPill,
  renderHardwareSummary,
  renderDeviceResources,
  maybeRefreshVisibleStorageTab,
  isGpioUiInteracting,
  isPeripheralUiInteracting,
  renderGpioOverview,
  renderPeripheralDiagram,
  renderMotorTab: () => motorTab?.render(),
  updateStoragePreviewProgressUi,
  showUpdateAvailablePopup,
  startFirmwareProgressPolling,
  stopFirmwareProgressPolling,
  beginFirmwareReconnectReload,
  setMqttConnectStatus,
  namedField,
  setScanStatus,
  updateWifiActionButton,
  updateMqttActionButton,
  updateStoragePreviewPlaybackControls,
  updateStorageFolderPlaybackStatus,
  syncStoragePlaybackFromStatus,
  populateButtonActionSelects,
  renderOledPreview,
  updateStorageVolumeMeter,
  updateStorageMeter,
  formatPlaybackClock,
});

deviceTab.bindEvents();
audioTab.bindEvents();
batteryTab.bindEvents();
configurationGpioTab.bindEvents();
configurationPeripheralsTab.bindEvents();
motorTab.bindEvents();
displayTab.bindEvents();
effectsTab.bindEvents();
mqttTab.bindEvents();
uiHistoryModule.bindEvents();

elements.peripheralDiagramSaveButton?.addEventListener("click", async () => {
  try {
    await exportPeripheralDiagramShare();
  } catch (error) {
    handleError(error);
  }
});

elements.peripheralDiagramRewireButton?.addEventListener("click", () => {
  try {
    renderPeripheralDiagram();
    const result = peripheralDiagramWiringModule?.rewireFromLabels() || { matchedAssignments: 0 };
    syncGpioMappingControls();
    renderPeripheralDiagram();
    if (result.matchedAssignments > 0) {
      queueSettingsSave(0);
      uiHistoryModule?.scheduleCapture();
      setMessage(`Rewired ${result.matchedAssignments} label assignment${result.matchedAssignments === 1 ? "" : "s"}`);
    } else {
      setMessage("No label-to-board matches found for rewiring");
    }
  } catch (error) {
    handleError(error);
  }
});

elements.peripheralDiagramResetWiringButton?.addEventListener("click", () => {
  try {
    if (!window.confirm("Reset all diagram wires back to automatic routing? This also clears every manually edited wire curve.")) {
      return;
    }
    const result = peripheralDiagramWiringModule?.resetManualWireCurves?.() || { cleared: false };
    renderPeripheralDiagram();
    setMessage(result.cleared ? "Reset all diagram wiring to default routing" : "Diagram wiring is already using default routing");
  } catch (error) {
    handleError(error);
  }
});

elements.peripheralDiagramLoadButton?.addEventListener("click", () => {
  if (!elements.peripheralDiagramLoadFile) {
    return;
  }
  elements.peripheralDiagramLoadFile.value = "";
  elements.peripheralDiagramLoadFile.click();
});

elements.peripheralDiagramLoadFile?.addEventListener("change", async () => {
  const file = elements.peripheralDiagramLoadFile.files?.[0];
  if (!file) {
    return;
  }
  try {
    await restorePeripheralDiagramShare(file);
  } catch (error) {
    handleError(error);
  } finally {
    elements.peripheralDiagramLoadFile.value = "";
  }
});
storageTab.bindEvents();
wifiTab.bindEvents();

function namedField(name) {
  return elements.settingsForm.elements.namedItem(name);
}

function normalizedPeripheralAudioProfiles() {
  const currentProfiles = Array.isArray(state.peripheralAudioProfiles) ? state.peripheralAudioProfiles : [];
  const primaryProfile = String(elements.peripheralAudioProfile?.value || currentProfiles[0] || "none").trim() || "none";
  const sanitizedProfiles = [primaryProfile, ...currentProfiles.slice(1)]
    .map((value) => String(value || "none").trim() || "none")
    .slice(0, MAX_PERIPHERAL_AUDIO_OUTPUTS);
  return sanitizedProfiles.length ? sanitizedProfiles : ["none"];
}

function normalizedPeripheralAudioInProfiles() {
  const currentProfiles = Array.isArray(state.peripheralAudioInProfiles) ? state.peripheralAudioInProfiles : [];
  const primaryProfile = String(elements.peripheralAudioInProfile?.value || currentProfiles[0] || "none").trim() || "none";
  const sanitizedProfiles = [primaryProfile, ...currentProfiles.slice(1)]
    .map((value) => String(value || "none").trim() || "none")
    .slice(0, MAX_PERIPHERAL_AUDIO_INPUTS);
  return sanitizedProfiles.length ? sanitizedProfiles : ["none"];
}

function normalizedPeripheralDisplayProfiles() {
  const currentProfiles = Array.isArray(state.peripheralDisplayProfiles) ? state.peripheralDisplayProfiles : [];
  const primaryProfile = String(elements.peripheralDisplayProfile?.value || currentProfiles[0] || "none").trim() || "none";
  const sanitizedProfiles = [primaryProfile, ...currentProfiles.slice(1)]
    .map((value) => String(value || "none").trim() || "none")
    .slice(0, MAX_PERIPHERAL_DISPLAYS);
  return sanitizedProfiles.length ? sanitizedProfiles : ["none"];
}

function hasConfiguredProfile(profiles) {
  return profiles.some((profile) => String(profile || "none").trim().toLowerCase() !== "none");
}

function hasConfiguredAudioOutput() {
  return hasConfiguredProfile(normalizedPeripheralAudioProfiles());
}

function hasConfiguredDisplayPeripheral() {
  return hasConfiguredProfile(normalizedPeripheralDisplayProfiles());
}

function hasConfiguredExternalStoragePeripheral() {
  return hasConfiguredProfile(normalizedPeripheralStorageProfiles());
}

function hasConfiguredBatterySensePeripheral() {
  if (!batteryDividerSensorSelected()) {
    return false;
  }
  return Number(elements.batteryAdcPin?.value || state.settings?.battery?.adcPin || 0) > 0;
}

function setTabVisibility(tabName, visible) {
  const button = document.querySelector(`.tab-button[data-tab="${tabName}"]`);
  const panel = document.getElementById(`tab-${tabName}`);
  if (button) {
    button.hidden = !visible;
    button.disabled = !visible;
    if (!visible) {
      button.setAttribute("aria-selected", "false");
    }
  }
  if (panel) {
    panel.hidden = !visible;
    if (!visible) {
      panel.classList.remove("active");
    }
  }
  if (!visible && activeTabName() === tabName) {
    activateTabByName("gpio");
  }
}

function restoreSavedActiveTabIfVisible() {
  let savedTab = "";
  try {
    savedTab = String(window.localStorage.getItem(ACTIVE_TAB_STORAGE_KEY) || "").trim();
  } catch {
    savedTab = "";
  }
  if (!savedTab || savedTab === activeTabName()) {
    return;
  }
  activateTabByName(savedTab);
}

function updateConfiguredFeatureVisibility() {
  const audioConfigured = hasConfiguredAudioOutput();
  const batteryConfigured = hasConfiguredBatterySensePeripheral();
  const displayConfigured = hasConfiguredDisplayPeripheral();
  const externalStorageConfigured = hasConfiguredExternalStoragePeripheral();
  const motorConfigured = normalizedPeripheralControlProfiles().some((profile) =>
    String(profile || "").trim().toLowerCase().includes("motor-driver")
  );

  setTabVisibility("motor", motorConfigured);
  setTabVisibility("playback", audioConfigured);
  setTabVisibility("effects", audioConfigured);
  setTabVisibility("battery", batteryConfigured);
  setTabVisibility("oled", displayConfigured);
  // The external-storage page is a live device file manager. The PC Designer
  // can configure SD hardware, but it has no mounted device filesystem.
  setTabVisibility("storage-external", externalStorageConfigured && !document.body.classList.contains("local-builder-mode"));

  const audioStat = elements.audioPill?.closest(".stat");
  if (audioStat) {
    audioStat.hidden = !audioConfigured;
  }

  const batteryStat = elements.batteryHero?.closest(".stat");
  if (batteryStat) {
    batteryStat.hidden = !batteryConfigured;
  }

}

function normalizedPeripheralSensorProfiles() {
  const liveProfiles = elements.peripheralSensorsList
    ? Array.from(elements.peripheralSensorsList.querySelectorAll("select[data-peripheral-sensor-index]"))
        .map((select) => String(select.value || "none").trim() || "none")
    : [];
  const currentProfiles = liveProfiles.length
    ? liveProfiles
    : (Array.isArray(state.peripheralSensorProfiles) ? state.peripheralSensorProfiles : []);
  const sanitizedProfiles = currentProfiles
    .map((value) => String(value || "none").trim() || "none")
    .slice(0, MAX_PERIPHERAL_SENSORS);
  return sanitizedProfiles.length ? sanitizedProfiles : ["none"];
}

function batteryDividerSensorSelected() {
  return normalizedPeripheralSensorProfiles().some((profile) => String(profile || "").trim().toLowerCase() === BATTERY_DIVIDER_SENSOR_PROFILE);
}

function ensureBatteryDividerSensorSelection() {
  state.peripheralSensorProfiles = normalizedPeripheralSensorProfiles();
  if (batteryDividerSensorSelected()) {
    return false;
  }

  const availableIndex = state.peripheralSensorProfiles.findIndex((profile) => String(profile || "none").trim().toLowerCase() === "none");
  state.peripheralSensorProfiles[availableIndex >= 0 ? availableIndex : 0] = BATTERY_DIVIDER_SENSOR_PROFILE;
  savePeripheralProfileSelections();
  renderPeripheralSensorControls();
  return true;
}

function batteryAdcCapablePins(status = state.status) {
  const chipFamily = activeChipFamily(status);
  if (chipFamily.includes("esp32s3") || chipFamily === "s3") {
    return Array.from({ length: 20 }, (_, index) => index + 1);
  }
  if (chipFamily === "esp32") {
    return [32, 33, 34, 35, 36, 39, 25, 26, 27, 14, 13, 12, 15, 4, 2, 0];
  }
  return [0, 1, 2, 3, 4, 5];
}

function normalizedPeripheralInputProfiles() {
  const currentProfiles = Array.isArray(state.peripheralInputProfiles) ? state.peripheralInputProfiles : [];
  const sanitizedProfiles = currentProfiles
    .map((value) => String(value || "none").trim() || "none")
    .slice(0, MAX_PERIPHERAL_INPUTS);
  return sanitizedProfiles.length ? sanitizedProfiles : ["none"];
}

function isTouchPeripheralInputProfile(profileValue) {
  const profile = String(profileValue || "none").trim().toLowerCase();
  return profile === "ttp223-touch-button" || profile === "esp32-native-touch-pad";
}

function factoryResetTouchInputIndex() {
  return normalizedPeripheralInputProfiles().findIndex(isTouchPeripheralInputProfile);
}

function normalizedPeripheralStorageProfiles() {
  const liveProfiles = elements.peripheralStorageList
    ? Array.from(elements.peripheralStorageList.querySelectorAll("select[data-peripheral-storage-index]"))
        .map((select) => String(select.value || "none").trim() || "none")
    : [];
  const currentProfiles = liveProfiles.length
    ? liveProfiles
    : (Array.isArray(state.peripheralStorageProfiles) ? state.peripheralStorageProfiles : []);
  const sanitizedProfiles = currentProfiles
    .map((value) => String(value || "none").trim() || "none")
    .slice(0, MAX_PERIPHERAL_STORAGES);
  return sanitizedProfiles.length ? sanitizedProfiles : ["none"];
}

function normalizedPeripheralCommunicationProfiles() {
  const currentProfiles = Array.isArray(state.peripheralCommunicationProfiles) ? state.peripheralCommunicationProfiles : [];
  const sanitizedProfiles = currentProfiles
    .map((value) => String(value || "none").trim() || "none")
    .slice(0, MAX_PERIPHERAL_COMMUNICATIONS);
  return sanitizedProfiles.length ? sanitizedProfiles : ["none"];
}

function normalizedPeripheralPowerProfiles() {
  const currentProfiles = Array.isArray(state.peripheralPowerProfiles) ? state.peripheralPowerProfiles : [];
  const sanitizedProfiles = currentProfiles
    .map((value) => String(value || "none").trim() || "none")
    .slice(0, MAX_PERIPHERAL_POWERS);
  return sanitizedProfiles.length ? sanitizedProfiles : ["none"];
}

function normalizedPeripheralControlProfiles() {
  const currentProfiles = Array.isArray(state.peripheralControlProfiles) ? state.peripheralControlProfiles : [];
  const sanitizedProfiles = currentProfiles
    .map((value) => String(value || "none").trim() || "none")
    .slice(0, MAX_PERIPHERAL_CONTROLS);
  return sanitizedProfiles.length ? sanitizedProfiles : ["none"];
}

function normalizedPeripheralExpansionProfiles() {
  const currentProfiles = Array.isArray(state.peripheralExpansionProfiles) ? state.peripheralExpansionProfiles : [];
  const sanitizedProfiles = currentProfiles
    .map((value) => String(value || "none").trim() || "none")
    .slice(0, MAX_PERIPHERAL_EXPANSIONS);
  return sanitizedProfiles.length ? sanitizedProfiles : ["none"];
}

function appendPeripheralOptions(select, options, selectedValue) {
  for (const optionConfig of options) {
    const option = document.createElement("option");
    option.value = optionConfig.value;
    option.textContent = optionConfig.label;
    option.selected = optionConfig.value === selectedValue;
    select.appendChild(option);
  }
}

function sanitizeStoredPeripheralProfiles(values, maxCount, fallback = ["none"]) {
  const source = Array.isArray(values) ? values : fallback;
  const sanitized = source
    .map((value) => String(value || "none").trim() || "none")
    .slice(0, maxCount);
  return sanitized.length ? sanitized : [...fallback];
}

function loadPeripheralProfileSelections() {
  try {
    const parsed = JSON.parse(window.localStorage.getItem(PERIPHERAL_PROFILE_SELECTIONS_STORAGE_KEY) || "{}");
    return isPlainObject(parsed) ? parsed : {};
  } catch {
    return {};
  }
}

function savePeripheralProfileSelections() {
  try {
    window.localStorage.setItem(PERIPHERAL_PROFILE_SELECTIONS_STORAGE_KEY, JSON.stringify({
      audioProfile: String(elements.peripheralAudioProfile?.value || "none"),
      audioProfiles: sanitizeStoredPeripheralProfiles(state.peripheralAudioProfiles, MAX_PERIPHERAL_AUDIO_OUTPUTS, [String(elements.peripheralAudioProfile?.value || "none")]),
      audioInProfile: String(elements.peripheralAudioInProfile?.value || "none"),
      audioInProfiles: sanitizeStoredPeripheralProfiles(state.peripheralAudioInProfiles, MAX_PERIPHERAL_AUDIO_INPUTS, [String(elements.peripheralAudioInProfile?.value || "none")]),
      displayProfile: String(elements.peripheralDisplayProfile?.value || "none"),
      displayProfiles: sanitizeStoredPeripheralProfiles(state.peripheralDisplayProfiles, MAX_PERIPHERAL_DISPLAYS, [String(elements.peripheralDisplayProfile?.value || "none")]),
      sensors: sanitizeStoredPeripheralProfiles(state.peripheralSensorProfiles, MAX_PERIPHERAL_SENSORS, ["none"]),
      inputs: sanitizeStoredPeripheralProfiles(state.peripheralInputProfiles, MAX_PERIPHERAL_INPUTS, ["none"]),
      controls: sanitizeStoredPeripheralProfiles(state.peripheralControlProfiles, MAX_PERIPHERAL_CONTROLS, ["none"]),
      expansions: sanitizeStoredPeripheralProfiles(state.peripheralExpansionProfiles, MAX_PERIPHERAL_EXPANSIONS, ["none"]),
      storage: sanitizeStoredPeripheralProfiles(state.peripheralStorageProfiles, MAX_PERIPHERAL_STORAGES, ["none"]),
      communication: sanitizeStoredPeripheralProfiles(state.peripheralCommunicationProfiles, MAX_PERIPHERAL_COMMUNICATIONS, ["none"]),
      power: sanitizeStoredPeripheralProfiles(state.peripheralPowerProfiles, MAX_PERIPHERAL_POWERS, ["none"]),
    }));
  } catch {
  }
}

function normalizePeripheralProfileSelections(value) {
  if (typeof value === "string") {
    try {
      const parsed = JSON.parse(value);
      return isPlainObject(parsed) ? parsed : {};
    } catch {
      return {};
    }
  }
  return isPlainObject(value) ? (cloneSettingsObject(value) || {}) : {};
}

function applyPeripheralProfileSelectionsState(persisted) {
  const normalizedPersisted = normalizePeripheralProfileSelections(persisted);
  const persistedAudioProfiles = sanitizeStoredPeripheralProfiles(
    normalizedPersisted.audioProfiles,
    MAX_PERIPHERAL_AUDIO_OUTPUTS,
    [String(normalizedPersisted.audioProfile || "none")],
  );
  if (elements.peripheralAudioProfile && [...elements.peripheralAudioProfile.options].some((option) => option.value === String(persistedAudioProfiles[0] || normalizedPersisted.audioProfile || "none"))) {
    elements.peripheralAudioProfile.value = String(persistedAudioProfiles[0] || normalizedPersisted.audioProfile || "none");
  }
  state.peripheralAudioProfiles = persistedAudioProfiles;
  const persistedAudioInProfiles = sanitizeStoredPeripheralProfiles(
    normalizedPersisted.audioInProfiles,
    MAX_PERIPHERAL_AUDIO_INPUTS,
    [String(normalizedPersisted.audioInProfile || "none")],
  );
  if (elements.peripheralAudioInProfile && [...elements.peripheralAudioInProfile.options].some((option) => option.value === String(persistedAudioInProfiles[0] || normalizedPersisted.audioInProfile || "none"))) {
    elements.peripheralAudioInProfile.value = String(persistedAudioInProfiles[0] || normalizedPersisted.audioInProfile || "none");
  }
  state.peripheralAudioInProfiles = persistedAudioInProfiles;
  const persistedDisplayProfiles = sanitizeStoredPeripheralProfiles(
    normalizedPersisted.displayProfiles,
    MAX_PERIPHERAL_DISPLAYS,
    [String(normalizedPersisted.displayProfile || "none")],
  );
  if (elements.peripheralDisplayProfile && [...elements.peripheralDisplayProfile.options].some((option) => option.value === String(persistedDisplayProfiles[0] || normalizedPersisted.displayProfile || "none"))) {
    elements.peripheralDisplayProfile.value = String(persistedDisplayProfiles[0] || normalizedPersisted.displayProfile || "none");
  }
  state.peripheralDisplayProfiles = persistedDisplayProfiles;
  state.peripheralSensorProfiles = sanitizeStoredPeripheralProfiles(normalizedPersisted.sensors, MAX_PERIPHERAL_SENSORS, ["none"]);
  state.peripheralInputProfiles = sanitizeStoredPeripheralProfiles(normalizedPersisted.inputs, MAX_PERIPHERAL_INPUTS, ["none"]);
  state.peripheralControlProfiles = sanitizeStoredPeripheralProfiles(normalizedPersisted.controls, MAX_PERIPHERAL_CONTROLS, ["none"]);
  state.peripheralExpansionProfiles = sanitizeStoredPeripheralProfiles(normalizedPersisted.expansions, MAX_PERIPHERAL_EXPANSIONS, ["none"]);
  state.peripheralStorageProfiles = sanitizeStoredPeripheralProfiles(normalizedPersisted.storage, MAX_PERIPHERAL_STORAGES, ["none"]);
  state.peripheralCommunicationProfiles = sanitizeStoredPeripheralProfiles(normalizedPersisted.communication, MAX_PERIPHERAL_COMMUNICATIONS, ["none"]);
  state.peripheralPowerProfiles = sanitizeStoredPeripheralProfiles(normalizedPersisted.power, MAX_PERIPHERAL_POWERS, ["none"]);
}

function restorePeripheralProfileSelections() {
  applyPeripheralProfileSelectionsState(loadPeripheralProfileSelections());
}

function effectiveDisplayProfile(settings = state.settings) {
  const selectedProfile = String(elements.peripheralDisplayProfile?.value || "").trim().toLowerCase();
  if (selectedProfile) {
    return selectedProfile;
  }
  if (settings?.oled?.enabled === false) {
    return "none";
  }
  return String(settings?.oled?.displayType || "oled").trim().toLowerCase() === "wape" ? "waveshare-screen" : "i2c-oled";
}

function effectiveStorageEnabled(settings = state.settings) {
  const primarySelect = elements.peripheralStorageList?.querySelector('select[data-peripheral-storage-index="0"]');
  const selectedProfile = String(primarySelect?.value || normalizedPeripheralStorageProfiles()[0] || "none").trim().toLowerCase();
  if (selectedProfile) {
    return selectedProfile !== "none";
  }
  return Boolean(settings?.sd?.enabled ?? false);
}

function loadPeripheralHelperBindings() {
  try {
    return normalizePeripheralHelperBindings(window.localStorage.getItem(PERIPHERAL_HELPER_BINDINGS_STORAGE_KEY) || "{}");
  } catch {
    return {};
  }
}

function savePeripheralHelperBindings() {
  try {
    window.localStorage.setItem(
      PERIPHERAL_HELPER_BINDINGS_STORAGE_KEY,
      JSON.stringify(isPlainObject(state.peripheralHelperBindings) ? state.peripheralHelperBindings : {}),
    );
  } catch {
  }
}

function peripheralHelperBindingSlotKey(groupKey, index) {
  return `${String(groupKey || "custom")}:${Number(index) || 0}`;
}

function peripheralHelperBindingValue(groupKey, index, signalLabel) {
  if (groupKey === "sensor" && signalLabel === "GPIO") {
    const sensorProfile = String(state.peripheralSensorProfiles?.[Number(index) || 0] || "none").trim().toLowerCase();
    if (sensorProfile === BATTERY_DIVIDER_SENSOR_PROFILE) {
      const adcPin = Number(elements.batteryAdcPin?.value || state.settings?.battery?.adcPin || 0);
      return Number.isFinite(adcPin) && adcPin > 0 ? String(adcPin) : "";
    }
  }
  const slot = state.peripheralHelperBindings?.[peripheralHelperBindingSlotKey(groupKey, index)];
  return typeof slot?.[signalLabel] === "string" ? slot[signalLabel] : "";
}

function helperBindingDisplayLabel(groupKey, signalLabel) {
  const normalizedSignal = String(signalLabel || "").trim().toUpperCase();
  if (groupKey === "input" && normalizedSignal === "SIG") {
    return "GPIO";
  }
  if (groupKey === "power") {
    if (normalizedSignal === "INPUT_VOLTAGE") {
      return "Input Voltage";
    }
    if (normalizedSignal === "OUTPUT_VOLTAGE") {
      return "Output Voltage";
    }
  }
  return String(signalLabel || "").trim();
}

function parseVoltageOptionValue(value) {
  const match = /^\s*([0-9]+(?:\.[0-9]+)?)\s*V\s*$/i.exec(String(value || ""));
  return match ? Number(match[1]) : NaN;
}

function powerVoltageOptions(profileValue, signalLabel, inputVoltage = "") {
  const normalizedProfile = String(profileValue || "none").trim().toLowerCase();
  const normalizedSignal = String(signalLabel || "").trim().toUpperCase();
  if (normalizedProfile.includes("step-down")) {
    if (normalizedSignal === "INPUT_VOLTAGE") {
      return POWER_STEP_DOWN_INPUT_OPTIONS.map((value) => ({ value, label: value }));
    }
    const inputValue = parseVoltageOptionValue(inputVoltage || POWER_STEP_DOWN_INPUT_OPTIONS[0]);
    return POWER_STEP_DOWN_OUTPUT_OPTIONS
      .filter((value) => parseVoltageOptionValue(value) < inputValue)
      .map((value) => ({ value, label: value }));
  }
  if (normalizedProfile.includes("step-up")) {
    if (normalizedSignal === "INPUT_VOLTAGE") {
      return POWER_STEP_UP_INPUT_OPTIONS.map((value) => ({ value, label: value }));
    }
    const inputValue = parseVoltageOptionValue(inputVoltage || POWER_STEP_UP_INPUT_OPTIONS[0]);
    return POWER_STEP_UP_OUTPUT_OPTIONS
      .filter((value) => parseVoltageOptionValue(value) > inputValue)
      .map((value) => ({ value, label: value }));
  }
  return [];
}

function powerHelperDefaultValue(profileValue, signalLabel, inputVoltage = "") {
  const normalizedProfile = String(profileValue || "none").trim().toLowerCase();
  const normalizedSignal = String(signalLabel || "").trim().toUpperCase();
  if (normalizedProfile.includes("step-down")) {
    if (normalizedSignal === "INPUT_VOLTAGE") {
      return "12V";
    }
    const availableOutputs = powerVoltageOptions(profileValue, signalLabel, inputVoltage || "12V");
    return availableOutputs.some((option) => option.value === "5V") ? "5V" : String(availableOutputs[0]?.value || "");
  }
  if (normalizedProfile.includes("step-up")) {
    if (normalizedSignal === "INPUT_VOLTAGE") {
      return "3.7V";
    }
    const availableOutputs = powerVoltageOptions(profileValue, signalLabel, inputVoltage || "3.7V");
    return availableOutputs.some((option) => option.value === "5V") ? "5V" : String(availableOutputs[0]?.value || "");
  }
  return "";
}

function helperBindingDefaultValue(groupKey, index, signalLabel) {
  const normalizedSignal = String(signalLabel || "").trim().toUpperCase();
  if (groupKey === "power") {
    const profileValue = peripheralHelperProfileValue(groupKey, index, state.settings || {});
    const inputVoltage = peripheralHelperBindingValue(groupKey, index, "INPUT_VOLTAGE") || powerHelperDefaultValue(profileValue, "INPUT_VOLTAGE");
    return powerHelperDefaultValue(profileValue, normalizedSignal, inputVoltage);
  }
  if (normalizedSignal === "CONTACT") {
    return "NO";
  }
  if (normalizedSignal === "SOURCE") {
    return "GND";
  }
  return "";
}

function normalizePowerHelperBindings(index) {
  const profileValue = peripheralHelperProfileValue("power", index, state.settings || {});
  const normalizedProfile = String(profileValue || "none").trim().toLowerCase();
  const slotKey = peripheralHelperBindingSlotKey("power", index);
  const currentSlot = isPlainObject(state.peripheralHelperBindings?.[slotKey]) ? { ...state.peripheralHelperBindings[slotKey] } : {};

  if (!normalizedProfile.includes("step-down") && !normalizedProfile.includes("step-up")) {
    delete currentSlot.INPUT_VOLTAGE;
    delete currentSlot.OUTPUT_VOLTAGE;
    if (Object.keys(currentSlot).length) {
      state.peripheralHelperBindings[slotKey] = currentSlot;
    } else {
      delete state.peripheralHelperBindings?.[slotKey];
    }
    savePeripheralHelperBindings();
    return;
  }

  const inputOptions = powerVoltageOptions(profileValue, "INPUT_VOLTAGE");
  const inputValues = new Set(inputOptions.map((option) => option.value));
  const normalizedInput = inputValues.has(currentSlot.INPUT_VOLTAGE)
    ? currentSlot.INPUT_VOLTAGE
    : powerHelperDefaultValue(profileValue, "INPUT_VOLTAGE");
  currentSlot.INPUT_VOLTAGE = normalizedInput;

  const outputOptions = powerVoltageOptions(profileValue, "OUTPUT_VOLTAGE", normalizedInput);
  const outputValues = new Set(outputOptions.map((option) => option.value));
  const normalizedOutput = outputValues.has(currentSlot.OUTPUT_VOLTAGE)
    ? currentSlot.OUTPUT_VOLTAGE
    : powerHelperDefaultValue(profileValue, "OUTPUT_VOLTAGE", normalizedInput);

  if (normalizedOutput) {
    currentSlot.OUTPUT_VOLTAGE = normalizedOutput;
  } else {
    delete currentSlot.OUTPUT_VOLTAGE;
  }

  state.peripheralHelperBindings[slotKey] = currentSlot;
  savePeripheralHelperBindings();
}

function setPeripheralHelperBindingValue(groupKey, index, signalLabel, value) {
  if (groupKey === "sensor" && signalLabel === "GPIO") {
    const sensorProfile = String(state.peripheralSensorProfiles?.[Number(index) || 0] || "none").trim().toLowerCase();
    if (sensorProfile === BATTERY_DIVIDER_SENSOR_PROFILE) {
      if (elements.batteryAdcPin) {
        elements.batteryAdcPin.value = String(value || "0");
        elements.batteryAdcPin.dispatchEvent(new Event("change", { bubbles: true }));
      }
      return;
    }
  }
  const slotKey = peripheralHelperBindingSlotKey(groupKey, index);
  state.peripheralHelperBindings ||= {};
  const normalizedSignalLabel = String(signalLabel || "").trim().toUpperCase();
  const normalizedGroupKey = String(groupKey || "custom");
  const normalizedValue = String(value || "");
  if (normalizedGroupKey === "input" && normalizedSignalLabel === "MAIN_CONTROL" && ["1", "true", "on", "yes"].includes(normalizedValue.toLowerCase())) {
    for (const [existingSlotKey, existingSlot] of Object.entries(state.peripheralHelperBindings)) {
      if (!existingSlotKey.startsWith("input:") || !isPlainObject(existingSlot) || existingSlotKey === slotKey) {
        continue;
      }
      delete existingSlot.MAIN_CONTROL;
      if (!Object.keys(existingSlot).length) {
        delete state.peripheralHelperBindings[existingSlotKey];
      }
    }
  }
  const slot = isPlainObject(state.peripheralHelperBindings[slotKey]) ? state.peripheralHelperBindings[slotKey] : {};
  if (normalizedValue) {
    slot[signalLabel] = normalizedValue;
  } else {
    delete slot[signalLabel];
  }
  if (Object.keys(slot).length) {
    state.peripheralHelperBindings[slotKey] = slot;
  } else {
    delete state.peripheralHelperBindings[slotKey];
  }
  if (normalizedGroupKey === "power") {
    normalizePowerHelperBindings(Number(index || 0));
    return;
  }
  savePeripheralHelperBindings();
}

function removePeripheralHelperBindingsForIndex(groupKey, index) {
  if (!isPlainObject(state.peripheralHelperBindings)) {
    return;
  }

  const normalizedGroupKey = String(groupKey || "custom");
  const removedIndex = Number(index || 0);
  const nextBindings = {};

  for (const [slotKey, slotValue] of Object.entries(state.peripheralHelperBindings)) {
    const [slotGroupKey, rawIndex] = String(slotKey || "").split(":");
    const slotIndex = Number(rawIndex || 0);
    if (slotGroupKey !== normalizedGroupKey || !Number.isInteger(slotIndex)) {
      nextBindings[slotKey] = slotValue;
      continue;
    }
    if (slotIndex < removedIndex) {
      nextBindings[slotKey] = slotValue;
      continue;
    }
    if (slotIndex > removedIndex) {
      nextBindings[peripheralHelperBindingSlotKey(slotGroupKey, slotIndex - 1)] = slotValue;
    }
  }

  state.peripheralHelperBindings = nextBindings;
  savePeripheralHelperBindings();
}

function helperBindingSignalOptions() {
  const options = [];
  const roleMap = gpioRoleMap(state.settings, state.status);
  for (const pin of validBoardPins(false)) {
    const roleLabels = roleMap.get(pin) || [];
    options.push({
      value: String(pin),
      label: roleLabels.length ? `GPIO${pin} (${roleLabels.join(" / ")})` : `GPIO${pin}`,
    });
  }
  return options;
}

function boardProfileChipFamily(boardProfile = activeGpioBoardProfile()) {
  const normalizedProfile = String(boardProfile || "").trim().toLowerCase();
  if (!normalizedProfile) {
    return "esp32s3";
  }
  if (normalizedProfile === "esp32-spk-n16r8" || normalizedProfile.startsWith("esp32-s3")) {
    return "esp32s3";
  }
  if (normalizedProfile.startsWith("esp32-s2")) {
    return "esp32s2";
  }
  if (normalizedProfile === "esp32-c3") {
    return "esp32c3";
  }
  if (normalizedProfile === "esp32-c6") {
    return "esp32c6";
  }
  return "esp32";
}

function activeChipFamily(status = state.status) {
  if (PC_DESIGNER_RUNTIME) return boardChipFamily(activeGpioBoardProfile(status));
  const chipFamily = String(status?.firmware?.chipFamily || "").trim().toLowerCase().replaceAll("-", "");
  if (chipFamily.includes("esp32s3") || chipFamily === "s3") {
    return "esp32s3";
  }
  if (chipFamily.includes("esp32s2") || chipFamily === "s2") {
    return "esp32s2";
  }
  if (chipFamily.includes("esp32c3") || chipFamily === "c3") {
    return "esp32c3";
  }
  if (chipFamily.includes("esp32c6") || chipFamily === "c6") {
    return "esp32c6";
  }
  if (chipFamily.includes("esp32")) {
    return "esp32";
  }
  return boardProfileChipFamily();
}

function touchCapablePins(status = state.status) {
  switch (activeChipFamily(status)) {
    case "esp32":
      return [0, 2, 4, 12, 13, 14, 15, 27, 32, 33];
    case "esp32s2":
    case "esp32s3":
      return [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14];
    default:
      return [];
  }
}

function reservedBoardPinKinds(boardProfile = activeGpioBoardProfile()) {
  const reservedPins = GPIO_BOARD_RESERVED_PINS[String(boardProfile || "").trim().toLowerCase()] || {};
  return new Map(Object.entries(reservedPins).map(([pin, meta]) => [Number(pin), String(meta?.kind || "").trim().toLowerCase()]));
}

function motorUnsafePins(boardProfile = activeGpioBoardProfile()) {
  const unsafeKinds = new Set(["usb", "serial", "strap", "psram", "camera", "sd", "jtag", "onboard"]);
  return new Set(
    [...reservedBoardPinKinds(boardProfile).entries()]
      .filter(([, kind]) => unsafeKinds.has(kind))
      .map(([pin]) => pin),
  );
}

function helperBindingSignalOptionsFor(groupKey, index, signalLabel) {
  const normalizedSignal = String(signalLabel || "").trim().toUpperCase();
  const profileValue = peripheralHelperProfileValue(groupKey, index, state.settings || {});

  if (groupKey === "power") {
    if (normalizedSignal === "INPUT_VOLTAGE") {
      return powerVoltageOptions(profileValue, normalizedSignal);
    }
    if (normalizedSignal === "OUTPUT_VOLTAGE") {
      const selectedInput = peripheralHelperBindingValue(groupKey, index, "INPUT_VOLTAGE") || powerHelperDefaultValue(profileValue, "INPUT_VOLTAGE");
      return powerVoltageOptions(profileValue, normalizedSignal, selectedInput);
    }
  }

  if (groupKey === "sensor" && normalizedSignal === "GPIO" && String(profileValue).trim().toLowerCase() === BATTERY_DIVIDER_SENSOR_PROFILE) {
    populateBatteryAdcPinOptions(state.settings);
    return [...(elements.batteryAdcPin?.options || [])]
      .filter((option) => Number(option.value) > 0)
      .map((option) => ({ value: String(option.value), label: String(option.textContent || `GPIO${option.value}`) }));
  }

  if (groupKey === "input" && normalizedSignal === "MAIN_CONTROL") {
    return [
      { value: "", label: "None" },
      { value: "1", label: "Use as main control" },
    ];
  }

  if (groupKey === "input" && String(signalLabel || "").trim().toUpperCase() === "CONTACT") {
    return [
      { value: "NO", label: "Normally Open (NO)" },
      { value: "NC", label: "Normally Closed (NC)" },
    ];
  }

   if (groupKey === "input" && String(signalLabel || "").trim().toUpperCase() === "SOURCE") {
    return [
      { value: "GND", label: "Switched to GND (internal pull-up)" },
      { value: "VCC", label: "Switched to VCC (internal pull-down)" },
    ];
  }

  const options = [];
  const roleMap = gpioRoleMap(state.settings, state.status);
  const touchSignal = groupKey === "input"
    && String(signalLabel || "").trim().toUpperCase() === "TOUCH"
    && String(profileValue || "none").trim().toLowerCase().includes("esp32-native-touch-pad");
  const motorSignal = groupKey === "control";
  const allowedPins = touchSignal ? new Set(touchCapablePins()) : null;
  const blockedPins = motorSignal ? motorUnsafePins() : null;
  for (const pin of validBoardPins(!["input", "sensor", "audioIn"].includes(groupKey))) {
    if (allowedPins && !allowedPins.has(pin)) {
      continue;
    }
    if (blockedPins && blockedPins.has(pin)) {
      continue;
    }
    const roleLabels = roleMap.get(pin) || [];
    options.push({
      value: String(pin),
      label: roleLabels.length ? `GPIO${pin} (${roleLabels.join(" / ")})` : `GPIO${pin}`,
    });
  }
  return options;
}

function helperSignalLabels(groupKey, profileValue) {
  const profile = String(profileValue || "none");
  if (!profile || profile === "none" || profile.includes("bluetooth")) {
    return [];
  }

  if (groupKey === "power" && (profile.includes("step-down") || profile.includes("step-up"))) {
    return ["INPUT_VOLTAGE", "OUTPUT_VOLTAGE"];
  }

  if (groupKey === "sensor" && profile.includes("battery-voltage-divider")) {
    return ["GPIO"];
  }

  if (groupKey === "input" && profile.includes("esp32-native-touch-pad")) {
    return ["TOUCH", "MAIN_CONTROL"];
  }

  if (groupKey === "input" && profile.includes("ttp223-touch-button")) {
    return ["SIG", "MAIN_CONTROL"];
  }

  if (groupKey === "input" && profile.includes("limit-switch")) {
    return ["COM", "CONTACT", "SOURCE"];
  }

  const templateSignals = peripheralDiagramTemplatePins(groupKey, profile)
    .map((label) => String(label || "").trim())
    .filter(Boolean);

  return templateSignals.filter((label) => ![
    "VCC",
    "GND",
    "5V",
    "3V3",
    "3.3V",
    "PWR",
    "VIN",
    "BATT",
    "BAT",
    "BATTERY",
    "3VO",
    "COMMON",
  ].includes(label.toUpperCase()));
}

function batteryDividerResistorMarkup(ohms) {
  return `
    <span class="peripheral-diagram-divider-resistor" aria-hidden="true">
      ${resistorBands(ohms).map(color=>`<span class="peripheral-diagram-divider-resistor-band" style="background:${color}"></span>`).join('')}
    </span>
  `;
}

function peripheralDiagramBatteryDividerMarkup(node) {
  const divider=voltageDividerMaximum(state.settings?.battery);
  return `
    <div class="peripheral-diagram-divider-block" aria-label="${escapeHtml(node.title || node.label)} placeholder">
      <div class="peripheral-diagram-divider-title">Voltage Divider</div>
      <div class="peripheral-diagram-divider-ohms">${resistorLabel(divider.dividerR1Ohms)} + ${resistorLabel(divider.dividerR2Ohms)}</div>
      <div class="peripheral-diagram-divider-schematic">
        <div class="peripheral-diagram-divider-node" aria-hidden="true"></div>
        <div class="peripheral-diagram-divider-gpio-contact">SIGNAL</div>
        <div class="peripheral-diagram-divider-row peripheral-diagram-divider-row-top">
          <span class="peripheral-diagram-divider-lead" aria-hidden="true"></span>
          ${batteryDividerResistorMarkup(divider.dividerR1Ohms)}
          <span class="peripheral-diagram-divider-tail" aria-hidden="true"></span>
          <span class="peripheral-diagram-divider-contact">VIN</span>
        </div>
        <div class="peripheral-diagram-divider-row peripheral-diagram-divider-row-bottom">
          <span class="peripheral-diagram-divider-lead" aria-hidden="true"></span>
          ${batteryDividerResistorMarkup(divider.dividerR2Ohms)}
          <span class="peripheral-diagram-divider-tail" aria-hidden="true"></span>
          <span class="peripheral-diagram-divider-contact">GND</span>
        </div>
      </div>
    </div>
  `;
}

function peripheralDiagramNativeTouchMarkup(node) {
  const runtimeStatus = inputRuntimeStatus(node.index);
  const assignedPin = peripheralHelperBindingValue("input", node.index, "TOUCH") || String(runtimeStatus?.pin || "");
  const numericPin = Number(assignedPin);
  const supported = runtimeStatus
    ? Boolean(runtimeStatus.touchSupported)
    : (Number.isFinite(numericPin) && touchCapablePins().includes(numericPin));
  const active = Boolean(runtimeStatus?.active);
  const sensitivity = inputTouchSensitivity(node.index);
  const pinLabel = Number.isFinite(numericPin) && numericPin >= 0 ? `GPIO${numericPin}` : "No GPIO";
  const rawValue = Number(runtimeStatus?.rawValue || 0);
  const baselineValue = Number(runtimeStatus?.baselineValue || 0);
  const deltaValue = baselineValue > 0 && rawValue > 0 ? Math.abs(rawValue - baselineValue) : 0;
  const sensitivityFieldId = `touch-sensitivity-${node.index}`;
  const sensitivityFieldName = `touch.sensitivity.${node.index}`;
  const metrics = runtimeStatus && baselineValue > 0
    ? `Raw ${rawValue} / Base ${baselineValue} / Delta ${deltaValue}`
    : "Select a touch-capable GPIO to enable live sensing";

  return `
    <div class="peripheral-diagram-touch-block" data-touch-input-index="${node.index}" aria-label="${escapeHtml(node.title || node.label)} touch input">
      <div class="peripheral-diagram-touch-header">
        <div class="peripheral-diagram-touch-title">${escapeHtml(node.title || node.label)}</div>
        <div class="peripheral-diagram-touch-pin" data-touch-pin="${node.index}">${escapeHtml(pinLabel)}</div>
      </div>
      <div class="peripheral-diagram-touch-pad ${active ? "is-active" : ""} ${supported ? "" : "is-unsupported"}" data-touch-pad="${node.index}">
        <span class="peripheral-diagram-touch-ring" aria-hidden="true"></span>
        <span class="peripheral-diagram-touch-light" aria-hidden="true"></span>
        <span class="peripheral-diagram-touch-state" data-touch-state="${node.index}">${escapeHtml(!supported ? "Unsupported GPIO" : (active ? "Touch detected" : "Idle"))}</span>
      </div>
      <label class="peripheral-diagram-touch-sensitivity">
        <span class="peripheral-diagram-touch-sensitivity-label">Sensitivity</span>
        <input
          id="${sensitivityFieldId}"
          name="${sensitivityFieldName}"
          type="range"
          min="5"
          max="100"
          step="1"
          value="${sensitivity}"
          data-touch-sensitivity-index="${node.index}"
          aria-label="Touch sensitivity for input ${node.index + 1}"
        />
        <span class="peripheral-diagram-touch-sensitivity-value" data-touch-sensitivity-value="${node.index}">${sensitivity}%</span>
      </label>
      <div class="peripheral-diagram-touch-metrics" data-touch-metrics="${node.index}">${escapeHtml(metrics)}</div>
    </div>
  `;
}

function realPeripheralBindingDefinitions(groupKey, profileValue, settings = state.settings, index = 0) {
  const profile = String(profileValue || "none").trim().toLowerCase();
  switch (groupKey) {
    case "audio":
      return index !== 0 || profile === "none" || profile.includes("bluetooth") || profile.includes("buzzer")
        ? []
        : [
            { key: "audio.wsPin", label: "I2S WS", element: elements.audioWsPin },
            { key: "audio.bclkPin", label: "I2S BCLK", element: elements.audioBclkPin },
            { key: "audio.doutPin", label: "I2S DIN", element: elements.audioDoutPin },
          ];
    case "display":
      if (index !== 0 || profile === "none") {
        return [];
      }
      if (profile === "i2c-oled") {
        return [
          { key: "oled.sdaPin", label: "OLED SDA", element: elements.oledSdaPin },
          { key: "oled.sclPin", label: "OLED SCL", element: elements.oledSclPin },
          { key: "oled.resetPin", label: "OLED RESET", element: elements.oledResetPin },
        ];
      }
      if (profile === "waveshare-screen") {
        return [
          { key: "oled.wapeTriggerPin", label: "Wape Trigger", element: elements.wapeTriggerPin },
        ];
      }
      return [];
    case "storage":
      return profile === "microsd-spi"
        ? [
            { key: "sd.csPin", label: "SD CS", element: elements.sdCsPin },
            { key: "sd.sckPin", label: "SD SCK", element: elements.sdSckPin },
            { key: "sd.mosiPin", label: "SD MOSI", element: elements.sdMosiPin },
            { key: "sd.misoPin", label: "SD MISO", element: elements.sdMisoPin },
          ]
        : [];
    default:
      return [];
  }
}

function dynamicFieldToken(value) {
  return String(value || "field")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "") || "field";
}

function ensureDynamicFieldIdentity(field, id, name) {
  if (!field) {
    return field;
  }
  if (!field.id) {
    field.id = id;
  }
  if (!field.name) {
    field.name = name;
  }
  return field;
}

function normalizeOwnedFormControlIds(form = elements.settingsForm) {
  if (!form) {
    return;
  }

  const seenIds = new Map();
  const formControls = [...form.elements].filter((field) => field instanceof HTMLInputElement || field instanceof HTMLSelectElement || field instanceof HTMLTextAreaElement);

  for (const field of formControls) {
    const originalId = String(field.id || "").trim();
    const baseId = originalId || dynamicFieldToken(
      field.name
      || field.getAttribute("aria-label")
      || field.getAttribute("data-target-name")
      || field.type
      || field.tagName.toLowerCase(),
    );
    const nextCount = (seenIds.get(baseId) || 0) + 1;
    seenIds.set(baseId, nextCount);
    const nextId = nextCount === 1 ? baseId : `${baseId}-${nextCount}`;

    if (originalId === nextId) {
      continue;
    }

    field.id = nextId;
    if (originalId) {
      for (const label of document.querySelectorAll("label")) {
        if (label.htmlFor === originalId) {
          label.htmlFor = nextId;
        }
      }
    }
  }
}

function peripheralDynamicFieldId(groupKey, index, suffix) {
  return `peripheral-${dynamicFieldToken(groupKey)}-${index + 1}-${dynamicFieldToken(suffix)}`;
}

function peripheralDynamicFieldName(groupKey, index, suffix) {
  return `peripheral.${dynamicFieldToken(groupKey)}.${index}.${dynamicFieldToken(suffix)}`;
}

function gpioDynamicFieldId(pin, scope = "") {
  const normalizedScope = dynamicFieldToken(scope);
  return normalizedScope
    ? `gpio-role-${dynamicFieldToken(pin)}-${normalizedScope}`
    : `gpio-role-${dynamicFieldToken(pin)}`;
}

function gpioDynamicFieldName(pin, scope = "") {
  const normalizedScope = dynamicFieldToken(scope);
  return normalizedScope
    ? `gpio.role.${dynamicFieldToken(pin)}.${normalizedScope}`
    : `gpio.role.${dynamicFieldToken(pin)}`;
}

function appendRealPeripheralBindingControl(container, definition) {
  const control = document.createElement("label");
  control.className = "peripheral-binding-control";

  const label = document.createElement("span");
  label.className = "peripheral-binding-label";
  label.textContent = definition.label.replace(/^OLED\s+/i, "").replace(/^I2S\s+/i, "");
  control.appendChild(label);

  const select = document.createElement("select");
  ensureDynamicFieldIdentity(
    select,
    peripheralDynamicFieldId(container.dataset.peripheralBindingGroup || "binding", Number(container.dataset.peripheralBindingIndex || 0), definition.key),
    peripheralDynamicFieldName(container.dataset.peripheralBindingGroup || "binding", Number(container.dataset.peripheralBindingIndex || 0), definition.key),
  );
  select.dataset.peripheralBindingKey = definition.key;
  select.setAttribute("aria-label", `${definition.label} GPIO binding`);
  control.htmlFor = select.id;

  if (definition.element) {
    for (const option of definition.element.options) {
      const cloned = document.createElement("option");
      cloned.value = option.value;
      cloned.textContent = option.textContent;
      cloned.disabled = option.disabled;
      cloned.selected = option.selected;
      select.appendChild(cloned);
    }
    select.value = definition.element.value;
  }

  control.appendChild(select);
  container.appendChild(control);
}

function appendHelperPeripheralBindingControl(container, groupKey, index, signalLabel) {
  const control = document.createElement("label");
  control.className = "peripheral-binding-control";

  const label = document.createElement("span");
  label.className = "peripheral-binding-label";
  label.textContent = helperBindingDisplayLabel(groupKey, signalLabel);
  control.appendChild(label);

  const select = document.createElement("select");
  ensureDynamicFieldIdentity(
    select,
    peripheralDynamicFieldId(groupKey, index, signalLabel),
    peripheralDynamicFieldName(groupKey, index, signalLabel),
  );
  select.dataset.peripheralHelperGroup = groupKey;
  select.dataset.peripheralHelperIndex = String(index);
  select.dataset.peripheralHelperSignal = signalLabel;
  select.setAttribute("aria-label", `${helperBindingDisplayLabel(groupKey, signalLabel)} helper setting`);
  control.htmlFor = select.id;

  const unusedOption = document.createElement("option");
  unusedOption.value = "";
  const optionConfigs = helperBindingSignalOptionsFor(groupKey, index, signalLabel);
  const hasExplicitEmptyOption = optionConfigs.some((optionConfig) => String(optionConfig?.value ?? "") === "");
  const noPinsAvailable = optionConfigs.length === 0;
  const normalizedSignalLabel = String(signalLabel || "").trim().toUpperCase();
  unusedOption.textContent = String(signalLabel || "").trim().toUpperCase() === "CONTACT"
    ? "Normally Open (NO)"
    : String(signalLabel || "").trim().toUpperCase() === "SOURCE"
      ? "Switched to GND (internal pull-up)"
    : (noPinsAvailable ? "No supported GPIOs" : "Unused");
  if (!hasExplicitEmptyOption && normalizedSignalLabel !== "CONTACT" && normalizedSignalLabel !== "SOURCE" && !(groupKey === "power" && ["INPUT_VOLTAGE", "OUTPUT_VOLTAGE"].includes(normalizedSignalLabel))) {
    select.appendChild(unusedOption);
  }

  const selectedValue = peripheralHelperBindingValue(groupKey, index, signalLabel)
    || helperBindingDefaultValue(groupKey, index, signalLabel);
  for (const optionConfig of optionConfigs) {
    const option = document.createElement("option");
    option.value = optionConfig.value;
    option.textContent = optionConfig.label;
    option.selected = optionConfig.value === selectedValue;
    select.appendChild(option);
  }
  select.disabled = noPinsAvailable;
  if (selectedValue && ![...select.options].some(option => option.value === selectedValue)) {
    const invalid = new Option(`GPIO${selectedValue} — invalid/reserved for this board (reassign)`, selectedValue);
    invalid.disabled = true;
    select.prepend(invalid);
  }
  select.value = selectedValue;

  control.appendChild(select);
  container.appendChild(control);
}

const defaultedPeripheralProfiles = new Map();
// Shared profile component identifies selection intent, separate from saved-data rendering.
document.addEventListener('change',event=>{
  const field=event.target,group=field?.dataset?.peripheralDefaultsGroup;
  if(group&&!state.settingsLoading)defaultedPeripheralProfiles.set(`${activeGpioBoardProfile()}:${group}:${field.dataset.peripheralDefaultsIndex}`,'none');
},true);
let assigningPeripheralDefaults = false;
function assignPeripheralDefaults(groupKey, profileValue, index, realDefinitions, helperSignals) {
  if(assigningPeripheralDefaults)return;
  const key=`${activeGpioBoardProfile()}:${groupKey}:${index}`,previous=defaultedPeripheralProfiles.get(key);
  if(previous===profileValue)return;
  defaultedPeripheralProfiles.set(key,profileValue);
  if(profileValue==='none')return;
  assigningPeripheralDefaults=true;
  try{
    const roles=gpioConfigRoleState(),own=new Set(realDefinitions.map(definition=>definition.key));
    for(const signal of helperSignals)own.add(`ui.${groupKey}.${index}.${signal.toLowerCase()}`);
    const occupied=occupiedPeripheralPins({roles:roles.roleToPin,ownRoles:own,bindings:state.peripheralHelperBindings,ownSlot:`${groupKey}:${index}`});
    if(Number(state.settings?.battery?.chargingSensePin)>0)occupied.add(Number(state.settings.battery.chargingSensePin));
    const blocked=motorUnsafePins();
    const layout=GPIO_BOARD_LAYOUTS[activeGpioBoardProfile()];
    const exposed=new Set([...(layout?.left||[]),...(layout?.right||[])].filter(entry=>entry.pin!==null&&entry.pin!==undefined).map(entry=>Number(entry.pin)));
    const selectPin=(candidates,preferred)=>freePeripheralPin({candidates:candidates.filter(pin=>exposed.has(Number(pin))),occupied,blocked,preferred});
    for(const definition of realDefinitions){
      const field=definition.element;if(!field||/reset|trigger/i.test(definition.key))continue;
      const current=String(field.value||'');
      // Saved assignments stay intact; newly enabled peripherals receive free defaults.
      if((previous===undefined||state.settingsLoading)&&current!==''&&Number(current)>=0){occupied.add(Number(current));continue;}
      const output=definition.key!=='sd.misoPin';
      const value=selectPin(validBoardPins(output),current);
      if(value===''){
        if(![...field.options].some(option=>option.value==='-1'))field.prepend(new Option('No supported free GPIOs','-1'));
        field.value='-1';
      }else{field.value=value;occupied.add(Number(value));}
    }
    for(const signal of helperSignals){
      if(['CONTACT','SOURCE','MAIN_CONTROL','INPUT_VOLTAGE','OUTPUT_VOLTAGE'].includes(signal))continue;
      const current=peripheralHelperBindingValue(groupKey,index,signal);
      if(current!==''&&(previous===undefined||state.settingsLoading)){occupied.add(Number(current));continue;}
      const options=helperBindingSignalOptionsFor(groupKey,index,signal);
      const value=selectPin(options.map(option=>option.value),current);
      if(value!==''){
        if(groupKey==='sensor'&&profileValue===BATTERY_DIVIDER_SENSOR_PROFILE)elements.batteryAdcPin.value=value;
        else setPeripheralHelperBindingValue(groupKey,index,signal,value);
        occupied.add(Number(value));
      }
    }
  }finally{assigningPeripheralDefaults=false;}
}

function renderPeripheralSelectionBindingGroup(container, groupKey, profileValue, index = 0) {
  if (!container) {
    return;
  }

  if (groupKey === "storage" && [elements.sdCsPin, elements.sdSckPin, elements.sdMosiPin, elements.sdMisoPin].some((field) => field && field.options.length === 0)) {
    populateSdPinOptions(state.settings, false);
  }
  if (groupKey === "display" && elements.wapeTriggerPin && elements.wapeTriggerPin.options.length === 0) {
    populateWapeTriggerPinOptions(state.settings);
  }

  container.dataset.peripheralBindingGroup = groupKey;
  container.dataset.peripheralBindingIndex = String(index);
  container.dataset.peripheralBindingProfile = String(profileValue || "none");
  container.innerHTML = "";

  const realDefinitions = realPeripheralBindingDefinitions(groupKey, profileValue, state.settings || {}, index);
  const helperSignalsForProfile = realDefinitions.length > 0 && ["audio", "display", "storage"].includes(groupKey)
    ? []
    : helperSignalLabels(groupKey, profileValue).filter((signalLabel) => !realDefinitions.some((definition) => definition.label.replace(/^OLED\s+/i, "").replace(/^I2S\s+/i, "") === signalLabel));

  assignPeripheralDefaults(groupKey,profileValue,index,realDefinitions,helperSignalsForProfile);
  container.hidden = realDefinitions.length === 0 && helperSignalsForProfile.length === 0;
  if (container.hidden) {
    return;
  }

  for (const definition of realDefinitions) {
    appendRealPeripheralBindingControl(container, definition);
  }
  for (const signalLabel of helperSignalsForProfile) {
    appendHelperPeripheralBindingControl(container, groupKey, index, signalLabel);
  }
}

function buildPeripheralSelectionBindingGroup(groupKey, profileValue, index = 0) {
  const container = document.createElement("div");
  container.className = "peripheral-binding-group";
  renderPeripheralSelectionBindingGroup(container, groupKey, profileValue, index);
  return container;
}

function peripheralProfileInstanceLabel(baseLabel, index, total) {
  return total > 1 ? `${baseLabel} ${index + 1}` : "";
}

function updatePrimaryPeripheralIndexLabel(element, baseLabel, total) {
  if (!element) {
    return;
  }
  const labelText = peripheralProfileInstanceLabel(baseLabel, 0, total);
  element.textContent = labelText;
  element.hidden = !labelText;
}

function buildPeripheralProfileComposite(groupKey, selectedValue, index, select, rowLabel = "") {
  select.dataset.peripheralDefaultsGroup=groupKey;select.dataset.peripheralDefaultsIndex=String(index);
  const stack = document.createElement("div");
  stack.className = "peripheral-profile-stack";
  ensureDynamicFieldIdentity(
    select,
    peripheralDynamicFieldId(groupKey, index, "profile"),
    peripheralDynamicFieldName(groupKey, index, "profile"),
  );
  const marksFactoryResetTouch = groupKey === "input"
    && isTouchPeripheralInputProfile(selectedValue)
    && index === factoryResetTouchInputIndex();
  if (rowLabel || marksFactoryResetTouch) {
    const heading = document.createElement("div");
    heading.className = "peripheral-profile-heading";
    if (rowLabel) {
      const label = document.createElement("div");
      label.className = "peripheral-profile-index-label";
      label.id = `${select.id}-label`;
      label.textContent = rowLabel;
      heading.appendChild(label);
      select.setAttribute("aria-labelledby", label.id);
    }
    if (marksFactoryResetTouch) {
      const marker = document.createElement("span");
      marker.className = "peripheral-factory-reset-marker";
      marker.textContent = "10s factory reset";
      marker.title = "This is the factory-reset touch input. Touch and hold it for 10 seconds to factory reset the device.";
      marker.setAttribute("aria-label", marker.title);
      heading.appendChild(marker);
    }
    stack.appendChild(heading);
  }

  const composite = document.createElement("div");
  composite.className = "peripheral-field-composite";
  composite.appendChild(select);
  if (selectedValue !== "none") {
    composite.appendChild(buildPeripheralSelectionBindingGroup(groupKey, selectedValue, index));
  }
  stack.appendChild(composite);
  if(groupKey==='sensor'&&selectedValue===BATTERY_DIVIDER_SENSOR_PROFILE){
    stack.append(createVoltageDividerControls({battery:state.settings?.battery,onChange:(values,resistorsChanged)=>{
      state.settings ||= {};state.settings.battery={...state.settings.battery,...values};
      if(resistorsChanged){const multiplier=voltageDividerMaximum(values).ratio;state.settings.battery.calibrationMultiplier=multiplier;state.settings.battery.measuredVoltage=0;state.batteryMeasuredVoltageInput='';if(elements.batteryMeasuredVoltage)elements.batteryMeasuredVoltage.value='';const field=elements.settingsForm.elements.namedItem('battery.calibrationMultiplier');if(field)field.value=String(multiplier);}
      renderPeripheralDiagram();queueSettingsSave(150);
    }}));
  }
  return stack;
}

function buildPeripheralActionButton({ addDatasetKey, removeDatasetKey, index, total, maxCount, singularLabel }) {
  const actionButton = document.createElement("button");
  actionButton.type = "button";
  actionButton.className = `peripheral-profile-action ${index === 0 ? "peripheral-profile-action-add" : "peripheral-profile-action-remove"}`;

  if (index === 0) {
    actionButton.textContent = "+";
    actionButton.dataset[addDatasetKey] = "true";
    actionButton.title = total >= maxCount ? `Maximum of ${maxCount} ${singularLabel}s reached` : `Add another ${singularLabel}`;
    actionButton.setAttribute("aria-label", actionButton.title);
    actionButton.disabled = total >= maxCount;
  } else {
    actionButton.textContent = "-";
    actionButton.dataset[removeDatasetKey] = String(index);
    actionButton.title = `Remove ${singularLabel} ${index + 1}`;
    actionButton.setAttribute("aria-label", actionButton.title);
  }

  return actionButton;
}

function peripheralBindingDefinitions(groupKey, settings = state.settings) {
  switch (groupKey) {
    case "audio":
      return gpioConfigRoleDefinitions(settings).filter((definition) => ["audio.wsPin", "audio.bclkPin", "audio.doutPin"].includes(definition.key));
    case "display": {
      const definitions = gpioConfigRoleDefinitions(settings);
      return definitions.filter((definition) => ["oled.sdaPin", "oled.sclPin", "oled.resetPin", "oled.wapeTriggerPin"].includes(definition.key));
    }
    case "storage":
      return gpioConfigRoleDefinitions(settings).filter((definition) => ["sd.csPin", "sd.sckPin", "sd.mosiPin", "sd.misoPin"].includes(definition.key));
    default:
      return [];
  }
}

function buildPeripheralBindingGroup(groupKey) {
  const container = document.createElement("div");
  container.className = "peripheral-binding-group";
  renderPeripheralBindingGroup(container, groupKey);
  return container;
}

function renderPeripheralBindingGroup(container, groupKey) {
  if (!container) {
    return;
  }

  if (groupKey === "storage" && [elements.sdCsPin, elements.sdSckPin, elements.sdMosiPin, elements.sdMisoPin].some((field) => field && field.options.length === 0)) {
    populateSdPinOptions(state.settings, false);
  }

  const definitions = peripheralBindingDefinitions(groupKey, state.settings || {});
  container.innerHTML = "";
  container.hidden = definitions.length === 0;
  if (!definitions.length) {
    return;
  }

  for (const definition of definitions) {
    const control = document.createElement("label");
    control.className = "peripheral-binding-control";

    const label = document.createElement("span");
    label.className = "peripheral-binding-label";
    label.textContent = definition.label.replace(/^OLED\s+/i, "").replace(/^I2S\s+/i, "");
    control.appendChild(label);

    const select = document.createElement("select");
    ensureDynamicFieldIdentity(
      select,
      peripheralDynamicFieldId(groupKey, 0, definition.key),
      peripheralDynamicFieldName(groupKey, 0, definition.key),
    );
    select.dataset.peripheralBindingKey = definition.key;
    select.setAttribute("aria-label", `${definition.label} GPIO binding`);
    control.htmlFor = select.id;

    if (definition.element) {
      for (const option of definition.element.options) {
        const cloned = document.createElement("option");
        cloned.value = option.value;
        cloned.textContent = option.textContent;
        cloned.selected = option.selected;
        select.appendChild(cloned);
      }
      select.value = definition.element.value;
    }

    control.appendChild(select);
    container.appendChild(control);
  }
}

function syncPeripheralBindingGroups(options = {}) {
  const force = Boolean(options.force);
  if (!force && isPeripheralUiInteracting()) {
    return;
  }
  state.peripheralAudioProfiles = normalizedPeripheralAudioProfiles();
  state.peripheralAudioInProfiles = normalizedPeripheralAudioInProfiles();
  state.peripheralDisplayProfiles = normalizedPeripheralDisplayProfiles();
  renderPeripheralSelectionBindingGroup(elements.peripheralAudioPins, "audio", elements.peripheralAudioProfile?.value || "none", 0);
  if (elements.peripheralAudioPins) {
    const hideAudioPins = String(elements.peripheralAudioProfile?.value || "none") === "none";
    elements.peripheralAudioPins.hidden = hideAudioPins;
    elements.peripheralAudioPins.style.display = hideAudioPins ? "none" : "";
  }
  renderPeripheralSelectionBindingGroup(elements.peripheralAudioInPins, "audioIn", elements.peripheralAudioInProfile?.value || "none", 0);
  if (elements.peripheralAudioInPins) {
    const hideAudioInPins = String(elements.peripheralAudioInProfile?.value || "none") === "none";
    elements.peripheralAudioInPins.hidden = hideAudioInPins;
    elements.peripheralAudioInPins.style.display = hideAudioInPins ? "none" : "";
  }
  renderPeripheralSelectionBindingGroup(elements.peripheralDisplayPins, "display", elements.peripheralDisplayProfile?.value || "none", 0);
  if (elements.peripheralDisplayPins) {
    const hideDisplayPins = String(elements.peripheralDisplayProfile?.value || "none") === "none";
    elements.peripheralDisplayPins.hidden = hideDisplayPins;
    elements.peripheralDisplayPins.style.display = hideDisplayPins ? "none" : "";
  }
  if (elements.peripheralStorageList) {
    for (const container of elements.peripheralStorageList.querySelectorAll(".peripheral-binding-group[data-peripheral-binding-group]")) {
      renderPeripheralSelectionBindingGroup(
        container,
        container.dataset.peripheralBindingGroup,
        container.dataset.peripheralBindingProfile,
        Number(container.dataset.peripheralBindingIndex || 0),
      );
    }
  }
  if (elements.peripheralAudioOutputsList) {
    for (const container of elements.peripheralAudioOutputsList.querySelectorAll(".peripheral-binding-group[data-peripheral-binding-group]")) {
      renderPeripheralSelectionBindingGroup(
        container,
        container.dataset.peripheralBindingGroup,
        container.dataset.peripheralBindingProfile,
        Number(container.dataset.peripheralBindingIndex || 0),
      );
    }
  }
  for (const listElement of [elements.peripheralAudioInList, elements.peripheralDisplayList]) {
    if (!listElement) {
      continue;
    }
    for (const container of listElement.querySelectorAll(".peripheral-binding-group[data-peripheral-binding-group]")) {
      renderPeripheralSelectionBindingGroup(
        container,
        container.dataset.peripheralBindingGroup,
        container.dataset.peripheralBindingProfile,
        Number(container.dataset.peripheralBindingIndex || 0),
      );
    }
  }
  for (const listSelector of [
    "#peripheralAudioOutputsList",
    "#peripheralAudioInList",
    "#peripheralDisplayList",
    "#peripheralSensorsList",
    "#peripheralInputsList",
    "#peripheralPowerList",
    "#peripheralControlsList",
    "#peripheralExpansionsList",
    "#peripheralCommunicationList",
  ]) {
    for (const container of document.querySelectorAll(`${listSelector} .peripheral-binding-group[data-peripheral-binding-group]`)) {
      renderPeripheralSelectionBindingGroup(
        container,
        container.dataset.peripheralBindingGroup,
        container.dataset.peripheralBindingProfile,
        Number(container.dataset.peripheralBindingIndex || 0),
      );
    }
  }
}

function loadPeripheralDiagramPositions() {
  try {
    const local = normalizePeripheralDiagramPositions(
      window.localStorage.getItem(PERIPHERAL_DIAGRAM_POSITIONS_STORAGE_KEY) || "{}",
    );
    if (Object.keys(local).length) {
      return local;
    }
  } catch {
  }
  return ensureUiSettings().peripheralDiagramPositions;
}

function savePeripheralDiagramPositions(options = {}) {
  const result = configurationSettingsPersistenceModule?.savePeripheralDiagramPositions(
    state.peripheralDiagramPositions,
    options,
  ) || Promise.resolve();
  Promise.resolve(result).finally(() => {
    uiHistoryModule?.scheduleCapture();
  });
  return result;
}

function peripheralDiagramSelectedLabel(element, fallbackValue) {
  return String(element?.selectedOptions?.[0]?.textContent || fallbackValue || "Custom").trim();
}

function normalizePeripheralDiagramSignalLabel(label) {
  return String(label || "")
    .replace(/^OLED\s+/i, "")
    .replace(/^I2S\s+/i, "")
    .replace(/^SD\s+/i, "")
    .replace(/^TX\s*\/\s*/i, "TX ")
    .replace(/^RX\s*\/\s*/i, "RX ")
    .trim();
}

function peripheralDiagramSignalKey(label) {
  return normalizePeripheralDiagramSignalLabel(label).toUpperCase();
}

function isPeripheralDiagramPowerLabel(label) {
  const key = peripheralDiagramSignalKey(label);
  return ["VCC", "VIN", "PWR", "VBUS", "5V", "3V3", "3.3V", "3VO"].includes(key)
    || key.startsWith("VCC ")
    || key.startsWith("VIN ")
    || key.startsWith("5V ")
    || key.startsWith("3V3 ")
    || key.startsWith("3.3V ");
}

function isPeripheralDiagramGroundLabel(label) {
  const key = peripheralDiagramSignalKey(label);
  return ["GND", "GROUND"].includes(key) || key.startsWith("GND ") || key.startsWith("GROUND ");
}

function peripheralDiagramBoardPowerLabel(node, label) {
  const key = peripheralDiagramSignalKey(label);
  if (["GND", "GROUND"].includes(key) || key.startsWith("GND ") || key.startsWith("GROUND ")) {
    return "GND";
  }
  if (["5V", "VIN", "VBUS"].includes(key) || key.startsWith("5V ") || key.startsWith("VIN ")) {
    return "5V";
  }
  if (["3V3", "3.3V", "3VO"].includes(key) || key.startsWith("3V3 ") || key.startsWith("3.3V ")) {
    return "3V3";
  }
  if (!["VCC", "PWR"].includes(key) && !key.startsWith("VCC ")) {
    return null;
  }
  if (String(node?.groupKey || "") === "power") {
    return "5V";
  }
  return ["audio", "control"].includes(String(node?.groupKey || "")) ? "5V" : "3V3";
}

function buildEditablePeripheralLabels(node) {
  if (!node) {
    return [];
  }

  if (node.kind === "board" && Array.isArray(node.boardLabels)) {
    return node.boardLabels.map((entry) => ({ ...entry }));
  }

  const groupKey = String(node.groupKey || "");
  const profileValue = String(node.profileValue || "none");
  const index = Number(node.index || 0);
  const labels = [];
  const used = new Set();
  const realBindings = realPeripheralBindingDefinitions(groupKey, profileValue, state.settings || {}, index);

  for (const binding of realBindings) {
    const pin = Number(binding?.element?.value ?? NaN);
    if (!Number.isFinite(pin) || pin < 0) {
      continue;
    }
    const normalized = normalizePeripheralDiagramSignalLabel(binding.label);
    const key = peripheralDiagramLabelId(normalized);
    if (!normalized || used.has(key)) {
      continue;
    }
    used.add(key);
    labels.push({ id: key, label: normalized });
  }

  for (const helperLabel of helperSignalLabels(groupKey, profileValue)) {
    const normalized = normalizePeripheralDiagramSignalLabel(helperLabel);
    const key = peripheralDiagramLabelId(normalized);
    const pin = Number(peripheralHelperBindingValue(groupKey, index, helperLabel) || NaN);
    if (!normalized || used.has(key) || !Number.isFinite(pin) || pin < 0) {
      continue;
    }
    used.add(key);
    labels.push({ id: key, label: normalized });
  }

  for (const pinLabel of Array.isArray(node.pins) ? node.pins : []) {
    if (!isPeripheralDiagramPowerLabel(pinLabel) && !isPeripheralDiagramGroundLabel(pinLabel)) {
      continue;
    }
    const normalized = normalizePeripheralDiagramSignalLabel(pinLabel);
    const key = peripheralDiagramLabelId(normalized);
    if (!normalized || used.has(key) || !peripheralDiagramBoardPowerLabel(node, pinLabel)) {
      continue;
    }
    used.add(key);
    labels.push({ id: key, label: normalized });
  }

  return labels;
}

function buildEditableBoardLabels(boardProfile = activeGpioBoardProfile()) {
  const profileKey = String(boardProfile || "");
  if (!profileKey) {
    return [];
  }

  const primary = GPIO_BOARD_LAYOUTS[profileKey] || { left: [], right: [] };
  const extra = GPIO_BOARD_EXTRA_LAYOUTS[profileKey] || { left: [], right: [] };
  const labels = [];

  const pushLabels = (entries, side, lane) => {
    const validEntries = entries.filter((entry) => entry && (entry.pin !== undefined || entry.label));
    validEntries.forEach((entry, index) => {
      const label = String(entry.label || (entry.pin != null ? `GPIO${entry.pin}` : "")).trim();
      if (!label) {
        return;
      }
      labels.push({
        id: peripheralDiagramBoardLabelEntryId({ pin: entry.pin, label, side, lane, index }),
        label,
        ...peripheralDiagramBoardLabelDefaultLayout({ side, lane, index, count: validEntries.length }),
        coordinateSpace: "visual",
      });
    });
  };

  pushLabels(primary.left || [], "left", 0);
  pushLabels(primary.right || [], "right", 0);
  pushLabels(extra.left || [], "left", 1);
  pushLabels(extra.right || [], "right", 1);
  return labels;
}

function peripheralDiagramBoardEditorNode() {
  const boardProfile = activeGpioBoardProfile();
  const boardImage = elements.peripheralDiagramBoardImage;
  if (!boardProfile || !boardImage) {
    return null;
  }

  const boardLabels = buildEditableBoardLabels(boardProfile);
  if (!boardLabels.length) {
    return null;
  }

  const boardTitle = String(boardImage.alt || peripheralDiagramSelectedLabel(elements.gpioBoardSelector, boardProfile) || "ESP Board")
    .replace(/\s+in peripheral diagram$/i, "")
    .trim();

  return {
    id: peripheralDiagramBoardNodeId(boardProfile),
    kind: "board",
    boardProfile,
    label: "ESP Board",
    title: boardTitle,
    src: boardImage.getAttribute("src") || boardImage.src,
    visualTransform: boardImage.style.transform || getComputedStyle(boardImage).transform || "",
    pins: [],
    boardLabels,
  };
}

function peripheralDiagramBindingPins(groupKey) {
  return peripheralBindingDefinitions(groupKey, state.settings || {}).map((definition) => {
    const bindingName = String(definition.label || "Pin")
      .replace(/^OLED\s+/i, "")
      .replace(/^I2S\s+/i, "")
      .trim();
    const gpioLabel = String(definition.element?.selectedOptions?.[0]?.textContent || definition.element?.value || "Auto").trim();
    return `${bindingName}: ${gpioLabel}`;
  });
}

function peripheralDiagramTemplatePins(groupKey, profileValue) {
  const profile = String(profileValue || "none");

  switch (groupKey) {
    case "audio":
      if (profile.includes("buzzer")) {
        return ["SIG", "VCC", "GND"];
      }
      return peripheralDiagramBindingPins("audio").length ? peripheralDiagramBindingPins("audio") : ["WS", "BCLK", "DOUT", "VCC", "GND"];
    case "audioIn":
      if (profile.includes("pdm")) {
        return ["CLK", "DATA", "VCC", "GND"];
      }
      if (profile.includes("adc") || profile.includes("line-in") || profile.includes("electret") || profile.includes("max9814") || profile.includes("max4466")) {
        return ["OUT", "VCC", "GND"];
      }
      if (profile.includes("codec") || profile.includes("external-i2s-adc") || profile.includes("es7243") || profile.includes("es7210")) {
        return ["WS", "BCLK", "DOUT", "SDA", "SCL"];
      }
      if (profile.includes("bluetooth")) {
        return ["BT", "PWR"];
      }
      return ["WS", "SCK", "SD", "VCC", "GND"];
    case "display":
      if (profile.includes("waveshare")) {
        return ["CTRL", "VCC", "GND"];
      }
      if (profile.includes("spi-tft")) {
        return ["SCK", "MOSI", "MISO", "CS", "DC", "RST", "BL", "VCC", "GND"];
      }
      return peripheralDiagramBindingPins("display").length ? peripheralDiagramBindingPins("display") : ["SDA", "SCL", "RST", "VCC", "GND"];
    case "storage":
      if (profile.includes("sdmmc")) {
        return ["CLK", "CMD", "D0", "D1", "D2", "D3"];
      }
      return peripheralDiagramBindingPins("storage").length ? peripheralDiagramBindingPins("storage") : ["CS", "SCK", "MOSI", "MISO"];
    case "input":
      if (profile.includes("rotary-encoder")) {
        return ["A", "B", "SW", "VCC", "GND"];
      }
      if (profile.includes("limit-switch")) {
        return ["COM", "NO", "NC"];
      }
      if (profile.includes("joystick")) {
        return ["VRX", "VRY", "SW", "VCC", "GND"];
      }
      if (profile.includes("analog-potentiometer")) {
        return ["OUT", "VCC", "GND"];
      }
      if (profile.includes("keypad")) {
        return ["R1", "R2", "R3", "R4", "C1", "C2", "C3", "C4"];
      }
      if (profile.includes("ir-receiver") || profile.includes("pir") || profile.includes("hall") || profile.includes("flow-meter") || profile.includes("water-leak") || profile.includes("vibration") || profile.includes("rf-433mhz")) {
        return ["OUT", "VCC", "GND"];
      }
      if (profile.includes("esp32-native-touch-pad")) {
        return ["TOUCH"];
      }
      if (profile.includes("button") || profile.includes("switch") || profile.includes("touch")) {
        return ["SIG", "VCC", "GND"];
      }
      return ["SIG", "VCC", "GND"];
    case "sensor":
      if (profile.includes("battery-voltage-divider")) {
        return ["GPIO", "Batt", "GND"];
      }
      if (profile.includes("ds18b20")) {
        return ["DQ", "VCC", "GND"];
      }
      if (profile.includes("bno") || profile.includes("mpu")) {
        return ["SDA", "SCL", "INT", "VCC", "GND"];
      }
      return ["SIG", "VCC", "GND"];
    case "control":
      if (profile.includes("dual-servo")) {
        return ["PWM1", "PWM2", "5V", "GND"];
      }
      if (profile.includes("drv8833")) {
        return ["IN1", "IN2", "IN3", "IN4", "VCC", "GND"];
      }
      if (profile.includes("servo") || profile.includes("buzzer") || profile.includes("vibration")) {
        return ["SIG", "VCC", "GND"];
      }
      if (profile.includes("led-pwm-dimmer")) {
        return ["PWM", "VCC", "GND"];
      }
      if (profile.includes("fan")) {
        return ["PWM", "TACH", "VCC", "GND"];
      }
      if (profile.includes("tb6612") || profile.includes("drv8833") || profile.includes("l298n") || profile.includes("dc-motor-driver-generic")) {
        return ["AIN1", "AIN2", "BIN1", "BIN2", "PWMA", "PWMB", "STBY"];
      }
      if (profile.includes("bts7960")) {
        return ["RPWM", "LPWM", "REN", "LEN", "VCC", "GND"];
      }
      if (profile.includes("stepper")) {
        return ["STEP", "DIR", "EN", "VIO", "GND"];
      }
      if (profile.includes("ws2812")) {
        return ["DIN", "5V", "GND"];
      }
      if (profile.includes("relay") || profile.includes("mosfet") || profile.includes("solenoid") || profile.includes("pump")) {
        return ["IN", "VCC", "GND"];
      }
      return ["IN1", "IN2", "ENA", "VCC", "GND"];
    case "communication":
      if (profile.includes("uart")) {
        return ["TX", "RX", "VCC", "GND"];
      }
      if (profile.includes("rs485")) {
        return ["TX", "RX", "DE", "RE"];
      }
      if (profile.includes("lora")) {
        return ["TX", "RX", "AUX", "M0", "M1"];
      }
      if (profile.includes("i2c")) {
        return ["SDA", "SCL", "VCC", "GND"];
      }
      if (profile.includes("spi")) {
        return ["SCK", "MOSI", "MISO", "CS"];
      }
      return ["SIG1", "SIG2", "VCC", "GND"];
    case "power":
      return ["VCC IN", "GND IN", "VCC OUT", "GND OUT"];
    case "expansion":
      if (profile.includes("74hc595")) {
        return ["SER", "SRCLK", "RCLK", "OE"];
      }
      if (profile.includes("74hc165")) {
        return ["PL", "CP", "Q7", "CE"];
      }
      if (profile.includes("4067")) {
        return ["S0", "S1", "S2", "S3", "SIG", "EN"];
      }
      if (profile.includes("4051")) {
        return ["S0", "S1", "S2", "SIG", "EN"];
      }
      if (profile.includes("mcp3008")) {
        return ["SCK", "MOSI", "MISO", "CS"];
      }
      if (profile.includes("i2c") || profile.includes("mcp23017") || profile.includes("pcf857") || profile.includes("ads") || profile.includes("mcp4725") || profile.includes("pca9685")) {
        return ["SDA", "SCL", "VCC", "GND"];
      }
      return ["BUS", "SIG", "VCC", "GND"];
    default:
      return ["SIG", "VCC", "GND"];
  }
}

function peripheralDiagramSlotStyle(groupKey, index) {
  const slotMap = {
    sensor: [
      "top: 112px; right: 24px;",
      "top: 208px; right: 24px;",
      "top: 112px; right: 154px;",
      "top: 208px; right: 154px;",
    ],
    communication: [
      "top: 112px; left: 18px;",
      "top: 208px; left: 18px;",
      "top: 112px; left: 148px;",
      "top: 208px; left: 148px;",
    ],
    expansion: [
      "bottom: 124px; left: 258px;",
      "bottom: 124px; left: 388px;",
      "bottom: 218px; left: 258px;",
      "bottom: 218px; left: 388px;",
    ],
    control: [
      "bottom: 18px; left: 258px;",
      "bottom: 18px; left: 388px;",
      "bottom: 112px; left: 258px;",
      "bottom: 112px; left: 388px;",
    ],
    storage: [
      "right: 14px; bottom: 28px;",
      "right: 144px; bottom: 28px;",
      "right: 14px; bottom: 150px;",
    ],
    power: [
      "bottom: 18px; left: 18px;",
      "bottom: 18px; left: 148px;",
      "bottom: 112px; left: 18px;",
    ],
  };
  return slotMap[groupKey]?.[index] || "";
}

function peripheralDiagramInlineStyle(node) {
  const savedPosition = state.peripheralDiagramPositions?.[node.id];
  if (savedPosition && Number.isFinite(savedPosition.x) && Number.isFinite(savedPosition.y)) {
    return `left:${savedPosition.x}px; top:${savedPosition.y}px; right:auto; bottom:auto; transform:none;`;
  }
  return node.style || "";
}

function peripheralDiagramRotation(nodeId) {
  const rotation = Number(state.peripheralDiagramPositions?.[nodeId]?.rotation || 0);
  if (!Number.isFinite(rotation)) {
    return 0;
  }
  const normalized = rotation % 360;
  return normalized < 0 ? normalized + 360 : normalized;
}

function peripheralDiagramUsesAsset(node) {
  return Boolean(node.src) && !state.peripheralDiagramAssetFailures?.[node.src];
}

function peripheralDiagramPlaceholderMarkup(node) {
  if (node.groupKey === "sensor" && String(node.profileValue || "").includes("battery-voltage-divider")) {
    return peripheralDiagramBatteryDividerMarkup(node);
  }

  if (node.groupKey === "input" && String(node.profileValue || "").includes("esp32-native-touch-pad")) {
    return peripheralDiagramNativeTouchMarkup(node);
  }

  return `
    <div class="peripheral-diagram-node-block" aria-label="${escapeHtml(node.title || node.label)} placeholder">
      <div class="peripheral-diagram-node-block-title">${escapeHtml(node.title || node.label)}</div>
      <div class="peripheral-diagram-node-block-pins">
        ${node.pins.map((pin) => `<span class="peripheral-diagram-node-block-pin">${escapeHtml(pin)}</span>`).join("")}
      </div>
    </div>
  `;
}

function peripheralDiagramNodeMarkup(node) {
  const styleValue = peripheralDiagramInlineStyle(node);
  const styleAttribute = styleValue ? ` style="${escapeHtml(styleValue)}"` : "";
  const usesAsset = peripheralDiagramUsesAsset(node);
  const rotation = peripheralDiagramRotation(node.id);
  const visualStyle = rotation ? ` style="transform:rotate(${rotation}deg);"` : "";

  if (usesAsset) {
    return `
      <div class="${node.className}" data-node-id="${escapeHtml(node.id)}"${styleAttribute}>
        <button type="button" class="peripheral-diagram-node-edit" data-node-edit="${escapeHtml(node.id)}" aria-label="Edit ${escapeHtml(node.label)} labels" title="Edit labels">Edit</button>
        <button type="button" class="peripheral-diagram-node-rotate" data-node-rotate="${escapeHtml(node.id)}" aria-label="Rotate ${escapeHtml(node.label)} clockwise" title="Rotate 90 degrees clockwise">↻</button>
        <div class="peripheral-diagram-node-visual"${visualStyle}>
          <img src="${escapeHtml(node.src)}" alt="${escapeHtml(node.title || node.label)} module" draggable="false" />
        </div>
        <div class="peripheral-diagram-node-label">${escapeHtml(node.label)}</div>
      </div>
    `;
  }

  return `
    <div class="${node.className}" data-node-id="${escapeHtml(node.id)}"${styleAttribute}>
      <button type="button" class="peripheral-diagram-node-edit" data-node-edit="${escapeHtml(node.id)}" aria-label="Edit ${escapeHtml(node.label)} labels" title="Edit labels">Edit</button>
      <button type="button" class="peripheral-diagram-node-rotate" data-node-rotate="${escapeHtml(node.id)}" aria-label="Rotate ${escapeHtml(node.label)} clockwise" title="Rotate 90 degrees clockwise">↻</button>
      <div class="peripheral-diagram-node-visual"${visualStyle}>
        ${peripheralDiagramPlaceholderMarkup(node)}
      </div>
      <div class="peripheral-diagram-node-label">${escapeHtml(node.label)}</div>
    </div>
  `;
}

function clampPeripheralDiagramPosition(nodeElement, x, y) {
  const stageRect = elements.peripheralDiagramStage?.getBoundingClientRect();
  if (!stageRect) {
    return { x, y };
  }

  const visualElement = nodeElement.querySelector(".peripheral-diagram-node-visual");
  const visualWidth = visualElement?.offsetWidth || nodeElement.offsetWidth;
  const visualHeight = visualElement?.offsetHeight || nodeElement.offsetHeight;
  const horizontalInset = Math.max(0, (nodeElement.offsetWidth - visualWidth) / 2);
  const verticalInset = Math.max(0, (nodeElement.offsetHeight - visualHeight) / 2);

  const minX = -horizontalInset;
  const minY = -verticalInset;
  const maxX = Math.max(minX, stageRect.width - nodeElement.offsetWidth + horizontalInset);
  const maxY = Math.max(minY, stageRect.height - nodeElement.offsetHeight + verticalInset);
  return {
    x: Math.min(Math.max(minX, x), maxX),
    y: Math.min(Math.max(minY, y), maxY),
  };
}

function applyPeripheralDiagramNodePosition(nodeElement, x, y) {
  const clamped = clampPeripheralDiagramPosition(nodeElement, x, y);
  nodeElement.style.left = `${clamped.x}px`;
  nodeElement.style.top = `${clamped.y}px`;
  nodeElement.style.right = "auto";
  nodeElement.style.bottom = "auto";
  nodeElement.style.transform = "none";
  return clamped;
}

function handlePeripheralDiagramPointerDown(event) {
  if (event.button !== 0 || !elements.peripheralDiagramStage) {
    return;
  }

  if (event.target.closest("input, select, textarea, button, a, label")) {
    return;
  }

  if (event.target.closest("[data-node-rotate]") || event.target.closest("[data-node-edit]")) {
    return;
  }

  const nodeElement = event.target.closest(".peripheral-diagram-node[data-node-id]");
  if (!nodeElement) {
    return;
  }

  const stageRect = elements.peripheralDiagramStage.getBoundingClientRect();
  const nodeRect = nodeElement.getBoundingClientRect();
  const nodeId = String(nodeElement.dataset.nodeId || "");
  if (!nodeId) {
    return;
  }

  event.preventDefault();
  const currentState = state.peripheralDiagramPositions?.[nodeId] || {};
  state.peripheralDiagramDrag = {
    nodeId,
    pointerId: event.pointerId,
    offsetX: event.clientX - nodeRect.left,
    offsetY: event.clientY - nodeRect.top,
  };
  nodeElement.classList.add("is-dragging");
  state.peripheralDiagramPositions[nodeId] = {
    ...currentState,
    x: nodeRect.left - stageRect.left,
    y: nodeRect.top - stageRect.top,
  };
  if (typeof nodeElement.setPointerCapture === "function") {
    nodeElement.setPointerCapture(event.pointerId);
  }
}

function handlePeripheralDiagramPointerMove(event) {
  const dragState = state.peripheralDiagramDrag;
  if (!dragState || dragState.pointerId !== event.pointerId || !elements.peripheralDiagramStage) {
    return;
  }

  const nodeElement = elements.peripheralDiagramItems?.querySelector(`[data-node-id="${dragState.nodeId}"]`);
  if (!nodeElement) {
    return;
  }

  const stageRect = elements.peripheralDiagramStage.getBoundingClientRect();
  const position = applyPeripheralDiagramNodePosition(
    nodeElement,
    event.clientX - stageRect.left - dragState.offsetX,
    event.clientY - stageRect.top - dragState.offsetY,
  );
  state.peripheralDiagramPositions[dragState.nodeId] = {
    ...(state.peripheralDiagramPositions?.[dragState.nodeId] || {}),
    ...position,
  };
  renderPeripheralDiagramWiring();
}

function handlePeripheralDiagramPointerUp(event) {
  const dragState = state.peripheralDiagramDrag;
  if (!dragState || dragState.pointerId !== event.pointerId) {
    return;
  }

  const nodeElement = elements.peripheralDiagramItems?.querySelector(`[data-node-id="${dragState.nodeId}"]`);
  nodeElement?.classList.remove("is-dragging");
  savePeripheralDiagramPositions();
  state.peripheralDiagramDrag = null;
  renderPeripheralDiagramWiring();
}

function handlePeripheralDiagramAssetError(event) {
  const image = event.target;
  if (!(image instanceof HTMLImageElement)) {
    return;
  }

  const nodeElement = image.closest(".peripheral-diagram-node[data-node-id]");
  const nodeId = String(nodeElement?.dataset.nodeId || "");
  const node = state.peripheralDiagramNodeMap?.[nodeId];
  if (!node?.src) {
    return;
  }

  state.peripheralDiagramAssetFailures[node.src] = true;
  if (nodeElement) {
    const rotation = peripheralDiagramRotation(node.id);
    const visualStyle = rotation ? ` style="transform:rotate(${rotation}deg);"` : "";
    nodeElement.innerHTML = `<button type="button" class="peripheral-diagram-node-rotate" data-node-rotate="${escapeHtml(node.id)}" aria-label="Rotate ${escapeHtml(node.label)} clockwise" title="Rotate 90 degrees clockwise">↻</button><div class="peripheral-diagram-node-visual"${visualStyle}>${peripheralDiagramPlaceholderMarkup(node)}</div><div class="peripheral-diagram-node-label">${escapeHtml(node.label)}</div>`;
  }
  renderPeripheralDiagramWiring();
}

function rotatePeripheralDiagramNode(nodeId) {
  if (!nodeId) {
    return;
  }

  const current = state.peripheralDiagramPositions?.[nodeId] || {};
  const nextRotation = (peripheralDiagramRotation(nodeId) + 90) % 360;
  const visual = elements.peripheralDiagramItems?.querySelector(`[data-node-id="${nodeId}"] .peripheral-diagram-node-visual`);
  rotatePeripheralDiagramNodeLabels(state, nodeId, {
    deltaDegrees: 90,
    visualWidth: visual?.offsetWidth || visual?.getBoundingClientRect?.().width || 1,
    visualHeight: visual?.offsetHeight || visual?.getBoundingClientRect?.().height || 1,
  });
  state.peripheralDiagramPositions[nodeId] = {
    ...current,
    rotation: nextRotation,
  };
  savePeripheralDiagramPositions();

  if (visual) {
    visual.style.transform = nextRotation ? `rotate(${nextRotation}deg)` : "";
  }
  renderPeripheralDiagramWiring();
}

function peripheralDiagramStageRelativeRect(element, stageRect) {
  if (!element || !stageRect) {
    return null;
  }
  const rect = element.getBoundingClientRect();
  if (!rect.width || !rect.height) {
    return null;
  }
  return {
    left: rect.left - stageRect.left,
    top: rect.top - stageRect.top,
    width: rect.width,
    height: rect.height,
  };
}

function peripheralDiagramControlLabelRects(stageRect) {
  if (!stageRect) {
    return [];
  }
  return [...document.querySelectorAll(".peripheral-diagram-floating-label")]
    .map((element) => peripheralDiagramStageRelativeRect(element, stageRect))
    .filter(Boolean);
}

function peripheralDiagramRectOverlapArea(left, right) {
  const overlapWidth = Math.max(0, Math.min(left.left + left.width, right.left + right.width) - Math.max(left.left, right.left));
  const overlapHeight = Math.max(0, Math.min(left.top + left.height, right.top + right.height) - Math.max(left.top, right.top));
  return overlapWidth * overlapHeight;
}

function peripheralDiagramControlCandidates(ownerRect, isBoard = false) {
  const horizontalWidth = isBoard ? 108 : 116;
  const horizontalHeight = 34;
  const verticalWidth = isBoard ? 52 : 62;
  const verticalHeight = isBoard ? 30 : 72;
  return {
    top: {
      left: ownerRect.left + ((ownerRect.width - horizontalWidth) / 2),
      top: ownerRect.top - (horizontalHeight + 8),
      width: horizontalWidth,
      height: horizontalHeight,
    },
    bottom: {
      left: ownerRect.left + ((ownerRect.width - horizontalWidth) / 2),
      top: ownerRect.top + ownerRect.height + 8,
      width: horizontalWidth,
      height: horizontalHeight,
    },
    left: {
      left: ownerRect.left - (verticalWidth + 10),
      top: ownerRect.top + ((ownerRect.height - verticalHeight) / 2),
      width: verticalWidth,
      height: verticalHeight,
    },
    right: {
      left: ownerRect.left + ownerRect.width + 10,
      top: ownerRect.top + ((ownerRect.height - verticalHeight) / 2),
      width: verticalWidth,
      height: verticalHeight,
    },
  };
}

function choosePeripheralDiagramControlDock(ownerRect, labelRects, stageRect, isBoard = false) {
  if (!ownerRect || !stageRect) {
    return "top";
  }
  const candidates = peripheralDiagramControlCandidates(ownerRect, isBoard);
  const preferredOrder = isBoard ? ["top", "bottom", "right", "left"] : ["top", "right", "left", "bottom"];
  let bestDock = preferredOrder[0];
  let bestScore = Number.POSITIVE_INFINITY;

  preferredOrder.forEach((dock, index) => {
    const candidate = candidates[dock];
    if (!candidate) {
      return;
    }
    const overlapScore = labelRects.reduce((total, rect) => total + peripheralDiagramRectOverlapArea(candidate, rect), 0);
    const outOfBoundsPenalty = (
      Math.max(0, -candidate.left)
      + Math.max(0, -candidate.top)
      + Math.max(0, (candidate.left + candidate.width) - stageRect.width)
      + Math.max(0, (candidate.top + candidate.height) - stageRect.height)
    ) * 200;
    const score = overlapScore + outOfBoundsPenalty + index;
    if (score < bestScore) {
      bestScore = score;
      bestDock = dock;
    }
  });

  return bestDock;
}

function updatePeripheralDiagramControlDocks() {
  const stageRect = elements.peripheralDiagramStage?.getBoundingClientRect();
  if (!stageRect) {
    return;
  }

  const labelRects = peripheralDiagramControlLabelRects(stageRect);
  elements.peripheralDiagramItems?.querySelectorAll(".peripheral-diagram-node[data-node-id]").forEach((nodeElement) => {
    const ownerRect = peripheralDiagramStageRelativeRect(nodeElement, stageRect);
    if (!ownerRect) {
      return;
    }
    nodeElement.dataset.controlDock = choosePeripheralDiagramControlDock(ownerRect, labelRects, stageRect, false);
  });

  const boardShellRect = peripheralDiagramStageRelativeRect(elements.peripheralDiagramBoardShell, stageRect);
  if (boardShellRect && elements.peripheralDiagramBoardShell) {
    elements.peripheralDiagramBoardShell.dataset.controlDock = choosePeripheralDiagramControlDock(boardShellRect, labelRects, stageRect, true);
  }
}

function renderPeripheralDiagramWiring(nodes = Object.values(state.peripheralDiagramNodeMap || {})) {
  peripheralDiagramWiringModule?.render(nodes);
  updatePeripheralDiagramControlDocks();
}

function handlePeripheralDiagramClick(event) {
  const boardEditButton = event.target.closest("[data-board-edit]");
  if (boardEditButton) {
    event.preventDefault();
    peripheralDiagramLabelEditorModule?.open(peripheralDiagramBoardEditorNode());
    return;
  }

  const editButton = event.target.closest("[data-node-edit]");
  if (editButton) {
    event.preventDefault();
    const nodeId = String(editButton.dataset.nodeEdit || "");
    if (nodeId) {
      peripheralDiagramLabelEditorModule?.open(state.peripheralDiagramNodeMap?.[nodeId] || null);
    }
    return;
  }

  const rotateButton = event.target.closest("[data-node-rotate]");
  if (!rotateButton) {
    return;
  }

  event.preventDefault();
  rotatePeripheralDiagramNode(String(rotateButton.dataset.nodeRotate || ""));
}

function peripheralDiagramControlHost(target) {
  return target instanceof Element
    ? target.closest(".peripheral-diagram-node[data-node-id], .peripheral-diagram-board-shell")
    : null;
}

function clearPeripheralDiagramControlHideTimer(host) {
  const timer = peripheralDiagramControlHideTimers.get(host);
  if (timer) {
    window.clearTimeout(timer);
    peripheralDiagramControlHideTimers.delete(host);
  }
}

function showPeripheralDiagramControls(host) {
  if (!host) {
    return;
  }
  clearPeripheralDiagramControlHideTimer(host);
  host.classList.add("controls-visible");
}

function schedulePeripheralDiagramControlHide(host) {
  if (!host) {
    return;
  }
  clearPeripheralDiagramControlHideTimer(host);
  peripheralDiagramControlHideTimers.set(host, window.setTimeout(() => {
    peripheralDiagramControlHideTimers.delete(host);
    if (!host.matches(":hover") && !host.matches(":focus-within")) {
      host.classList.remove("controls-visible");
    }
  }, PERIPHERAL_DIAGRAM_CONTROL_HIDE_DELAY_MS));
}

function handlePeripheralDiagramControlPointerOver(event) {
  showPeripheralDiagramControls(peripheralDiagramControlHost(event.target));
}

function handlePeripheralDiagramControlPointerOut(event) {
  const host = peripheralDiagramControlHost(event.target);
  if (!host) {
    return;
  }
  const nextHost = peripheralDiagramControlHost(event.relatedTarget);
  if (nextHost === host) {
    return;
  }
  schedulePeripheralDiagramControlHide(host);
}

function handlePeripheralDiagramControlFocusIn(event) {
  showPeripheralDiagramControls(peripheralDiagramControlHost(event.target));
}

function handlePeripheralDiagramControlFocusOut(event) {
  const host = peripheralDiagramControlHost(event.target);
  if (!host) {
    return;
  }
  const nextHost = peripheralDiagramControlHost(event.relatedTarget);
  if (nextHost === host) {
    return;
  }
  schedulePeripheralDiagramControlHide(host);
}

function setupPeripheralDiagramInteractions() {
  if (!elements.peripheralDiagramItems || !elements.peripheralDiagramStage || elements.peripheralDiagramItems.dataset.interactionsReady === "true") {
    return;
  }

  elements.peripheralDiagramItems.dataset.interactionsReady = "true";
  elements.peripheralDiagramItems.addEventListener("pointerdown", handlePeripheralDiagramPointerDown);
  elements.peripheralDiagramStage.addEventListener("click", handlePeripheralDiagramClick);
  elements.peripheralDiagramStage.addEventListener("input", (event) => {
    const target = event.target;
    if (!(target instanceof HTMLInputElement) || target.dataset.touchSensitivityIndex === undefined) {
      return;
    }
    syncTouchSensitivityLabels(target.dataset.touchSensitivityIndex, target.value);
  });
  elements.peripheralDiagramStage.addEventListener("change", (event) => {
    const target = event.target;
    if (!(target instanceof HTMLInputElement) || target.dataset.touchSensitivityIndex === undefined) {
      return;
    }
    const inputIndex = Number(target.dataset.touchSensitivityIndex || 0);
    setPeripheralHelperBindingValue("input", inputIndex, "SENSITIVITY", String(target.value || 55));
    syncTouchSensitivityLabels(inputIndex, target.value);
    renderPeripheralDiagram();
    queueSettingsSave(0);
  });
  elements.peripheralDiagramStage.addEventListener("pointerover", handlePeripheralDiagramControlPointerOver);
  elements.peripheralDiagramStage.addEventListener("pointerout", handlePeripheralDiagramControlPointerOut);
  elements.peripheralDiagramStage.addEventListener("focusin", handlePeripheralDiagramControlFocusIn);
  elements.peripheralDiagramStage.addEventListener("focusout", handlePeripheralDiagramControlFocusOut);
  elements.peripheralDiagramItems.addEventListener("error", handlePeripheralDiagramAssetError, true);
  document.addEventListener("pointermove", handlePeripheralDiagramPointerMove);
  document.addEventListener("pointerup", handlePeripheralDiagramPointerUp);
  document.addEventListener("pointercancel", handlePeripheralDiagramPointerUp);
}

function renderPeripheralDiagram() {
  if (!elements.peripheralDiagramItems) {
    return;
  }

  const nodes = [];
  const audioProfiles = normalizedPeripheralAudioProfiles();
  const audioInProfiles = normalizedPeripheralAudioInProfiles();
  const displayProfiles = normalizedPeripheralDisplayProfiles();
  const sensorProfiles = Array.isArray(state.peripheralSensorProfiles) ? state.peripheralSensorProfiles : [];
  const storageProfiles = Array.isArray(state.peripheralStorageProfiles) ? state.peripheralStorageProfiles : [];
  const powerProfiles = Array.isArray(state.peripheralPowerProfiles) ? state.peripheralPowerProfiles : [];
  const inputProfiles = Array.isArray(state.peripheralInputProfiles) ? state.peripheralInputProfiles : [];
  const controlProfiles = Array.isArray(state.peripheralControlProfiles) ? state.peripheralControlProfiles : [];
  const communicationProfiles = Array.isArray(state.peripheralCommunicationProfiles) ? state.peripheralCommunicationProfiles : [];
  const expansionProfiles = Array.isArray(state.peripheralExpansionProfiles) ? state.peripheralExpansionProfiles : [];

  audioProfiles.slice(0, MAX_PERIPHERAL_AUDIO_OUTPUTS).forEach((profile, index) => {
    const normalizedProfile = String(profile || "none");
    if (normalizedProfile === "none") {
      return;
    }
    const audioAsset = PERIPHERAL_DIAGRAM_ASSET_MAP.audio[normalizedProfile];
    const audioPins = peripheralDiagramTemplatePins("audio", normalizedProfile);
    const label = index === 0 ? "Audio Out" : `Audio Out ${index + 1}`;
    const title = index === 0
      ? peripheralDiagramSelectedLabel(elements.peripheralAudioProfile, "Audio Out")
      : peripheralDiagramOptionLabel(PERIPHERAL_AUDIO_PROFILE_OPTIONS, normalizedProfile, label);
    const nodeId = index === 0 ? "audio-out" : `audio-out-${index}`;
    nodes.push(audioAsset
      ? { id: nodeId, className: "peripheral-diagram-node peripheral-diagram-node-audio", groupKey: "audio", index, profileValue: normalizedProfile, label, title, pins: audioPins, ...audioAsset }
      : {
          id: nodeId,
          className: "peripheral-diagram-node peripheral-diagram-node-audio",
          groupKey: "audio",
          index,
          profileValue: normalizedProfile,
          label,
          title,
          pins: audioPins,
        });
  });

  audioInProfiles.slice(0, MAX_PERIPHERAL_AUDIO_INPUTS).forEach((profile, index) => {
    const normalizedProfile = String(profile || "none");
    if (normalizedProfile === "none") {
      return;
    }
    const audioInAsset = PERIPHERAL_DIAGRAM_ASSET_MAP.audioIn[normalizedProfile];
    const audioInPins = peripheralDiagramTemplatePins("audioIn", normalizedProfile);
    const label = index === 0 ? "Audio In" : `Audio In ${index + 1}`;
    const title = index === 0
      ? peripheralDiagramSelectedLabel(elements.peripheralAudioInProfile, "Audio In")
      : peripheralDiagramOptionLabel(PERIPHERAL_AUDIO_IN_PROFILE_OPTIONS, normalizedProfile, label);
    const nodeId = index === 0 ? "audio-in" : `audio-in-${index}`;
    nodes.push(audioInAsset
      ? { id: nodeId, className: "peripheral-diagram-node peripheral-diagram-node-audio-in", groupKey: "audioIn", index, profileValue: normalizedProfile, label, title, pins: audioInPins, ...audioInAsset }
      : {
          id: nodeId,
          className: "peripheral-diagram-node peripheral-diagram-node-audio-in",
          groupKey: "audioIn",
          index,
          profileValue: normalizedProfile,
          label,
          title,
          pins: audioInPins,
        });
  });

  displayProfiles.slice(0, MAX_PERIPHERAL_DISPLAYS).forEach((profile, index) => {
    const normalizedProfile = String(profile || "none");
    if (normalizedProfile === "none") {
      return;
    }
    const displayAsset = PERIPHERAL_DIAGRAM_ASSET_MAP.display[normalizedProfile];
    const displayPins = peripheralDiagramTemplatePins("display", normalizedProfile);
    const label = index === 0 ? "Display" : `Display ${index + 1}`;
    const title = index === 0
      ? peripheralDiagramSelectedLabel(elements.peripheralDisplayProfile, "Display")
      : peripheralDiagramOptionLabel(PERIPHERAL_DISPLAY_PROFILE_OPTIONS, normalizedProfile, label);
    const nodeId = index === 0 ? "display" : `display-${index}`;
    nodes.push(displayAsset
      ? { id: nodeId, className: "peripheral-diagram-node peripheral-diagram-node-display", groupKey: "display", index, profileValue: normalizedProfile, label, title, pins: displayPins, ...displayAsset }
      : {
          id: nodeId,
          className: "peripheral-diagram-node peripheral-diagram-node-display",
          groupKey: "display",
          index,
          profileValue: normalizedProfile,
          label,
          title,
          pins: displayPins,
        });
  });

  storageProfiles.slice(0, 3).forEach((profile, index) => {
    const normalizedProfile = String(profile || "none");
    if (normalizedProfile === "none") {
      return;
    }
    const storageAsset = PERIPHERAL_DIAGRAM_ASSET_MAP.storage[normalizedProfile];
    const baseNode = {
      id: `storage-${index}`,
      className: index === 0 ? "peripheral-diagram-node peripheral-diagram-node-storage" : "peripheral-diagram-node",
      style: index === 0 ? "" : peripheralDiagramSlotStyle("storage", index),
      groupKey: "storage",
      index,
      profileValue: normalizedProfile,
      label: storageProfiles.length > 1 ? `Storage ${index + 1}` : "Storage",
      title: peripheralDiagramOptionLabel(PERIPHERAL_STORAGE_PROFILE_OPTIONS, normalizedProfile, "Storage"),
      pins: peripheralDiagramTemplatePins("storage", normalizedProfile),
    };
    nodes.push(storageAsset
      ? { ...baseNode, src: storageAsset.src }
      : baseNode);
  });

  powerProfiles.slice(0, MAX_PERIPHERAL_POWERS).forEach((profile, index) => {
    const normalizedProfile = String(profile || "none");
    if (normalizedProfile === "none") {
      return;
    }
    const powerAsset = PERIPHERAL_DIAGRAM_ASSET_MAP.power[normalizedProfile];
    const baseNode = {
      id: `power-${index}`,
      className: "peripheral-diagram-node peripheral-diagram-node-power",
      style: peripheralDiagramSlotStyle("power", index),
      groupKey: "power",
      index,
      profileValue: normalizedProfile,
      label: powerProfiles.length > 1 ? `Power ${index + 1}` : "Power",
      title: peripheralDiagramOptionLabel(PERIPHERAL_POWER_PROFILE_OPTIONS, normalizedProfile, `Power ${index + 1}`),
      pins: peripheralDiagramTemplatePins("power", normalizedProfile),
    };
    nodes.push(powerAsset
      ? { ...baseNode, src: powerAsset.src }
      : baseNode);
  });

  inputProfiles.slice(0, 4).forEach((profile, index) => {
    const normalizedProfile = String(profile || "none");
    if (normalizedProfile === "none") {
      return;
    }
    const inputAsset = PERIPHERAL_DIAGRAM_ASSET_MAP.input[normalizedProfile];
    const baseNode = {
      id: `input-${index}`,
      className: `peripheral-diagram-node peripheral-diagram-node-input-${index}`,
      groupKey: "input",
      index,
      profileValue: normalizedProfile,
      label: inputProfiles.length > 1 ? `Input ${index + 1}` : "Input",
      title: peripheralDiagramOptionLabel(PERIPHERAL_INPUT_PROFILE_OPTIONS, normalizedProfile, `Input ${index + 1}`),
      pins: peripheralDiagramTemplatePins("input", normalizedProfile),
    };
    nodes.push(inputAsset
      ? { ...baseNode, src: inputAsset.src }
      : baseNode);
  });

  sensorProfiles.slice(0, 4).forEach((profile, index) => {
    const normalizedProfile = String(profile || "none");
    if (normalizedProfile === "none") {
      return;
    }
    nodes.push({
      id: `sensor-${index}`,
      className: "peripheral-diagram-node",
      style: peripheralDiagramSlotStyle("sensor", index),
      groupKey: "sensor",
      index,
      profileValue: normalizedProfile,
      label: sensorProfiles.length > 1 ? `Sensor ${index + 1}` : "Sensor",
      title: peripheralDiagramOptionLabel(PERIPHERAL_SENSOR_PROFILE_OPTIONS, normalizedProfile, `Sensor ${index + 1}`),
      pins: peripheralDiagramTemplatePins("sensor", normalizedProfile),
    });
  });

  communicationProfiles.slice(0, 4).forEach((profile, index) => {
    const normalizedProfile = String(profile || "none");
    if (normalizedProfile === "none") {
      return;
    }
    nodes.push({
      id: `communication-${index}`,
      className: "peripheral-diagram-node",
      style: peripheralDiagramSlotStyle("communication", index),
      groupKey: "communication",
      index,
      profileValue: normalizedProfile,
      label: communicationProfiles.length > 1 ? `Comm ${index + 1}` : "Comm",
      title: peripheralDiagramOptionLabel(PERIPHERAL_COMMUNICATION_PROFILE_OPTIONS, normalizedProfile, `Comm ${index + 1}`),
      pins: peripheralDiagramTemplatePins("communication", normalizedProfile),
    });
  });

  expansionProfiles.slice(0, 4).forEach((profile, index) => {
    const normalizedProfile = String(profile || "none");
    if (normalizedProfile === "none") {
      return;
    }
    nodes.push({
      id: `expansion-${index}`,
      className: "peripheral-diagram-node",
      style: peripheralDiagramSlotStyle("expansion", index),
      groupKey: "expansion",
      index,
      profileValue: normalizedProfile,
      label: expansionProfiles.length > 1 ? `Expansion ${index + 1}` : "Expansion",
      title: peripheralDiagramOptionLabel(PERIPHERAL_EXPANSION_PROFILE_OPTIONS, normalizedProfile, `Expansion ${index + 1}`),
      pins: peripheralDiagramTemplatePins("expansion", normalizedProfile),
    });
  });

  controlProfiles.slice(0, 4).forEach((profile, index) => {
    const normalizedProfile = String(profile || "none");
    if (normalizedProfile === "none") {
      return;
    }
    const controlAsset = PERIPHERAL_DIAGRAM_ASSET_MAP.control?.[normalizedProfile];
    const baseNode = {
      id: `control-${index}`,
      className: "peripheral-diagram-node peripheral-diagram-node-control",
      style: peripheralDiagramSlotStyle("control", index),
      groupKey: "control",
      index,
      profileValue: normalizedProfile,
      label: controlProfiles.length > 1 ? `Control ${index + 1}` : "Control",
      title: peripheralDiagramOptionLabel(PERIPHERAL_CONTROL_PROFILE_OPTIONS, normalizedProfile, `Control ${index + 1}`),
      pins: peripheralDiagramTemplatePins("control", normalizedProfile),
    };
    nodes.push(controlAsset
      ? {
          ...baseNode,
          ...controlAsset,
          className: controlAsset.className || baseNode.className,
        }
      : baseNode);
  });

  state.peripheralDiagramNodeMap = Object.fromEntries(nodes.map((node) => [node.id, node]));
  elements.peripheralDiagramItems.innerHTML = nodes.map((node) => peripheralDiagramNodeMarkup(node)).join("");
  if (elements.peripheralDiagramBoardEdit) {
    const boardNode = peripheralDiagramBoardEditorNode();
    elements.peripheralDiagramBoardEdit.hidden = !boardNode;
    elements.peripheralDiagramBoardEdit.setAttribute("aria-label", `Edit ${boardNode?.title || "ESP board"} labels`);
    elements.peripheralDiagramBoardEdit.title = "Edit labels";
  }
  if (elements.peripheralDiagramPlaceholderText) {
    elements.peripheralDiagramPlaceholderText.hidden = nodes.length > 0;
  }
  updateConfiguredFeatureVisibility();
  renderPeripheralDiagramWiring(nodes);
}

function renderPeripheralAudioOutputControls() {
  configurationPeripheralsTab?.renderPeripheralAudioOutputControls();
  normalizeOwnedFormControlIds();
}

function renderPeripheralAudioInControls() {
  configurationPeripheralsTab?.renderPeripheralAudioInControls();
  normalizeOwnedFormControlIds();
}

function renderPeripheralDisplayControls() {
  displayTab?.renderPeripheralDisplayControls();
  normalizeOwnedFormControlIds();
}

function renderPeripheralSensorControls() {
  configurationPeripheralsTab?.renderPeripheralSensorControls();
  normalizeOwnedFormControlIds();
}

function renderPeripheralInputControls() {
  configurationPeripheralsTab?.renderPeripheralInputControls();
  normalizeOwnedFormControlIds();
  motorTab?.render();
}

function renderPeripheralStorageControls() {
  configurationPeripheralsTab?.renderPeripheralStorageControls();
  normalizeOwnedFormControlIds();
}

function renderPeripheralPowerControls() {
  configurationPeripheralsTab?.renderPeripheralPowerControls();
  normalizeOwnedFormControlIds();
}

function applyPeripheralStorageProfileSelection(storageIndex, value) {
  configurationPeripheralsTab?.applyPeripheralStorageProfileSelection(storageIndex, value);
}

function renderPeripheralControlControls() {
  configurationPeripheralsTab?.renderPeripheralControlControls();
  normalizeOwnedFormControlIds();
  motorTab?.render();
}

function renderPeripheralExpansionControls() {
  configurationPeripheralsTab?.renderPeripheralExpansionControls();
  normalizeOwnedFormControlIds();
}

function renderPeripheralCommunicationControls() {
  configurationPeripheralsTab?.renderPeripheralCommunicationControls();
  normalizeOwnedFormControlIds();
}

function defaultButtonActionForField(fieldName) {
  return fieldName === "device.button1Action" ? "previous" : "next";
}

function primaryAudioOutputUsesI2s() {
  const profile = String(
    elements.peripheralAudioProfile?.value
    || normalizedPeripheralAudioProfiles()[0]
    || "none",
  ).trim().toLowerCase();
  return profile.includes("i2s") || profile.includes("audio-codec");
}

function currentI2sPins() {
  if (!primaryAudioOutputUsesI2s()) {
    return [];
  }
  return [
    Number(elements.audioWsPin?.value || state.settings?.audio?.wsPin || 0),
    Number(elements.audioBclkPin?.value || state.settings?.audio?.bclkPin || 0),
    Number(elements.audioDoutPin?.value || state.settings?.audio?.doutPin || 0),
  ].filter((pin) => Number.isFinite(pin));
}

function hasDistinctI2sPins() {
  const pins = currentI2sPins().filter((pin) => pin >= 0);
  return pins.length === 3 && new Set(pins).size === 3;
}

function isPlaybackActive(status = state.status) {
  const playbackState = String(status?.playback?.state || "idle");
  return playbackState === "playing" || playbackState === "buffering";
}

function currentPlaybackHeroTitle(status = state.status) {
  return playbackStatusModule?.currentPlaybackHeroTitle(status) || "No station selected";
}

function updatePlaybackHeroControls() {
  playbackStatusModule?.updatePlaybackHeroControls();
}

async function stepRadioStationSelection(delta) {
  await playbackStatusModule?.stepRadioStationSelection(delta);
}

function populateAudioI2sPinOptions(settings = state.settings) {
  const audioFields = [elements.audioWsPin, elements.audioBclkPin, elements.audioDoutPin];
  if (audioFields.some((field) => !field)) {
    return;
  }

  const selectedPins = {
    ws: String(elements.audioWsPin.value || settings?.audio?.wsPin || DEFAULT_ESP32S3_AUDIO_PINS.ws),
    bclk: String(elements.audioBclkPin.value || settings?.audio?.bclkPin || DEFAULT_ESP32S3_AUDIO_PINS.bclk),
    dout: String(elements.audioDoutPin.value || settings?.audio?.doutPin || DEFAULT_ESP32S3_AUDIO_PINS.dout),
  };

  for (const [field, selected] of [[elements.audioWsPin, selectedPins.ws], [elements.audioBclkPin, selectedPins.bclk], [elements.audioDoutPin, selectedPins.dout]]) {
    fillBoardPinSelect(field, selected, true);
  }
}

function validBoardPins(output = false) {
  return chipPins(activeChipFamily(), output, activeGpioBoardProfile());
}

function fillBoardPinSelect(field, selected, output = true, blocked = new Set(), disabledValue = null) {
  const choices = pinChoices(validBoardPins(output), selected === String(disabledValue) ? "" : selected, blocked);
  field.innerHTML = "";
  if (disabledValue !== null) field.append(new Option("Disabled", String(disabledValue)));
  for (const choice of choices) {
    const option = new Option(choice.label, choice.value);
    option.disabled = choice.disabled;
    field.append(option);
  }
  field.value = String(selected ?? disabledValue ?? "");
}

function boardPinAssignmentIssues() {
  const inputs = new Set(validBoardPins(false));
  const outputs = new Set(validBoardPins(true));
  const issues = [];
  for (const [pin, roles] of gpioRoleMap(state.settings, state.status)) {
    if (!inputs.has(pin)) issues.push(`GPIO${pin}: ${roles.join(" / ")} — invalid or reserved on the selected board`);
  }
  const roleState = gpioConfigRoleState(state.settings);
  for (const definition of roleState.definitions) {
    const pin = roleState.roleToPin.get(definition.key);
    const input = definition.key.startsWith("battery.") || definition.key.startsWith("ui.input.") || definition.key === "sd.misoPin";
    if (pin !== null && !input && inputs.has(pin) && !outputs.has(pin)) issues.push(`GPIO${pin}: ${definition.label} requires an output-capable pin`);
  }
  return [...new Set(issues)];
}

function renderBoardPinWarnings() {
  const panel = document.getElementById("gpioAssignmentWarnings");
  if (!panel) return;
  const issues = boardPinAssignmentIssues();
  panel.hidden = true;
  panel.textContent = "";
}

function validateBoardPinAssignments() {
  const issues = boardPinAssignmentIssues();
  if (issues.length) throw new Error(issues.join("; "));
}

function populateBatteryAdcPinOptions(settings = state.settings) {
  if (!elements.batteryAdcPin) {
    return;
  }

  const selectedPin = String(elements.batteryAdcPin.value || settings?.battery?.adcPin || "0");
  const selectedPinNumber = Number(selectedPin || 0);
  const reservedPins = new Set(currentI2sPins());
  const statusLedPin = Number(elements.statusLedPin?.value || settings?.device?.statusLedPin || 0);
  if (Number.isFinite(statusLedPin) && statusLedPin > 0) {
    reservedPins.add(statusLedPin);
  }
  for (const pin of currentSdPins(settings)) {
    reservedPins.add(pin);
  }
  const adcPins = batteryAdcCapablePins()
    .filter((pin) => !reservedPins.has(pin) || pin === selectedPinNumber);

  elements.batteryAdcPin.innerHTML = "";

  const noneOption = document.createElement("option");
  noneOption.value = "0";
  noneOption.textContent = "None";
  elements.batteryAdcPin.append(noneOption);

  for (const pin of adcPins) {
    const option = document.createElement("option");
    option.value = String(pin);
    option.textContent = `GPIO${pin}`;
    elements.batteryAdcPin.append(option);
  }

  if ([...elements.batteryAdcPin.options].some((option) => option.value === selectedPin)) {
    elements.batteryAdcPin.value = selectedPin;
  } else {
    elements.batteryAdcPin.value = "0";
  }
}

function availableStatusLedPins() {
  return validBoardPins(true);
}

function activeGpioBoardProfile(status = state.status) {
  const autodetectEnabled = Boolean(elements.gpioBoardAutodetect?.checked ?? true);
  const selectedBoard = String(elements.gpioBoardSelector?.value || "");
  if (!autodetectEnabled && selectedBoard && GPIO_BOARD_LAYOUTS[selectedBoard]) {
    return selectedBoard;
  }
  return detectGpioBoardProfile(status);
}

function saveGpioBoardPreferences() {
  configurationGpioTab?.saveGpioBoardPreferences();
}

function restoreGpioBoardPreferences() {
  configurationGpioTab?.restoreGpioBoardPreferences();
}

function isGpioUiInteracting() {
  return configurationGpioTab?.isGpioUiInteracting() ?? false;
}

function isTopPeripheralSelect(element) {
  return element instanceof HTMLSelectElement && (
    element === elements.peripheralAudioProfile ||
    element === elements.peripheralAudioInProfile ||
    element === elements.peripheralDisplayProfile ||
    element.matches("[data-peripheral-audio-index]") ||
    element.matches("[data-peripheral-audio-in-index]") ||
    element.matches("[data-peripheral-display-index]") ||
    element.matches("[data-peripheral-sensor-index]") ||
    element.matches("[data-peripheral-input-index]") ||
    element.matches("[data-peripheral-power-index]") ||
    element.matches("[data-peripheral-storage-index]") ||
    element.matches("[data-peripheral-communication-index]") ||
    element.matches("[data-peripheral-control-index]") ||
    element.matches("[data-peripheral-expansion-index]") ||
    element.matches("[data-peripheral-binding-key]") ||
    element.matches("[data-peripheral-helper-signal]")
  );
}

function isPeripheralUiInteracting() {
  if (state.peripheralUiInteractionDepth > 0 || state.peripheralMenuOpen) {
    return true;
  }
  return isTopPeripheralSelect(document.activeElement) || Boolean(document.activeElement?.closest?.(".voltage-divider-controls"));
}

function statusLedRoleLabel(settings = state.settings, status = state.status) {
  const statusLedPin = Number(elements.statusLedPin?.value || settings?.device?.statusLedPin || 0);
  if (statusLedPin === 21) {
    return "Builtin RGB";
  }
  const boardProfile = activeGpioBoardProfile(status);
  if (statusLedPin === 48 && WS_STATUS_LED_BOARD_PROFILES.has(boardProfile)) {
    return "Builtin RGB";
  }
  if (statusLedPin === 10 && WS_STATUS_LED_BOARD_PROFILES.has(boardProfile)) {
    return "WS Status LED";
  }
  return "Status LED";
}

function boardDefaultStatusLedPin(boardProfile = activeGpioBoardProfile()) {
  return WS_STATUS_LED_BOARD_PROFILES.has(String(boardProfile || "").trim().toLowerCase()) ? 48 : 22;
}

function statusLedPinLabel(pin) {
  const chipFamily = String(state.status?.firmware?.chipFamily || "esp32s3").toLowerCase();
  if (chipFamily === "esp32s3") {
    if (pin === 48 && WS_STATUS_LED_BOARD_PROFILES.has(activeGpioBoardProfile())) {
      return "GPIO48 (built-in WS2812 default)";
    }
    if (pin === 21) {
      return "GPIO21 (built-in WS2812)";
    }
    if (pin === 10 && WS_STATUS_LED_BOARD_PROFILES.has(activeGpioBoardProfile())) {
      return "GPIO10 (WS status LED default)";
    }
  }
  return `GPIO${pin}`;
}

function populateStatusLedPinOptions(settings = state.settings) {
  if (!elements.statusLedPin) {
    return;
  }

  const boardProfile = activeGpioBoardProfile();
  const defaultBoardPin = boardDefaultStatusLedPin(boardProfile);
  const persistedPin = Number(settings?.device?.statusLedPin ?? NaN);
  const currentPin = Number(elements.statusLedPin.value || persistedPin || NaN);
  const shouldPreferBoardDefault =
    Boolean(elements.gpioBoardAutodetect?.checked ?? true)
    && WS_STATUS_LED_BOARD_PROFILES.has(boardProfile)
    && (!Number.isFinite(currentPin) || currentPin === 22 || currentPin === 10 || currentPin === 21);
  const selectedPin = String(shouldPreferBoardDefault ? defaultBoardPin : (elements.statusLedPin.value || settings?.device?.statusLedPin || ""));
  const reservedPins = new Set(currentI2sPins());
  for (const pin of currentSdPins(settings)) {
    reservedPins.add(pin);
  }
  fillBoardPinSelect(elements.statusLedPin, selectedPin, true, reservedPins);
}

function chipMaxPin() {
  const chipFamily = activeChipFamily();
  return chipFamily === "esp32" ? 39 : 48;
}

function currentButtonPins() {
  const chipFamily = activeChipFamily();
  return chipFamily === "esp32" ? [5, 18] : [5, 6];
}

function normalizedInputProfileValue(index) {
  const profiles = normalizedPeripheralInputProfiles();
  const fallbackProfile = "none";
  const profileValue = String(profiles[index] || fallbackProfile).trim().toLowerCase();
  return profileValue || fallbackProfile;
}

function inputSignalBindingLabel(index, profileValue = normalizedInputProfileValue(index)) {
  const normalizedProfile = String(profileValue || "none").trim().toLowerCase();
  if (normalizedProfile.includes("esp32-native-touch-pad")) {
    return "TOUCH";
  }
  if (normalizedProfile.includes("limit-switch")) {
    return "COM";
  }
  return "SIG";
}

function inputAssignedPin(index) {
  const profileValue = normalizedInputProfileValue(index);
  if (profileValue === "none") {
    return null;
  }
  const signalLabel = inputSignalBindingLabel(index, profileValue);
  const bindingValue = Number(peripheralHelperBindingValue("input", index, signalLabel));
  if (Number.isFinite(bindingValue) && bindingValue >= 0) {
    return bindingValue;
  }
  const fallbackPin = currentButtonPins()[index];
  return Number.isFinite(fallbackPin) ? fallbackPin : null;
}

function setInputAssignedPin(index, value) {
  const numericIndex = Number(index);
  if (!Number.isInteger(numericIndex) || numericIndex < 0) {
    return;
  }

  state.peripheralInputProfiles = normalizedPeripheralInputProfiles();
  const normalizedValue = String(value || "").trim();
  if (!normalizedValue) {
    state.peripheralInputProfiles[numericIndex] = "none";
    setPeripheralHelperBindingValue("input", numericIndex, "COM", "");
    setPeripheralHelperBindingValue("input", numericIndex, "SIG", "");
    setPeripheralHelperBindingValue("input", numericIndex, "TOUCH", "");
  } else {
    if (normalizedInputProfileValue(numericIndex) === "none") {
      state.peripheralInputProfiles[numericIndex] = "physical-button";
    }
    const activeSignal = inputSignalBindingLabel(numericIndex);
    ["COM", "SIG", "TOUCH"].filter((signal) => signal !== activeSignal).forEach((signal) => {
      setPeripheralHelperBindingValue("input", numericIndex, signal, "");
    });
    setPeripheralHelperBindingValue("input", numericIndex, activeSignal, normalizedValue);
  }
  savePeripheralProfileSelections();
  configurationPeripheralsTab?.renderPeripheralInputControls();
}

function inputRuntimeStatus(index) {
  const numericIndex = Number(index);
  const button1 = state.status?.input?.button1 || null;
  const button2 = state.status?.input?.button2 || null;
  if (Number(button1?.configuredIndex) === numericIndex) {
    return button1;
  }
  if (Number(button2?.configuredIndex) === numericIndex) {
    return button2;
  }
  return null;
}

function inputTouchSensitivity(index) {
  const helperValue = Number(peripheralHelperBindingValue("input", index, "SENSITIVITY"));
  if (Number.isFinite(helperValue) && helperValue >= 5 && helperValue <= 100) {
    return Math.round(helperValue);
  }
  const runtimeValue = Number(inputRuntimeStatus(index)?.sensitivityPercent);
  if (Number.isFinite(runtimeValue) && runtimeValue >= 5 && runtimeValue <= 100) {
    return Math.round(runtimeValue);
  }
  return 55;
}

function syncTouchSensitivityLabels(index, value) {
  const normalizedIndex = String(index);
  const normalizedValue = `${Math.max(5, Math.min(100, Number(value) || 55))}%`;
  for (const label of document.querySelectorAll(`[data-touch-sensitivity-value="${normalizedIndex}"]`)) {
    label.textContent = normalizedValue;
  }
}

function storageTargetLabel(target = state.activeStorageTarget) {
  return target === "sd" ? "SD Card" : "Flash FS";
}

function flashStorageAvailable(status = state.status) {
  return Boolean(status?.system?.spiffs?.available);
}

function resolveStorageTarget(target = state.activeStorageTarget || "flash") {
  return target === "flash" && !flashStorageAvailable() ? "sd" : target;
}

function activateTabByName(tabName, options = {}) {
  return tabNavigation?.activateTabByName(tabName, options) || false;
}

function activeTabName() {
  return tabNavigation?.activeTabName() || "";
}

function hasVisibleNativeTouchDiagram() {
  return activeTabName() === "gpio" && Boolean(document.querySelector(".peripheral-diagram-touch-block"));
}

function inputRuntimeStatusFromPayload(index, status = state.status) {
  const numericIndex = Number(index);
  const button1 = status?.input?.button1 || null;
  const button2 = status?.input?.button2 || null;
  if (Number(button1?.configuredIndex) === numericIndex) {
    return button1;
  }
  if (Number(button2?.configuredIndex) === numericIndex) {
    return button2;
  }
  return null;
}

function refreshVisibleNativeTouchDiagram(status = state.status) {
  for (const block of document.querySelectorAll("[data-touch-input-index]")) {
    const index = Number(block.getAttribute("data-touch-input-index") || -1);
    if (!Number.isInteger(index) || index < 0) {
      continue;
    }

    const runtimeStatus = inputRuntimeStatusFromPayload(index, status);
    const assignedPin = peripheralHelperBindingValue("input", index, "TOUCH") || String(runtimeStatus?.pin || "");
    const numericPin = Number(assignedPin);
    const supported = runtimeStatus
      ? Boolean(runtimeStatus.touchSupported)
      : (Number.isFinite(numericPin) && touchCapablePins().includes(numericPin));
    const active = Boolean(runtimeStatus?.active);
    const rawValue = Number(runtimeStatus?.rawValue || 0);
    const baselineValue = Number(runtimeStatus?.baselineValue || 0);
    const deltaValue = baselineValue > 0 && rawValue > 0 ? Math.abs(rawValue - baselineValue) : 0;
    const pinLabel = Number.isFinite(numericPin) && numericPin >= 0 ? `GPIO${numericPin}` : "No GPIO";
    const metrics = runtimeStatus && baselineValue > 0
      ? `Raw ${rawValue} / Base ${baselineValue} / Delta ${deltaValue}`
      : "Select a touch-capable GPIO to enable live sensing";

    const pinElement = block.querySelector(`[data-touch-pin="${index}"]`);
    const padElement = block.querySelector(`[data-touch-pad="${index}"]`);
    const stateElement = block.querySelector(`[data-touch-state="${index}"]`);
    const metricsElement = block.querySelector(`[data-touch-metrics="${index}"]`);

    if (pinElement) {
      pinElement.textContent = pinLabel;
    }
    if (padElement) {
      padElement.classList.toggle("is-active", active);
      padElement.classList.toggle("is-unsupported", !supported);
    }
    if (stateElement) {
      stateElement.textContent = !supported ? "Unsupported GPIO" : (active ? "Touch detected" : "Idle");
    }
    if (metricsElement) {
      metricsElement.textContent = metrics;
    }
    if (runtimeStatus && Number.isFinite(Number(runtimeStatus.sensitivityPercent))) {
      syncTouchSensitivityLabels(index, runtimeStatus.sensitivityPercent);
    }
  }
}

function stopTouchLivePolling() {
  if (!state.touchLivePollTimer) {
    return;
  }
  window.clearInterval(state.touchLivePollTimer);
  state.touchLivePollTimer = null;
}

function updateTouchLivePolling() {
  if (!hasVisibleNativeTouchDiagram()) {
    stopTouchLivePolling();
    return;
  }
  if (state.touchLivePollTimer) {
    return;
  }
  state.touchLivePollTimer = window.setInterval(() => {
    loadStatus().catch((error) => console.error(error));
  }, 250);
}

function refreshVisiblePeripheralDiagram() {
  renderPeripheralDiagram();
  updateTouchLivePolling();
  window.setTimeout(() => {
    if (activeTabName() === "gpio") {
      renderPeripheralDiagram();
      updateTouchLivePolling();
    }
  }, 0);
}

async function refreshExternalStorageTab(directoryPath = state.currentStoragePathByTarget.sd || "/", options = {}) {
  return storageTab?.refreshExternalStorageTab(directoryPath, options);
}

function updateStorageAvailabilityUi(status = state.status) {
  storageTab?.updateStorageAvailabilityUi(status);
}

function currentSdPins(settings = state.settings, options = {}) {
  const { respectEnabled = true } = options;
  const enabled = Boolean(elements.sdEnabled?.checked ?? settings?.sd?.enabled ?? false);
  if (respectEnabled && !enabled) {
    return [];
  }

  return [
    Number(elements.sdCsPin?.value || settings?.sd?.csPin || DEFAULT_SD_GPIO_PINS.cs),
    Number(elements.sdSckPin?.value || settings?.sd?.sckPin || DEFAULT_SD_GPIO_PINS.sck),
    Number(elements.sdMosiPin?.value || settings?.sd?.mosiPin || DEFAULT_SD_GPIO_PINS.mosi),
    Number(elements.sdMisoPin?.value || settings?.sd?.misoPin || DEFAULT_SD_GPIO_PINS.miso),
  ].filter((pin) => Number.isFinite(pin) && pin >= 0);
}

function reservedSdPins(settings = state.settings) {
  const reservedPins = new Set(currentI2sPins());
  const batteryAdcPin = Number(elements.batteryAdcPin?.value || settings?.battery?.adcPin || 0);
  const chargingSensePin = Number(settings?.battery?.chargingSensePin || 0);
  const statusLedPin = Number(elements.statusLedPin?.value || settings?.device?.statusLedPin || 0);
  const displayType = String(elements.displayType?.value || settings?.oled?.displayType || "oled").toLowerCase();
  const wapeTriggerPin = Number(elements.wapeTriggerPin?.value || settings?.oled?.wapeTriggerPin || 0);

  if (Number.isFinite(batteryAdcPin) && batteryAdcPin > 0) {
    reservedPins.add(batteryAdcPin);
  }
  if (Number.isFinite(chargingSensePin) && chargingSensePin > 0) {
    reservedPins.add(chargingSensePin);
  }
  if (Number.isFinite(statusLedPin) && statusLedPin >= 0) {
    reservedPins.add(statusLedPin);
  }
  if (displayType === "wape" && Number.isFinite(wapeTriggerPin) && wapeTriggerPin > 0) {
    reservedPins.add(wapeTriggerPin);
  }

  return reservedPins;
}

function populateSdPinOptions(settings = state.settings, syncBindings = true) {
  const reserved = reservedSdPins(settings);
  for (const [field, key] of [[elements.sdCsPin, "csPin"], [elements.sdSckPin, "sckPin"], [elements.sdMosiPin, "mosiPin"], [elements.sdMisoPin, "misoPin"]]) {
    if (field) fillBoardPinSelect(field, field.value || settings?.sd?.[key] || 0, key !== "misoPin", reserved);
  }
  if (syncBindings) syncPeripheralBindingGroups();
}

function reservedOledPins(settings = state.settings) {
  const reservedPins = new Set(currentI2sPins());
  const batteryAdcPin = Number(elements.batteryAdcPin?.value || settings?.battery?.adcPin || 0);
  const chargingSensePin = Number(settings?.battery?.chargingSensePin || 0);
  const statusLedPin = Number(elements.statusLedPin?.value || settings?.device?.statusLedPin || 0);

  if (Number.isFinite(batteryAdcPin) && batteryAdcPin >= 0) {
    reservedPins.add(batteryAdcPin);
  }
  if (Number.isFinite(chargingSensePin) && chargingSensePin > 0) {
    reservedPins.add(chargingSensePin);
  }
  if (Number.isFinite(statusLedPin) && statusLedPin >= 0) {
    reservedPins.add(statusLedPin);
  }
  for (const pin of currentSdPins(settings)) {
    reservedPins.add(pin);
  }

  return reservedPins;
}

function choosePreferredOledPins(settings = state.settings) {
  const preferredOrder = Array.from({ length: chipMaxPin() + 1 }, (_, index) => index);
  const reservedPins = reservedOledPins(settings);

  const pickPin = (preferredPins, excludePin = -1) => {
    for (const pin of [...preferredPins, ...preferredOrder]) {
      if (!Number.isFinite(pin) || pin === excludePin || reservedPins.has(pin) || pin < 0) {
        continue;
      }
      return pin;
    }
    return excludePin;
  };

  const selectedSda = Number(elements.oledSdaPin?.value || settings?.oled?.sdaPin || -1);
  const selectedScl = Number(elements.oledSclPin?.value || settings?.oled?.sclPin || -1);
  const safeSda = !reservedPins.has(selectedSda) && selectedSda >= 0
    ? selectedSda
    : pickPin([DEFAULT_ESP32S3_OLED_PREFERRED_PINS.sda, Number(settings?.oled?.sdaPin), 8, 13, 14, 15, 16, 17, 18, 19, 20]);
  const safeScl = !reservedPins.has(selectedScl) && selectedScl >= 0 && selectedScl !== safeSda
    ? selectedScl
    : pickPin([DEFAULT_ESP32S3_OLED_PREFERRED_PINS.scl, Number(settings?.oled?.sclPin), 13, 14, 15, 16, 17, 18, 19, 20], safeSda);

  return {
    sda: safeSda,
    scl: safeScl,
  };
}

function populateOledPinOptions(settings = state.settings) {
  if (!elements.oledSdaPin || !elements.oledSclPin || !elements.oledResetPin) return;
  const reserved = reservedOledPins(settings);
  fillBoardPinSelect(elements.oledSdaPin, elements.oledSdaPin.value || settings?.oled?.sdaPin || 4, true, reserved);
  fillBoardPinSelect(elements.oledSclPin, elements.oledSclPin.value || settings?.oled?.sclPin || 5, true, reserved);
  fillBoardPinSelect(elements.oledResetPin, elements.oledResetPin.value || settings?.oled?.resetPin || -1, true, reserved, -1);
}

function availableWapeTriggerPins(settings = state.settings) {
  const chipFamily = activeChipFamily();
  const maxPin = chipFamily === "esp32" ? 39 : 48;
  const reservedPins = new Set(currentI2sPins());
  const batteryAdcPin = Number(elements.batteryAdcPin?.value || settings?.battery?.adcPin || 0);
  const statusLedPin = Number(elements.statusLedPin?.value || settings?.device?.statusLedPin || 0);
  if (Number.isFinite(batteryAdcPin) && batteryAdcPin > 0) {
    reservedPins.add(batteryAdcPin);
  }
  if (Number.isFinite(statusLedPin) && statusLedPin > 0) {
    reservedPins.add(statusLedPin);
  }
  for (const pin of currentSdPins(settings)) {
    reservedPins.add(pin);
  }
  if (chipFamily === "esp32s3") {
    reservedPins.add(21);
  }
  return validBoardPins(true)
    .filter((pin) => pin === 0 || !reservedPins.has(pin));
}

function populateWapeTriggerPinOptions(settings = state.settings) {
  if (!elements.wapeTriggerPin) {
    return;
  }

  const selectedPin = String(elements.wapeTriggerPin.value || settings?.oled?.wapeTriggerPin || "0");
  elements.wapeTriggerPin.innerHTML = "";

  for (const pin of availableWapeTriggerPins(settings)) {
    const option = document.createElement("option");
    option.value = String(pin);
    option.textContent = pin === 0 ? "Disabled" : `GPIO${pin}`;
    elements.wapeTriggerPin.append(option);
  }

  if ([...elements.wapeTriggerPin.options].some((option) => option.value === selectedPin)) {
    elements.wapeTriggerPin.value = selectedPin;
  } else {
    elements.wapeTriggerPin.value = "0";
  }
}

function updateDisplayModeUi() {
  displayTab?.updateDisplayModeUi();
}

function decodeHtmlEntities(value) {
  return String(value || "")
    .replaceAll("&amp;", "&")
    .replaceAll("&quot;", '"')
    .replaceAll("&#39;", "'")
    .replaceAll("&apos;", "'")
    .replaceAll("&lt;", "<")
    .replaceAll("&gt;", ">")
    .replaceAll("&nbsp;", " ");
}

function safeDecodePercentEncoding(value, plusAsSpace = false) {
  const input = String(value || "");
  try {
    return decodeURIComponent(plusAsSpace ? input.replaceAll("+", " ") : input);
  } catch {
    return input;
  }
}

function derivePlaybackTitleFromUrl(url) {
  const rawUrl = String(url || "").trim();
  if (!rawUrl) {
    return "";
  }

  const trimmed = rawUrl.split("#", 1)[0].split("?", 1)[0];
  const lastSlash = trimmed.lastIndexOf("/");
  const filename = lastSlash >= 0 ? trimmed.slice(lastSlash + 1) : trimmed;
  return decodeHtmlEntities(safeDecodePercentEncoding(filename).replaceAll("_", " ")).replace(/\s+/g, " ").trim();
}

function normalizePlaybackTitle(title, fallbackUrl = "") {
  let nextTitle = String(title || "").trim();
  if (!nextTitle) {
    return derivePlaybackTitleFromUrl(fallbackUrl);
  }

  const streamTitleMatch = nextTitle.match(/StreamTitle=(['"]?)(.*)\1;?/i);
  if (streamTitleMatch?.[2]) {
    nextTitle = streamTitleMatch[2];
  }

  nextTitle = decodeHtmlEntities(safeDecodePercentEncoding(nextTitle, true)).replace(/^['"]+|['"]+$/g, "").replace(/\s+/g, " ").trim();
  if (!nextTitle) {
    return derivePlaybackTitleFromUrl(fallbackUrl);
  }

  if (/^https?:\/\//i.test(nextTitle) || nextTitle.includes("authSig=")) {
    return derivePlaybackTitleFromUrl(nextTitle || fallbackUrl);
  }

  return nextTitle;
}

function availableButtonActionOptions() {
  const audioEnabled = Boolean(state.status?.firmware?.audioEnabled ?? true);
  const options = [
    { value: "none", label: "No Action" },
    { value: "ha_previous", label: "Home Assistant Previous Event" },
    { value: "ha_next", label: "Home Assistant Next Event" },
    { value: "volume_down", label: "Volume Down (-5%)" },
    { value: "volume_up", label: "Volume Up (+5%)" },
  ];

  if (!audioEnabled) {
    return options;
  }

  return [
    { value: "previous", label: "Previous Track" },
    { value: "next", label: "Next Track" },
    { value: "play_pause", label: "Play / Pause" },
    { value: "replay_current", label: "Replay Current" },
    { value: "stop", label: "Stop Playback" },
    ...options,
  ];
}

function populateButtonActionSelect(fieldName) {
  const field = namedField(fieldName);
  if (!field) {
    return;
  }

  const options = availableButtonActionOptions();
  const signature = options.map((option) => option.value).join("|");
  const key = String(fieldName || "").replace(/^device\./, "");
  const currentValue = String(field.value || state.settings?.device?.[key] || defaultButtonActionForField(fieldName)).trim();

  if (field.dataset.optionSignature !== signature) {
    field.innerHTML = "";
    for (const optionConfig of options) {
      const option = document.createElement("option");
      option.value = optionConfig.value;
      option.textContent = optionConfig.label;
      field.appendChild(option);
    }
    field.dataset.optionSignature = signature;
  }

  const values = [...field.options].map((option) => option.value);
  field.value = values.includes(currentValue) ? currentValue : defaultButtonActionForField(fieldName);
}

function populateButtonActionSelects() {
  populateButtonActionSelect("device.button1Action");
  populateButtonActionSelect("device.button2Action");
}

function loadRecentPlayback() {
  try {
    return JSON.parse(window.localStorage.getItem("notifierRecentPlayback") || "[]");
  } catch {
    return [];
  }
}

function saveRecentPlayback() {
  window.localStorage.setItem("notifierRecentPlayback", JSON.stringify(state.recentPlayback.slice(0, 6)));
}

function selectedFirmwareVersion() {
  return firmwareTab?.selectedFirmwareVersion() || null;
}

function updateFirmwareSelectionLabel() {
  firmwareTab?.updateFirmwareSelectionLabel();
}

function showFirmwareListStatus(text, isError = false) {
  firmwareTab?.showFirmwareListStatus(text, isError);
}

async function loadRadioCountries(forceRefresh = false) {
  await radioBrowserModule?.loadRadioCountries(forceRefresh);
}

async function loadRadioStations(countryName) {
  await radioBrowserModule?.loadRadioStations(countryName);
}

async function applySelectedRadioStation(options = {}) {
  await radioBrowserModule?.applySelectedRadioStation(options);
}

function renderFirmwareList(releases, currentVersion, latestVersion, selectedVersion) {
  firmwareTab?.renderFirmwareList(releases, currentVersion, latestVersion, selectedVersion);
}

function firmwareReleaseNote(release) {
  const variant = String(release?.variantLabel || "").toLowerCase();

  if (variant.includes("hacs slim")) {
    return "HACS slim build: MQTT media-player integration focused build with reduced local UI footprint.";
  }

  if (variant.includes("hacs")) {
    return "HACS build: best choice for Home Assistant MQTT Media Player integration and media-player style control.";
  }

  return "Standard build: general notifier firmware with the local web UI and the project’s default MQTT control model.";
}

function beginFirmwareReconnectReload(initialDelayMs = 12000) {
  if (state.firmwareReloadPending) {
    return;
  }
  state.firmwareReloadPending = true;
  if (state.firmwareReloadTimer) {
    window.clearTimeout(state.firmwareReloadTimer);
    state.firmwareReloadTimer = null;
  }
  state.rebootOverlayArmed = true;
  showRebootOverlay("Firmware installed — rebooting device...", 30);
  // A GitHub OTA may acknowledge completion several seconds before the
  // scheduled reboot. Do not mistake the still-running old firmware for the
  // restarted device, while still allowing an observed disconnect to reconnect
  // and refresh immediately when the new firmware is available.
  state.rebootOverlayReconnectAllowedAt = Math.max(
    state.rebootOverlayReconnectAllowedAt,
    state.rebootOverlayStartedAt + Math.max(0, Number(initialDelayMs || 0)),
  );
}

function setPill(element, label, mode) {
  element.textContent = label;
  element.className = `stat-value ${mode}`;
}

function toast(message) {
  const template = document.getElementById("toastTemplate");
  const node = template.content.firstElementChild.cloneNode(true);
  node.textContent = message;
  document.body.appendChild(node);
  window.setTimeout(() => node.remove(), 2500);
}

function setMessage(message, isError = false) {
  elements.message.textContent = message;
  elements.message.style.color = isError ? "#b42318" : "#333333";
}

function setScanStatus(message, isError = false) {
  wifiTab?.setScanStatus(message, isError);
}

function setMqttConnectStatus(message, isError = false) {
  mqttTab?.setMqttConnectStatus(message, isError);
}

function setStorageStatus(message, isError = false) {
  if (!elements.storageStatus) {
    return;
  }
  elements.storageStatus.textContent = message;
  elements.storageStatus.style.color = isError ? "#b42318" : "";
}

function isApHost() {
  const host = String(window.location.hostname || "").trim();
  return host === "192.168.4.1";
}

function usableStationIp(status) {
  const stationIp = String(status?.network?.ip || "").trim();
  if (!status?.network?.wifiConnected || !stationIp || stationIp === "0.0.0.0" || stationIp === "192.168.4.1") {
    return "";
  }
  return stationIp;
}

function showWifiHandoff(stationIp, redirectUrl, message) {
  if (elements.wifiHandoffLink) {
    elements.wifiHandoffLink.textContent = redirectUrl;
    elements.wifiHandoffLink.href = redirectUrl;
  }
  if (elements.wifiHandoffOpen) {
    elements.wifiHandoffOpen.href = redirectUrl;
  }
  if (elements.wifiHandoffStatus) {
    elements.wifiHandoffStatus.textContent = message;
  }
  if (elements.wifiHandoffOverlay) {
    elements.wifiHandoffOverlay.hidden = false;
  }
  setMessage(`Wi-Fi connected. New device address: ${stationIp}`);
}

function probeStationAddress(stationIp, redirectUrl, initialDelayMs = 0) {
  const startedAt = Date.now();
  const probeDeadlineMs = 90000;

  const scheduleProbe = (delayMs) => {
    if (state.stationRedirectProbeTimer) {
      window.clearTimeout(state.stationRedirectProbeTimer);
    }
    state.stationRedirectProbeTimer = window.setTimeout(runProbe, Math.max(0, delayMs));
  };

  const runProbe = () => {
    state.stationRedirectProbeTimer = null;
    if (Date.now() - startedAt >= probeDeadlineMs) {
      if (elements.wifiHandoffStatus) {
        elements.wifiHandoffStatus.textContent =
          "Reconnect this device to your home Wi-Fi if it did not reconnect automatically, then tap Open speaker.";
      }
      return;
    }

    const probe = new Image();
    let settled = false;
    const timeout = window.setTimeout(() => finish(false), 2500);
    const finish = (reachable) => {
      if (settled) {
        return;
      }
      settled = true;
      window.clearTimeout(timeout);
      probe.onload = null;
      probe.onerror = null;
      if (reachable) {
        if (elements.wifiHandoffStatus) {
          elements.wifiHandoffStatus.textContent = "Speaker found on the home network. Opening it now...";
        }
        window.setTimeout(() => window.location.replace(redirectUrl), 250);
        return;
      }
      scheduleProbe(1200);
    };
    probe.onload = () => finish(true);
    probe.onerror = () => finish(false);
    probe.src = `http://${stationIp}/wifi-handoff.svg?t=${Date.now()}`;
  };

  scheduleProbe(initialDelayMs);
}

async function beginStationHandoff(stationIp) {
  const fallbackUrl = `http://${stationIp}/`;
  showWifiHandoff(
    stationIp,
    fallbackUrl,
    "The setup network will close. Waiting for this device to rejoin your home Wi-Fi...",
  );

  try {
    const handoff = await request("/api/wifi/handoff", { method: "POST", body: "{}" });
    const confirmedIp = String(handoff?.stationIp || stationIp).trim();
    const redirectUrl = String(handoff?.redirectUrl || `http://${confirmedIp}/`);
    const shutdownDelayMs = Math.max(0, Number(handoff?.shutdownDelayMs || 0));
    showWifiHandoff(
      confirmedIp,
      redirectUrl,
      handoff?.accessPointWillStop
        ? "Address received. The setup network is closing; waiting for this device to rejoin your home Wi-Fi..."
        : "Address received. Looking for the speaker on your home network...",
    );
    probeStationAddress(confirmedIp, redirectUrl, shutdownDelayMs + 500);
  } catch (error) {
    if (elements.wifiHandoffStatus) {
      elements.wifiHandoffStatus.textContent =
        "The speaker is still completing its connection. Retrying the handoff...";
    }
    window.setTimeout(() => {
      state.stationRedirectInProgress = false;
      maybeRedirectToStationIp(state.status || {}, { force: true });
    }, 1500);
  }
}

function maybeRedirectToStationIp(status, { force = false } = {}) {
  if (state.stationRedirectInProgress) {
    return;
  }

  const stationIp = usableStationIp(status);
  if (!stationIp || stationIp === window.location.hostname || (!force && !isApHost())) {
    return;
  }

  state.stationRedirectInProgress = true;
  const setupAccessPointContext = isApHost() || (force && Boolean(status?.network?.apMode));
  if (setupAccessPointContext) {
    void beginStationHandoff(stationIp);
    return;
  }

  setMessage(`Wi-Fi connected. Opening ${stationIp}...`);
  window.setTimeout(() => window.location.replace(`http://${stationIp}/`), 500);
}

function isNumericLikeField(field) {
  return field?.type === "number" || field?.id === "batteryMeasuredVoltage";
}

function normalizeDecimalField(field) {
  if (!field || !isNumericLikeField(field) || typeof field.value !== "string") {
    return;
  }
  const normalized = field.value.replaceAll(",", ".");
  if (normalized !== field.value) {
    field.value = normalized;
  }
}

function setHeaderActionsMenuOpen(open) {
  state.headerActionsMenuOpen = Boolean(open);
  if (elements.headerActionsMenu) {
    elements.headerActionsMenu.dataset.open = String(state.headerActionsMenuOpen);
    elements.headerActionsMenu.setAttribute("aria-hidden", String(!state.headerActionsMenuOpen));
  }
  if (elements.headerActionsButton) {
    elements.headerActionsButton.setAttribute("aria-expanded", String(state.headerActionsMenuOpen));
  }
}

function rebootOverlayCircumference() {
  return 2 * Math.PI * 52;
}

function updateRebootOverlayProgress(remainingSeconds, totalSeconds) {
  if (elements.rebootOverlayCountdown) {
    elements.rebootOverlayCountdown.textContent = String(Math.max(0, Math.ceil(remainingSeconds)));
  }
  if (elements.rebootOverlayProgress) {
    const clampedTotal = Math.max(1, Number(totalSeconds || 30));
    const clampedRemaining = Math.max(0, Math.min(clampedTotal, Number(remainingSeconds || 0)));
    const progress = 1 - (clampedRemaining / clampedTotal);
    const circumference = rebootOverlayCircumference();
    elements.rebootOverlayProgress.style.strokeDasharray = `${circumference}`;
    elements.rebootOverlayProgress.style.strokeDashoffset = `${circumference * (1 - progress)}`;
  }
}

function hideRebootOverlay() {
  if (state.rebootOverlayTimer) {
    window.clearInterval(state.rebootOverlayTimer);
    state.rebootOverlayTimer = null;
  }
  if (state.rebootOverlayPollTimer) {
    window.clearInterval(state.rebootOverlayPollTimer);
    state.rebootOverlayPollTimer = null;
  }
  state.rebootOverlayCountdownRemaining = 0;
  state.rebootOverlayStartedAt = 0;
  state.rebootOverlayReconnectAllowedAt = 0;
  state.rebootOverlaySawDisconnect = false;
  state.rebootOverlayArmed = false;
  if (elements.rebootOverlay) {
    elements.rebootOverlay.hidden = true;
  }
}

function clearFirmwareReconnectState() {
  state.awaitingFirmwareReboot = false;
  state.firmwareReloadPending = false;
  if (state.firmwareReloadTimer) {
    window.clearTimeout(state.firmwareReloadTimer);
    state.firmwareReloadTimer = null;
  }
  hideRebootOverlay();
}

function resetTransientOverlays() {
  hideRebootOverlay();
  setHeaderActionsMenuOpen(false);
}

function performFrontendHardRefresh() {
  try {
    if ("caches" in window) {
      caches.keys().then((keys) => Promise.all(keys.map((key) => caches.delete(key)))).catch(() => {});
    }
  } catch {
  }
  const url = new URL(window.location.href);
  url.searchParams.set("ts", String(Date.now()));
  window.location.replace(url.toString());
}

function showRebootOverlay(title = "Rebooting device...", totalSeconds = 30) {
  if (!state.rebootOverlayArmed) {
    hideRebootOverlay();
    return;
  }
  const overlayArmed = state.rebootOverlayArmed;
  hideRebootOverlay();
  state.rebootOverlayArmed = overlayArmed;
  setHeaderActionsMenuOpen(false);
  const reconnectGraceMs = Math.min(6000, Math.max(1500, Math.round(Number(totalSeconds || 30) * 250)));
  state.rebootOverlayStartedAt = Date.now();
  state.rebootOverlayReconnectAllowedAt = state.rebootOverlayStartedAt + reconnectGraceMs;
  state.rebootOverlaySawDisconnect = false;
  state.rebootOverlayCountdownRemaining = totalSeconds;
  if (elements.rebootOverlayTitle) {
    elements.rebootOverlayTitle.textContent = title;
  }
  if (elements.rebootOverlayStatus) {
    elements.rebootOverlayStatus.textContent = "Waiting for device to come back online.";
  }
  updateRebootOverlayProgress(totalSeconds, totalSeconds);
  if (elements.rebootOverlay) {
    elements.rebootOverlay.hidden = false;
  }

  const startedAt = state.rebootOverlayStartedAt;
  let reloadTriggered = false;
  const tryReconnect = async () => {
    try {
      await fetch(`/api/status?ts=${Date.now()}`, { cache: "no-store" });
      if (!state.rebootOverlaySawDisconnect && Date.now() < state.rebootOverlayReconnectAllowedAt) {
        return;
      }
      if (elements.rebootOverlayStatus) {
        elements.rebootOverlayStatus.textContent = "Device is back online. Refreshing page...";
      }
      if (!reloadTriggered) {
        reloadTriggered = true;
        hideRebootOverlay();
        window.setTimeout(() => performFrontendHardRefresh(), 250);
      }
    } catch {
      state.rebootOverlaySawDisconnect = true;
      const elapsedSeconds = Math.floor((Date.now() - startedAt) / 1000);
      if (elapsedSeconds >= totalSeconds && elements.rebootOverlayStatus) {
        elements.rebootOverlayStatus.textContent = "Still waiting for reconnect...";
      }
    }
  };

  state.rebootOverlayTimer = window.setInterval(() => {
    const elapsed = (Date.now() - startedAt) / 1000;
    const remaining = Math.max(0, totalSeconds - elapsed);
    state.rebootOverlayCountdownRemaining = remaining;
    updateRebootOverlayProgress(remaining, totalSeconds);
  }, 250);

  state.rebootOverlayPollTimer = window.setInterval(() => {
    tryReconnect().catch(() => {});
  }, 1500);
}

function parseDecimalFieldValue(field, fallback = 0) {
  if (!field) {
    return Number(fallback || 0);
  }
  normalizeDecimalField(field);
  const normalized = String(field.value || "").trim();
  if (!normalized) {
    return Number(fallback || 0);
  }
  const parsed = Number(normalized);
  return Number.isFinite(parsed) ? parsed : Number(fallback || 0);
}

function delay(ms) {
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}

function formatTransferRate(bytesPerSecond) {
  const numericRate = Number(bytesPerSecond || 0);
  if (!Number.isFinite(numericRate) || numericRate <= 0) {
    return "0 B/s";
  }
  if (numericRate >= 1024 * 1024) {
    return `${(numericRate / (1024 * 1024)).toFixed(1)} MiB/s`;
  }
  if (numericRate >= 1024) {
    return `${Math.round(numericRate / 1024)} KiB/s`;
  }
  return `${Math.round(numericRate)} B/s`;
}

function storageStreamUrl(path, target = state.activeStorageTarget, download = false) {
  const encodedPath = encodeURIComponent(String(path || ""));
  return `/api/storage/file?target=${encodeURIComponent(target)}&path=${encodedPath}${download ? "&download=1" : ""}`;
}

function normalizeStorageDirectoryPath(path) {
  const raw = String(path || "/").trim().replaceAll("\\", "/");
  if (!raw || raw === "/") {
    return "/";
  }
  const normalized = `/${raw.replace(/^\/+/, "").replace(/\/+/g, "/")}`.replace(/\/+$/, "");
  return normalized || "/";
}

function storageBaseName(path) {
  const normalized = String(path || "").trim().replaceAll("\\", "/");
  if (!normalized) {
    return "";
  }
  const trimmed = normalized.endsWith("/") && normalized.length > 1
    ? normalized.slice(0, -1)
    : normalized;
  const parts = trimmed.split("/").filter(Boolean);
  return parts[parts.length - 1] || trimmed;
}

function storageParentPath(path) {
  const normalized = normalizeStorageDirectoryPath(path);
  if (normalized === "/") {
    return "";
  }
  const parts = normalized.split("/").filter(Boolean);
  parts.pop();
  return parts.length ? `/${parts.join("/")}` : "/";
}

function storageFileExtension(path) {
  const cleanPath = String(path || "");
  const dotIndex = cleanPath.lastIndexOf(".");
  return dotIndex >= 0 ? cleanPath.substring(dotIndex + 1).toLowerCase() : "";
}

function isAudioStoragePath(path) {
  return STORAGE_AUDIO_EXTENSIONS.has(storageFileExtension(path));
}

function activeStorageEntries(target = state.activeStorageTarget) {
  return Array.isArray(state.currentStorageEntriesByTarget[target]) ? state.currentStorageEntriesByTarget[target] : [];
}

function activeStorageMeta(target = state.activeStorageTarget) {
  const meta = state.currentStorageMetaByTarget[target];
  return meta && typeof meta === "object" ? meta : {};
}

function setStorageMeta(meta, target = state.activeStorageTarget) {
  state.currentStorageMetaByTarget[target] = { ...activeStorageMeta(target), ...(meta || {}) };
}

function formatLoadProgress(loaded, total) {
  const safeLoaded = Math.max(0, Number(loaded || 0));
  const safeTotal = Math.max(0, Number(total || 0));
  if (!safeTotal) {
    return `${safeLoaded} loaded`;
  }
  const percent = Math.max(0, Math.min(100, Math.round((safeLoaded * 100) / safeTotal)));
  return `${safeLoaded}/${safeTotal} (${percent}%)`;
}

function mergeStorageEntries(existingEntries, incomingEntries) {
  const merged = [...(Array.isArray(existingEntries) ? existingEntries : [])];
  const seenPaths = new Set(merged.map((entry) => String(entry?.path || "")));
  for (const entry of Array.isArray(incomingEntries) ? incomingEntries : []) {
    const path = String(entry?.path || "");
    if (!path || seenPaths.has(path)) {
      continue;
    }
    seenPaths.add(path);
    merged.push(entry);
  }
  return merged;
}

function activeStorageAudioEntries(target = state.activeStorageTarget) {
  return activeStorageEntries(target).filter((entry) => !entry?.isDirectory && isAudioStoragePath(entry.path));
}

function currentStoragePreviewQueueIndex(target = state.activeStorageTarget) {
  const currentPath = String(state.storagePreviewItem?.path || "");
  if (!currentPath) {
    return -1;
  }
  return activeStorageAudioEntries(target).findIndex((entry) => entry.path === currentPath);
}

function activeStorageSelection(target = state.activeStorageTarget) {
  return Array.isArray(state.storageSelectedPathsByTarget[target]) ? state.storageSelectedPathsByTarget[target] : [];
}

function setStorageSelection(paths, target = state.activeStorageTarget) {
  const uniquePaths = [...new Set((paths || []).filter(Boolean))];
  state.storageSelectedPathsByTarget[target] = uniquePaths;
}

function clearStorageSelection(target = state.activeStorageTarget) {
  setStorageSelection([], target);
}

function selectedStoragePlaybackEntry(target = state.activeStorageTarget) {
  const selectedPaths = activeStorageSelection(target);
  if (selectedPaths.length !== 1) {
    return null;
  }
  const entry = activeStorageEntries(target).find((candidate) => candidate.path === selectedPaths[0]);
  if (!entry || entry.isDirectory || !isAudioStoragePath(entry.path)) {
    return null;
  }
  return entry;
}

function rerenderStorageManager(target = state.activeStorageTarget) {
  renderStorageManager({
    target,
    storage: state.storageInfoByTarget[target] || {},
    currentPath: state.currentStoragePathByTarget[target] || "/",
    entries: activeStorageEntries(target),
    ...activeStorageMeta(target),
  });
}

function formatPlaybackClock(seconds) {
  const totalSeconds = Math.max(0, Math.floor(Number(seconds) || 0));
  const minutes = Math.floor(totalSeconds / 60);
  const remainder = totalSeconds % 60;
  return `${String(minutes).padStart(2, "0")}:${String(remainder).padStart(2, "0")}`;
}

function storageEntryIconSvg(entry) {
  if (entry?.isDirectory) {
    return '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 7h6l2 2h10v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"></path></svg>';
  }
  if (isAudioStoragePath(entry?.path || "")) {
    return '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 4v10"></path><path d="M12 14a3 3 0 1 1-2 2.83V8h8"></path></svg>';
  }
  return '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 3h6l5 5v13H8a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"></path><path d="M14 3v5h5"></path></svg>';
}

function storageBadgeLabel(entry) {
  if (entry?.isDirectory) {
    return "Folder";
  }
  return storageFileExtension(entry?.path || "")?.toUpperCase() || "FILE";
}

function storageItemSubtitle(entry) {
  if (entry?.isDirectory) {
    return `Folder • ${entry.path || ""}`;
  }
  return `${formatBytes(entry?.sizeBytes || 0)} • ${entry?.path || ""}`;
}

function updateStorageToolbar(storage = state.storageInfoByTarget[state.activeStorageTarget] || {}) {
  const entries = activeStorageEntries();
  const selectedPaths = activeStorageSelection();
  const playableEntry = selectedStoragePlaybackEntry();
  const selectedTrackPlaying = storageEntryIsPlaying(playableEntry);
  const allVisibleSelected = entries.length > 0 && entries.every((entry) => selectedPaths.includes(entry.path));
  const setButtonLabel = (button, label) => {
    if (!button) {
      return;
    }
    const labelNode = button.querySelector("span");
    if (labelNode) {
      labelNode.textContent = label;
    }
  };

  if (elements.storageSelectModeButton) {
    elements.storageSelectModeButton.classList.toggle("active", state.storageSelectionMode);
    setButtonLabel(elements.storageSelectModeButton, state.storageSelectionMode ? "Done" : "Select");
  }
  if (elements.storageSelectAllButton) {
    elements.storageSelectAllButton.disabled = !storage.mounted || entries.length === 0;
    setButtonLabel(elements.storageSelectAllButton, allVisibleSelected ? "Clear" : "All");
  }
  if (elements.storageDeleteButton) {
    elements.storageDeleteButton.disabled = selectedPaths.length === 0;
    setButtonLabel(elements.storageDeleteButton, selectedPaths.length > 1 ? `Delete (${selectedPaths.length})` : "Delete");
  }
  if (elements.storagePlayButton) {
    elements.storagePlayButton.disabled = !playableEntry;
    elements.storagePlayButton.classList.toggle("active", Boolean(playableEntry) && !selectedTrackPlaying);
    elements.storagePlayButton.classList.toggle("playing", selectedTrackPlaying);
    elements.storagePlayButton.title = playableEntry
      ? `${selectedTrackPlaying ? "Stop" : "Play"} ${playableEntry.name || playableEntry.path}`
      : "Select one audio file to play";
    elements.storagePlayButton.setAttribute("aria-label", elements.storagePlayButton.title);
    const iconState = selectedTrackPlaying ? "stop" : "play";
    if (elements.storagePlayButton.dataset.iconState !== iconState) {
      elements.storagePlayButton.dataset.iconState = iconState;
      elements.storagePlayButton.innerHTML = selectedTrackPlaying
        ? '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 7h10v10H7z"></path></svg>'
        : '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 5v14l11-7z"></path></svg>';
    }
  }
  if (elements.storageUploadButton) {
    elements.storageUploadButton.disabled = !storage.mounted || state.storageUploadInProgress;
  }
  if (elements.storageNewFolderButton) {
    elements.storageNewFolderButton.disabled = !storage.mounted;
  }
  if (elements.storageRemountButton) {
    const isSdTarget = state.activeStorageTarget === "sd";
    elements.storageRemountButton.hidden = !isSdTarget;
    elements.storageRemountButton.disabled = !isSdTarget;
  }
}

function updateStoragePreviewPlaybackControls() {
  const audioEntries = activeStorageAudioEntries();
  const queueIndex = currentStoragePreviewQueueIndex();
  const deviceActive = Boolean(state.storagePreviewPlaybackMode.deviceActive);
  const hasPreviewItem = Boolean(state.storagePreviewItem);

  if (elements.storagePreviewPlayButton) {
    elements.storagePreviewPlayButton.textContent = deviceActive ? "Stop" : "Play";
    elements.storagePreviewPlayButton.classList.toggle("secondary", deviceActive);
    elements.storagePreviewPlayButton.disabled = !hasPreviewItem;
  }
  if (elements.storagePreviewPrevButton) {
    elements.storagePreviewPrevButton.disabled = audioEntries.length <= 1 || queueIndex < 0;
  }
  if (elements.storagePreviewNextButton) {
    elements.storagePreviewNextButton.disabled = audioEntries.length <= 1 || queueIndex < 0;
  }
  if (elements.storagePreviewLoopButton) {
    elements.storagePreviewLoopButton.setAttribute("aria-pressed", String(Boolean(state.storagePreviewPlaybackMode.loop)));
  }
  if (elements.storagePreviewShuffleButton) {
    elements.storagePreviewShuffleButton.setAttribute("aria-pressed", String(Boolean(state.storagePreviewPlaybackMode.shuffle)));
  }

  const playingEntry = currentStoragePlayingEntry();
  const selectedEntry = selectedStoragePlaybackEntry();
  const actionableEntry = playingEntry || selectedEntry || state.storagePreviewItem;
  const activeEntryPlaying = Boolean(playingEntry && isPlaybackActive(state.status));
  const canStep = audioEntries.length > 1 && Boolean(playingEntry || state.storagePreviewItem);
  if (elements.storageInlinePlayButton) {
    const iconState = activeEntryPlaying ? "stop" : "play";
    if (elements.storageInlinePlayButton.dataset.iconState !== iconState) {
      elements.storageInlinePlayButton.dataset.iconState = iconState;
      elements.storageInlinePlayButton.innerHTML = activeEntryPlaying
        ? '<svg class="media-control-icon" viewBox="0 0 24 24" aria-hidden="true"><rect x="6" y="6" width="12" height="12" rx="1"></rect></svg>'
        : '<svg class="media-control-icon media-control-play" viewBox="0 0 24 24" aria-hidden="true"><path d="m7 4 14 8-14 8z"></path></svg>';
    }
    elements.storageInlinePlayButton.classList.toggle("playing", activeEntryPlaying);
    elements.storageInlinePlayButton.disabled = !actionableEntry;
    elements.storageInlinePlayButton.title = activeEntryPlaying ? "Stop this song" : "Play selected song";
    elements.storageInlinePlayButton.setAttribute("aria-label", elements.storageInlinePlayButton.title);
  }
  if (elements.storageInlinePrevButton) elements.storageInlinePrevButton.disabled = !canStep;
  if (elements.storageInlineNextButton) elements.storageInlineNextButton.disabled = !canStep;
  if (elements.storageInlineShuffleButton) elements.storageInlineShuffleButton.setAttribute("aria-pressed", String(Boolean(state.storagePreviewPlaybackMode.shuffle)));
  if (elements.storageInlineRepeatButton) elements.storageInlineRepeatButton.setAttribute("aria-pressed", String(Boolean(state.storagePreviewPlaybackMode.loop)));
  if (elements.storageInlineAutoplayButton) elements.storageInlineAutoplayButton.setAttribute("aria-pressed", String(Boolean(state.storagePreviewPlaybackMode.autoplay)));

  updateStoragePreviewProgressUi();
}

function restoreStorageLocation() {
  try {
    const saved = JSON.parse(window.localStorage.getItem(STORAGE_LOCATION_STORAGE_KEY) || "{}");
    const target = saved?.target === "flash" ? "flash" : "sd";
    const directory = normalizeStorageDirectoryPath(saved?.directory || "/");
    state.activeStorageTarget = target;
    state.currentStoragePathByTarget[target] = directory;
  } catch {
    state.activeStorageTarget = "sd";
    state.currentStoragePathByTarget.sd = "/";
  }
}

function saveStorageLocation(target, directory) {
  try {
    window.localStorage.setItem(STORAGE_LOCATION_STORAGE_KEY, JSON.stringify({
      target: resolveStorageTarget(target),
      directory: normalizeStorageDirectoryPath(directory),
    }));
  } catch {
  }
}

function updateStorageFolderPlaybackStatus(status = state.status) {
  const playingPath = storagePlaybackPathFromStatus(status);
  const entriesByPath = new Map(activeStorageEntries().map((entry) => [entry.path, entry]));
  for (const row of elements.storageFileList?.querySelectorAll(".storage-file-row") || []) {
    const playing = Boolean(playingPath && row.dataset.storagePath === playingPath && isPlaybackActive(status));
    row.classList.toggle("playing", playing);
    row.toggleAttribute("aria-current", playing);
    const badge = row.querySelector(".storage-file-badge");
    const entry = entriesByPath.get(row.dataset.storagePath);
    if (badge && entry) badge.textContent = playing ? "Playing" : storageBadgeLabel(entry);
  }
  updateStorageToolbar();
  updateStoragePreviewPlaybackControls();
}

function releaseStoragePreviewUrls() {
  if (state.storagePreviewObjectUrl) {
    URL.revokeObjectURL(state.storagePreviewObjectUrl);
    state.storagePreviewObjectUrl = "";
  }
  if (state.storagePreviewArtworkUrl) {
    URL.revokeObjectURL(state.storagePreviewArtworkUrl);
    state.storagePreviewArtworkUrl = "";
  }
}

function updateStoragePreviewPath(path = "") {
  if (!elements.storagePreviewPath) {
    return;
  }
  const label = String(path || "").trim();
  elements.storagePreviewPath.textContent = label || "\u00a0";
  elements.storagePreviewPath.title = label;
}

function updateStoragePreviewProgressUi() {
  const playbackState = String(state.status?.playback?.state || "idle");
  const deviceActive = Boolean(state.storagePreviewPlaybackMode.deviceActive);
  const hasPreviewItem = Boolean(state.storagePreviewItem);
  const buffering = deviceActive && playbackState === "buffering";
  const fileManagerActive = deviceActive && String(state.status?.playback?.source || "") === "file-manager";
  const duration = fileManagerActive ? Math.max(0, Number(state.status?.playback?.durationSeconds || 0)) : 0;
  const position = fileManagerActive ? Math.min(duration, Math.max(0, Number(state.status?.playback?.positionSeconds || 0))) : 0;
  const trackName = currentStoragePlayingEntry()?.name || selectedStoragePlaybackEntry()?.name || state.storagePreviewItem?.name || "Select a song";
  const clockLabel = `${formatPlaybackClock(position)} / ${formatPlaybackClock(duration)}`;
  const label = `${trackName} · ${clockLabel}`;
  const progressPercent = duration > 0 ? Math.min(100, (position / duration) * 100) : 0;
  for (const slider of [elements.storageInlineSeekSlider, elements.storagePreviewSeekSlider]) {
    if (!slider) continue;
    slider.max = String(duration);
    if (!state.storagePreviewPlaybackMode.seeking) slider.value = String(position);
    slider.disabled = !fileManagerActive || duration <= 0;
  }
  updateStorageMeter(elements.storageInlineTrackMeter, "[data-storage-track-text]", label, progressPercent);
  if (elements.storageInlineSeekSlider) elements.storageInlineSeekSlider.setAttribute("aria-valuetext", label);
  if (elements.storagePreviewProgressLabel) {
    elements.storagePreviewProgressLabel.textContent = buffering && duration <= 0
      ? "Reading track duration..."
      : (fileManagerActive ? clockLabel : (hasPreviewItem ? "Ready on device" : clockLabel));
  }
}

function updateStorageMeter(meter, textSelector, label, percent) {
  if (!meter) return;
  const normalizedPercent = Math.min(100, Math.max(0, Number(percent) || 0));
  meter.style.setProperty("--meter-width", `${meter.clientWidth}px`);
  meter.style.setProperty("--meter-progress", `${normalizedPercent}%`);
  meter.querySelectorAll(textSelector).forEach((node) => { node.textContent = label; });
  window.requestAnimationFrame(() => {
    const primary = meter.querySelector(`${textSelector}[data-meter-primary]`);
    const available = Math.max(0, meter.clientWidth - 30);
    meter.classList.toggle("is-overflowing", Boolean(primary && primary.scrollWidth > available));
  });
}

function updateStorageVolumeMeter(value) {
  const volume = Math.min(100, Math.max(0, Number(value) || 0));
  updateStorageMeter(elements.storageInlineVolumeMeter, "[data-storage-volume-text]", `Volume ${Math.round(volume)}%`, volume);
  elements.storageInlineVolumeSlider?.setAttribute("aria-valuetext", `Volume ${Math.round(volume)}%`);
}

function parseSynchsafeInt(bytes) {
  return ((bytes[0] & 0x7f) << 21) | ((bytes[1] & 0x7f) << 14) | ((bytes[2] & 0x7f) << 7) | (bytes[3] & 0x7f);
}

function decodeId3Text(frameBytes) {
  if (!frameBytes?.length) {
    return "";
  }

  const encoding = frameBytes[0];
  const body = frameBytes.slice(1);
  if (!body.length) {
    return "";
  }

  try {
    if (encoding === 0) {
      return new TextDecoder("latin1").decode(body).replace(/\u0000/g, " ").trim();
    }
    if (encoding === 1) {
      if (body[0] === 0xfe && body[1] === 0xff) {
        return new TextDecoder("utf-16be").decode(body.slice(2)).replace(/\u0000/g, " ").trim();
      }
      if (body[0] === 0xff && body[1] === 0xfe) {
        return new TextDecoder("utf-16le").decode(body.slice(2)).replace(/\u0000/g, " ").trim();
      }
      return new TextDecoder("utf-16le").decode(body).replace(/\u0000/g, " ").trim();
    }
    if (encoding === 2) {
      return new TextDecoder("utf-16be").decode(body).replace(/\u0000/g, " ").trim();
    }
    if (encoding === 3) {
      return new TextDecoder("utf-8").decode(body).replace(/\u0000/g, " ").trim();
    }
  } catch {
  }

  return "";
}

function findId3Delimiter(bytes, start, encoding) {
  if (encoding === 0 || encoding === 3) {
    const end = bytes.indexOf(0, start);
    return end >= 0 ? end : bytes.length;
  }

  for (let index = start; index + 1 < bytes.length; index += 1) {
    if (bytes[index] === 0 && bytes[index + 1] === 0) {
      return index;
    }
  }
  return bytes.length;
}

function parseId3Metadata(arrayBuffer) {
  const bytes = new Uint8Array(arrayBuffer);
  if (bytes.length < 10 || String.fromCharCode(...bytes.slice(0, 3)) !== "ID3") {
    return {};
  }

  const version = bytes[3];
  const tagSize = parseSynchsafeInt(bytes.slice(6, 10));
  const limit = Math.min(bytes.length, 10 + tagSize);
  let cursor = 10;
  const metadata = {};

  while (cursor + 10 <= limit) {
    const frameId = String.fromCharCode(...bytes.slice(cursor, cursor + 4));
    if (!frameId.trim()) {
      break;
    }

    const frameSizeBytes = bytes.slice(cursor + 4, cursor + 8);
    const frameSize = version === 4
      ? parseSynchsafeInt(frameSizeBytes)
      : ((frameSizeBytes[0] << 24) | (frameSizeBytes[1] << 16) | (frameSizeBytes[2] << 8) | frameSizeBytes[3]);
    if (!frameSize || cursor + 10 + frameSize > limit) {
      break;
    }

    const frame = bytes.slice(cursor + 10, cursor + 10 + frameSize);
    if (frameId === "TIT2") {
      metadata.title = decodeId3Text(frame) || metadata.title;
    } else if (frameId === "TPE1") {
      metadata.artist = decodeId3Text(frame) || metadata.artist;
    } else if (frameId === "TALB") {
      metadata.album = decodeId3Text(frame) || metadata.album;
    } else if (frameId === "APIC" && frame.length > 4) {
      const encoding = frame[0];
      const mimeEnd = frame.indexOf(0, 1);
      if (mimeEnd > 1) {
        const mimeType = new TextDecoder("latin1").decode(frame.slice(1, mimeEnd));
        const descriptionEnd = findId3Delimiter(frame, mimeEnd + 2, encoding);
        const imageStart = descriptionEnd + ((encoding === 0 || encoding === 3) ? 1 : 2);
        if (imageStart < frame.length) {
          metadata.artworkBytes = frame.slice(imageStart);
          metadata.artworkType = mimeType || "image/jpeg";
        }
      }
    }

    cursor += 10 + frameSize;
  }

  return metadata;
}

function filenameStem(value) {
  return String(value || "")
    .replace(/^.*[\\/]/, "")
    .replace(/\.[^.]+$/, "")
    .replace(/[_-]+/g, " ")
    .replace(/\s+/g, " ")
    .trim();
}

function normalizedArtworkSearchTerm(value) {
  return String(value || "")
    .replace(/\.[^.]+$/, "")
    .replace(/^\s*\d+\s*[\].\-_:)]*\s*/i, "")
    .replace(/\bfeat\.?\b.*$/i, "")
    .replace(/\bft\.?\b.*$/i, "")
    .replace(/[_-]+/g, " ")
    .replace(/\s+/g, " ")
    .trim();
}

function setStoragePreviewArtworkStatus(message) {
  if (elements.storagePreviewArtworkStatus) {
    elements.storagePreviewArtworkStatus.textContent = message || "";
  }
}

function storageFilenameStem(value) {
  return String(value || "")
    .replace(/^.*[\\/]/, "")
    .replace(/\.[^.]+$/, "");
}

function storageParentDirectoryPath(path) {
  const normalized = normalizeStorageDirectoryPath(path);
  if (normalized === "/") {
    return "/";
  }
  const lastSlash = normalized.lastIndexOf("/");
  return lastSlash <= 0 ? "/" : normalized.slice(0, lastSlash);
}

function artworkExtensionFromType(contentType = "") {
  const normalized = String(contentType || "").toLowerCase();
  if (normalized.includes("png")) {
    return "png";
  }
  if (normalized.includes("webp")) {
    return "webp";
  }
  if (normalized.includes("jpeg") || normalized.includes("jpg")) {
    return "jpg";
  }
  return "jpg";
}

function localArtworkCandidatePaths(path) {
  const directoryPath = storageParentDirectoryPath(path);
  const stem = storageFilenameStem(path);
  const extensions = ["jpg", "jpeg", "png", "webp"];
  const candidates = [];

  for (const extension of extensions) {
    candidates.push(storageJoinPath(directoryPath, `${stem}.cover.${extension}`));
  }
  for (const basename of ["cover", "folder", "front", "album"]) {
    for (const extension of extensions) {
      candidates.push(storageJoinPath(directoryPath, `${basename}.${extension}`));
    }
  }

  return candidates.filter(Boolean);
}

async function applyStoragePreviewArtworkSource(sourceUrl) {
  if (!sourceUrl || !elements.storagePreviewArtwork) {
    return false;
  }

  const image = elements.storagePreviewArtwork;
  const loaded = await new Promise((resolve) => {
    const cleanup = () => {
      image.onload = null;
      image.onerror = null;
    };
    image.onload = () => {
      cleanup();
      resolve(true);
    };
    image.onerror = () => {
      cleanup();
      resolve(false);
    };
    image.src = sourceUrl;
    image.hidden = false;
  });

  if (loaded && elements.storagePreviewArtworkFallback) {
    elements.storagePreviewArtworkFallback.hidden = true;
  }
  if (!loaded) {
    image.hidden = true;
    image.removeAttribute("src");
    if (elements.storagePreviewArtworkFallback) {
      elements.storagePreviewArtworkFallback.hidden = false;
    }
  }
  return loaded;
}

async function fetchRemoteArtworkUrl({ title = "", artist = "", album = "", fileName = "" } = {}) {
  const searchTerms = [
    [artist, album].filter(Boolean).join(" "),
    [artist, title].filter(Boolean).join(" "),
    [album, title].filter(Boolean).join(" "),
    title,
    album,
    normalizedArtworkSearchTerm(fileName),
    filenameStem(fileName),
  ].map((term) => normalizedArtworkSearchTerm(term)).filter(Boolean);

  const uniqueTerms = [...new Set(searchTerms)];

  for (const term of uniqueTerms) {
    try {
      const response = await fetch(`https://itunes.apple.com/search?term=${encodeURIComponent(term)}&entity=song&limit=10`, {
        cache: "no-store",
      });
      if (!response.ok) {
        continue;
      }
      const payload = await response.json();
      const results = Array.isArray(payload?.results) ? payload.results : [];
      const artworkUrl = results
        .map((item) => item?.artworkUrl100 || item?.artworkUrl60 || item?.artworkUrl30 || "")
        .find(Boolean);
      if (artworkUrl) {
        return artworkUrl.replace(/\/[0-9]+x[0-9]+bb\./, "/600x600bb.");
      }
    } catch {
    }
  }

  return "";
}

async function findLocalArtworkUrl(entry, target = state.activeStorageTarget) {
  if (!entry?.path) {
    return "";
  }

  const resolvedTarget = resolveStorageTarget(target);
  const availableEntries = activeStorageEntries(resolvedTarget)
    .filter((candidate) => !candidate?.isDirectory)
    .map((candidate) => String(candidate.path || ""));
  for (const candidatePath of localArtworkCandidatePaths(entry.path)) {
    if (availableEntries.includes(candidatePath)) {
      return storageStreamUrl(candidatePath, resolvedTarget);
    }
  }

  if (resolvedTarget === "sd") {
    return "";
  }

  const directoryPath = storageParentDirectoryPath(entry.path);
  try {
    const payload = await request(`/api/storage?target=${encodeURIComponent(resolvedTarget)}&dir=${encodeURIComponent(directoryPath)}`);
    const remoteEntries = Array.isArray(payload?.entries) ? payload.entries : [];
    const remotePaths = new Set(remoteEntries.filter((candidate) => !candidate?.isDirectory).map((candidate) => String(candidate.path || "")));
    for (const candidatePath of localArtworkCandidatePaths(entry.path)) {
      if (remotePaths.has(candidatePath)) {
        return storageStreamUrl(candidatePath, resolvedTarget);
      }
    }
  } catch {
  }

  return "";
}

async function uploadArtworkToStorage(entry, artworkBlob, contentType = "") {
  if (!entry?.path || !(artworkBlob instanceof Blob) || artworkBlob.size <= 0) {
    return "";
  }

  const directoryPath = storageParentDirectoryPath(entry.path);
  const filename = `${storageFilenameStem(entry.path)}.cover.${artworkExtensionFromType(contentType || artworkBlob.type)}`;
  const formData = new FormData();
  formData.append("file", artworkBlob, filename);

  const response = await fetch(`/api/storage/upload?target=${encodeURIComponent(state.activeStorageTarget)}&dir=${encodeURIComponent(directoryPath)}`, {
    method: "POST",
    body: formData,
  });
  if (!response.ok) {
    const text = await response.text();
    throw new Error(text || "Artwork upload failed.");
  }

  try {
    const payload = await response.json();
    return String(payload?.path || storageJoinPath(directoryPath, filename));
  } catch {
    return storageJoinPath(directoryPath, filename);
  }
}

async function cacheRemoteArtworkForEntry(entry, remoteArtworkUrl) {
  if (!entry?.path || !remoteArtworkUrl) {
    return "";
  }

  if (state.storagePreviewTarget === "sd" || shouldDeferSdReads()) {
    return "";
  }

  try {
    const response = await fetch(remoteArtworkUrl, { cache: "no-store" });
    if (!response.ok) {
      return "";
    }
    const artworkBlob = await response.blob();
    const cachedPath = await uploadArtworkToStorage(entry, artworkBlob, response.headers.get("content-type") || artworkBlob.type);
    return cachedPath;
  } catch {
    return "";
  }
}

function resetStoragePreviewUi() {
  if (elements.storagePreviewArtwork) {
    elements.storagePreviewArtwork.hidden = true;
    elements.storagePreviewArtwork.removeAttribute("src");
  }
  if (elements.storagePreviewArtworkFallback) {
    elements.storagePreviewArtworkFallback.hidden = false;
  }
  updateStoragePreviewPath("");
  updateStoragePreviewProgressUi();
  setStoragePreviewArtworkStatus("Waiting for artwork lookup.");
}


function setStoragePreviewSummary({ title, artist, album, fileName, sizeBytes, path } = {}) {
  const resolvedTitle = title || fileName || path || "Track Preview";
  const resolvedArtist = artist || "Unknown artist";
  const resolvedAlbum = album || "Unknown album";
  const resolvedFileName = fileName || path || resolvedTitle;

  if (elements.storagePreviewTitle) {
    elements.storagePreviewTitle.textContent = resolvedTitle;
  }
  if (elements.storagePreviewMeta) {
    elements.storagePreviewMeta.textContent = `${resolvedArtist} • ${resolvedAlbum} • ${formatBytes(sizeBytes || 0)}`;
  }
  updateStoragePreviewPath(path || resolvedFileName);
  if (elements.storagePreviewAlbum) {
    elements.storagePreviewAlbum.textContent = `${resolvedTitle}\n${resolvedArtist}\n${resolvedAlbum || resolvedFileName}`;
  }
}

function storageJoinPath(directoryPath, name) {
  const leaf = String(name || "").trim().replaceAll("\\", "/").replace(/^\/+/, "").replace(/\/+$/, "");
  if (!leaf) {
    return "";
  }
  const dir = normalizeStorageDirectoryPath(directoryPath);
  return dir === "/" ? `/${leaf}` : `${dir}/${leaf}`;
}

function storagePlaybackRef(path, target = state.activeStorageTarget) {
  return `${resolveStorageTarget(target)}:${normalizeStorageDirectoryPath(path)}`;
}

function syncStoragePlaybackFromStatus(status = state.status) {
  const playback = status?.playback || {};
  const source = String(playback.source || "");
  const url = String(playback.url || "").replaceAll("\\", "/");
  const match = /^(sd|flash):(\/.*)$/i.exec(url);
  if (!source.startsWith("file-manager") || !match || !isPlaybackActive(status)) {
    if (!isPlaybackActive(status)) state.storagePlaybackFocusUrl = "";
    return;
  }

  const target = resolveStorageTarget(match[1].toLowerCase());
  const path = normalizeStorageDirectoryPath(match[2]);
  const entry = activeStorageAudioEntries(target).find((candidate) => normalizeStorageDirectoryPath(candidate.path) === path) || {
    path,
    name: storageBaseName(path),
    isDirectory: false,
    sizeBytes: 0,
  };
  state.activeStorageTarget = target;
  state.storagePreviewTarget = target;
  state.storagePreviewItem = entry;
  setStorageSelection([path], target);

  if (activeTabName() !== "storage-external" || target !== "sd") {
    return;
  }
  const parent = storageParentPath(path) || "/";
  if (state.storagePlaybackFocusUrl === url) {
    return;
  }
  state.storagePlaybackFocusUrl = url;
  storageTab?.openStorageManager(target, parent)
    .then(() => scrollStorageTrackIntoView(path))
    .catch(handleError);
}

function storagePlaybackPathFromStatus(status = state.status, target = state.activeStorageTarget) {
  const resolvedTarget = resolveStorageTarget(target);
  const playbackUrl = String(status?.playback?.url || "").replaceAll("\\", "/");
  const prefix = `${resolvedTarget}:`;
  return playbackUrl.toLowerCase().startsWith(prefix)
    ? normalizeStorageDirectoryPath(playbackUrl.slice(prefix.length))
    : "";
}

function currentStoragePlayingEntry(target = state.activeStorageTarget, status = state.status) {
  const playingPath = storagePlaybackPathFromStatus(status, target);
  return playingPath
    ? activeStorageAudioEntries(target).find((entry) => normalizeStorageDirectoryPath(entry.path) === playingPath) || null
    : null;
}

function storageEntryIsPlaying(entry, target = state.activeStorageTarget, status = state.status) {
  if (!entry || !isPlaybackActive(status)) {
    return false;
  }
  return storagePlaybackRef(entry.path, target) === String(status?.playback?.url || "");
}

function isFileManagerPlayback(status = state.status) {
  return isForegroundPlaybackActive(status) && String(status?.playback?.source || "") === "file-manager";
}

function prepareFolderPlaybackQueue() {
  const playingEntry = currentStoragePlayingEntry();
  if (playingEntry) {
    state.storagePreviewItem = playingEntry;
    state.storagePreviewTarget = state.activeStorageTarget;
  }
}

async function stepFolderPlayback(delta) {
  prepareFolderPlaybackQueue();
  await advanceStoragePreviewTrack(delta, {
    autoplayDevice: true,
    respectModes: true,
    showPreview: false,
  });
}

async function stepContextPlayback(delta) {
  if (isFileManagerPlayback()) {
    await stepFolderPlayback(delta);
    return;
  }
  await stepRadioStationSelection(delta);
}

async function seekFileManagerPlayback(deltaSeconds) {
  if (!isFileManagerPlayback() || state.storagePreviewPlaybackMode.seeking) return;
  const playback = state.status?.playback || {};
  const duration = Math.max(0, Number(playback.durationSeconds || 0));
  if (duration <= 0) return;
  const positionSeconds = Math.min(duration, Math.max(0, Math.round(Number(playback.positionSeconds || 0) + deltaSeconds)));
  state.storagePreviewPlaybackMode.seeking = true;
  try {
    await request("/api/playback/seek", { method: "POST", body: JSON.stringify({ positionSeconds }) });
    playback.positionSeconds = positionSeconds;
    updateStoragePreviewProgressUi();
  } finally {
    state.storagePreviewPlaybackMode.seeking = false;
  }
}

function bindPlaybackStepButton(button, delta, clickAction) {
  if (!button) return;
  let holdTimer = 0;
  let repeatTimer = 0;
  let suppressNextClick = false;

  const stopHolding = () => {
    window.clearTimeout(holdTimer);
    window.clearInterval(repeatTimer);
    holdTimer = 0;
    repeatTimer = 0;
    button.classList.remove("holding");
  };

  button.addEventListener("pointerdown", (event) => {
    if (event.button !== 0 || button.disabled || !isFileManagerPlayback()) return;
    holdTimer = window.setTimeout(() => {
      suppressNextClick = true;
      button.classList.add("holding");
      seekFileManagerPlayback(delta * 10).catch(handleError);
      repeatTimer = window.setInterval(() => seekFileManagerPlayback(delta * 10).catch(handleError), 320);
    }, 440);
  });
  button.addEventListener("pointerup", stopHolding);
  button.addEventListener("pointercancel", stopHolding);
  button.addEventListener("pointerleave", (event) => {
    if (event.buttons) stopHolding();
  });
  button.addEventListener("click", (event) => {
    if (suppressNextClick) {
      suppressNextClick = false;
      event.preventDefault();
      return;
    }
    clickAction().catch(handleError);
  });
}

function scrollStorageTrackIntoView(path, behavior = "smooth") {
  if (!path || !elements.storageFileList) {
    return;
  }
  const row = [...elements.storageFileList.querySelectorAll(".storage-file-row")]
    .find((candidate) => candidate.dataset.storagePath === path);
  if (!row) return;
  const listRect = elements.storageFileList.getBoundingClientRect();
  const rowRect = row.getBoundingClientRect();
  const targetTop = elements.storageFileList.scrollTop + rowRect.top - listRect.top - 4;
  elements.storageFileList.scrollTo({ top: Math.max(0, targetTop), behavior });
}

function renderStorageBreadcrumbs(currentPath) {
  if (!elements.storageBreadcrumbs || !elements.storageUpButton) {
    return;
  }

  const normalized = normalizeStorageDirectoryPath(currentPath);
  const segments = normalized.split("/").filter(Boolean);
  const crumbs = [{ label: "Root", path: "/", current: normalized === "/" }];
  let runningPath = "";
  for (const segment of segments) {
    runningPath += `/${segment}`;
    crumbs.push({ label: runningPath, path: runningPath, current: runningPath === normalized });
  }

  elements.storageBreadcrumbs.innerHTML = `
    <div class="storage-crumb-trail">
      ${crumbs.map((crumb, index) => (
        `<button type="button" class="storage-crumb storage-crumb-inline${crumb.current ? ' storage-crumb-current' : ''}" data-storage-nav="${escapeHtml(crumb.path)}"${crumb.current ? ' aria-current="page"' : ''}>${escapeHtml(index === 0 ? crumb.label : crumb.label)}</button>`
      )).join("")}
    </div>
  `;
  elements.storageUpButton.disabled = normalized === "/";
}

function renderStorageRow(entry, selectionMode, selectedPaths) {
  const path = String(entry?.path || "");
  const isSelected = selectedPaths.includes(path);
  const typeClass = entry?.isDirectory ? "folder" : "file";
  const checkboxId = `storage-checkbox-${dynamicFieldToken(path || entry?.name || "item")}`;
  const checkboxName = `storage.checkbox.${dynamicFieldToken(path || entry?.name || "item")}`;
  const playing = storageEntryIsPlaying(entry);
  return `
    <div class="storage-file-row ${entry?.isDirectory ? "storage-folder-row" : ""} ${isSelected ? "selected" : ""} ${playing ? "playing" : ""}" data-storage-path="${escapeHtml(path)}" data-storage-kind="${entry?.isDirectory ? "folder" : "file"}" tabindex="0" aria-selected="${isSelected ? "true" : "false"}" ${playing ? 'aria-current="true"' : ""}>
      <label class="storage-entry-check" for="${checkboxId}" ${selectionMode ? "" : "hidden"}>
        <input id="${checkboxId}" name="${checkboxName}" type="checkbox" data-storage-checkbox="${escapeHtml(path)}" ${isSelected ? "checked" : ""} aria-label="Select ${escapeHtml(entry?.name || path || "item")}">
      </label>
      <span class="storage-entry-icon ${typeClass}" aria-hidden="true">${storageEntryIconSvg(entry)}</span>
      <div class="storage-file-meta">
        <div class="storage-file-name">${escapeHtml(entry?.name || path || (entry?.isDirectory ? "Folder" : "File"))}</div>
        <div class="storage-file-subtitle">${escapeHtml(storageItemSubtitle(entry))}</div>
      </div>
      <div class="storage-file-trailing">
        <div class="storage-file-badge">${escapeHtml(playing ? "Playing" : storageBadgeLabel(entry))}</div>
        <span class="storage-file-chevron" aria-hidden="true">&#8250;</span>
      </div>
    </div>
  `;
}

function setStorageSelectionMode(enabled) {
  state.storageSelectionMode = Boolean(enabled);
  if (!state.storageSelectionMode && activeStorageSelection().length > 1) {
    setStorageSelection(activeStorageSelection().slice(0, 1));
  }
  rerenderStorageManager();
}

function toggleStorageSelection(path, { additive = false } = {}) {
  if (!path) {
    return;
  }

  const current = activeStorageSelection();
  if (!state.storageSelectionMode) {
    setStorageSelection([path]);
    rerenderStorageManager();
    return;
  }

  if (!additive) {
    setStorageSelection(current.includes(path) && current.length === 1 ? [] : [path]);
  } else if (current.includes(path)) {
    setStorageSelection(current.filter((item) => item !== path));
  } else {
    setStorageSelection([...current, path]);
  }
  rerenderStorageManager();
}

function selectAllStorageEntries() {
  const entries = activeStorageEntries();
  if (!entries.length) {
    return;
  }

  const allPaths = entries.map((entry) => entry.path).filter(Boolean);
  const selectedPaths = activeStorageSelection();
  const allSelected = allPaths.every((path) => selectedPaths.includes(path));
  setStorageSelection(allSelected ? [] : allPaths);
  rerenderStorageManager();
}

async function deleteSelectedStorageItems() {
  const selectedPaths = activeStorageSelection();
  if (!selectedPaths.length) {
    setStorageStatus("Select a file or folder first.", true);
    return;
  }

  const prompt = selectedPaths.length === 1
    ? `Delete ${selectedPaths[0]}?`
    : `Delete ${selectedPaths.length} selected items?`;
  if (!window.confirm(prompt)) {
    return;
  }

  setStorageStatus(`Deleting ${selectedPaths.length === 1 ? selectedPaths[0] : `${selectedPaths.length} items`}...`);
  for (const path of selectedPaths) {
    await request(`/api/storage/delete?target=${encodeURIComponent(state.activeStorageTarget)}&dir=${encodeURIComponent(state.currentStoragePathByTarget[state.activeStorageTarget] || "/")}`, {
      method: "POST",
      body: JSON.stringify({ path }),
    });
  }

  clearStorageSelection();
  if (state.storageSelectionMode) {
    state.storageSelectionMode = false;
  }
  const payload = await refreshStorageManager(state.activeStorageTarget);
  await loadStatus();
  setStorageStatus(payload?.message || (selectedPaths.length === 1 ? `Deleted ${selectedPaths[0]}` : `Deleted ${selectedPaths.length} items`));
  toast(selectedPaths.length === 1 ? `Deleted ${selectedPaths[0]}` : `Deleted ${selectedPaths.length} items`);
}

function closeStoragePreview() {
  if (state.storagePreviewAudio) {
    state.storagePreviewAudio.pause();
    state.storagePreviewAudio.currentTime = 0;
    state.storagePreviewAudio.removeAttribute("src");
  }
  state.storagePreviewPlaybackMode.deviceActive = false;
  state.storagePreviewPlaybackMode.previousDeviceActive = false;
  state.storagePreviewPlaybackMode.suppressAutoAdvance = false;
  releaseStoragePreviewUrls();
  state.storagePreviewItem = null;
  resetStoragePreviewUi();
  updateStoragePreviewPlaybackControls();
  if (elements.storagePreviewMeta) {
    elements.storagePreviewMeta.textContent = "Select a track to preview it.";
  }
  updateStoragePreviewPath("");
  if (elements.storagePreviewAlbum) {
    elements.storagePreviewAlbum.textContent = "Album art unavailable";
  }
  state.storagePreviewTarget = state.activeStorageTarget;
  if (elements.storagePreviewModal?.open) {
    elements.storagePreviewModal.close();
  }
}

async function queueStoragePlayback(entry, target = state.activeStorageTarget) {
  if (!entry || entry.isDirectory || !isAudioStoragePath(entry.path)) {
    setStorageStatus("Select one audio file first.", true);
    return false;
  }

  const resolvedTarget = resolveStorageTarget(target);
  state.storagePreviewItem = entry;
  state.storagePreviewTarget = resolvedTarget;
  setStorageSelection([entry.path], resolvedTarget);
  state.storagePreviewPlaybackMode.suppressAutoAdvance = false;
  if (resolvedTarget === state.activeStorageTarget) {
    rerenderStorageManager(resolvedTarget);
  }
  const payload = {
    url: storagePlaybackRef(entry.path, resolvedTarget),
    label: normalizePlaybackTitle(entry.name || entry.path, entry.path),
    type: "file-manager",
  };

  await request("/api/play", { method: "POST", body: JSON.stringify(payload) });
  state.recentPlayback.unshift(payload);
  state.recentPlayback = state.recentPlayback.filter((item, index, array) => index === array.findIndex((candidate) => candidate.url === item.url && candidate.type === item.type));
  saveRecentPlayback();
  renderRecentPlayback();

  const started = await pollStatusUntil(
    (status) => {
      const playbackState = String(status?.playback?.state || "idle");
      const playbackUrl = String(status?.playback?.url || "");
      return (playbackState === "playing" || playbackState === "buffering") && playbackUrl === payload.url;
    },
    12,
    150,
  );

  setMessage(started ? `Playing ${payload.label}` : `Playback queued for ${payload.label}`);
  setStorageStatus(started ? `Playing ${payload.label}` : `Playback queued for ${payload.label}`);
  if (!started) {
    await loadStatus();
  }

  updateStorageFolderPlaybackStatus();
  return started;
}

async function playStoragePreviewOnDevice() {
  const entry = state.storagePreviewItem;
  if (!entry) {
    return;
  }

  state.storagePreviewPlaybackMode.suppressAutoAdvance = false;
  const started = await queueStoragePlayback(entry, state.storagePreviewTarget);
  state.storagePreviewPlaybackMode.deviceActive = started;
  updateStoragePreviewPlaybackControls();
}

async function stopStoragePreviewPlayback() {
  if (state.storagePreviewAudio) {
    state.storagePreviewAudio.pause();
    state.storagePreviewAudio.currentTime = 0;
  }
  state.storagePreviewPlaybackMode.suppressAutoAdvance = true;
  await request("/api/stop", { method: "POST", body: JSON.stringify({}) });
  state.storagePreviewPlaybackMode.deviceActive = false;
  state.storagePreviewPlaybackMode.previousDeviceActive = false;
  updateStoragePreviewPlaybackControls();
  await loadStatus();
  setStorageStatus("Playback stopped");
}

async function toggleStoragePreviewPlayback() {
  if (state.storagePreviewPlaybackMode.deviceActive) {
    await stopStoragePreviewPlayback();
    return;
  }
  await playStoragePreviewOnDevice();
}

async function activateStoragePreviewEntry(entry, { autoplayDevice = false, showPreview = Boolean(elements.storagePreviewModal?.open) } = {}) {
  if (showPreview) {
    await openStoragePreview(entry);
  } else {
    state.storagePreviewItem = entry;
    state.storagePreviewTarget = state.activeStorageTarget;
    setStorageSelection([entry.path], state.activeStorageTarget);
    updateStoragePreviewPlaybackControls();
  }
  if (autoplayDevice) {
    await playStoragePreviewOnDevice();
  } else {
    state.storagePreviewPlaybackMode.deviceActive = false;
    updateStoragePreviewPlaybackControls();
  }
}

async function hydrateStoragePreviewMetadataAndArtwork(entry, requestId) {
  if (!entry || requestId !== state.storagePreviewRequestId) {
    return;
  }

  const previewTarget = resolveStorageTarget(state.storagePreviewTarget);
  let title = entry.name || entry.path || "Track Preview";
  let artist = "Unknown artist";
  let album = "Unknown album";
  let artworkApplied = false;
  const extension = storageFileExtension(entry.path);
  const canScanEmbedded = previewTarget !== "sd" && extension === "mp3" && Number(entry.sizeBytes || 0) > 0 &&
    Number(entry.sizeBytes || 0) <= STORAGE_PREVIEW_EMBEDDED_SCAN_MAX_BYTES;

  if (canScanEmbedded) {
    setStoragePreviewArtworkStatus("Checking embedded artwork...");
    try {
      const response = await fetch(storageStreamUrl(entry.path, state.storagePreviewTarget), { cache: "no-store" });
      const blob = await response.blob();
      if (requestId !== state.storagePreviewRequestId) {
        return;
      }

      const metadata = parseId3Metadata(await blob.arrayBuffer());
      if (requestId !== state.storagePreviewRequestId) {
        return;
      }

      title = metadata.title || title;
      artist = metadata.artist || artist;
      album = metadata.album || album;
      setStoragePreviewSummary({
        title,
        artist,
        album,
        fileName: entry.name,
        sizeBytes: entry.sizeBytes || blob.size,
        path: entry.path,
      });

      if (metadata.artworkBytes?.length) {
        const artworkType = String(metadata.artworkType || "image/jpeg").startsWith("image/")
          ? metadata.artworkType
          : "image/jpeg";
        state.storagePreviewArtworkUrl = URL.createObjectURL(new Blob([metadata.artworkBytes], { type: artworkType }));
        artworkApplied = await applyStoragePreviewArtworkSource(state.storagePreviewArtworkUrl);
        if (artworkApplied) {
          setStoragePreviewArtworkStatus("Loaded embedded artwork from file.");
        }
      }
    } catch (error) {
      console.warn("Embedded artwork scan failed", error);
    }
  }

  if (requestId !== state.storagePreviewRequestId) {
    return;
  }

  if (!artworkApplied) {
    setStoragePreviewArtworkStatus(canScanEmbedded
      ? "Checking cached artwork in this folder..."
      : (previewTarget === "sd"
        ? "Skipping SD artwork scan so playback stays immediate."
        : "Skipping heavy embedded scan. Checking cached artwork in this folder..."));
    const localArtworkUrl = previewTarget === "sd" ? "" : await findLocalArtworkUrl(entry, previewTarget);
    if (requestId !== state.storagePreviewRequestId) {
      return;
    }
    if (localArtworkUrl) {
      artworkApplied = await applyStoragePreviewArtworkSource(localArtworkUrl);
      if (artworkApplied) {
        setStoragePreviewArtworkStatus("Loaded cached artwork from storage.");
      }
    }
  }

  if (requestId !== state.storagePreviewRequestId) {
    return;
  }

  if (!artworkApplied && navigator.onLine !== false) {
    setStoragePreviewArtworkStatus("Searching online artwork...");
    const remoteArtworkUrl = await fetchRemoteArtworkUrl({
      title,
      artist: artist === "Unknown artist" ? "" : artist,
      album: album === "Unknown album" ? "" : album,
      fileName: entry.name || entry.path,
    });
    if (requestId !== state.storagePreviewRequestId) {
      return;
    }
    if (remoteArtworkUrl) {
      artworkApplied = await applyStoragePreviewArtworkSource(remoteArtworkUrl);
      if (artworkApplied) {
        if (previewTarget === "sd") {
          setStoragePreviewArtworkStatus("Loaded online artwork.");
        } else {
          setStoragePreviewArtworkStatus("Loaded online artwork. Saving cache for offline use...");
          const cachedPath = await cacheRemoteArtworkForEntry(entry, remoteArtworkUrl);
          if (requestId !== state.storagePreviewRequestId) {
            return;
          }
          setStoragePreviewArtworkStatus(cachedPath
            ? "Loaded online artwork and cached it for offline use."
            : "Loaded online artwork.");
          if (cachedPath && storageParentPath(cachedPath) === normalizeStorageDirectoryPath(state.currentStoragePathByTarget[state.storagePreviewTarget] || "/")) {
            injectVisibleStorageEntry(state.storagePreviewTarget, {
              path: cachedPath,
              name: storageBaseName(cachedPath),
              isDirectory: false,
              sizeBytes: 0,
              url: storageStreamUrl(cachedPath, state.storagePreviewTarget),
            });
          }
        }
      }
    }
  }

  if (requestId !== state.storagePreviewRequestId) {
    return;
  }

  if (!artworkApplied) {
    setStoragePreviewArtworkStatus(navigator.onLine === false
      ? "No embedded or cached artwork found. Device is offline."
      : "No artwork found in file, folder cache, or online search.");
  }

  setStoragePreviewSummary({
    title,
    artist,
    album,
    fileName: entry.name,
    sizeBytes: entry.sizeBytes,
    path: entry.path,
  });
}

async function advanceStoragePreviewTrack(delta, options = {}) {
  const { autoplayDevice = false, respectModes = false, showPreview = Boolean(elements.storagePreviewModal?.open) } = options;
  const entries = activeStorageAudioEntries();
  if (!entries.length) {
    return;
  }

  const currentIndex = currentStoragePreviewQueueIndex();
  if (currentIndex < 0) {
    await activateStoragePreviewEntry(entries[0], { autoplayDevice, showPreview });
    return;
  }

  let nextIndex = currentIndex;
  if (respectModes && state.storagePreviewPlaybackMode.shuffle && entries.length > 1) {
    do {
      nextIndex = Math.floor(Math.random() * entries.length);
    } while (nextIndex === currentIndex);
  } else {
    nextIndex = currentIndex + delta;
    if (nextIndex < 0 || nextIndex >= entries.length) {
      if (!state.storagePreviewPlaybackMode.loop && !respectModes) {
        nextIndex = Math.max(0, Math.min(entries.length - 1, nextIndex));
      } else if (!state.storagePreviewPlaybackMode.loop && respectModes) {
        state.storagePreviewPlaybackMode.deviceActive = false;
        updateStoragePreviewPlaybackControls();
        return;
      } else {
        nextIndex = nextIndex < 0 ? entries.length - 1 : 0;
      }
    }
  }

  if (nextIndex === currentIndex && entries.length === 1 && !state.storagePreviewPlaybackMode.loop && respectModes) {
    state.storagePreviewPlaybackMode.deviceActive = false;
    updateStoragePreviewPlaybackControls();
    return;
  }

  await activateStoragePreviewEntry(entries[nextIndex], { autoplayDevice, showPreview });
}

async function openStoragePreview(entry) {
  if (!entry || entry.isDirectory || !isAudioStoragePath(entry.path)) {
    return;
  }

  state.storagePreviewRequestId += 1;
  const requestId = state.storagePreviewRequestId;
  state.storagePreviewItem = entry;
  state.storagePreviewTarget = state.activeStorageTarget;
  releaseStoragePreviewUrls();
  resetStoragePreviewUi();

  if (elements.storagePreviewTitle) {
    elements.storagePreviewTitle.textContent = entry.name || "Track Preview";
  }
  if (elements.storagePreviewMeta) {
    elements.storagePreviewMeta.textContent = `${formatBytes(entry.sizeBytes || 0)} • ${storageBadgeLabel(entry)}`;
  }
  updateStoragePreviewPath(entry.path);
  if (elements.storagePreviewAlbum) {
    elements.storagePreviewAlbum.textContent = `${entry.name || entry.path}\nUnknown artist\nUnknown album`;
  }
  setStoragePreviewArtworkStatus("Playback can start immediately. Artwork is loading in the background.");
  elements.storagePreviewModal?.showModal();

  updateStoragePreviewPlaybackControls();
  hydrateStoragePreviewMetadataAndArtwork(entry, requestId).catch((error) => {
    if (requestId === state.storagePreviewRequestId) {
      console.warn("Preview metadata/artwork load failed", error);
      setStoragePreviewArtworkStatus("Artwork lookup failed.");
    }
  });
}

function renderStorageManager(payload) {
  const target = resolveStorageTarget(String(payload?.target || state.activeStorageTarget || "flash"));
  const label = storageTargetLabel(target);
  const storage = payload?.storage || state.storageInfoByTarget[target] || {};
  const currentPath = normalizeStorageDirectoryPath(payload?.currentPath || state.currentStoragePathByTarget[target] || "/");
  const incomingEntries = Array.isArray(payload?.entries) ? payload.entries : (Array.isArray(payload?.files) ? payload.files : []);
  const appendEntries = Boolean(payload?.append) && currentPath === state.currentStoragePathByTarget[target];
  const entries = appendEntries ? mergeStorageEntries(activeStorageEntries(target), incomingEntries) : incomingEntries;
  const meta = {
    offset: Number(payload?.offset || 0),
    returned: Number(payload?.returned || incomingEntries.length || 0),
    nextOffset: Number(payload?.nextOffset || entries.length || 0),
    hasMore: Boolean(payload?.hasMore),
    totalEntries: Number(payload?.totalEntries || activeStorageMeta(target).totalEntries || entries.length || 0),
    loadingMore: Boolean(payload?.loadingMore),
    requestId: Number(payload?.requestId || activeStorageMeta(target).requestId || 0),
  };
  state.activeStorageTarget = target;
  state.storageInfoByTarget[target] = storage;
  state.currentStorageEntriesByTarget[target] = entries;
  state.currentStoragePathByTarget[target] = currentPath;
  saveStorageLocation(target, currentPath);
  setStorageMeta(meta, target);

  if (elements.storageTitle) {
    elements.storageTitle.textContent = `${label} File Manager`;
  }
  renderStorageBreadcrumbs(currentPath);

  if (elements.storageSummary) {
    const loadedCount = entries.length;
    const progressSummary = formatLoadProgress(loadedCount, meta.totalEntries || loadedCount);
    const progressLabel = meta.loadingMore || meta.hasMore
      ? ` • showing ${progressSummary}${meta.loadingMore ? ", loading more..." : ""}`
      : "";
    const cardLabel = target === "sd" && Number(storage.cardSizeBytes || 0) > 0
      ? ` • card ${formatBytes(storage.cardSizeBytes || 0)}`
      : "";
    elements.storageSummary.textContent = storage.mounted
      ? `${formatBytes(storage.usedBytes || 0)} used of ${formatBytes(storage.totalBytes || 0)} filesystem • ${formatBytes(storage.freeBytes || 0)} free${cardLabel} • ${currentPath}${progressLabel}`
      : (target === "sd"
        ? "SD card is not mounted or not wired."
        : "Flash filesystem is not mounted.");
  }
  if (elements.storageLimit) {
    elements.storageLimit.textContent = storage.mounted
      ? `Max upload: ${formatBytes(storage.maxUploadBytes || 0)}`
      : "Uploads unavailable";
  }
  if (elements.storageUploadButton) {
    elements.storageUploadButton.disabled = !storage.mounted || state.storageUploadInProgress;
  }
  updateStorageToolbar(storage);
  if (!elements.storageFileList) {
    return;
  }

  if (!storage.mounted) {
    clearStorageSelection(target);
    updateStorageToolbar(storage);
    elements.storageFileList.innerHTML = `<div class="storage-empty-note">${label} is not mounted, so melody storage is unavailable.</div>`;
    return;
  }

  if (!entries.length) {
    clearStorageSelection(target);
    updateStorageToolbar(storage);
    elements.storageFileList.innerHTML = `<div class="storage-empty-note">${meta.loadingMore ? "Loading files..." : "This folder is empty."}</div>`;
    return;
  }

  const visiblePaths = entries.map((entry) => entry.path).filter(Boolean);
  setStorageSelection(activeStorageSelection(target).filter((path) => visiblePaths.includes(path)), target);
  const selectedPaths = activeStorageSelection(target);
  const loadingRow = meta.loadingMore
    ? '<div class="storage-empty-note">Loading more files...</div>'
    : "";
  elements.storageFileList.innerHTML = `${entries.map((entry) => renderStorageRow(entry, state.storageSelectionMode, selectedPaths)).join("")}${loadingRow}`;
  updateStorageToolbar(storage);
  updateStoragePreviewPlaybackControls();
}

async function loadMoreStorageEntries(target = state.activeStorageTarget) {
  const meta = activeStorageMeta(target);
  if (meta.loadingMore || !meta.hasMore) {
    return;
  }

  const requestId = Number(meta.requestId || state.storageListRequestId || 0);
  const directoryPath = state.currentStoragePathByTarget[target] || "/";
  setStorageMeta({ loadingMore: true }, target);
  rerenderStorageManager(target);
  const currentEntries = activeStorageEntries(target).length;
  const currentTotal = Number(meta.totalEntries || 0);
  setStorageStatus(`Loading files... ${formatLoadProgress(currentEntries, currentTotal)}`);
  try {
    const payload = await request(`/api/storage?target=${encodeURIComponent(target)}&dir=${encodeURIComponent(normalizeStorageDirectoryPath(directoryPath))}&offset=${encodeURIComponent(Number(meta.nextOffset || 0))}&limit=${encodeURIComponent(STORAGE_SCROLL_PAGE_SIZE)}`);
    if (requestId !== state.storageListRequestId) {
      return;
    }

    renderStorageManager({ ...payload, append: true, loadingMore: false, requestId });
    const loadedEntries = activeStorageEntries(target).length;
    const totalEntries = Number(payload?.totalEntries || activeStorageMeta(target).totalEntries || loadedEntries);
    if (!payload?.hasMore) {
      setStorageStatus("Ready");
    } else {
      setStorageStatus(`Loading files... ${formatLoadProgress(loadedEntries, totalEntries)}`);
    }
  } catch (error) {
    if (requestId === state.storageListRequestId) {
      setStorageMeta({ loadingMore: false }, target);
      rerenderStorageManager(target);
      setStorageStatus(`Unable to load more files: ${error.message}`, true);
    }
    throw error;
  }
}

async function refreshStorageManager(target = state.activeStorageTarget, directoryPath = state.currentStoragePathByTarget[target] || "/", options = {}) {
  const resolvedTarget = resolveStorageTarget(target);
  const normalizedDirectoryPath = normalizeStorageDirectoryPath(directoryPath);
  const requestId = state.storageListRequestId + 1;
  state.storageListRequestId = requestId;
  setStorageMeta({ loadingMore: false, requestId, hasMore: false, nextOffset: 0 }, resolvedTarget);
  const payload = await request(`/api/storage?${storageQueryParams({
    target: resolvedTarget,
    dir: normalizedDirectoryPath,
    offset: 0,
    limit: STORAGE_INITIAL_PAGE_SIZE,
    live: Boolean(options.live),
    reindex: Boolean(options.reindex),
  })}`);
  if (requestId !== state.storageListRequestId) {
    return payload;
  }

  renderStorageManager({ ...payload, append: false, loadingMore: false, requestId });
  if (payload?.hasMore) {
    setStorageStatus(`Loading files... ${formatLoadProgress(activeStorageEntries(resolvedTarget).length, Number(payload?.totalEntries || 0))}`);
  } else {
    setStorageMeta({ loadingMore: false, requestId }, resolvedTarget);
    rerenderStorageManager(resolvedTarget);
    setStorageStatus(`Ready • ${formatLoadProgress(activeStorageEntries(resolvedTarget).length, Number(payload?.totalEntries || activeStorageEntries(resolvedTarget).length))}`);
  }
  queueMicrotask(() => ensureStorageListFilled(resolvedTarget).catch((error) => console.error(error)));
  return payload;
}

function maybeRefreshVisibleStorageTab(force = false) {
  storageTab?.maybeRefreshVisibleStorageTab(force);
}

async function reindexEffectsFiles() {
  return effectsTab?.reindexEffectsFiles();
}

async function reindexStorageDirectory(target = state.activeStorageTarget, directoryPath = state.currentStoragePathByTarget[target] || "/") {
  const resolvedTarget = resolveStorageTarget(target);
  if (state.storageReindexInProgressByTarget[resolvedTarget]) {
    const message = "Storage reindex is already in progress.";
    setStorageStatus(message);
    toast(message);
    return;
  }
  if (resolvedTarget === "sd" && shouldDeferSdReads()) {
    setStorageStatus("Stopping playback before reindexing...");
    await stopPlayback();
    const playbackStopped = !shouldDeferSdReads() || await pollStatusUntil(
      (status) => {
        const playbackState = String(status?.playback?.state || "idle");
        return playbackState !== "playing" && playbackState !== "buffering";
      },
      32,
      150,
    );
    if (playbackStopped && !shouldDeferSdReads()) {
      setStorageStatus("Playback stopped. Starting reindex...");
    } else {
      const message = "Playback is still stopping. Try reindexing again in a moment.";
      setStorageStatus(message, true);
      toast(message);
      return;
    }
  }
  state.storageReindexInProgressByTarget[resolvedTarget] = true;
  if (elements.storageReindexButton) {
    elements.storageReindexButton.disabled = true;
  }
  try {
    setStorageStatus("Starting reindex...");
    await rebuildStorageIndexFromBrowser(resolvedTarget, directoryPath, (progress) => {
      setStorageStatus(formatBrowserReindexStatus(progress, "Reindexing files..."));
    });
    setStorageStatus("Reindex complete. Reloading...");
    if (resolvedTarget === "sd") {
      await refreshExternalStorageTab(directoryPath);
    } else {
      await refreshStorageManager(resolvedTarget, directoryPath);
    }
  } finally {
    state.storageReindexInProgressByTarget[resolvedTarget] = false;
    if (elements.storageReindexButton) {
      elements.storageReindexButton.disabled = false;
    }
  }
}

async function remountStorageDirectory(target = state.activeStorageTarget, directoryPath = state.currentStoragePathByTarget[target] || "/") {
  const resolvedTarget = resolveStorageTarget(target);
  if (resolvedTarget !== "sd") {
    return;
  }

  if (elements.storageRemountButton) {
    elements.storageRemountButton.disabled = true;
  }

  try {
    setStorageStatus("Remounting SD card...");
    const payload = await request(`/api/storage/remount?target=${encodeURIComponent(resolvedTarget)}&dir=${encodeURIComponent(normalizeStorageDirectoryPath(directoryPath))}`, {
      method: "POST",
      body: JSON.stringify({}),
    });
    renderStorageManager(payload);
    setStorageStatus(payload?.message || "SD card remounted.");
  } finally {
    updateStorageToolbar(state.storageInfoByTarget[state.activeStorageTarget] || {});
  }
}

async function ensureStorageListFilled(target = state.activeStorageTarget) {
  if (!elements.storageFileList) {
    return;
  }
  let attempts = 0;
  while (attempts < 6) {
    const meta = activeStorageMeta(target);
    if (!meta?.hasMore || meta.loadingMore) {
      return;
    }
    if (elements.storageFileList.scrollHeight > elements.storageFileList.clientHeight + 12) {
      return;
    }
    attempts += 1;
    await loadMoreStorageEntries(target);
    await delay(30);
  }
}

async function openStorageManager(target = "flash", directoryPath = state.currentStoragePathByTarget[target] || "/") {
  await storageTab?.openStorageManager(target, directoryPath);
}

async function createStorageFolder() {
  const name = window.prompt("Folder name");
  if (!name) {
    return;
  }
  const trimmedName = String(name).trim();
  if (!trimmedName) {
    setStorageStatus("Folder name is required.", true);
    return;
  }
  setStorageStatus(`Creating ${trimmedName}...`);
  const result = await request(`/api/storage/mkdir?target=${encodeURIComponent(state.activeStorageTarget)}&dir=${encodeURIComponent(state.currentStoragePathByTarget[state.activeStorageTarget] || "/")}`, {
    method: "POST",
    body: JSON.stringify({ name: trimmedName }),
  });
  clearStorageSelection();
  renderStorageManager(result);
  setStorageStatus(result.message || `Created ${trimmedName}`);
  toast(result.message || `Created ${trimmedName}`);
}

async function uploadStorageFiles(selectedFiles = [...(elements.storageFileInput?.files || [])]) {
  const files = [...selectedFiles].filter((file) => file instanceof File);
  if (!files.length) {
    setStorageStatus("Select one or more audio files first.", true);
    return;
  }

  state.storageUploadInProgress = true;
  let completedUploads = 0;

  try {
    for (let index = 0; index < files.length; index += 1) {
      const file = files[index];
      const info = state.storageInfoByTarget[state.activeStorageTarget] || (await refreshStorageManager(state.activeStorageTarget)).storage || {};
      const maxUploadBytes = Number(info.maxUploadBytes || 0);
      if (file.size <= 0) {
        throw new Error(`${file.name} is empty.`);
      }
      if (maxUploadBytes <= 0 || file.size > maxUploadBytes) {
        throw new Error(`${file.name} exceeds remaining space. Max upload is ${formatBytes(maxUploadBytes)}.`);
      }

      renderStorageManager({ storage: info, files: [] });
      setStorageStatus(`Uploading ${file.name} (${index + 1}/${files.length})...`);
      if (elements.storageProgressWrap) elements.storageProgressWrap.hidden = false;
      elements.storageProgressFill.style.width = "0%";
      elements.storageProgressLabel.textContent = `Uploading ${file.name} (${index + 1}/${files.length})... 0%`;

      const formData = new FormData();
      formData.append("file", file, file.name);
      const uploadStartedAt = performance.now();

      const payload = await new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open("POST", `/api/storage/upload?target=${encodeURIComponent(state.activeStorageTarget)}&dir=${encodeURIComponent(state.currentStoragePathByTarget[state.activeStorageTarget] || "/")}`);

        xhr.upload.addEventListener("progress", (event) => {
          if (!event.lengthComputable) {
            return;
          }
          const percent = Math.max(0, Math.min(100, Math.round((event.loaded * 100) / event.total)));
          const elapsedSeconds = Math.max((performance.now() - uploadStartedAt) / 1000, 0.001);
          const rate = event.loaded / elapsedSeconds;
          elements.storageProgressFill.style.width = `${percent}%`;
          elements.storageProgressLabel.textContent = `Uploading ${file.name} (${index + 1}/${files.length})... ${percent}% (${formatBytes(event.loaded)} / ${formatBytes(event.total)} at ${formatTransferRate(rate)})`;
        });

        xhr.addEventListener("load", () => {
          let responsePayload = {};
          try {
            responsePayload = JSON.parse(xhr.responseText || "{}");
          } catch {
            responsePayload = {};
          }

          if (xhr.status >= 200 && xhr.status < 300) {
            resolve(responsePayload);
            return;
          }
          reject(new Error(responsePayload.error || xhr.statusText || "Storage upload failed."));
        });

        xhr.addEventListener("error", () => reject(new Error("Storage upload failed.")));
        xhr.send(formData);
      });

      renderStorageManager(payload);
      injectVisibleStorageEntry(state.activeStorageTarget, {
        path: payload.path,
        name: storageBaseName(payload.path || file.name),
        isDirectory: false,
        sizeBytes: file.size,
        url: `/api/storage/file?target=${encodeURIComponent(state.activeStorageTarget)}&path=${encodeURIComponent(payload.path || "")}`,
      });
      await loadStatus();
      if (state.activeStorageTarget === "sd" && normalizeStorageDirectoryPath(storageParentPath(payload.path || "")) === "/media/wav") {
        mergeEffectFileOptions([{
          value: `sd:${payload.path}`,
          label: `SD: ${storageBaseName(payload.path || file.name)}`,
        }]);
        renderEffectFileOptions(state.settings);
      }
      completedUploads += 1;
      setStorageStatus(payload.message || `Uploaded ${file.name}`);
    }

    const completionMessage = files.length === 1
      ? `Uploaded ${files[0].name}`
      : `Uploaded ${completedUploads} files`;
    toast(completionMessage);
  } catch (error) {
    await refreshStorageManager(state.activeStorageTarget);
    if (completedUploads > 0 && completedUploads < files.length) {
      throw new Error(`${error.message} (${completedUploads}/${files.length} uploaded)`);
    }
    throw error;
  } finally {
    state.storageUploadInProgress = false;
    if (elements.storageProgressWrap) elements.storageProgressWrap.hidden = true;
    elements.storageFileInput.value = "";
    if (state.storageInfoByTarget[state.activeStorageTarget]) {
      elements.storageUploadButton.disabled = !state.storageInfoByTarget[state.activeStorageTarget].mounted || state.storageUploadInProgress;
    }
    loadStatus().catch((error) => console.error(error));
  }
}

function settingsSubsetMatches(actual, expected) {
  return configurationSettingsPersistenceModule?.settingsSubsetMatches(actual, expected) ?? false;
}

async function refreshSettingsAfterSave(expectedSettings, attempts = 8, delayMs = 250) {
  return configurationSettingsPersistenceModule?.refreshSettingsAfterSave(expectedSettings, attempts, delayMs) ?? false;
}

async function pollStatusUntil(predicate, attempts, delayMs) {
  for (let attempt = 0; attempt < attempts; attempt += 1) {
    await loadStatus();
    if (predicate(state.status)) {
      return true;
    }
    await delay(delayMs);
  }

  return false;
}

async function waitForSettingsIdle(attempts = 50, delayMs = 100) {
  for (let attempt = 0; attempt < attempts; attempt += 1) {
    if (!state.settingsLoading && !state.settingsSaving) {
      return true;
    }
    await delay(delayMs);
  }

  return !state.settingsLoading && !state.settingsSaving;
}

function oledDimensions() {
  const configuredWidth = Number(namedField("oled.width")?.value || state.settings?.oled?.width || 128);
  const configuredHeight = Number(namedField("oled.height")?.value || state.settings?.oled?.height || 64);
  const rotation = Number(namedField("oled.rotation")?.value || state.settings?.oled?.rotation || 0);
  const swapped = rotation === 90 || rotation === 270;

  return {
    configuredWidth,
    configuredHeight,
    rotation,
    effectiveWidth: swapped ? configuredHeight : configuredWidth,
    effectiveHeight: swapped ? configuredWidth : configuredHeight,
  };
}

function charsForWidth(width, textSize) {
  return Math.max(4, Math.floor(width / (6 * textSize)));
}

function oledTopDividerY(height) {
  return Math.min(11, Math.floor(height / 4));
}

function oledBottomDividerY(height) {
  return Math.max(height - 12, height - 12);
}

function truncateOledText(text, maxChars) {
  const value = String(text || "");
  if (value.length <= maxChars) {
    return value;
  }
  return `${value.slice(0, Math.max(0, maxChars - 1))}~`;
}

function oledScrollWindow(text, maxChars, offset) {
  const value = String(text || "");
  if (value.length <= maxChars) {
    return value;
  }

  const padded = `${value}   `;
  const start = offset % padded.length;
  if (start + maxChars <= padded.length) {
    return padded.slice(start, start + maxChars);
  }

  const end = (start + maxChars) - padded.length;
  return `${padded.slice(start)}${padded.slice(0, end)}`;
}

function renderOledPreview() {
  displayTab?.renderOledPreview();
}

function formatBytes(bytes) {
  const numericBytes = Number(bytes || 0);
  if (!Number.isFinite(numericBytes) || numericBytes <= 0) {
    return "0 B";
  }
  if (numericBytes >= 1024 * 1024) {
    return `${(numericBytes / (1024 * 1024)).toFixed(numericBytes >= 10 * 1024 * 1024 ? 0 : 1)} MiB`;
  }
  if (numericBytes >= 1024) {
    return `${Math.round(numericBytes / 1024)} KiB`;
  }
  return `${Math.round(numericBytes)} B`;
}

function setResourceBar(fillElement, percent) {
  if (!fillElement) {
    return;
  }

  const clampedPercent = Math.max(0, Math.min(100, Math.round(Number(percent || 0))));
  fillElement.style.width = `${clampedPercent}%`;
  fillElement.classList.remove("ok", "warn", "bad");
  fillElement.classList.add(clampedPercent >= 80 ? "bad" : (clampedPercent >= 55 ? "warn" : "ok"));
}

function updateResourceCard(valueElement, fillElement, metaElement, valueText, percent, metaText) {
  if (valueElement) {
    valueElement.textContent = valueText;
  }
  setResourceBar(fillElement, percent);
  if (metaElement) {
    metaElement.textContent = metaText;
  }
}

function pinSummary(pin) {
  const numericPin = Number(pin);
  return Number.isFinite(numericPin) && numericPin >= 0 ? `GPIO${numericPin}` : "-";
}

function setCurrentFirmwareVersion(version) {
  const displayVersion = version || "-";
  elements.firmwareVersionCard.textContent = displayVersion;
  if (elements.heroFirmwareVersion) {
    elements.heroFirmwareVersion.textContent = displayVersion;
  }
  if (elements.heroFirmwareChannel) {
    elements.heroFirmwareChannel.textContent = `(${firmwareReleaseChannel(displayVersion)})`;
  }
}

function detectGpioBoardProfile(status = state.status) {
  const explicitBoardProfile = String(status?.hardware?.boardProfile || "").trim();
  if (explicitBoardProfile && GPIO_BOARD_LAYOUTS[explicitBoardProfile]) {
    return explicitBoardProfile;
  }
  const firmwareChipFamily = String(status?.firmware?.chipFamily || "").toLowerCase();
  const chipModel = String(status?.hardware?.chipModel || "").toLowerCase();
  const psramAvailable = Boolean(status?.system?.psram?.available || status?.system?.psram?.mounted || false);
  const chipToken = firmwareChipFamily || chipModel.replace(/[^a-z0-9]/g, "");

  if (chipToken.includes("esp32c6") || chipToken === "c6") {
    return "esp32-c6";
  }
  if (chipToken.includes("esp32c3") || chipToken === "c3") {
    return "esp32-c3";
  }
  if (chipToken.includes("esp32s2") || chipToken === "s2") {
    return "esp32-s2-psram";
  }
  if (chipToken.includes("esp32s3") || chipToken === "s3") {
    return "esp32-s3-super-mini";
  }
  if (chipToken === "esp32" || chipModel.includes("esp32")) {
    return psramAvailable ? "esp32-wrover" : "esp32-wroom";
  }
  return "esp32-s3-super-mini";
}

function updateGpioBoardSelectorMode(status = state.status, options = {}) {
  configurationGpioTab?.updateGpioBoardSelectorMode(status, options);
}

function updateGpioBoardImage() {
  configurationGpioTab?.updateGpioBoardImage();
}

function firmwareReleaseChannel(version) {
  const normalized = String(version || "").trim().toLowerCase();
  if (!normalized) {
    return "release";
  }
  return /(alpha|beta|rc|dev|nightly|preview|pre)/.test(normalized) ? "beta" : "release";
}

function setFirmwareAuthorLink(settings = state.settings) {
  if (!elements.heroFirmwareAuthorLink) {
    return;
  }
  const owner = String(settings?.ota?.owner || "elma-iot").trim() || "elma-iot";
  const repository = String(settings?.ota?.repository || "ELMA-IoT").trim() || "ELMA-IoT";
  elements.heroFirmwareAuthorLink.href = `https://github.com/${encodeURIComponent(owner)}/${encodeURIComponent(repository)}`;
}

function currentGpioRoleNumericValue(element, fallback = undefined) {
  const rawValue = element?.value;
  if (rawValue === "") {
    return undefined;
  }
  const numericValue = Number(rawValue);
  if (Number.isFinite(numericValue)) {
    return numericValue;
  }
  return fallback;
}

function peripheralHelperProfileValue(groupKey, index, settings = state.settings) {
  switch (groupKey) {
    case "audio":
      return String(normalizedPeripheralAudioProfiles()[index] || "none");
    case "audioIn":
      return String(normalizedPeripheralAudioInProfiles()[index] || "none");
    case "display":
      return String(normalizedPeripheralDisplayProfiles()[index] || "none");
    case "sensor":
      return String(normalizedPeripheralSensorProfiles()[index] || "none");
    case "input":
      return String(normalizedPeripheralInputProfiles()[index] || "none");
    case "storage":
      return String(normalizedPeripheralStorageProfiles()[index] || "none");
    case "power":
      return String(normalizedPeripheralPowerProfiles()[index] || "none");
    case "control":
      return String(normalizedPeripheralControlProfiles()[index] || "none");
    case "expansion":
      return String(normalizedPeripheralExpansionProfiles()[index] || "none");
    case "communication":
      return String(normalizedPeripheralCommunicationProfiles()[index] || "none");
    default:
      return "none";
  }
}

function peripheralHelperProfileLabel(groupKey, profileValue, index) {
  switch (groupKey) {
    case "audio":
      return peripheralDiagramOptionLabel(PERIPHERAL_AUDIO_PROFILE_OPTIONS, profileValue, index === 0 ? "Audio Out" : `Audio Out ${index + 1}`);
    case "audioIn":
      return peripheralDiagramOptionLabel(PERIPHERAL_AUDIO_IN_PROFILE_OPTIONS, profileValue, index === 0 ? "Audio In" : `Audio In ${index + 1}`);
    case "display":
      return peripheralDiagramOptionLabel(PERIPHERAL_DISPLAY_PROFILE_OPTIONS, profileValue, index === 0 ? "Display" : `Display ${index + 1}`);
    case "sensor":
      return peripheralDiagramOptionLabel(PERIPHERAL_SENSOR_PROFILE_OPTIONS, profileValue, `Sensor ${index + 1}`);
    case "input":
      return peripheralDiagramOptionLabel(PERIPHERAL_INPUT_PROFILE_OPTIONS, profileValue, `Input ${index + 1}`);
    case "storage":
      return peripheralDiagramOptionLabel(PERIPHERAL_STORAGE_PROFILE_OPTIONS, profileValue, `Storage ${index + 1}`);
    case "power":
      return peripheralDiagramOptionLabel(PERIPHERAL_POWER_PROFILE_OPTIONS, profileValue, `Power ${index + 1}`);
    case "control":
      return peripheralDiagramOptionLabel(PERIPHERAL_CONTROL_PROFILE_OPTIONS, profileValue, `Control ${index + 1}`);
    case "expansion":
      return peripheralDiagramOptionLabel(PERIPHERAL_EXPANSION_PROFILE_OPTIONS, profileValue, `Expansion ${index + 1}`);
    case "communication":
      return peripheralDiagramOptionLabel(PERIPHERAL_COMMUNICATION_PROFILE_OPTIONS, profileValue, `Comm ${index + 1}`);
    default:
      return "Peripheral";
  }
}

function gpioRoleMap(settings = state.settings, status = state.status) {
  const roleMap = new Map();
  const addRole = (pin, label) => {
    const numericPin = Number(pin);
    if (!Number.isFinite(numericPin) || numericPin < 0) {
      return;
    }
    if (!roleMap.has(numericPin)) {
      roleMap.set(numericPin, []);
    }
    roleMap.get(numericPin).push(label);
  };

  const audio = settings?.audio || {};
  const battery = settings?.battery || {};
  const device = settings?.device || {};
  const oled = settings?.oled || {};
  const sd = settings?.sd || {};
  const selectedAudioProfile = String(elements.peripheralAudioProfile?.value || "").trim().toLowerCase();
  const displayProfile = effectiveDisplayProfile(settings);
  const audioEnabled = selectedAudioProfile
    ? (selectedAudioProfile !== "none" && !selectedAudioProfile.includes("bluetooth"))
    : Boolean(settings?.audio?.enabled ?? true);
  const chipFamily = String(status?.firmware?.chipFamily || "esp32s3").toLowerCase();
  const displayType = displayProfile === "waveshare-screen" ? "wape" : "oled";
  const oledEnabled = displayProfile !== "none";
  const sdEnabled = effectiveStorageEnabled(settings);

  if (audioEnabled) {
    addRole(currentGpioRoleNumericValue(elements.audioDoutPin, audio.doutPin), "I2S DIN");
    addRole(currentGpioRoleNumericValue(elements.audioWsPin, audio.wsPin), "I2S WS");
    addRole(currentGpioRoleNumericValue(elements.audioBclkPin, audio.bclkPin), "I2S BCLK");
  }
  if (batteryDividerSensorSelected()) {
    const batteryAdcPin = currentGpioRoleNumericValue(elements.batteryAdcPin, battery.adcPin);
    if (Number.isFinite(batteryAdcPin) && batteryAdcPin > 0) {
      addRole(batteryAdcPin, "Battery ADC");
    }
  }
  if (Number(battery.chargingSensePin || 0) > 0) {
    addRole(battery.chargingSensePin, "Charge Sense");
  }
  addRole(currentGpioRoleNumericValue(elements.statusLedPin, device.statusLedPin), statusLedRoleLabel(settings, status));

  if (displayType === "wape") {
    const wapeTriggerPin = currentGpioRoleNumericValue(elements.wapeTriggerPin, oled.wapeTriggerPin);
    if (Number(wapeTriggerPin || 0) > 0) {
      addRole(wapeTriggerPin, "Wape Trigger");
    }
  } else if (oledEnabled) {
    addRole(currentGpioRoleNumericValue(elements.oledSdaPin, oled.sdaPin), "OLED SDA");
    addRole(currentGpioRoleNumericValue(elements.oledSclPin, oled.sclPin), "OLED SCL");
    const oledResetPin = currentGpioRoleNumericValue(elements.oledResetPin, oled.resetPin);
    if (Number(oledResetPin ?? -1) >= 0) {
      addRole(oledResetPin, "OLED RESET");
    }
  }

  if (sdEnabled) {
    addRole(currentGpioRoleNumericValue(elements.sdCsPin, sd.csPin), "SD CS");
    addRole(currentGpioRoleNumericValue(elements.sdSckPin, sd.sckPin), "SD SCK");
    addRole(currentGpioRoleNumericValue(elements.sdMosiPin, sd.mosiPin), "SD MOSI");
    addRole(currentGpioRoleNumericValue(elements.sdMisoPin, sd.misoPin), "SD MISO");
  }

  const button1Pin = inputAssignedPin(0);
  const button2Pin = inputAssignedPin(1);
  if (button1Pin !== null) {
    addRole(button1Pin, "Button 1");
  }
  if (button2Pin !== null) {
    addRole(button2Pin, "Button 2");
  }

  for (const [slotKey, slotBindings] of Object.entries(isPlainObject(state.peripheralHelperBindings) ? state.peripheralHelperBindings : {})) {
    if (!isPlainObject(slotBindings)) {
      continue;
    }
    const [groupKey, rawIndex] = String(slotKey || "").split(":");
    const index = Number(rawIndex || 0);
    if (groupKey === "input" && (index === 0 || index === 1)) {
      continue;
    }
    const profileValue = peripheralHelperProfileValue(groupKey, index, settings);
    if (!profileValue || profileValue === "none") {
      continue;
    }
    const profileLabel = peripheralHelperProfileLabel(groupKey, profileValue, index);
    for (const [signalLabel, pinValue] of Object.entries(slotBindings)) {
      if (["CONTACT", "MAIN_CONTROL", "SOURCE", "INPUT_VOLTAGE", "OUTPUT_VOLTAGE"].includes(String(signalLabel || "").trim().toUpperCase())) {
        continue;
      }
      const numericPin = Number(pinValue);
      if (!Number.isFinite(numericPin) || numericPin < 0) {
        continue;
      }
      addRole(numericPin, `${profileLabel} ${String(signalLabel || "").trim()}`.trim());
    }
  }

  return roleMap;
}

function gpioRoleValue(path, fallback = undefined) {
  const segments = String(path || "").split(".");
  let current = state.settings || {};
  for (const segment of segments) {
    current = current?.[segment];
  }
  return current ?? fallback;
}

function controlHelperRoleDefinitions() {
  const controlProfiles = normalizedPeripheralControlProfiles();
  return controlProfiles.flatMap((profileValue, index) => {
    const normalizedProfile = String(profileValue || "none").trim().toLowerCase();
    if (normalizedProfile !== "drv8833-dual-motor-driver") {
      return [];
    }
    return ["IN1", "IN2", "IN3", "IN4"].map((signalLabel) => ({
      key: `ui.control.${index}.${signalLabel.toLowerCase()}`,
      label: controlProfiles.length > 1 ? `Control ${index + 1} ${signalLabel}` : `DRV8833 ${signalLabel}`,
      unusedValue: "",
      getValue: () => peripheralHelperBindingValue("control", index, signalLabel),
      setValue: (value) => setPeripheralHelperBindingValue("control", index, signalLabel, value),
      isAssigned: (value) => Number.isFinite(value) && value >= 0,
    }));
  });
}

function gpioConfigRoleDefinitions(settings = state.settings) {
  const selectedAudioProfile = String(elements.peripheralAudioProfile?.value || "").trim().toLowerCase();
  const audioEnabled = selectedAudioProfile
    ? (selectedAudioProfile !== "none" && !selectedAudioProfile.includes("bluetooth"))
    : (settings?.audio?.enabled !== false);
  const displayProfile = effectiveDisplayProfile(settings);
  const displayType = displayProfile === "waveshare-screen" ? "wape" : "oled";
  const definitions = [
    { key: "device.statusLedPin", label: statusLedRoleLabel(settings), element: elements.statusLedPin, isAssigned: (value) => Number.isFinite(value) },
  ];

  if (batteryDividerSensorSelected()) {
    definitions.unshift({ key: "battery.adcPin", label: "Battery ADC", element: elements.batteryAdcPin, unusedValue: 0, isAssigned: (value) => Number.isFinite(value) && value > 0 });
  }

  definitions.unshift(
    {
      key: "ui.input.1.pin",
      label: "Button 2",
      unusedValue: "",
      getValue: () => inputAssignedPin(1),
      setValue: (value) => setInputAssignedPin(1, value),
      isAssigned: (value) => Number.isFinite(value) && value >= 0,
    },
    {
      key: "ui.input.0.pin",
      label: "Button 1",
      unusedValue: "",
      getValue: () => inputAssignedPin(0),
      setValue: (value) => setInputAssignedPin(0, value),
      isAssigned: (value) => Number.isFinite(value) && value >= 0,
    },
  );

  definitions.push(...controlHelperRoleDefinitions());

  if (audioEnabled) {
    definitions.unshift(
      { key: "audio.doutPin", label: "I2S DIN", element: elements.audioDoutPin, isAssigned: (value) => Number.isFinite(value) },
      { key: "audio.bclkPin", label: "I2S BCLK", element: elements.audioBclkPin, isAssigned: (value) => Number.isFinite(value) },
      { key: "audio.wsPin", label: "I2S WS", element: elements.audioWsPin, isAssigned: (value) => Number.isFinite(value) },
    );
  }

  if (effectiveStorageEnabled(settings)) {
    definitions.push(
      { key: "sd.csPin", label: "SD CS", element: elements.sdCsPin, isAssigned: (value) => Number.isFinite(value) },
      { key: "sd.sckPin", label: "SD SCK", element: elements.sdSckPin, isAssigned: (value) => Number.isFinite(value) },
      { key: "sd.mosiPin", label: "SD MOSI", element: elements.sdMosiPin, isAssigned: (value) => Number.isFinite(value) },
      { key: "sd.misoPin", label: "SD MISO", element: elements.sdMisoPin, isAssigned: (value) => Number.isFinite(value) },
    );
  }

  if (displayProfile === "none") {
    return definitions;
  }

  if (displayType === "wape") {
    definitions.push({
      key: "oled.wapeTriggerPin",
      label: "Wape Trigger",
      element: elements.wapeTriggerPin,
      unusedValue: 0,
      isAssigned: (value) => Number.isFinite(value) && value > 0,
    });
  } else {
    definitions.push(
      { key: "oled.sdaPin", label: "OLED SDA", element: elements.oledSdaPin, isAssigned: (value) => Number.isFinite(value) && value >= 0 },
      { key: "oled.sclPin", label: "OLED SCL", element: elements.oledSclPin, isAssigned: (value) => Number.isFinite(value) && value >= 0 },
      { key: "oled.resetPin", label: "OLED RESET", element: elements.oledResetPin, unusedValue: -1, isAssigned: (value) => Number.isFinite(value) && value >= 0 },
    );
  }

  return definitions;
}

function currentPinForGpioRole(definition) {
  const rawValue = typeof definition.getValue === "function"
    ? definition.getValue()
    : (definition.element?.value ?? gpioRoleValue(definition.key));
  if (rawValue === "") {
    return null;
  }
  const numericValue = Number(rawValue);
  return definition.isAssigned(numericValue) ? numericValue : null;
}

function gpioConfigRoleState(settings = state.settings) {
  const definitions = gpioConfigRoleDefinitions(settings);
  const byKey = new Map(definitions.map((definition) => [definition.key, definition]));
  const pinToRole = new Map();
  const roleToPin = new Map();

  for (const definition of definitions) {
    const pin = currentPinForGpioRole(definition);
    roleToPin.set(definition.key, pin);
    if (pin !== null && !pinToRole.has(pin)) {
      pinToRole.set(pin, definition.key);
    }
  }

  return { definitions, byKey, pinToRole, roleToPin };
}

function gpioConfigOptions(pin, currentRoleKey, roleState) {
  return roleState.definitions.filter((definition) => {
    const input = definition.key.startsWith("battery.") || definition.key.startsWith("ui.input.") || definition.key === "sd.misoPin";
    if (!validBoardPins(!input).includes(Number(pin))) return definition.key === currentRoleKey;
    const assignedPin = roleState.roleToPin.get(definition.key);
    if (definition.key === currentRoleKey) {
      return true;
    }
    if (currentRoleKey && assignedPin === null) {
      return false;
    }
    return assignedPin !== pin;
  });
}

function setGpioExtraExpanded(expanded) {
  configurationGpioTab?.setGpioExtraExpanded(expanded);
}

function renderGpioOverview() {
  configurationGpioTab?.renderGpioOverview();
  renderBoardPinWarnings();
}

function syncGpioMappingControls() {
  populateAudioI2sPinOptions(state.settings);
  populateBatteryAdcPinOptions(state.settings);
  populateStatusLedPinOptions(state.settings);
  populateSdPinOptions(state.settings);
  populateOledPinOptions(state.settings);
  populateWapeTriggerPinOptions(state.settings);
  updateAudioI2sUi();
  updateAudioUiState();
  updateBatteryUi();
  updateDisplayModeUi();
  syncPeripheralBindingGroups();
  renderOledPreview();
  renderDeviceResources(state.status || {});
  renderGpioOverview();
  motorTab?.render();
}

function applyGpioRoleSelection(pin, selectedRoleKey) {
  configurationGpioTab?.applyGpioRoleSelection(pin, selectedRoleKey);
}

function renderHardwareSummary(status) {
  hardwareTab?.renderHardwareSummary(status);
}

function renderDeviceResources(status) {
  hardwareTab?.renderDeviceResources(status);
}

function updateHeaderActionsButtonStatus() {
  if (!elements.headerActionsButton) {
    return;
  }
  const status = state.deviceReachable === true ? "true" : (state.deviceReachable === false ? "false" : "unknown");
  elements.headerActionsButton.dataset.deviceOnline = status;
  elements.headerActionsButton.title = state.deviceReachable === false ? "Device offline" : "Device actions";
  elements.headerActionsButton.setAttribute("aria-label", state.deviceReachable === false ? "Device actions, device offline" : "Device actions");
}

function isApiPath(path) {
  return typeof path === "string" && (path.startsWith("/api/") || path === "/api/status");
}

function isExpectedOfflineError(error) {
  return Boolean(error?.isDeviceOffline);
}

function markDeviceReachability(reachable) {
  const normalized = reachable === true ? true : (reachable === false ? false : null);
  if (state.deviceReachable === normalized) {
    return;
  }
  state.deviceReachable = normalized;
  if (normalized !== false) {
    state.offlineNoticeShown = false;
  }
  updateHeaderActionsButtonStatus();
}

async function request(path, options = {}) {
  let response;
  try {
    response = await fetch(path, {
      headers: { "Content-Type": "application/json" },
      ...options,
    });
  } catch (error) {
    if (isApiPath(path)) {
      markDeviceReachability(false);
      const offlineError = new Error("Device offline");
      offlineError.cause = error;
      offlineError.isDeviceOffline = true;
      offlineError.path = path;
      throw offlineError;
    }
    throw error;
  }
  if (isApiPath(path)) {
    markDeviceReachability(true);
  }
  if (!response.ok) {
    const text = await response.text();
    let message = text || `HTTP ${response.status}`;
    if (text) {
      try {
        const payload = JSON.parse(text);
        if (payload && typeof payload === "object" && typeof payload.error === "string" && payload.error.trim()) {
          message = payload.error.trim();
        }
      } catch {
      }
    }
    throw new Error(message);
  }
  const contentType = response.headers.get("content-type") || "";
  return contentType.includes("application/json") ? response.json() : response.text();
}

function formatReindexProgress(reindex) {
  const processed = Number(reindex?.processedEntries || 0);
  const total = Number(reindex?.totalEntries || 0);
  const stage = String(reindex?.stage || "working");
  return `${stage} ${formatLoadProgress(processed, total)}`;
}

async function fetchStorageEntryCount(target, directoryPath) {
  const payload = await request(`/api/storage/count?target=${encodeURIComponent(target)}&dir=${encodeURIComponent(normalizeStorageDirectoryPath(directoryPath))}`);
  return Number(payload?.totalEntries || 0);
}

async function uploadGeneratedStorageIndex(target, directoryPath, contents, onProgress) {
  await new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open("POST", `/api/storage/upload?target=${encodeURIComponent(target)}&dir=${encodeURIComponent(normalizeStorageDirectoryPath(directoryPath))}`);
    xhr.upload.addEventListener("progress", (event) => {
      if (!event.lengthComputable) {
        return;
      }
      onProgress?.(Math.max(0, Math.min(100, Math.round((event.loaded * 100) / event.total))));
    });
    xhr.addEventListener("load", () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve();
      } else {
        reject(new Error(xhr.responseText || xhr.statusText || "Index upload failed."));
      }
    });
    xhr.addEventListener("error", () => reject(new Error("Index upload failed.")));

    const formData = new FormData();
    formData.append("file", new Blob([contents], { type: "text/plain" }), ".index");
    xhr.send(formData);
  });
}

async function rebuildStorageIndexFromBrowser(target, directoryPath, onProgress) {
  const normalizedDirectoryPath = normalizeStorageDirectoryPath(directoryPath);
  const totalEntries = Math.max(0, await fetchStorageEntryCount(target, normalizedDirectoryPath));
  const pageSize = 10;
  let offset = 0;
  let processedEntries = 0;
  let hasMore = true;
  const lines = [];

  onProgress?.({ stage: "counting", processedEntries: 0, totalEntries });
  while (hasMore) {
    const payload = await request(`/api/storage?${storageQueryParams({
      target,
      dir: normalizedDirectoryPath,
      offset,
      limit: pageSize,
      live: true,
      reindex: false,
    })}`);
    for (const entry of payload?.entries || []) {
      const name = String(entry?.name || "");
      if (!name || name === ".index" || name === ".index.tmp") {
        continue;
      }
      lines.push(entry.isDirectory ? `${name}/` : name);
      processedEntries += 1;
      onProgress?.({ stage: "counting", processedEntries, totalEntries: Math.max(totalEntries, processedEntries) });
    }
    offset = Number(payload?.nextOffset || offset + (payload?.entries?.length || 0));
    hasMore = Boolean(payload?.hasMore);
  }

  onProgress?.({ stage: "writing", processedEntries: Math.max(totalEntries, processedEntries), totalEntries: Math.max(totalEntries, processedEntries) });
  await uploadGeneratedStorageIndex(target, normalizedDirectoryPath, `${lines.join("\n")}\n`, (percent) => {
    onProgress?.({
      stage: "writing",
      processedEntries: Math.max(totalEntries, processedEntries),
      totalEntries: Math.max(totalEntries, processedEntries),
      uploadPercent: percent,
    });
  });
  onProgress?.({ stage: "complete", processedEntries: Math.max(totalEntries, processedEntries), totalEntries: Math.max(totalEntries, processedEntries), uploadPercent: 100 });
  return { totalEntries: Math.max(totalEntries, processedEntries) };
}

function formatBrowserReindexStatus(progress, labelPrefix) {
  const stage = String(progress?.stage || "counting");
  if (stage === "writing") {
    return `${labelPrefix} writing index... ${Math.max(0, Math.min(100, Math.round(Number(progress?.uploadPercent || 0))))}%`;
  }
  if (stage === "complete") {
    return `${labelPrefix} 100%`;
  }
  return `${labelPrefix} ${formatLoadProgress(Number(progress?.processedEntries || 0), Number(progress?.totalEntries || 0))}`;
}

function shouldDeferSdReads() {
  return false;
}

function mergeEffectFileOptions(options) {
  effectsTab?.mergeEffectFileOptions(options);
}

function injectVisibleStorageEntry(target, entry) {
  if (!entry?.path) {
    return;
  }
  const entries = activeStorageEntries(target);
  if (entries.some((item) => item.path === entry.path)) {
    setStorageSelection([entry.path], target);
    rerenderStorageManager(target);
    elements.storageFileList?.scrollTo({ top: 0, behavior: "smooth" });
    return;
  }
  state.currentStorageEntriesByTarget[target] = [entry, ...entries];
  setStorageSelection([entry.path], target);
  rerenderStorageManager(target);
  elements.storageFileList?.scrollTo({ top: 0, behavior: "smooth" });
}

function showUpdateAvailablePopup(status) {
  const latestVersion = String(status?.ota?.latestVersion || "");
  if (!latestVersion || state.updatePopupShownVersion === latestVersion) {
    return;
  }
  state.updatePopupShownVersion = latestVersion;
  const autoUpdateEnabled = Boolean(state.settings?.ota?.autoUpdate);
  if (elements.updateAvailableBody) {
    elements.updateAvailableBody.textContent = autoUpdateEnabled
      ? `A newer firmware ${latestVersion} is available. Auto-update is enabled, so the device will start installing it after the update-available sound finishes.`
      : `A newer firmware ${latestVersion} is available. Auto-update is disabled, so the device is waiting for a manual install.`;
  }
  if (elements.updateAvailableDialog?.showModal) {
    elements.updateAvailableDialog.showModal();
  }
}

function closeUpdateAvailablePopup() {
  if (elements.updateAvailableDialog?.open) {
    elements.updateAvailableDialog.close();
  }
}

function effectSelectElements() {
  return EFFECT_SELECT_CONFIG
    .map((item) => ({ ...item, element: elements[item.id], volumeElement: elements[item.volumeId] }))
    .filter((item) => item.element);
}

function isSupportedAudioFilename(name) {
  return STORAGE_AUDIO_EXTENSIONS.has(storageFileExtension(name));
}

function effectFileLabelFromEntry(source, entry) {
  return `${source.prefix}: ${entry.name}`;
}

function storageQueryParams({ target, dir, offset = 0, limit = 20, live = false, reindex = false }) {
  const params = new URLSearchParams();
  params.set("target", target);
  params.set("dir", dir);
  params.set("offset", String(offset));
  params.set("limit", String(limit));
  if (live) {
    params.set("live", "1");
  }
  if (reindex) {
    params.set("reindex", "1");
  }
  return params.toString();
}

function effectFileLabelFromValue(value) {
  const normalized = String(value || "").trim();
  if (!normalized) {
    return "None";
  }
  const separatorIndex = normalized.indexOf(":");
  if (separatorIndex <= 0) {
    return normalized;
  }
  const target = normalized.slice(0, separatorIndex).toLowerCase();
  const path = normalized.slice(separatorIndex + 1);
  const prefix = target === "sd" ? "SD" : (target === "flash" ? "Flash" : target.toUpperCase());
  return `${prefix}: ${storageBaseName(path)}`;
}

function configuredEffectValue(settings, field, element) {
  const configuredValue = settings?.effects?.[field];
  if (configuredValue !== undefined && configuredValue !== null && String(configuredValue).trim()) {
    return String(configuredValue).trim();
  }
  const savedValue = String(element?.dataset?.savedEffectValue || "").trim();
  if (savedValue) {
    return savedValue;
  }
  return String(element?.value || "").trim();
}

function sortEffectFileOptions(options) {
  return [...options].sort((left, right) => left.label.localeCompare(right.label, undefined, { sensitivity: "base" }));
}

function sdSettingsChanged(previousSettings, nextSettings) {
  const previous = previousSettings?.sd || {};
  const next = nextSettings?.sd || {};
  return Boolean(previous.enabled) !== Boolean(next.enabled)
    || Number(previous.csPin || 0) !== Number(next.csPin || 0)
    || Number(previous.sckPin || 0) !== Number(next.sckPin || 0)
    || Number(previous.mosiPin || 0) !== Number(next.mosiPin || 0)
    || Number(previous.misoPin || 0) !== Number(next.misoPin || 0);
}

function effectFilesCacheKey(settings = state.settings) {
  const sd = settings?.sd || {};
  const flashEnabled = true;
  return JSON.stringify({
    sdEnabled: true,
    sdCsPin: Number(sd.csPin || 0),
    sdSckPin: Number(sd.sckPin || 0),
    sdMosiPin: Number(sd.mosiPin || 0),
    sdMisoPin: Number(sd.misoPin || 0),
    flashEnabled,
    sources: EFFECT_FILE_SOURCES.map((source) => `${source.target}:${source.dir}`).join("|"),
  });
}

function persistEffectFileOptionsCache() {
  if (!state.effectFileOptionsLoaded || !state.effectFileOptions.length || !state.effectFileOptionsCacheKey) {
    return;
  }
  try {
    window.sessionStorage.setItem(EFFECT_FILES_CACHE_STORAGE_KEY, JSON.stringify({
      key: state.effectFileOptionsCacheKey,
      options: state.effectFileOptions,
    }));
  } catch {
  }
}

function clearEffectFileOptionsCache() {
  effectsTab?.clearEffectFileOptionsCache();
}

function effectFilesReadyMessage() {
  return "Changing a selection previews that audio file on the device.";
}

function effectFilesUnavailableMessage() {
  return "No supported audio files found in SD /media/wav or flash /wav.";
}

function restoreEffectFileOptionsFromCache(settings = state.settings) {
  const cacheKey = effectFilesCacheKey(settings);
  try {
    const raw = window.sessionStorage.getItem(EFFECT_FILES_CACHE_STORAGE_KEY);
    if (!raw) {
      return false;
    }
    const parsed = JSON.parse(raw);
    if (parsed?.key !== cacheKey || !Array.isArray(parsed?.options)) {
      return false;
    }
    state.effectFileOptions = sortEffectFileOptions(parsed.options
      .filter((option) => option && typeof option.value === "string" && typeof option.label === "string"));
    state.effectFileOptionsLoaded = true;
    state.effectFileOptionsCacheKey = cacheKey;
    return state.effectFileOptions.length > 0;
  } catch {
    return false;
  }
}

function renderEffectFileOptions(settings = state.settings) {
  effectsTab?.renderEffectFileOptions(settings);
}

async function loadEffectFileOptions(options = {}) {
  return effectsTab?.loadEffectFileOptions(options);
}

async function previewEffectFile(effectRef, effectLabel, { source = "effect-preview" } = {}) {
  if (!effectRef) {
    return;
  }
  await request("/api/play", {
    method: "POST",
    body: JSON.stringify({
      url: effectRef,
      label: effectLabel,
      type: source,
    }),
  });
  setMessage(source === "effect-ambient" ? `Starting ${effectLabel}` : `Previewing ${effectLabel}`);
}

function syncEffectsPage(settings = state.settings) {
  effectsTab?.syncEffectsPage(settings);
}

function syncBatteryPage(settings = state.settings) {
  batteryTab?.syncBatteryPage(settings);
}

function syncPageSections(settings = state.settings) {
  syncEffectsPage(settings);
  syncBatteryPage(settings);
  updateAudioI2sUi();
  updateAudioUiState();
  updateLowBatterySleepUi();
  updateConditionalVisibility();
  updateDisplayModeUi();
  syncPeripheralBindingGroups();
  renderHardwareSummary(state.status || {});
  renderDeviceResources(state.status || {});
  renderOledPreview();
}

function effectVolumePercentValue(value, fallback = 100) {
  const trimmedValue = String(value ?? "").trim();
  if (!trimmedValue) {
    return fallback;
  }
  const numericValue = Math.round(Number(trimmedValue));
  if (!Number.isFinite(numericValue)) {
    return fallback;
  }
  return Math.max(0, Math.min(100, numericValue));
}

function effectVolumeSetting(config, settings = state.settings) {
  return Number(settings?.effects?.[config.volumeField] ?? (config.source === "effect-ambient" ? 20 : 100));
}

async function setEffectVolume(config, value) {
  const normalizedValue = effectVolumePercentValue(value, effectVolumeSetting(config));
  if (config.volumeElement) {
    config.volumeElement.value = String(normalizedValue);
  }
  await configurationSettingsPersistenceModule?.saveDirtyBatteryMeasurement();
}

function fillForm(data) {
  configurationSettingsPersistenceModule?.fillForm(data);
}

function updateAudioUiState() {
  audioTab?.updateAudioUiState();
}

function updateAudioI2sUi() {
  audioTab?.updateAudioI2sUi();
}

function oledPinsConflictWithAudio(payload) {
  const displayType = String(payload?.oled?.displayType || state.settings?.oled?.displayType || "oled").toLowerCase();
  const oledEnabled = Boolean(payload?.oled?.enabled ?? state.settings?.oled?.enabled ?? false);
  const audioEnabled = Boolean(payload?.audio?.enabled ?? state.settings?.audio?.enabled ?? true);
  if (!oledEnabled || !audioEnabled || displayType === "wape") {
    return false;
  }

  const audioPins = new Set([
    Number(payload?.audio?.wsPin || state.settings?.audio?.wsPin || 0),
    Number(payload?.audio?.bclkPin || state.settings?.audio?.bclkPin || 0),
    Number(payload?.audio?.doutPin || state.settings?.audio?.doutPin || 0),
  ].filter((pin) => Number.isFinite(pin) && pin >= 0));

  const oledPins = [
    Number(payload?.oled?.sdaPin ?? state.settings?.oled?.sdaPin ?? -1),
    Number(payload?.oled?.sclPin ?? state.settings?.oled?.sclPin ?? -1),
    Number(payload?.oled?.resetPin ?? state.settings?.oled?.resetPin ?? -1),
  ].filter((pin) => Number.isFinite(pin) && pin >= 0);

  return oledPins.some((pin) => audioPins.has(pin));
}

function oledPinsConflictInternally(payload) {
  const displayType = String(payload?.oled?.displayType || state.settings?.oled?.displayType || "oled").toLowerCase();
  const oledEnabled = Boolean(payload?.oled?.enabled ?? state.settings?.oled?.enabled ?? false);
  if (!oledEnabled || displayType === "wape") {
    return false;
  }

  const sdaPin = Number(payload?.oled?.sdaPin ?? state.settings?.oled?.sdaPin ?? -1);
  const sclPin = Number(payload?.oled?.sclPin ?? state.settings?.oled?.sclPin ?? -1);
  const resetPin = Number(payload?.oled?.resetPin ?? state.settings?.oled?.resetPin ?? -1);

  if (sdaPin < 0 || sclPin < 0 || sdaPin === sclPin) {
    return true;
  }
  if (resetPin >= 0 && (resetPin === sdaPin || resetPin === sclPin)) {
    return true;
  }
  return false;
}

function sdPinsConflictInternally(payload) {
  const enabled = Boolean(payload?.sd?.enabled ?? state.settings?.sd?.enabled ?? false);
  if (!enabled) {
    return false;
  }

  const pins = [
    Number(payload?.sd?.csPin ?? state.settings?.sd?.csPin ?? DEFAULT_SD_GPIO_PINS.cs),
    Number(payload?.sd?.sckPin ?? state.settings?.sd?.sckPin ?? DEFAULT_SD_GPIO_PINS.sck),
    Number(payload?.sd?.mosiPin ?? state.settings?.sd?.mosiPin ?? DEFAULT_SD_GPIO_PINS.mosi),
    Number(payload?.sd?.misoPin ?? state.settings?.sd?.misoPin ?? DEFAULT_SD_GPIO_PINS.miso),
  ];

  return new Set(pins).size !== pins.length;
}

function sdPinsConflictWithReservedFunctions(payload) {
  const enabled = Boolean(payload?.sd?.enabled ?? state.settings?.sd?.enabled ?? false);
  if (!enabled) {
    return false;
  }

  const audioEnabled = Boolean(payload?.audio?.enabled ?? state.settings?.audio?.enabled ?? true);

  const sdPins = new Set([
    Number(payload?.sd?.csPin ?? state.settings?.sd?.csPin ?? DEFAULT_SD_GPIO_PINS.cs),
    Number(payload?.sd?.sckPin ?? state.settings?.sd?.sckPin ?? DEFAULT_SD_GPIO_PINS.sck),
    Number(payload?.sd?.mosiPin ?? state.settings?.sd?.mosiPin ?? DEFAULT_SD_GPIO_PINS.mosi),
    Number(payload?.sd?.misoPin ?? state.settings?.sd?.misoPin ?? DEFAULT_SD_GPIO_PINS.miso),
  ]);

  const reservedPins = new Set([
    ...(audioEnabled ? [
      Number(payload?.audio?.wsPin ?? state.settings?.audio?.wsPin ?? DEFAULT_ESP32S3_AUDIO_PINS.ws),
      Number(payload?.audio?.bclkPin ?? state.settings?.audio?.bclkPin ?? DEFAULT_ESP32S3_AUDIO_PINS.bclk),
      Number(payload?.audio?.doutPin ?? state.settings?.audio?.doutPin ?? DEFAULT_ESP32S3_AUDIO_PINS.dout),
    ] : []),
    Number(payload?.battery?.adcPin ?? state.settings?.battery?.adcPin ?? 0),
    Number(payload?.battery?.chargingSensePin ?? state.settings?.battery?.chargingSensePin ?? 0),
    Number(payload?.device?.statusLedPin ?? state.settings?.device?.statusLedPin ?? 0),
  ].filter((pin) => Number.isFinite(pin) && pin >= 0));

  const displayType = String(payload?.oled?.displayType ?? state.settings?.oled?.displayType ?? "oled").toLowerCase();
  const wapeTriggerPin = Number(payload?.oled?.wapeTriggerPin ?? state.settings?.oled?.wapeTriggerPin ?? 0);
  if (displayType === "wape" && Number.isFinite(wapeTriggerPin) && wapeTriggerPin > 0) {
    reservedPins.add(wapeTriggerPin);
  }

  return [...sdPins].some((pin) => reservedPins.has(pin));
}

function updateLowBatterySleepUi() {
  batteryTab?.updateLowBatterySleepUi();
}

function updateBatteryUi() {
  batteryTab?.updateBatteryUi();
}

function currentBatteryCalibrationMultiplier() {
  return batteryTab?.currentBatteryCalibrationMultiplier() ?? 2.0;
}

function cloneSettingsObject(value) {
  return value === undefined ? undefined : JSON.parse(JSON.stringify(value));
}

function isPlainObject(value) {
  return Boolean(value) && typeof value === "object" && !Array.isArray(value);
}

function normalizePeripheralDiagramPositions(value) {
  if (typeof value === "string") {
    try {
      const parsed = JSON.parse(value);
      return isPlainObject(parsed) ? parsed : {};
    } catch {
      return {};
    }
  }
  return isPlainObject(value) ? (cloneSettingsObject(value) || {}) : {};
}

function normalizePeripheralHelperBindings(value) {
  if (typeof value === "string") {
    try {
      const parsed = JSON.parse(value);
      return isPlainObject(parsed) ? parsed : {};
    } catch {
      return {};
    }
  }
  return isPlainObject(value) ? (cloneSettingsObject(value) || {}) : {};
}

function normalizeUiSettings(uiSettings = {}) {
  const source = isPlainObject(uiSettings) ? uiSettings : {};
  return {
    gpioBoardAutodetect: Object.prototype.hasOwnProperty.call(source, "gpioBoardAutodetect")
      ? Boolean(source.gpioBoardAutodetect)
      : true,
    gpioBoardSelection: String(source.gpioBoardSelection || ""),
    peripheralDiagramPositions: normalizePeripheralDiagramPositions(
      source.peripheralDiagramPositions ?? source.peripheralDiagramLayout,
    ),
    peripheralHelperBindings: normalizePeripheralHelperBindings(source.peripheralHelperBindings),
    peripheralProfiles: normalizePeripheralProfileSelections(source.peripheralProfiles),
    motorRuntimeConfig: normalizeMotorRuntimeConfig(source.motorRuntimeConfig),
  };
}

function ensureUiSettings(settings = state.settings) {
  const normalized = normalizeUiSettings(settings?.ui);
  if (settings) {
    settings.ui = normalized;
  }
  return normalized;
}

function mergeSettingsObjects(baseValue, overrideValue) {
  return configurationSettingsSnapshotModule?.mergeSettingsObjects(baseValue, overrideValue);
}

function currentSettingsSnapshot() {
  return configurationSettingsSnapshotModule?.currentSettingsSnapshot() || {};
}

function applyPeripheralProfileSelections(snapshot) {
  configurationSettingsSnapshotModule?.applyPeripheralProfileSelections(snapshot);
}

function syncPeripheralProfilesFromSettings(settings = state.settings) {
  if (isPeripheralUiInteracting()) {
    return;
  }
  state.peripheralAudioProfiles = normalizedPeripheralAudioProfiles();
  state.peripheralAudioInProfiles = normalizedPeripheralAudioInProfiles();
  state.peripheralDisplayProfiles = normalizedPeripheralDisplayProfiles();
  if (elements.peripheralAudioProfile && document.activeElement !== elements.peripheralAudioProfile) {
    const inferredAudioProfile = String(state.peripheralAudioProfiles[0] || elements.peripheralAudioProfile.value || "none").trim() || "none";
    if ([...elements.peripheralAudioProfile.options].some((option) => option.value === inferredAudioProfile)) {
      elements.peripheralAudioProfile.value = inferredAudioProfile;
    }
  }
  state.peripheralAudioProfiles[0] = String(elements.peripheralAudioProfile?.value || state.peripheralAudioProfiles[0] || "none");
  renderPeripheralAudioOutputControls();
  state.peripheralAudioInProfiles[0] = String(elements.peripheralAudioInProfile?.value || state.peripheralAudioInProfiles[0] || "none");
  renderPeripheralAudioInControls();
  if (elements.peripheralDisplayProfile && document.activeElement !== elements.peripheralDisplayProfile) {
    const inferredDisplayProfile = String(state.peripheralDisplayProfiles[0] || elements.peripheralDisplayProfile.value || "none").trim() || "none";
    if ([...elements.peripheralDisplayProfile.options].some((option) => option.value === inferredDisplayProfile)) {
      elements.peripheralDisplayProfile.value = inferredDisplayProfile;
    }
  }
  state.peripheralDisplayProfiles[0] = String(elements.peripheralDisplayProfile?.value || state.peripheralDisplayProfiles[0] || "none");
  renderPeripheralDisplayControls();

  const currentStorageProfiles = normalizedPeripheralStorageProfiles();
  const nextPrimaryStorageProfile = String(currentStorageProfiles[0] || "none").trim() || "none";
  state.peripheralStorageProfiles = [nextPrimaryStorageProfile, ...currentStorageProfiles.slice(1)];
  state.peripheralPowerProfiles = normalizedPeripheralPowerProfiles();

  const currentSensorProfiles = (Array.isArray(state.peripheralSensorProfiles) && state.peripheralSensorProfiles.length
    ? state.peripheralSensorProfiles
    : ["none"])
    .map((value) => String(value || "none").trim() || "none");
  const nextSensorProfiles = currentSensorProfiles.length ? [...currentSensorProfiles] : ["none"];
  const batterySensorIndex = nextSensorProfiles.findIndex((profile) => profile.toLowerCase() === BATTERY_DIVIDER_SENSOR_PROFILE);
  if (Number(settings?.battery?.adcPin || 0) > 0) {
    if (batterySensorIndex < 0) {
      const availableIndex = nextSensorProfiles.findIndex((profile) => profile.toLowerCase() === "none");
      nextSensorProfiles[availableIndex >= 0 ? availableIndex : 0] = BATTERY_DIVIDER_SENSOR_PROFILE;
    }
  } else if (batterySensorIndex >= 0) {
    nextSensorProfiles[batterySensorIndex] = "none";
  }
  state.peripheralSensorProfiles = nextSensorProfiles;

  renderPeripheralStorageControls();
  renderPeripheralPowerControls();
}

function currentBackupUiState() {
  return configurationBackupModule?.currentBackupUiState() || {};
}

function applyBackupUiState(uiState, options = {}) {
  configurationBackupModule?.applyBackupUiState(uiState, options);
}

function backupTimestamp() {
  return configurationBackupModule?.backupTimestamp() || "";
}

function backupDeviceLabel(settings) {
  return configurationBackupModule?.backupDeviceLabel(settings) || "elma-iot";
}

function createConfigurationBackupMarkdown(settings) {
  return configurationBackupModule?.createConfigurationBackupMarkdown(settings) || "";
}

function isForegroundPlaybackActive(status = state.status) {
  return isPlaybackActive(status) && String(status?.playback?.source || "") !== "effect-ambient";
}

const EQUALIZER_PRESET_GAINS = Object.freeze({
  flat: [0, 0, 0],
  clear: [-1, 1, 4],
  rock: [4, 1, 3],
  bass: [6, 0, -1],
  classical: [1, 2, 4],
  voice: [-3, 5, 2],
  jazz: [3, 2, 3],
  podcast: [-4, 6, 1],
  night: [-3, 2, -3],
});

function renderEqualizerPreset(preset = elements.audioEqualizerPreset?.value, customGains = null) {
  const normalizedPreset = String(preset || "flat").toLowerCase();
  const gains = normalizedPreset === "custom" && Array.isArray(customGains)
    ? customGains.map((value) => Math.max(-6, Math.min(6, Number(value) || 0)))
    : (EQUALIZER_PRESET_GAINS[normalizedPreset] || EQUALIZER_PRESET_GAINS.flat);
  if (elements.audioEqualizerPreset) {
    elements.audioEqualizerPreset.value = normalizedPreset;
  }
  const bands = [
    [elements.equalizerLowSlider, elements.equalizerLowValue, gains[0]],
    [elements.equalizerPresenceSlider, elements.equalizerPresenceValue, gains[1]],
    [elements.equalizerHighSlider, elements.equalizerHighValue, gains[2]],
  ];
  for (const [slider, output, gain] of bands) {
    if (slider) {
      slider.value = String(gain);
    }
    if (output) {
      output.textContent = `${gain > 0 ? "+" : ""}${gain} dB`;
    }
  }
}

function currentEqualizerGains() {
  return [elements.equalizerLowSlider, elements.equalizerPresenceSlider, elements.equalizerHighSlider]
    .map((slider) => Math.max(-6, Math.min(6, Number(slider?.value) || 0)));
}

async function setEqualizer(preset, gains) {
  const normalizedPreset = String(preset || "custom").toLowerCase();
  const [lowDb, presenceDb, highDb] = gains;
  state.settings ||= {};
  state.settings.audio ||= {};
  Object.assign(state.settings.audio, { equalizerPreset: normalizedPreset, equalizerLowDb: lowDb, equalizerPresenceDb: presenceDb, equalizerHighDb: highDb });
  await request("/api/equalizer", {
    method: "POST",
    body: JSON.stringify({ preset: normalizedPreset, lowDb, presenceDb, highDb }),
  });
  setMessage(`Equalizer ${normalizedPreset === "custom" ? "custom curve" : normalizedPreset} saved`);
}

function downloadTextFile(filename, content, mimeType = "text/markdown;charset=utf-8") {
  configurationBackupModule?.downloadTextFile(filename, content, mimeType);
}

function parseConfigurationBackup(text) {
  return configurationBackupModule?.parseConfigurationBackup(text) || { settings: {}, uiState: {}, meta: {} };
}

function validateSettingsPayload(submittedSettings) {
  if (submittedSettings.oled?.enabled !== false && oledPinsConflictInternally(submittedSettings)) {
    throw new Error("OLED SDA, SCL, and RESET must use different GPIOs.");
  }
  if (submittedSettings.oled?.enabled !== false && oledPinsConflictWithAudio(submittedSettings)) {
    throw new Error("OLED SDA, SCL, and RESET cannot reuse the active MAX98357A I2S pins. Change the display pins or disable OLED first.");
  }
  if (submittedSettings.sd?.enabled !== false && sdPinsConflictInternally(submittedSettings)) {
    throw new Error("SD card CS, SCK, MOSI, and MISO must use four different GPIOs.");
  }
  if (submittedSettings.sd?.enabled !== false && sdPinsConflictWithReservedFunctions(submittedSettings)) {
    throw new Error("SD card pins cannot reuse the active audio, battery, status LED, or Wape trigger GPIOs.");
  }
}

async function applySettingsPayload(submittedSettings, options = {}) {
  return configurationSettingsPersistenceModule?.applySettingsPayload(submittedSettings, options);
}

function updateDerivedBatteryCalibration() {
  batteryTab?.updateDerivedBatteryCalibration();
}

function collectForm() {
  return configurationSettingsPersistenceModule?.collectForm() || {};
}

function updateConditionalVisibility() {
  const showStatic = elements.useStaticIpToggle.checked;
  for (const node of document.querySelectorAll(".static-ip-group")) {
    node.style.display = showStatic ? "grid" : "none";
  }
}

function queueSettingsSave(delayMs = SETTINGS_AUTOSAVE_DELAY_MS) {
  configurationSettingsPersistenceModule?.queueSettingsSave(delayMs);
}

function renderWifiNetworks(networks) {
  wifiTab?.renderWifiNetworks(networks);
}
async function awaitPendingSettingsSave() {
  await configurationSettingsPersistenceModule?.awaitPendingSettingsSave();
}

function resetWifiNetworkList(message = "Select a scanned SSID") {
  state.wifiSelectionPending = false;
  renderWifiNetworks([]);
  const placeholder = elements.wifiNetworkList.firstElementChild;
  if (placeholder) {
    placeholder.textContent = message;
  }
}

function renderRecentPlayback() {
  if (!state.recentPlayback.length) {
    elements.recentPlaybackList.innerHTML = '<div class="firmware-item"><div class="firmware-meta"><div class="firmware-title">No recent playback</div><div class="firmware-subtitle">Played URLs will appear here for one-click reuse.</div></div></div>';
    return;
  }

  elements.recentPlaybackList.innerHTML = state.recentPlayback.map((item, index) => `
    <div class="firmware-item">
      <div class="firmware-meta">
        <div class="firmware-title">${escapeHtml(normalizePlaybackTitle(item.label, item.url))}</div>
        <div class="firmware-subtitle">${escapeHtml(item.type)} | ${escapeHtml(item.url)}</div>
      </div>
      <button type="button" class="secondary recent-play-button" data-index="${index}">Use</button>
    </div>
  `).join("");

  for (const button of document.querySelectorAll(".recent-play-button")) {
    button.addEventListener("click", () => {
      const item = state.recentPlayback[Number(button.dataset.index)];
      document.getElementById("playUrl").value = item.url;
      document.getElementById("playLabel").value = item.label || "";
      document.getElementById("playType").value = item.type || "stream";
      toast("Loaded recent playback entry");
    });
  }
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function renderStatus(status) {
  statusRenderModule?.renderStatus(status);
  wifiTab?.renderPowerStatus(status);
  refreshVisibleNativeTouchDiagram(status);
  updateTouchLivePolling();
}

async function loadSettings() {
  await configurationSettingsPersistenceModule?.loadSettings();
}

async function saveSettings(options = {}) {
  await configurationSettingsPersistenceModule?.saveSettings(options);
}

function updateWifiActionButton() {
  wifiTab?.updateWifiActionButton();
}

function updateMqttActionButton() {
  mqttTab?.updateMqttActionButton();
}

function updatePlaybackActionButton() {
  if (!elements.playbackActionButton) {
    return;
  }

  const playbackActionInProgress = String(state.playbackActionInProgress || "");
  const playbackActive = isForegroundPlaybackActive();
  const audioEnabled = Boolean(state.status?.firmware?.audioEnabled);

  if (playbackActionInProgress === "play") {
    elements.playbackActionButton.textContent = "Starting...";
    elements.playbackActionButton.classList.remove("secondary");
    elements.playbackActionButton.disabled = true;
    elements.playbackActionButton.title = "Waiting for playback to start";
    updatePlaybackHeroControls();
    return;
  }

  if (playbackActionInProgress === "stop") {
    elements.playbackActionButton.textContent = "Stopping...";
    elements.playbackActionButton.classList.add("secondary");
    elements.playbackActionButton.disabled = true;
    elements.playbackActionButton.title = "Waiting for playback to stop";
    updatePlaybackHeroControls();
    return;
  }

  elements.playbackActionButton.textContent = playbackActive ? "Stop" : "Play";
  elements.playbackActionButton.classList.toggle("secondary", playbackActive);
  elements.playbackActionButton.disabled = !audioEnabled;
  elements.playbackActionButton.title = audioEnabled ? "" : "Audio playback is disabled in this firmware build";
  updatePlaybackHeroControls();
}

function setupTabs() {
  logsTab ||= createLogsTab({ request });
  tabNavigation = initTabNavigation({
    storageKey: ACTIVE_TAB_STORAGE_KEY,
    onActivate(resolvedTabName) {
      logsTab.setActive(resolvedTabName === "logs");
      if (resolvedTabName === "gpio") {
        renderGpioOverview();
        refreshVisiblePeripheralDiagram();
      } else {
        stopTouchLivePolling();
      }

      if (resolvedTabName === "firmware" && !desktopFlashView && !document.body.classList.contains("local-builder-mode")) {
        refreshFirmwareInfo(true).catch(handleError);
      }

      if (resolvedTabName === "motor") motorTab?.render();
      if (resolvedTabName === "oled") renderOledPreview();

      if (resolvedTabName === "effects" && state.settings) {
        syncEffectsPage(state.settings);
      }

      if (resolvedTabName === "storage-external") {
        storageTab?.maybeRefreshVisibleStorageTab(true);
      }
    },
  });
}

function setupPasswordToggles() {
  for (const button of document.querySelectorAll(".password-toggle")) {
    button.addEventListener("click", () => {
      const field = elements.settingsForm.elements.namedItem(button.dataset.targetName);
      if (!field) {
        return;
      }
      const reveal = field.type === "password";
      field.type = reveal ? "text" : "password";
      button.classList.toggle("revealed", reveal);
    });
  }
}

async function loadStatus() {
  if (state.storageUploadInProgress || state.statusRequestInFlight) {
    return state.status;
  }

  state.statusRequestInFlight = true;
  try {
    const status = await request(`/api/status?ts=${Date.now()}`);
    renderStatus(status);
    if (elements.rebootOverlay && !elements.rebootOverlay.hidden) {
      if (!state.rebootOverlayArmed) {
        hideRebootOverlay();
        return status;
      }
      const reconnectGraceElapsed = !state.rebootOverlayReconnectAllowedAt || Date.now() >= state.rebootOverlayReconnectAllowedAt;
      if (state.rebootOverlaySawDisconnect || reconnectGraceElapsed) {
        clearFirmwareReconnectState();
      }
    }
    return status;
  } catch (error) {
    if (isExpectedOfflineError(error)) {
      markDeviceReachability(false);
      return state.status;
    }
    throw error;
  } finally {
    state.statusRequestInFlight = false;
  }
}

function startStatusPolling(intervalMs = 2000) {
  if (state.statusPollTimer) {
    return;
  }

  document.addEventListener("visibilitychange", () => {
    if (!document.hidden && !document.body.classList.contains("local-builder-mode")) {
      loadStatus().catch((error) => console.error(error));
    }
  });

  state.statusPollTimer = window.setInterval(() => {
    // The PC Designer represents a future device and its loopback status is
    // static. Polling it rebuilt GPIO diagrams, previews, and motor controls
    // every two seconds, interrupting focused inputs and open dropdowns.
    if (document.hidden || document.body.classList.contains("local-builder-mode")) {
      return;
    }
    loadStatus().catch((error) => console.error(error));
  }, intervalMs);
}

async function refreshFirmwareInfo(forceRefresh = false) {
  return firmwareTab?.refreshFirmwareInfo(forceRefresh);
}

function stopFirmwareProgressPolling() {
  firmwareTab?.stopFirmwareProgressPolling();
}

function stopWifiScanPolling() {
  wifiTab?.stopWifiScanPolling();
}

function startFirmwareProgressPolling() {
  firmwareTab?.startFirmwareProgressPolling();
}

async function scanWifiNetworks() {
  return wifiTab?.scanWifiNetworks();
}

async function connectWifi() {
  return wifiTab?.connectWifi();
}

async function connectMqtt() {
  return mqttTab?.connectMqtt();
}

async function republishMqttDiscovery() {
  return mqttTab?.republishMqttDiscovery();
}

async function submitPlay(event) {
  if (event) {
    event.preventDefault();
  }
  state.playbackActionInProgress = "play";
  updatePlaybackActionButton();
  const payload = {
    url: elements.playUrl.value,
    label: normalizePlaybackTitle(elements.playLabel.value, elements.playUrl.value),
    type: elements.playType.value,
  };
  try {
    await request("/api/play", { method: "POST", body: JSON.stringify(payload) });
    state.recentPlayback.unshift(payload);
    state.recentPlayback = state.recentPlayback.filter((item, index, array) => index === array.findIndex((entry) => entry.url === item.url && entry.type === item.type));
    saveRecentPlayback();
    renderRecentPlayback();
    const started = await pollStatusUntil(
      (status) => {
        return isForegroundPlaybackActive(status) && String(status?.playback?.url || "") === String(payload.url || "");
      },
      12,
      150,
    );
    setMessage(started ? "Playback started" : "Playback queued");
    toast(started ? "Playback started" : "Playback queued");
    if (!started) {
      await loadStatus();
    }
  } finally {
    state.playbackActionInProgress = "";
    updatePlaybackActionButton();
  }
}

async function stopPlayback() {
  state.playbackActionInProgress = "stop";
  updatePlaybackActionButton();
  try {
    await request("/api/stop", { method: "POST", body: JSON.stringify({}) });
    const stopped = await pollStatusUntil(
      (status) => {
        return !isForegroundPlaybackActive(status);
      },
      12,
      150,
    );
    setMessage(stopped ? "Playback stopped" : "Stopping playback");
    toast(stopped ? "Playback stopped" : "Stopping playback");
    if (!stopped) {
      await loadStatus();
    }
  } finally {
    state.playbackActionInProgress = "";
    updatePlaybackActionButton();
  }
}

async function setVolume(volumePercent) {
  state.settings ||= {};
  state.settings.device ||= {};
  state.settings.device.savedVolumePercent = volumePercent;
  if (namedField("device.savedVolumePercent")) {
    namedField("device.savedVolumePercent").value = volumePercent;
  }
  await request("/api/volume", {
    method: "POST",
    body: JSON.stringify({ volumePercent }),
  });
  elements.volumeSlider.value = volumePercent;
  setMessage(`Volume saved at ${volumePercent}%`);
}

async function handlePlaybackAction() {
  if (isForegroundPlaybackActive()) {
    await stopPlayback();
    return;
  }

  if (String(state.status?.playback?.source || "") === "file-manager" && state.storagePreviewItem) {
    await queueStoragePlayback(state.storagePreviewItem, state.storagePreviewTarget);
    return;
  }

  if (!elements.playForm.reportValidity()) {
    return;
  }

  await submitPlay();
}

async function exportConfigurationBackup() {
  await configurationBackupModule?.exportConfigurationBackup();
}

async function restoreConfigurationBackup(file) {
  await configurationBackupModule?.restoreConfigurationBackup(file);
}

async function exportPeripheralDiagramShare() {
  await configurationBackupModule?.exportPeripheralDiagramShare();
}

async function restorePeripheralDiagramShare(file) {
  await configurationBackupModule?.restorePeripheralDiagramShare(file);
}

async function checkOta() {
  return firmwareTab?.checkOta();
}

async function installSelectedFirmware() {
  return firmwareTab?.installSelectedFirmware();
}

function updateLocalFirmwareLabel() {
  firmwareTab?.updateLocalFirmwareLabel();
}

async function uploadLocalFirmware() {
  return firmwareTab?.uploadLocalFirmware();
}

async function cancelLocalFirmwareUpload() {
  return firmwareTab?.cancelLocalFirmwareUpload();
}

function isLocalFirmwareUploadActive() {
  return Boolean(firmwareTab?.isLocalUploadActive());
}

async function postSimple(path, message) {
  await request(path, { method: "POST", body: JSON.stringify({}) });
  setMessage(message);
  toast(message);
}

async function shutdownServer() {
  await request("/api/server-shutdown", { method: "POST", body: JSON.stringify({}) });
  setMessage("Web UI locked. Use MQTT payload 'unlock' on <baseTopic>/cmd/web_ui to restore access.");
  toast("Web UI locked");
}

async function requestDeviceRestart(path, {
  title = "Rebooting device...",
  message = "Reboot requested",
  totalSeconds = 30,
  saveDirtySettingsBeforeRestart = true,
} = {}) {
  if (saveDirtySettingsBeforeRestart && state.settingsDirty) {
    await saveSettings({ silent: true });
  }
  if (saveDirtySettingsBeforeRestart) {
    await awaitPendingSettingsSave();
  }
  state.rebootOverlayArmed = true;
  showRebootOverlay(title, totalSeconds);
  try {
    await fetch(path, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({}),
      cache: "no-store",
    });
  } catch {
    // The device may drop the HTTP response while rebooting.
  }
  setMessage(message);
  toast(message);
}

async function copyCurrentUrl() {
  const value = elements.currentUrl.value;
  if (!value) {
    toast("No current URL to copy");
    return;
  }
  await navigator.clipboard.writeText(value);
  toast("Copied current URL");
}

async function copyStorageUrl(path) {
  const url = `${window.location.origin}${storageStreamUrl(path)}`;
  await navigator.clipboard.writeText(url);
  setStorageStatus(`Copied URL for ${path}`);
  toast("File URL copied");
}

async function triggerDisplay() {
  await displayTab?.triggerDisplay();
}

bindPlaybackStepButton(elements.playbackPrevButton, -1, () => stepContextPlayback(-1));
elements.playbackHeroToggleButton?.addEventListener("click", () => handlePlaybackAction().catch(handleError));
bindPlaybackStepButton(elements.playbackNextButton, 1, () => stepContextPlayback(1));
bindPlaybackStepButton(elements.storageInlinePrevButton, -1, () => stepFolderPlayback(-1));
bindPlaybackStepButton(elements.storageInlineNextButton, 1, () => stepFolderPlayback(1));
elements.playbackActionButton?.addEventListener("click", () => handlePlaybackAction().catch(handleError));
document.getElementById("copyUrlButton").addEventListener("click", () => copyCurrentUrl().catch(handleError));
document.getElementById("checkOtaButton").addEventListener("click", () => checkOta().catch(handleError));
document.getElementById("applyOtaButton").addEventListener("click", () => installSelectedFirmware().catch(handleError));
elements.updateAvailableCloseButton?.addEventListener("click", closeUpdateAvailablePopup);
elements.updateAvailableDialog?.addEventListener("cancel", (event) => {
  event.preventDefault();
  closeUpdateAvailablePopup();
});
document.getElementById("uploadFirmwareButton").addEventListener("click", () => {
  if (isLocalFirmwareUploadActive()) {
    cancelLocalFirmwareUpload().catch(handleError);
    return;
  }
  elements.localFirmwareFile.value = "";
  updateLocalFirmwareLabel();
  elements.localFirmwareFile.click();
});
elements.localFirmwareFile?.addEventListener("change", () => {
  updateLocalFirmwareLabel();
  if (elements.localFirmwareFile.files && elements.localFirmwareFile.files[0]) {
    uploadLocalFirmware().catch(handleError);
  }
});
elements.headerActionsButton?.addEventListener("click", (event) => {
  event.preventDefault();
  event.stopPropagation();
  setHeaderActionsMenuOpen(!state.headerActionsMenuOpen);
});
elements.headerRefreshButton?.addEventListener("click", (event) => {
  event.preventDefault();
  setHeaderActionsMenuOpen(false);
  performFrontendHardRefresh();
});
elements.headerRebootButton?.addEventListener("click", (event) => {
  event.preventDefault();
  requestDeviceRestart("/api/reboot", {
    title: "Rebooting device...",
    message: "Reboot requested",
    totalSeconds: 30,
  }).catch(handleError);
});
elements.headerShutdownButton?.addEventListener("click", (event) => {
  event.preventDefault();
  setHeaderActionsMenuOpen(false);
  const mqttConfigured = Boolean(state.settings?.mqtt?.host && String(state.settings.mqtt.host).trim());
  const mqttConnected = Boolean(state.status?.network?.mqttConnected);
  if (!mqttConfigured || !mqttConnected) {
    const reason = !mqttConfigured
      ? "MQTT is not configured."
      : "MQTT is not connected right now.";
    const message = `${reason} Web UI lock is blocked because you would not be able to unlock the device remotely. Configure and connect MQTT first.`;
    setMessage(message, true);
    toast(message);
    return;
  }
  if (!window.confirm("Lock the web interface now?\n\nThis device is remote. Once locked, the web UI will become unavailable until you either unlock it over MQTT with payload 'unlock' on <baseTopic>/cmd/web_ui, or physically reboot/reset the device.\n\nWi-Fi and MQTT will stay running.")) {
    return;
  }
  shutdownServer().catch(handleError);
});
document.addEventListener("click", (event) => {
  if (!state.headerActionsMenuOpen) {
    return;
  }
  const target = event.target;
  if (elements.headerActionsButton?.contains(target) || elements.headerActionsMenu?.contains(target)) {
    return;
  }
  setHeaderActionsMenuOpen(false);
});
document.addEventListener("keydown", (event) => {
  if (event.key === "Escape" && state.headerActionsMenuOpen) {
    setHeaderActionsMenuOpen(false);
  }
});
window.addEventListener("pageshow", () => {
  resetTransientOverlays();
});
document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "hidden") {
    hideRebootOverlay();
  }
});
elements.playForm.addEventListener("submit", (event) => handlePlaybackAction(event).catch(handleError));
elements.radioCountrySelect?.addEventListener("change", (event) => {
  loadRadioStations(event.target.value).catch(handleError);
});
elements.radioStationSelect?.addEventListener("change", () => {
  applySelectedRadioStation({ autoPlay: true }).catch(handleError);
});
elements.volumeSlider.addEventListener("change", (event) => setVolume(Number(event.target.value)).catch(handleError));
elements.storagePreviewVolumeSlider?.addEventListener("change", (event) => setVolume(Number(event.target.value)).catch(handleError));
elements.volumeSlider.addEventListener("input", (event) => {
  elements.volumeValue.textContent = `${event.target.value}%`;
});
elements.audioEqualizerPreset?.addEventListener("change", (event) => {
  const preset = event.target.value;
  const gains = EQUALIZER_PRESET_GAINS[preset] || currentEqualizerGains();
  renderEqualizerPreset(preset, gains);
  setEqualizer(preset, gains).catch(handleError);
});
for (const slider of [elements.equalizerLowSlider, elements.equalizerPresenceSlider, elements.equalizerHighSlider]) {
  slider?.addEventListener("input", () => {
    renderEqualizerPreset("custom", currentEqualizerGains());
  });
  slider?.addEventListener("change", () => {
    const gains = currentEqualizerGains();
    renderEqualizerPreset("custom", gains);
    setEqualizer("custom", gains).catch(handleError);
  });
}
renderEqualizerPreset();
elements.storagePreviewVolumeSlider?.addEventListener("input", (event) => {
  if (elements.storagePreviewVolumeValue) {
    elements.storagePreviewVolumeValue.textContent = `${event.target.value}%`;
  }
});
for (const field of [elements.sdEnabled, elements.sdCsPin, elements.sdSckPin, elements.sdMosiPin, elements.sdMisoPin]) {
  field?.addEventListener("change", () => {
    populateSdPinOptions();
    populateStatusLedPinOptions();
    populateBatteryAdcPinOptions();
    populateOledPinOptions();
    populateWapeTriggerPinOptions();
    updateBatteryUi();
    renderDeviceResources(state.status || {});
  });
}
for (const field of [elements.oledSdaPin, elements.oledSclPin, elements.oledResetPin]) {
  field?.addEventListener("change", () => {
    state.settingsDirty = true;
  });
}
elements.useStaticIpToggle.addEventListener("change", updateConditionalVisibility);

for (const field of elements.settingsForm.elements) {
  if (!field || !field.name) {
    continue;
  }
  if (field.hasAttribute("data-wifi-power")) {
    // Dedicated power controls save only on Apply, never on each drag event.
    continue;
  }

  if (field.name === "device.savedVolumePercent") {
    continue;
  }

  if (field.name === "audio.equalizerPreset" || field.name === "audio.equalizerLowDb" ||
      field.name === "audio.equalizerPresenceDb" || field.name === "audio.equalizerHighDb") {
    continue;
  }

  if (field.name === "audio.wsPin" || field.name === "audio.bclkPin" || field.name === "audio.doutPin") {
    continue;
  }

  if (field.name === "device.statusLedPin") {
    field.addEventListener("change", () => {
      populateSdPinOptions();
      populateOledPinOptions();
      populateBatteryAdcPinOptions();
      populateWapeTriggerPinOptions();
      updateBatteryUi();
      state.settingsDirty = true;
    });
    continue;
  }

  if (field.type === "checkbox" || field.tagName === "SELECT") {
    field.addEventListener("change", () => {
      if (field.name.startsWith("wifi.") && !document.body.classList.contains("local-builder-mode")) {
        state.settingsDirty = true;
      } else {
        queueSettingsSave(150);
      }
      if (field.name === "wifi.ssid" || field.name === "wifi.password") {
        updateWifiActionButton();
      }
      if (field.name?.startsWith("device.lowBattery")) {
        updateLowBatterySleepUi();
      }
      if (field.name?.startsWith("oled.")) {
        renderOledPreview();
      }
      if (field.name?.startsWith("mqtt.")) {
        setMqttConnectStatus("");
      }
    });
    continue;
  }

  field.addEventListener("input", (event) => {
    normalizeDecimalField(event.target);
    if (event.target.name === "battery.calibrationMultiplier") {
      updateDerivedBatteryCalibration();
    }
    if (event.target.name === "wifi.ssid" || event.target.name === "wifi.password") {
      if (event.target.name === "wifi.ssid" && elements.wifiNetworkList) {
        const selectedNetwork = String(elements.wifiNetworkList.value || "").trim();
        const typedSsid = String(event.target.value || "").trim();
        if (!selectedNetwork || typedSsid !== selectedNetwork) {
          elements.wifiNetworkList.value = "";
          state.wifiSelectionPending = false;
        }
      }
      updateWifiActionButton();
      if (!state.wifiConnectInProgress) {
        setScanStatus("");
      }
    }
    if (event.target.name?.startsWith("device.lowBattery")) {
      updateLowBatterySleepUi();
    }
    if (event.target.name?.startsWith("oled.")) {
      renderOledPreview();
    }
    if (event.target.name?.startsWith("mqtt.")) {
      setMqttConnectStatus("");
    }
    if (event.target.name.startsWith("wifi.") && !document.body.classList.contains("local-builder-mode")) {
      // Network changes can disconnect the page. Keep them local until the
      // explicit Connect action submits the complete credential/IP fields.
      state.settingsDirty = true;
    } else {
      queueSettingsSave();
    }
  });

  field.addEventListener("blur", (event) => {
    normalizeDecimalField(event.target);
    if (state.settingsDirty && !event.target.name.startsWith("wifi.")) {
      saveSettings({ silent: true }).catch(handleError);
    }
  });
}

function handleError(error) {
  if (isExpectedOfflineError(error)) {
    markDeviceReachability(false);
    setMessage("Device offline", true);
    if (!state.offlineNoticeShown) {
      toast("Device offline");
      state.offlineNoticeShown = true;
    }
    return;
  }
  console.error(error);
  setMessage(error.message, true);
  toast(`Error: ${error.message}`);
}

resetTransientOverlays();
const desktopFlashView = new URLSearchParams(window.location.search).get("elmaView") === "flash";
if (desktopFlashView) {
  document.body.classList.add("desktop-flash-view");
}
restoreStorageLocation();
state.peripheralDiagramPositions = loadPeripheralDiagramPositions();
restorePeripheralProfileSelections();
setupTabs();
setupPasswordToggles();
setupPeripheralDiagramInteractions();
renderRecentPlayback();
renderPeripheralAudioOutputControls();
renderPeripheralAudioInControls();
renderPeripheralDisplayControls();
renderPeripheralSensorControls();
renderPeripheralInputControls();
renderPeripheralPowerControls();
renderPeripheralControlControls();
renderPeripheralExpansionControls();
renderPeripheralStorageControls();
renderPeripheralCommunicationControls();
renderPeripheralDiagram();
syncGpioMappingControls();
updateWifiActionButton();
populateButtonActionSelects();
renderOledPreview();
normalizeOwnedFormControlIds();
restoreGpioBoardPreferences();
updateGpioBoardSelectorMode(state.status);
updateGpioBoardImage();
updateHeaderActionsButtonStatus();
loadRadioCountries().catch(handleError);

uiHistoryModule.captureSnapshot({ replace: true });

Promise.allSettled([loadStatus(), loadSettings()])
  .then((results) => {
    for (const result of results) {
      if (result.status === "rejected") {
        handleError(result.reason);
      }
    }
    if (desktopFlashView) {
      activateTabByName("firmware", { persist: false });
    } else {
      restoreSavedActiveTabIfVisible();
    }
    uiHistoryModule?.captureSnapshot({ replace: true });
  });
localBuilder.initialize().catch(handleError);
window.elmaSaveDesignerSettings = () => saveSettings({ silent: true });
window.elmaCollectDesignerSettings = () => JSON.stringify(collectForm());
window.elmaBeginFlashSync = () => {
  if (!desktopFlashView) return;
  elements.localBuilderCompileFlash.disabled = true;
  elements.localBuilderProgressLabel.textContent = "Synchronizing Device Designer settings...";
};
window.elmaRefreshFlashSettings = async () => {
  if (!desktopFlashView) return;
  try {
    await loadSettings();
    elements.localBuilderProgressLabel.textContent = "Designer configuration synchronized — ready to compile and flash";
  } finally {
    elements.localBuilderCompileFlash.disabled = false;
  }
};
startStatusPolling();
