"""ELMA Flasher: standalone ELMA device designer, compiler and flash utility."""

from __future__ import annotations

import base64
import binascii
import concurrent.futures
import contextlib
import ctypes
import gzip
import hashlib
import http.client
import io
import ipaddress
import json
import os
import pathlib
import queue
import random
import re
import shutil
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import tkinter as tk
import urllib.error
import urllib.parse
import urllib.request
import uuid
import webbrowser
from xml.sax.saxutils import escape as xml_escape
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from tkinter import filedialog, messagebox, ttk
from typing import Callable

import esptool
import serial
from serial.tools import list_ports
from zeroconf import ServiceBrowser, ServiceListener, Zeroconf

from migration_importer import import_device
from gpio_validation import validate_gpio_settings


APP_VERSION = "0.1.43"
WINDOWS_APP_USER_MODEL_ID = "ELMA.IoT.Flasher"
FLASH_BAUD = 460800
CONSOLE_BAUD = 115200
OTA_DATA_ADDRESS = 0xE000
OTA_DATA_SIZE = 0x2000
APPLICATION_ADDRESS = 0x10000
MAX_APPLICATION_SIZE = 0x1F0000
PROVISION_TIMEOUT_SECONDS = 120
ORANGE = "#ef8b00"
DARK = "#2a2926"
PAPER = "#f7f5ef"
WHITE = "#fffdf8"
MUTED = "#6e6961"
RED = "#b42318"
GREEN = "#18864b"
CHIP_FAMILIES = {"esp32": "ESP32", "esp32s3": "ESP32-S3", "esp32c3": "ESP32-C3"}
CHIP_CHOICES = {"auto": "Auto-detect (recommended)", **CHIP_FAMILIES}
BOARD_PROFILES = {
    "esp32-s3-super-mini": (1, "esp32s3"),
    "esp32-s3-zero": (2, "esp32s3"),
    "esp32-s3-psram": (3, "esp32s3"),
    "esp32-spk-n16r8": (4, "esp32s3"),
    "esp32-s3-devkit-c1": (5, "esp32s3"),
    "esp32-s3-cam-module": (6, "esp32s3"),
    "esp32-wrover": (7, "esp32"),
    "esp32-wroom": (8, "esp32"),
    "esp32-mini": (9, "esp32"),
    "wemos-lolin32-mini": (10, "esp32"),
    "esp32-c3": (11, "esp32c3"),
}
DEFAULT_BOARD_PROFILE = {
    "esp32s3": "esp32-s3-super-mini",
    "esp32": "esp32-wroom",
    "esp32c3": "esp32-c3",
}
BOARD_ASSET_FILES = {
    "esp32-s3-super-mini": "esp32-s3-supermini-breadboard.svg",
    "esp32-s3-zero": "esp32-s3-zero-breadboard.svg",
    "esp32-s3-psram": "esp32-s3-psram-breadboard.svg",
    "esp32-spk-n16r8": "esp32-spk-n16r8-breadboard.svg",
    "esp32-s3-devkit-c1": "esp32-s3-devkit-c1-n8r8-v1-breadboard.svg",
    "esp32-s3-cam-module": "esp32-s3-cam-module-breadboard.svg",
    "esp32-wrover": "esp32-wrover-breadboard.svg",
    "esp32-wroom": "esp32-wroom-breadboard.svg",
    "esp32-mini": "esp32-mini-breadboard.svg",
    "wemos-lolin32-mini": "wemos-lolin32-mini-breadboard.svg",
    "esp32-c3": "esp32-c3-breadboard.svg",
}
KNOWN_CHIP_MODELS = {
    "esp32s3": "ESP32-S3",
    "esp32c3": "ESP32-C3",
    "esp32c6": "ESP32-C6",
    "esp32s2": "ESP32-S2",
    "esp32h2": "ESP32-H2",
    "esp32c2": "ESP32-C2",
    "esp32c5": "ESP32-C5",
    "esp32p4": "ESP32-P4",
    "esp32": "ESP32",
}


def resource_path(relative: str) -> pathlib.Path:
    base = pathlib.Path(getattr(sys, "_MEIPASS", pathlib.Path(__file__).resolve().parent))
    resolved = base / relative
    if not getattr(sys, "frozen", False) and relative.replace("\\", "/").startswith("assets/"):
        development_asset = pathlib.Path(__file__).resolve().parents[2] / ".elma-flasher-build" / relative
        if development_asset.exists():
            return development_asset
    return resolved


def configure_windows_identity() -> None:
    """Give the process its own Windows taskbar identity before creating a window."""
    if sys.platform == "win32":
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(WINDOWS_APP_USER_MODEL_ID)


def window_has_windows_icon(root: tk.Tk) -> bool:
    """Confirm that the native top-level window exposes a taskbar icon handle."""
    if sys.platform != "win32":
        return True
    user32 = ctypes.windll.user32
    window = int(root.winfo_id())
    handles = []
    while window and window not in handles:
        handles.append(window)
        window = int(user32.GetParent(window))
    wm_geticon = 0x007F
    for handle in handles:
        for icon_type in (1, 0, 2):  # ICON_BIG, ICON_SMALL, ICON_SMALL2
            if user32.SendMessageW(handle, wm_geticon, icon_type, 0):
                return True
        if user32.GetClassLongPtrW(handle, -14) or user32.GetClassLongPtrW(handle, -34):
            return True
    return False


def chip_family_from_image(data: bytes) -> str:
    if len(data) < 24 or data[0] != 0xE9:
        raise ValueError("The selected file is not a valid ESP32 application image.")
    chip_id = data[12] | (data[13] << 8)
    if chip_id == 0x0000:
        return "esp32"
    if chip_id == 0x0009:
        return "esp32s3"
    if chip_id == 0x0005:
        return "esp32c3"
    raise ValueError(f"Unsupported Espressif image chip ID 0x{chip_id:04X}.")


def chip_family_from_esptool_output(output: str) -> str:
    normalized = output.upper().replace("_", "-")
    for model, label in KNOWN_CHIP_MODELS.items():
        if re.search(rf"\b{re.escape(label)}\b", normalized):
            return model
    raise RuntimeError("The flashing engine connected, but did not report a recognized ESP chip model.")


def flash_size_from_esptool_output(output: str) -> str:
    match = re.search(r"(?:Embedded\s+)?Flash\s+(\d+)\s*(MB|KB)\b", output, re.I)
    return f"{match.group(1)} {match.group(2).upper()}" if match else ""


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def sanitize_clone_configuration(settings: object) -> dict:
    if not isinstance(settings, dict):
        raise ValueError("Source device returned an invalid configuration object.")
    cloned = json.loads(json.dumps(settings))
    device = cloned.setdefault("device", {})
    mqtt = cloned.setdefault("mqtt", {})
    wifi = cloned.setdefault("wifi", {})
    if isinstance(device, dict):
        device.pop("deviceName", None)
        device.pop("friendlyName", None)
    if isinstance(mqtt, dict):
        mqtt.pop("clientId", None)
        mqtt.pop("baseTopic", None)
    if isinstance(wifi, dict):
        # A source device's static address must never be duplicated onto a new
        # device. It can be supplied explicitly in Preconfigure target instead.
        wifi["useStaticIp"] = False
        for field in ("staticIp", "gateway", "subnet", "dns1", "dns2"):
            wifi.pop(field, None)
    cloned.pop("usingSavedSettings", None)
    return cloned


def merge_configuration(base: dict, overrides: dict) -> dict:
    merged = json.loads(json.dumps(base))
    for section, values in overrides.items():
        if not isinstance(values, dict):
            merged[section] = values
            continue
        target = merged.setdefault(section, {})
        if not isinstance(target, dict):
            target = {}
            merged[section] = target
        target.update(values)
    # These identities are always derived by firmware from the target efuse MAC.
    device = merged.setdefault("device", {})
    mqtt = merged.setdefault("mqtt", {})
    if isinstance(device, dict):
        device.pop("deviceName", None)
    if isinstance(mqtt, dict):
        mqtt.pop("clientId", None)
        mqtt.pop("baseTopic", None)
    return merged


def friendly_error(error: BaseException) -> str:
    message = str(error).strip() or error.__class__.__name__
    lowered = message.lower()
    if "access is denied" in lowered or "permission" in lowered or "could not open port" in lowered:
        return "The COM port is busy or unavailable. Close serial monitors and other flashing tools, reconnect the device, and try again."
    if "failed to connect" in lowered or "connecting" in lowered or "bootloader" in lowered:
        return "Could not enter the ESP bootloader. Hold BOOT, tap RESET, release BOOT after connection starts, and retry."
    if "urlopen" in lowered or "timed out" in lowered or "host" in lowered:
        return "The source device could not be reached. Verify its IP address, Wi-Fi connection, and web credentials."
    if "401" in lowered or "unauthorized" in lowered:
        return "The source device rejected its web username or password."
    if "disconnected" in lowered or "device not configured" in lowered:
        return "The USB device disconnected. Check its data cable and power, reconnect it, and retry."
    return message


class HttpDeviceClient:
    def __init__(self, host: str, username: str = "", password: str = "", timeout: float = 45) -> None:
        value = host.strip().rstrip("/")
        if not value:
            raise ValueError("Enter the source device IP address.")
        if "://" not in value:
            value = f"http://{value}"
        parsed = urllib.parse.urlparse(value)
        if parsed.scheme != "http" or not parsed.hostname or parsed.path not in ("", "/"):
            raise ValueError("Use an HTTP device IP or hostname without a path, for example 192.168.1.41.")
        self.base_url = value
        self.timeout = timeout
        self.authorization = ""
        if username or password:
            token = base64.b64encode(f"{username}:{password}".encode()).decode()
            self.authorization = f"Basic {token}"

    def _request(
        self,
        path: str,
        method: str = "GET",
        data: bytes | None = None,
        content_type: str = "application/json",
    ) -> bytes:
        headers = {"Accept": "application/json", "User-Agent": f"ELMA-Flasher/{APP_VERSION}"}
        if data is not None:
            headers["Content-Type"] = content_type
        if self.authorization:
            headers["Authorization"] = self.authorization
        request = urllib.request.Request(f"{self.base_url}{path}", headers=headers, data=data, method=method)
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                return response.read()
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", "replace")
            raise RuntimeError(f"Source device HTTP {error.code}: {detail or error.reason}") from error

    def json(self, path: str) -> dict:
        try:
            value = json.loads(self._request(path).decode("utf-8"))
        except json.JSONDecodeError as error:
            raise RuntimeError(f"Source device returned invalid JSON for {path}.") from error
        if not isinstance(value, dict):
            raise RuntimeError(f"Source device returned an invalid object for {path}.")
        return value

    def json_request(self, path: str, method: str = "POST", value: dict | None = None) -> dict:
        data = json.dumps(value or {}, separators=(",", ":")).encode("utf-8")
        try:
            result = json.loads(self._request(path, method=method, data=data).decode("utf-8"))
        except json.JSONDecodeError as error:
            raise RuntimeError(f"Destination device returned invalid JSON for {path}.") from error
        if not isinstance(result, dict):
            raise RuntimeError(f"Destination device returned an invalid object for {path}.")
        return result

    def put_binary(self, path: str, data: bytes) -> dict:
        try:
            result = json.loads(self._request(path, method="PUT", data=data, content_type="application/octet-stream").decode("utf-8"))
        except json.JSONDecodeError as error:
            raise RuntimeError("Destination device returned an invalid OTA chunk acknowledgement.") from error
        if not isinstance(result, dict):
            raise RuntimeError("Destination device returned an invalid OTA chunk acknowledgement.")
        return result

    def binary(self, path: str, expected_size: int = 0) -> bytes:
        data = self._request(path)
        if expected_size and len(data) != expected_size:
            raise RuntimeError(f"Incomplete firmware part: received {len(data)} of {expected_size} bytes.")
        return data


class EsptoolOutput(io.TextIOBase):
    def __init__(self, callback) -> None:
        self.callback = callback
        self.buffer = ""

    def writable(self) -> bool:
        return True

    def write(self, value: str) -> int:
        self.buffer += str(value).replace("\r", "\n")
        lines = self.buffer.split("\n")
        self.buffer = lines.pop()
        for line in lines:
            if line.strip():
                self.callback(line.strip())
        return len(value)

    def flush(self) -> None:
        if self.buffer.strip():
            self.callback(self.buffer.strip())
        self.buffer = ""


def default_designer_settings() -> dict:
    """A complete-enough future-device document for the shared web configurator."""
    return {
        "usingSavedSettings": False,
        "device": {
            "deviceName": "elma-future-device", "friendlyName": "ELMA Future Device",
            "savedVolumePercent": 35, "audioMuted": False, "statusLedPin": 8,
            "statusLedType": "neopixel",
            "lowBatterySleepEnabled": False, "lowBatterySleepThresholdPercent": 10,
            "lowBatteryWakeIntervalMinutes": 30, "powerCycleFactoryResetEnabled": True,
            "touchHoldFactoryResetEnabled": True,
        },
        "wifi": {
            "ssid": "", "password": "", "apSsid": "", "apPassword": "",
            "apFallbackEnabled": True, "useStaticIp": False, "staticIp": "",
            "staTxPowerDbm": 15.0, "apTxPowerDbm": 15.0,
            "gateway": "", "subnet": "255.255.255.0", "dns1": "", "dns2": "",
        },
        "mqtt": {
            "host": "", "port": 1883, "username": "", "password": "",
            "clientId": "", "baseTopic": "", "discoveryEnabled": True,
        },
        "audio": {"enabled": False, "rememberLastPlayed": True, "wsPin": 5, "bclkPin": 4, "doutPin": 3},
        "oled": {"enabled": False, "displayType": "oled", "driver": "ssd1306", "i2cAddress": 60, "width": 128, "height": 64, "rotation": 0, "dimTimeoutSeconds": 60, "sdaPin": 4, "sclPin": 5, "resetPin": -1},
        "sd": {"enabled": False, "csPin": 10, "sckPin": 6, "mosiPin": 7, "misoPin": 2},
        "battery": {"adcPin": 0, "calibrationMultiplier": 2.0, "measuredVoltage": 0, "movingAverageWindowSize": 8, "updateIntervalMs": 5000},
        "ota": {"autoCheck": True, "autoUpdate": False},
        "webAuth": {"enabled": False, "username": "admin", "password": ""},
        "effects": {},
        "ui": {
            "gpioBoardAutodetect": True, "gpioBoardSelection": "esp32-c3",
            "peripheralDiagramPositions": {}, "peripheralHelperBindings": {},
            "peripheralProfiles": {
                "audioProfile": "none", "audioProfiles": ["none"], "audioInProfile": "none",
                "audioInProfiles": ["none"], "displayProfile": "none", "displayProfiles": ["none"],
                "sensors": ["none"], "inputs": ["none"], "controls": ["none"],
                "expansions": ["none"], "storage": ["none"], "communication": ["none"], "power": ["none"],
            },
        },
    }


class DesignerJob:
    def __init__(self) -> None:
        self.id = uuid.uuid4().hex
        self.state = "queued"
        self.progress = 0
        self.status = "Queued"
        self.compatibility = "Resolving chip capabilities and selected peripherals."
        self.log: list[str] = []
        self.error = ""
        self.ip_address = ""
        self.cancelled = False
        self.cancel_deferred = False
        self.phase = "queued"
        self.transport = "usb"
        self.target_kind = ""
        self.target_version = ""
        self.critical_flash = False
        self.upload_session_id = ""
        self.network_client: HttpDeviceClient | None = None
        self.process: subprocess.Popen | None = None
        self.profile = ""
        self.application_bytes = 0
        self.flash_capacity_bytes = 0
        self.ram_used_bytes = 0
        self.ram_total_bytes = 0
        self.firmware_file = ""

    def append(self, value: str) -> None:
        line = str(value).strip()
        if line:
            self.log.append(line)
            self.log = self.log[-600:]

    def public(self) -> dict:
        return {
            "jobId": self.id, "state": self.state, "progress": self.progress,
            "status": self.status, "compatibility": self.compatibility,
            "log": self.log, "error": self.error, "ipAddress": self.ip_address,
            "profile": self.profile, "applicationBytes": self.application_bytes,
            "flashCapacityBytes": self.flash_capacity_bytes,
            "ramUsedBytes": self.ram_used_bytes, "ramTotalBytes": self.ram_total_bytes,
            "firmwareFile": self.firmware_file,
            "phase": self.phase, "transport": self.transport,
            "targetKind": self.target_kind, "cancelDeferred": self.cancel_deferred,
        }


class DesignerServer:
    """Loopback-only backend for the cloned web configurator and native flashing."""
    def __init__(self, flasher: "FlasherApplication") -> None:
        self.flasher = flasher
        self.httpd: ThreadingHTTPServer | None = None
        self.thread: threading.Thread | None = None
        self.url = ""
        self.settings_lock = threading.RLock()
        default_path = self.last_configuration_path() or self.settings_path()
        self.migrated_configuration_source: pathlib.Path | None = None
        if not default_path.is_file() and self.settings_persistence_enabled():
            legacy_path = self.find_legacy_configuration()
            if legacy_path is not None:
                migrated = self.load_designer_settings(legacy_path)
                default_path.parent.mkdir(parents=True, exist_ok=True)
                temporary = default_path.with_suffix(".tmp")
                temporary.write_text(json.dumps(migrated, indent=2), encoding="utf-8")
                temporary.replace(default_path)
                self.migrated_configuration_source = legacy_path
        self.active_settings_path: pathlib.Path | None = default_path if default_path.is_file() else None
        self.settings = self.load_designer_settings(self.active_settings_path)
        self.jobs: dict[str, DesignerJob] = {}
        self.lock = threading.Lock()
        self.pc_wifi_connected = False
        self.pc_wifi_ssid = ""
        self.pc_mqtt_connected = False
        self.settings_save_revision = 0
        self.settings_saved_revision = 0
        self.network_device_cache: dict[str, dict] = {}

    def web_root(self) -> pathlib.Path:
        bundled = resource_path("web")
        if bundled.is_dir():
            return bundled
        return pathlib.Path(__file__).resolve().parents[2] / "web"

    @staticmethod
    def portable_home() -> pathlib.Path:
        launcher_home = os.environ.get("ELMA_PORTABLE_HOME", "").strip()
        if launcher_home:
            return pathlib.Path(launcher_home).resolve()
        if getattr(sys, "frozen", False):
            return pathlib.Path(sys.executable).resolve().parent
        return pathlib.Path(os.environ.get("LOCALAPPDATA", tempfile.gettempdir())) / "ELMA IoT" / "Flasher"

    @staticmethod
    def settings_path() -> pathlib.Path:
        return DesignerServer.portable_home() / "ELMA-Flasher.config.json"

    @staticmethod
    def application_state_path() -> pathlib.Path:
        return DesignerServer.portable_home() / "ELMA-Flasher.state.json"

    @classmethod
    def last_configuration_path(cls) -> pathlib.Path | None:
        if not cls.settings_persistence_enabled():
            return None
        try:
            state = json.loads(cls.application_state_path().read_text(encoding="utf-8"))
            filename = state.get("lastConfigurationPath")
            if filename:
                path = pathlib.Path(filename).expanduser().resolve()
                if cls.configuration_from_document(json.loads(path.read_text(encoding="utf-8"))) is not None:
                    return path
            # Older builds remembered only the folder. Recover the newest named
            # document there, excluding application state and build caches.
            directory = state.get("lastConfigurationDirectory")
            if directory:
                candidates = sorted(pathlib.Path(directory).glob("*.json"), key=lambda p: p.stat().st_mtime, reverse=True)
                for path in candidates:
                    if path.name.startswith("ELMA-Flasher."):
                        continue
                    try:
                        if cls.configuration_from_document(json.loads(path.read_text(encoding="utf-8"))) is not None:
                            return path.resolve()
                    except (OSError, ValueError):
                        continue
        except (OSError, TypeError, ValueError, AttributeError):
            pass
        return None

    @staticmethod
    def settings_persistence_enabled() -> bool:
        return not any(argument.endswith("-test") for argument in sys.argv[1:])

    @staticmethod
    def configuration_from_document(payload: object) -> dict | None:
        if not isinstance(payload, dict):
            return None
        candidate = payload.get("settings") if isinstance(payload.get("settings"), dict) else payload
        defaults = default_designer_settings()
        settings = {key: value for key, value in candidate.items() if key in defaults}
        return settings if settings else None

    @classmethod
    def find_legacy_configuration(cls) -> pathlib.Path | None:
        """Find the combined state/config document written by older portable builds."""
        home = cls.portable_home()
        candidates: list[pathlib.Path] = [cls.application_state_path()]
        try:
            state = json.loads(cls.application_state_path().read_text(encoding="utf-8"))
            previous_directory = pathlib.Path(str(state.get("lastConfigurationDirectory", ""))).expanduser()
            if previous_directory.is_dir():
                candidates.append(previous_directory / "ELMA-Flasher.state.json")
                candidates.append(previous_directory / "ELMA-Flasher.config.json")
        except (OSError, TypeError, ValueError, json.JSONDecodeError):
            pass
        try:
            sibling_states = sorted(
                (path for path in home.parent.glob("*/ELMA-Flasher.state.json") if path.parent != home),
                key=lambda path: path.stat().st_mtime,
                reverse=True,
            )
            candidates.extend(sibling_states)
        except OSError:
            pass

        seen: set[pathlib.Path] = set()
        for path in candidates:
            try:
                resolved = path.resolve()
                if resolved in seen:
                    continue
                seen.add(resolved)
                payload = json.loads(resolved.read_text(encoding="utf-8"))
                if cls.configuration_from_document(payload) is not None:
                    return resolved
            except (OSError, TypeError, ValueError, json.JSONDecodeError):
                continue
        return None

    @staticmethod
    def merge_settings(target: dict, source: dict) -> None:
        for key, value in source.items():
            if isinstance(value, dict) and isinstance(target.get(key), dict):
                DesignerServer.merge_settings(target[key], value)
            else:
                target[key] = value

    @staticmethod
    def restore_legacy_peripheral_ui(settings: dict, source: dict) -> None:
        """Recreate the fixed legacy Notifier profile when old state omitted UI metadata."""
        if "ui" in source:
            return
        audio = settings.get("audio", {})
        oled = settings.get("oled", {})
        battery = settings.get("battery", {})
        signature = (
            (audio.get("doutPin"), audio.get("wsPin"), audio.get("bclkPin")) == (25, 26, 27)
            and (oled.get("sdaPin"), oled.get("sclPin")) == (23, 19)
            and battery.get("adcPin") == 36
        )
        if not signature:
            return
        settings["usingSavedSettings"] = True
        audio["enabled"] = True
        oled["enabled"] = True
        oled["displayType"] = "oled"
        ui = settings["ui"]
        ui["gpioBoardAutodetect"] = False
        ui["gpioBoardSelection"] = "wemos-lolin32-mini"
        ui["peripheralProfiles"] = {
            "audioProfile": "pcm5102-i2s-dac",
            "audioProfiles": ["pcm5102-i2s-dac"],
            "audioInProfile": "none",
            "audioInProfiles": ["none"],
            "displayProfile": "i2c-oled",
            "displayProfiles": ["i2c-oled"],
            "sensors": ["battery-voltage-divider-220k"],
            "inputs": ["none"],
            "controls": ["none"],
            "expansions": ["none"],
            "storage": ["none"],
            "communication": ["none"],
            "power": ["none"],
        }

    def load_designer_settings(self, path: pathlib.Path | None = None) -> dict:
        defaults = default_designer_settings()
        if not self.settings_persistence_enabled() or path is None:
            return defaults
        try:
            saved = json.loads(path.read_text(encoding="utf-8"))
            saved = self.configuration_from_document(saved)
            if saved is None:
                return defaults
        except (OSError, json.JSONDecodeError):
            return defaults

        self.merge_settings(defaults, saved)
        self.restore_legacy_peripheral_ui(defaults, saved)
        return defaults

    def open_configuration(self, path: pathlib.Path) -> None:
        resolved = path.expanduser().resolve()
        try:
            raw = json.loads(resolved.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            raise ValueError(f"Configuration JSON is invalid: {error.msg}.") from error
        except OSError as error:
            raise ValueError(f"Configuration file could not be opened: {error}.") from error
        if isinstance(raw, dict) and isinstance(raw.get("settings"), dict):
            raw = raw["settings"]
        if not isinstance(raw, dict):
            raise ValueError("Configuration file must contain a JSON object.")
        settings = default_designer_settings()
        self.merge_settings(settings, raw)
        self.restore_legacy_peripheral_ui(settings, raw)
        with self.settings_lock:
            self.settings = settings
            self.active_settings_path = resolved
            self.settings_save_revision += 1
            self.settings_saved_revision = self.settings_save_revision
        self.remember_configuration_directory(resolved.parent)

    def save_designer_settings(self, settings: dict | None = None, path: pathlib.Path | None = None) -> pathlib.Path:
        if not self.settings_persistence_enabled():
            return path or self.active_settings_path or self.settings_path()
        destination = (path or self.active_settings_path)
        if destination is None:
            raise ValueError("Choose a name for the configuration before saving.")
        destination = destination.expanduser().resolve()
        with self.settings_lock:
            snapshot = json.loads(json.dumps(settings if settings is not None else self.settings))
            revision = self.settings_save_revision
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(f".{destination.name}.{os.getpid()}.tmp")
        temporary.write_text(json.dumps(snapshot, indent=2), encoding="utf-8")
        temporary.replace(destination)
        with self.settings_lock:
            self.active_settings_path = destination
            if revision == self.settings_save_revision:
                self.settings_saved_revision = revision
        self.remember_configuration_directory(destination.parent)
        return destination

    def save_designer_settings_async(self) -> None:
        """Mark the named document dirty; File/Save controls disk persistence."""
        with self.settings_lock:
            self.settings_save_revision += 1

    def configuration_dirty(self) -> bool:
        with self.settings_lock:
            return self.settings_save_revision != self.settings_saved_revision

    def last_configuration_directory(self) -> pathlib.Path:
        fallback = self.portable_home()
        try:
            payload = json.loads(self.application_state_path().read_text(encoding="utf-8"))
            saved = pathlib.Path(str(payload.get("lastConfigurationDirectory", ""))).expanduser()
            if saved.is_dir():
                return saved.resolve()
        except (OSError, TypeError, ValueError, json.JSONDecodeError):
            pass
        return fallback.resolve()

    def remember_configuration_directory(self, directory: pathlib.Path) -> None:
        if not self.settings_persistence_enabled() or not directory.is_dir():
            return
        path = self.application_state_path()
        payload = {}
        try:
            loaded = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(loaded, dict):
                payload = loaded
        except (OSError, json.JSONDecodeError):
            pass
        payload["lastConfigurationDirectory"] = str(directory.resolve())
        if self.active_settings_path is not None:
            payload["lastConfigurationPath"] = str(self.active_settings_path.resolve())
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(".tmp")
        temporary.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        temporary.replace(path)

    @staticmethod
    def generated_firmware_directory() -> pathlib.Path:
        if getattr(sys, "frozen", False):
            return DesignerServer.portable_home()
        return pathlib.Path(__file__).resolve().parents[2] / "release-assets" / f"v{APP_VERSION}"

    @staticmethod
    def generated_firmware_name(family: str, capabilities: dict, firmware_mode: str = "full") -> str:
        if firmware_mode == "minimal":
            return f"{family}-ota-bridge-v{APP_VERSION}.bin"
        suffix = "-hacs" if bool(capabilities.get("hacs", True)) else ""
        if not bool(capabilities.get("webUi", True)):
            suffix += "-slim"
        return f"{family}-notifier{suffix}-v{APP_VERSION}.bin"

    def project_root(self) -> pathlib.Path:
        if not getattr(sys, "frozen", False):
            return pathlib.Path(__file__).resolve().parents[2]
        source = resource_path("builder_project")
        target = pathlib.Path(os.environ.get("LOCALAPPDATA", tempfile.gettempdir())) / "ELMA IoT" / "Flasher" / f"builder-{APP_VERSION}"
        marker = target / ".elma-builder-ready"
        digest = hashlib.sha256()
        for path in sorted((item for item in source.rglob("*") if item.is_file()), key=lambda item: item.relative_to(source).as_posix()):
            relative = path.relative_to(source).as_posix().encode("utf-8")
            digest.update(len(relative).to_bytes(4, "big"))
            digest.update(relative)
            with path.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
        expected_marker = digest.hexdigest()
        try:
            current_marker = marker.read_text(encoding="utf-8").strip()
        except OSError:
            current_marker = ""
        if current_marker != expected_marker:
            target.mkdir(parents=True, exist_ok=True)
            for name in ("src", "include", "scripts", "partitions", "web"):
                destination = target / name
                if destination.exists():
                    shutil.rmtree(destination)
                shutil.copytree(source / name, destination)
            for name in ("platformio.ini", "sdkconfig.defaults", "package.json", "package-lock.json"):
                if (source / name).is_file():
                    shutil.copy2(source / name, target / name)
            temporary_marker = marker.with_suffix(".tmp")
            temporary_marker.write_text(expected_marker, encoding="utf-8")
            temporary_marker.replace(marker)
        return target

    def compiler_command(self) -> list[str]:
        bundled = resource_path("ELMA-Compiler-Core.exe")
        if bundled.is_file():
            return [str(bundled)]
        found = shutil.which("pio") or shutil.which("platformio")
        if found:
            return [found]
        raise RuntimeError("The portable compiler core is missing. Reinstall ELMA Flasher or use the complete release package.")

    @staticmethod
    def terminate_job_process(job: DesignerJob) -> None:
        process = job.process
        if process is None or process.poll() is not None:
            return
        if sys.platform == "win32":
            subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                creationflags=subprocess.CREATE_NO_WINDOW,
                check=False,
            )
        else:
            process.terminate()

    @staticmethod
    def chip_family_from_label(value: object) -> str:
        label = str(value or "").strip().lower().replace("-", "")
        if "esp32c6" in label or "esp32s2" in label:
            return ""
        if "esp32s3" in label or label == "s3":
            return "esp32s3"
        if "esp32c3" in label or label == "c3":
            return "esp32c3"
        if "esp32" in label:
            return "esp32"
        return ""

    def probe_network_device(self, payload: dict, timeout: float = 2.0) -> dict:
        host = str(payload.get("ip", "")).strip()
        username = str(payload.get("username", ""))
        password = str(payload.get("password", ""))
        client = HttpDeviceClient(host, username, password, timeout=timeout)
        ip_value = urllib.parse.urlparse(client.base_url).hostname or host
        cached = self.network_device_cache.get(ip_value)
        if cached and cached.get("kind") == "esphome" and cached.get("chip"):
            return dict(cached)

        try:
            status = client.json("/api/status")
            firmware = status.get("firmware", {}) if isinstance(status, dict) else {}
            chip = self.chip_family_from_label(firmware.get("chipFamily"))
            if chip:
                system = status.get("system", {}) if isinstance(status.get("system"), dict) else {}
                return {
                    "ip": ip_value,
                    "kind": "elma",
                    "name": str(system.get("hostname") or system.get("deviceName") or "ELMA device"),
                    "version": str(firmware.get("version", "")),
                    "chip": chip,
                    "upload": "elma-chunked-ota",
                }
        except (RuntimeError, OSError, urllib.error.URLError):
            pass

        try:
            tasmota = client.json("/cm?cmnd=Status%200")
            firmware = tasmota.get("StatusFWR", {}) if isinstance(tasmota, dict) else {}
            chip = self.chip_family_from_label(firmware.get("Hardware") or firmware.get("ESP"))
            if chip and ("Status" in tasmota or "StatusFWR" in tasmota):
                status = tasmota.get("Status", {}) if isinstance(tasmota.get("Status"), dict) else {}
                return {
                    "ip": ip_value,
                    "kind": "tasmota",
                    "name": str(status.get("FriendlyName", ["Tasmota"])[0] if isinstance(status.get("FriendlyName"), list) else status.get("DeviceName") or "Tasmota"),
                    "version": str(firmware.get("Version", "")),
                    "chip": chip,
                    "upload": "tasmota-web-ota",
                }
        except (RuntimeError, OSError, urllib.error.URLError):
            pass

        try:
            page = client._request("/").decode("utf-8", "replace")
            if "esphome" in page.lower():
                chip = self.chip_family_from_label(page)
                name_match = re.search(r"<title>([^<]+)</title>", page, re.IGNORECASE)
                return {
                    "ip": ip_value,
                    "kind": "esphome",
                    "name": name_match.group(1).strip() if name_match else "ESPHome device",
                    "version": "",
                    "chip": chip,
                    "upload": "arduino-ota",
                }
        except (RuntimeError, OSError, urllib.error.URLError):
            pass
        raise RuntimeError("The address did not identify as a compatible ELMA, Tasmota, or ESPHome device.")

    def scan_network_devices(self, payload: dict) -> list[dict]:
        local_addresses: set[str] = set()
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.connect(("8.8.8.8", 80))
                local_addresses.add(str(probe.getsockname()[0]))
        except OSError:
            pass
        try:
            for address in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
                local_addresses.add(str(address[4][0]))
        except OSError:
            pass
        candidates: set[str] = set()
        for address in local_addresses:
            try:
                parsed = ipaddress.ip_address(address)
                if parsed.is_loopback or parsed.is_link_local:
                    continue
                candidates.update(str(host) for host in ipaddress.ip_network(f"{address}/24", strict=False).hosts())
            except ValueError:
                continue
        candidates.difference_update(local_addresses)

        esphome_devices: dict[str, dict] = {}

        class EspHomeListener(ServiceListener):
            def add_service(listener_self, zeroconf: Zeroconf, service_type: str, name: str) -> None:
                info = zeroconf.get_service_info(service_type, name, timeout=700)
                if info is None:
                    return
                properties = {
                    key.decode("utf-8", "replace").lower(): value.decode("utf-8", "replace")
                    for key, value in info.properties.items()
                }
                board = properties.get("board", "")
                platform = properties.get("platform", "")
                chip = owner.chip_family_from_label(f"{platform} {board}")
                for address in info.parsed_scoped_addresses():
                    try:
                        if ipaddress.ip_address(address).version != 4:
                            continue
                    except ValueError:
                        continue
                    esphome_devices[address] = {
                        "ip": address,
                        "kind": "esphome",
                        "name": properties.get("friendly_name") or name.split(".")[0],
                        "version": properties.get("version", ""),
                        "chip": chip,
                        "board": board,
                        "upload": "arduino-ota",
                    }

            def update_service(listener_self, zeroconf: Zeroconf, service_type: str, name: str) -> None:
                listener_self.add_service(zeroconf, service_type, name)

            def remove_service(listener_self, _zeroconf: Zeroconf, _service_type: str, _name: str) -> None:
                return

        owner = self
        zeroconf = Zeroconf()
        browser = ServiceBrowser(zeroconf, "_esphomelib._tcp.local.", EspHomeListener())
        time.sleep(1.4)
        browser.cancel()
        zeroconf.close()

        def inspect(address: str) -> dict | None:
            try:
                with socket.create_connection((address, 80), timeout=0.45):
                    pass
            except OSError:
                return None
            try:
                return self.probe_network_device({**payload, "ip": address}, timeout=1.0)
            except (RuntimeError, ValueError, OSError):
                return None

        devices: list[dict] = []
        # A manually entered address is a strong hint. Probe it with a normal
        # timeout before the broad ARP burst, which can make small/weak Wi-Fi
        # devices miss the scanner's short per-address connection window.
        hint = str(payload.get("hintIp", "")).strip()
        if hint:
            try:
                hinted = self.probe_network_device({**payload, "ip": hint}, timeout=2.5)
                devices.append(hinted)
                candidates.discard(str(hinted.get("ip", hint)))
            except (RuntimeError, ValueError, OSError):
                pass
        with concurrent.futures.ThreadPoolExecutor(max_workers=32, thread_name_prefix="elma-lan-scan") as executor:
            for result in executor.map(inspect, sorted(candidates)):
                if result:
                    devices.append(result)
        known_ips = {str(item.get("ip", "")) for item in devices}
        devices.extend(device for address, device in esphome_devices.items() if address not in known_ips)
        self.network_device_cache = {str(item["ip"]): dict(item) for item in devices}
        return sorted(devices, key=lambda item: tuple(int(part) for part in str(item["ip"]).split(".")))

    @staticmethod
    def legacy_ota_capacity(chip: str, kind: str, version: str) -> int | None:
        numbers = tuple(int(value) for value in re.findall(r"\d+", str(version))[:3])
        if chip == "esp32" and kind == "elma" and numbers and numbers < (0, 1, 12):
            return 0x190000
        return None

    def import_network_configuration(self, payload: dict) -> dict:
        host = str(payload.get("ip", "")).strip()
        username = str(payload.get("username", ""))
        password = str(payload.get("password", ""))
        detected = self.probe_network_device(payload, timeout=3.0)
        client = HttpDeviceClient(host, username, password, timeout=8.0)
        result = import_device(client, detected, str(payload.get("yaml", "")))
        result["ip"] = urllib.parse.urlparse(client.base_url).hostname or host
        return result

    def upload_elma_ota(self, job: DesignerJob, client: HttpDeviceClient, application: bytes, filename: str) -> None:
        session_id = uuid.uuid4().hex
        job.network_client = client
        job.upload_session_id = session_id
        start_value = {"sessionId": session_id, "filename": filename, "size": len(application)}
        client.json_request("/api/firmware/upload/start", value=start_value)
        job.append("ELMA OTA opened the inactive application partition; the running firmware remains untouched until finish succeeds.")
        try:
            offset = 0
            failures = 0
            while offset < len(application):
                if job.cancelled:
                    client.json_request("/api/firmware/upload/cancel", value={"sessionId": session_id})
                    raise InterruptedError("OTA upload cancelled safely; the active firmware remains unchanged")
                chunk = application[offset:offset + 8192]
                query = urllib.parse.urlencode({"sessionId": session_id, "offset": offset})
                try:
                    acknowledgement = client.put_binary(f"/api/firmware/upload/chunk?{query}", chunk)
                    upload = acknowledgement.get("upload", {})
                    offset = int(upload.get("offset", offset + len(chunk)))
                    failures = 0
                except (OSError, RuntimeError, urllib.error.URLError) as error:
                    failures += 1
                    if failures > 8:
                        raise RuntimeError(f"OTA transfer could not recover after {failures - 1} retries: {error}") from error
                    job.append(f"Wi-Fi interruption during OTA; reconnecting and resuming (attempt {failures}/8).")
                    time.sleep(min(5, failures))
                    try:
                        status = client.json("/api/firmware/upload/status").get("upload", {})
                        if status.get("active") and status.get("sessionId") == session_id:
                            offset = int(status.get("offset", offset))
                        elif not status.get("active"):
                            client.json_request("/api/firmware/upload/start", value=start_value)
                            offset = 0
                    except (OSError, RuntimeError, urllib.error.URLError):
                        continue
                    continue
                percent = round(offset * 100 / len(application))
                job.progress = min(98, 70 + round(percent * 0.28))
                job.status = f"Uploading firmware to IP device — {percent}%"
            if job.cancelled:
                client.json_request("/api/firmware/upload/cancel", value={"sessionId": session_id})
                raise InterruptedError("OTA upload cancelled safely; the active firmware remains unchanged")
            try:
                client.json_request("/api/firmware/upload/finish", value={"sessionId": session_id})
            except (OSError, urllib.error.URLError) as error:
                # A device may close the socket immediately after committing the
                # inactive partition. Treat it as success only after it returns.
                job.append("Final OTA acknowledgement was interrupted by restart; verifying that the device returns.")
                for _ in range(15):
                    time.sleep(2)
                    try:
                        returned = client.json("/api/status")
                        if isinstance(returned.get("firmware"), dict):
                            break
                    except (OSError, RuntimeError, urllib.error.URLError):
                        continue
                else:
                    raise RuntimeError(f"OTA finalization could not be verified after device restart: {error}") from error
        finally:
            job.upload_session_id = ""
            job.network_client = None

    def upload_elma_legacy_ota(self, job: DesignerJob, client: HttpDeviceClient, application: bytes, filename: str) -> None:
        parsed = urllib.parse.urlparse(client.base_url)
        boundary = f"----ELMAFlasher{uuid.uuid4().hex}"
        prefix = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"{filename}\"\r\n"
                  "Content-Type: application/octet-stream\r\n\r\n").encode("ascii")
        suffix = f"\r\n--{boundary}--\r\n".encode("ascii")
        connection = http.client.HTTPConnection(parsed.hostname, parsed.port or 80, timeout=45)
        connection.putrequest("POST", "/api/firmware/upload")
        connection.putheader("User-Agent", f"ELMA-Flasher/{APP_VERSION}")
        connection.putheader("Content-Type", f"multipart/form-data; boundary={boundary}")
        connection.putheader("Content-Length", str(len(prefix) + len(application) + len(suffix)))
        if client.authorization:
            connection.putheader("Authorization", client.authorization)
        connection.endheaders()
        job.append("Older ELMA firmware detected; using its compatible one-shot OTA endpoint. The minimal image and compiled-image cache make retries smaller and faster.")
        try:
            connection.send(prefix)
            for offset in range(0, len(application), 8192):
                if job.cancelled:
                    connection.close()
                    raise InterruptedError("Legacy OTA connection cancelled before finalization")
                chunk = application[offset:offset + 8192]
                connection.send(chunk)
                percent = round((offset + len(chunk)) * 100 / len(application))
                job.progress = min(98, 70 + round(percent * 0.28))
                job.status = f"Uploading firmware to older ELMA device — {percent}%"
            connection.send(suffix)
            response = connection.getresponse()
            body = response.read().decode("utf-8", "replace")
            if response.status >= 400:
                raise RuntimeError(f"Legacy ELMA OTA HTTP {response.status}: {body[:240]}")
        finally:
            connection.close()

    def apply_settings_after_legacy_ota(self, job: DesignerJob, client: HttpDeviceClient, settings: dict) -> None:
        """Wait for the compatibility firmware, then apply the Designer document."""
        job.status = "Legacy firmware installed — waiting to apply configuration"
        for attempt in range(30):
            if attempt:
                time.sleep(2)
            try:
                status = client.json("/api/status")
                firmware = status.get("firmware", {}) if isinstance(status, dict) else {}
                if str(firmware.get("version", "")) == APP_VERSION:
                    client.json_request("/api/settings", value=settings)
                    job.append("The new legacy-compatible firmware accepted the configured GPIO and peripheral settings.")
                    return
            except (OSError, RuntimeError, urllib.error.URLError):
                continue
        raise RuntimeError(
            "Firmware upload completed, but the updated device did not return in time to apply its configuration. "
            "Reconnect to the device and load the saved configuration without reflashing."
        )

    def upload_tasmota_ota(
        self,
        job: DesignerJob,
        host: str,
        username: str,
        password: str,
        application: bytes,
        filename: str,
    ) -> None:
        parsed = urllib.parse.urlparse(HttpDeviceClient(host).base_url)
        boundary = f"----ELMAFlasher{uuid.uuid4().hex}"
        prefix = (
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="u2"; filename="{filename}"\r\n'
            "Content-Type: application/octet-stream\r\n\r\n"
        ).encode("ascii")
        suffix = f"\r\n--{boundary}--\r\n".encode("ascii")
        connection = http.client.HTTPConnection(parsed.hostname, parsed.port or 80, timeout=30)
        connection.putrequest("POST", "/u2")
        connection.putheader("User-Agent", f"ELMA-Flasher/{APP_VERSION}")
        connection.putheader("Content-Type", f"multipart/form-data; boundary={boundary}")
        connection.putheader("Content-Length", str(len(prefix) + len(application) + len(suffix)))
        if username or password:
            token = base64.b64encode(f"{username}:{password}".encode()).decode()
            connection.putheader("Authorization", f"Basic {token}")
        connection.endheaders()
        try:
            connection.send(prefix)
            for offset in range(0, len(application), 16384):
                if job.cancelled:
                    connection.close()
                    raise InterruptedError("Tasmota OTA upload cancelled safely before activation")
                chunk = application[offset:offset + 16384]
                connection.send(chunk)
                percent = round((offset + len(chunk)) * 100 / len(application))
                job.progress = min(98, 70 + round(percent * 0.28))
                job.status = f"Uploading ELMA firmware through Tasmota OTA — {percent}%"
            if job.cancelled:
                connection.close()
                raise InterruptedError("Tasmota OTA upload cancelled safely before activation")
            connection.send(suffix)
            response = connection.getresponse()
            response_body = response.read().decode("utf-8", "replace")
            if response.status >= 400:
                raise RuntimeError(f"Tasmota OTA HTTP {response.status}: {response_body[:240]}")
        finally:
            connection.close()

    def upload_esphome_ota(
        self,
        job: DesignerJob,
        host: str,
        password: str,
        firmware_path: pathlib.Path,
    ) -> None:
        response_errors = {
            0x80: "invalid OTA magic",
            0x81: "could not prepare flash memory",
            0x82: "OTA password is invalid",
            0x83: "failed while writing flash",
            0x84: "failed while finishing the update",
            0x85: "device requires a manual reset before its first OTA update",
            0x86: "current ESPHome flash configuration is invalid",
            0x87: "ELMA firmware does not match the destination flash configuration",
            0x88: "destination does not have enough OTA space",
            0x89: "destination OTA partition is too small",
            0x8A: "destination has no OTA partition",
            0x8B: "uploaded firmware checksum mismatch",
            0xFF: "unknown ESPHome OTA error",
        }

        def receive_exactly(connection: socket.socket, size: int, expected: int | tuple[int, ...] | None, description: str) -> bytes:
            result = b""
            while len(result) < size:
                chunk = connection.recv(size - len(result))
                if not chunk:
                    raise RuntimeError(f"ESPHome disconnected while waiting for {description}.")
                result += chunk
            if expected is not None:
                accepted = expected if isinstance(expected, tuple) else (expected,)
                if result[0] not in accepted:
                    detail = response_errors.get(result[0], f"unexpected response 0x{result[0]:02X}")
                    raise RuntimeError(f"ESPHome OTA rejected {description}: {detail}.")
            return result

        firmware = firmware_path.read_bytes()
        connection = socket.create_connection((host, 3232), timeout=10)
        try:
            connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            connection.sendall(bytes((0x6C, 0x26, 0xF7, 0x5C, 0x45)))
            version_reply = receive_exactly(connection, 2, 0x00, "protocol version")
            version = version_reply[1]
            if version not in (1, 2):
                raise RuntimeError(f"ESPHome OTA protocol version {version} is unsupported.")
            connection.sendall(bytes((0x01,)))
            features = receive_exactly(connection, 1, (0x40, 0x46), "feature negotiation")[0]
            upload = gzip.compress(firmware, compresslevel=9) if features == 0x46 else firmware
            auth = receive_exactly(connection, 1, (0x01, 0x41), "authentication request")[0]
            if auth == 0x01:
                if not password:
                    raise RuntimeError("ESPHome requires an OTA password.")
                nonce = receive_exactly(connection, 32, None, "authentication nonce").decode("ascii")
                cnonce = hashlib.md5(str(random.random()).encode()).hexdigest()
                connection.sendall(cnonce.encode("ascii"))
                digest = hashlib.md5(f"{password}{nonce}{cnonce}".encode()).hexdigest()
                connection.sendall(digest.encode("ascii"))
                receive_exactly(connection, 1, 0x41, "authentication result")
            connection.settimeout(30)
            connection.sendall(struct.pack("!I", len(upload)))
            receive_exactly(connection, 1, 0x42, "binary size")
            connection.sendall(hashlib.md5(upload).hexdigest().encode("ascii"))
            receive_exactly(connection, 1, 0x43, "binary checksum")
            connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 0)
            for offset in range(0, len(upload), 8192):
                if job.cancelled:
                    connection.close()
                    raise InterruptedError("ESPHome OTA upload cancelled; the previous active firmware remains selected")
                chunk = upload[offset:offset + 8192]
                connection.sendall(chunk)
                if version >= 2:
                    receive_exactly(connection, 1, 0x47, "chunk acknowledgement")
                percent = round((offset + len(chunk)) * 100 / len(upload))
                job.progress = min(98, 70 + round(percent * 0.28))
                job.status = f"Uploading ELMA firmware through ESPHome OTA — {percent}%"
            if job.cancelled:
                connection.close()
                raise InterruptedError("ESPHome OTA upload cancelled; the previous active firmware remains selected")
            connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            receive_exactly(connection, 1, 0x44, "firmware receive result")
            receive_exactly(connection, 1, 0x45, "update activation result")
            connection.sendall(bytes((0x00,)))
        finally:
            connection.close()

    def start(self) -> str:
        if self.httpd:
            return self.url
        owner = self

        class Handler(SimpleHTTPRequestHandler):
            def __init__(handler_self, *args, **kwargs):
                super().__init__(*args, directory=str(owner.web_root()), **kwargs)

            def log_message(handler_self, _format, *_args):
                return

            def json_response(handler_self, value, status=200):
                data = json.dumps(value, separators=(",", ":")).encode()
                handler_self.send_response(status)
                handler_self.send_header("Content-Type", "application/json")
                handler_self.send_header("Content-Length", str(len(data)))
                handler_self.send_header("Cache-Control", "no-store")
                handler_self.end_headers()
                handler_self.wfile.write(data)

            def read_json(handler_self):
                length = int(handler_self.headers.get("Content-Length", "0") or 0)
                if length > 2 * 1024 * 1024:
                    raise ValueError("Request is too large")
                return json.loads(handler_self.rfile.read(length) or b"{}")

            def do_GET(handler_self):
                path = urllib.parse.urlparse(handler_self.path).path
                if path == "/api/builder/status":
                    handler_self.json_response({"active": True, "version": APP_VERSION, "runtime": "pc-designer"})
                elif path == "/api/builder/ports":
                    ports = [{"device": p.device, "description": p.description, "hwid": p.hwid} for p in list_ports.comports()]
                    handler_self.json_response({"ports": ports})
                elif path.startswith("/api/builder/jobs/"):
                    job_id = path.split("/")[4]
                    job = owner.jobs.get(job_id)
                    handler_self.json_response(job.public() if job else {"error": "Unknown builder job"}, 200 if job else 404)
                elif path == "/api/settings":
                    with owner.settings_lock:
                        handler_self.json_response(json.loads(json.dumps(owner.settings)))
                elif path == "/api/wifi/scan":
                    try:
                        handler_self.json_response({"started": True, "scanning": False, "networks": owner.scan_pc_wifi()})
                    except RuntimeError as error:
                        handler_self.json_response({"error": str(error)}, 409)
                elif path == "/api/status":
                    handler_self.json_response(owner.mock_status())
                elif path == "/api/logs":
                    handler_self.json_response({"source": "designer", "revision": "designer", "text": "",
                                                "notice": "Device logs are available in the flashed device's web interface."})
                elif path.startswith("/api/"):
                    handler_self.json_response({"error": "This runtime action is unavailable while designing a future device."}, 409)
                else:
                    super().do_GET()

            def do_POST(handler_self):
                path = urllib.parse.urlparse(handler_self.path).path
                try:
                    body = handler_self.read_json()
                    if path == "/api/settings":
                        if not isinstance(body, dict):
                            raise ValueError("Settings must be a JSON object")
                        with owner.settings_lock:
                            changed = body != owner.settings
                            owner.settings = body
                        if changed:
                            owner.save_designer_settings_async()
                        handler_self.json_response({"ok": True})
                    elif path == "/api/motor/config":
                        if not isinstance(body, dict):
                            raise ValueError("Motor settings must be a JSON object")
                        with owner.settings_lock:
                            previous = owner.settings.setdefault("ui", {}).get("motorRuntimeConfig")
                            owner.settings.setdefault("ui", {})["motorRuntimeConfig"] = body
                        if body != previous:
                            owner.save_designer_settings_async()
                        handler_self.json_response({"ok": True})
                    elif path == "/api/pc/wifi/test":
                        handler_self.json_response(owner.test_pc_wifi(body))
                    elif path == "/api/mqtt":
                        action = str(body.get("action", "connect"))
                        if action == "disconnect":
                            owner.pc_mqtt_connected = False
                            handler_self.json_response({"ok": True, "connected": False})
                        elif action == "connect":
                            owner.test_pc_mqtt()
                            handler_self.json_response({"ok": True, "connected": True})
                        else:
                            handler_self.json_response({"error": "This MQTT runtime action is unavailable in the PC designer."}, 409)
                    elif path == "/api/builder/network-devices/scan":
                        handler_self.json_response({"devices": owner.scan_network_devices(body)})
                    elif path == "/api/builder/network-devices/probe":
                        handler_self.json_response(owner.probe_network_device(body))
                    elif path == "/api/builder/migration/import":
                        handler_self.json_response(owner.import_network_configuration(body))
                    elif path == "/api/builder/jobs":
                        job = owner.create_job(body)
                        handler_self.json_response({"jobId": job.id}, 202)
                    elif path.startswith("/api/builder/jobs/") and path.endswith("/cancel"):
                        job_id = path.split("/")[4]
                        job = owner.jobs.get(job_id)
                        if not job:
                            handler_self.json_response({"error": "Unknown builder job"}, 404)
                            return
                        job.cancelled = True
                        if job.critical_flash:
                            job.cancel_deferred = True
                            job.status = "Cancellation requested — completing the critical USB write safely"
                            job.append("Cancel requested after USB erase/write began. The verified write will finish so the device is not left unbootable.")
                        elif job.upload_session_id and job.network_client is not None:
                            try:
                                job.network_client.json_request(
                                    "/api/firmware/upload/cancel",
                                    value={"sessionId": job.upload_session_id},
                                )
                                job.append("Destination confirmed that the inactive OTA upload was aborted.")
                            except RuntimeError as error:
                                job.append(f"OTA cancel acknowledgement pending: {error}")
                        if job.process and job.process.poll() is None and not job.critical_flash:
                            owner.terminate_job_process(job)
                        handler_self.json_response({"ok": True})
                    else:
                        handler_self.json_response({"error": "Unsupported local builder action"}, 404)
                except (ValueError, json.JSONDecodeError) as error:
                    handler_self.json_response({"error": str(error)}, 400)
                except (RuntimeError, OSError, urllib.error.URLError) as error:
                    handler_self.json_response({"error": str(error)}, 409)

        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.httpd.server_port}/"
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()
        return self.url

    def stop(self) -> None:
        server = self.httpd
        thread = self.thread
        self.httpd = None
        self.thread = None
        self.url = ""
        if server is not None:
            server.shutdown()
            server.server_close()
        if thread is not None and thread.is_alive():
            thread.join(timeout=2)

    def mock_status(self) -> dict:
        selected = str(self.settings.get("ui", {}).get("gpioBoardSelection", "esp32-c3"))
        chip = BOARD_PROFILES.get(selected, (0, "esp32s3"))[1]
        return {
            "firmware": {"version": APP_VERSION, "channel": "designer", "chipFamily": chip, "audioEnabled": True},
            "device": {"friendlyName": "ELMA Device Designer", "deviceName": "hardware-id-assigned-after-flash", "ipAddress": "PC configuration", "connected": False},
            "network": {"wifiConnected": self.pc_wifi_connected, "mqttConnected": self.pc_mqtt_connected, "wifiRssi": 0, "ssid": self.pc_wifi_ssid, "ip": "PC", "apMode": False},
            "battery": {"voltage": 0, "raw": 0, "charging": False},
            "playback": {"state": "idle", "type": "idle", "title": "Not flashed", "url": "", "source": "designer", "volumePercent": 0},
            "otaManager": {"busy": False, "message": "Local device designer", "updateAvailable": False},
            "ota": {"busy": False, "message": "Local device designer", "updateAvailable": False, "latestVersion": ""},
            "settings": {"usingSaved": False},
            "hardware": {"chipModel": CHIP_FAMILIES.get(chip, chip), "flashSize": 0, "freeHeap": 0},
            "system": {"freeHeap": 0, "cpuLoadPercent": 0, "cpuFrequencyMhz": 0, "sram": {}, "psram": {}, "spiffs": {}, "sd": {}},
            "storage": {"flash": {"available": False}, "sd": {"available": False}},
            "display": {"enabled": False},
        }

    @staticmethod
    def _run_netsh(*arguments: str, timeout: int = 15) -> str:
        if sys.platform != "win32" or not shutil.which("netsh"):
            raise RuntimeError("No Wi-Fi is available on this PC.")
        completed = subprocess.run(
            ["netsh", *arguments], capture_output=True, text=True, errors="replace",
            timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW,
        )
        output = (completed.stdout or "") + (completed.stderr or "")
        if completed.returncode:
            raise RuntimeError(output.strip() or "Windows could not access a Wi-Fi adapter.")
        return output

    def scan_pc_wifi(self) -> list[dict]:
        interfaces = self._run_netsh("wlan", "show", "interfaces")
        if re.search(r"(?im)^\s*(?:name|description|state)\s*:", interfaces) is None:
            raise RuntimeError("No Wi-Fi is available on this PC.")
        output = self._run_netsh("wlan", "show", "networks", "mode=bssid", timeout=25)
        networks: list[dict] = []
        current: dict | None = None
        for line in output.splitlines():
            ssid_match = re.match(r"\s*SSID\s+\d+\s*:\s*(.*)$", line, re.IGNORECASE)
            if ssid_match:
                ssid = ssid_match.group(1).strip()
                current = {"ssid": ssid, "rssi": -100, "encrypted": True, "authentication": ""}
                if ssid:
                    networks.append(current)
                continue
            if current is None:
                continue
            auth_match = re.match(r"\s*Authentication\s*:\s*(.*)$", line, re.IGNORECASE)
            signal_match = re.match(r"\s*Signal\s*:\s*(\d+)%", line, re.IGNORECASE)
            if auth_match:
                authentication = auth_match.group(1).strip()
                current["authentication"] = authentication
                current["encrypted"] = "open" not in authentication.lower()
            elif signal_match:
                current["rssi"] = max(-100, min(-50, int(signal_match.group(1)) // 2 - 100))
        return networks

    @staticmethod
    def _wifi_profile_xml(ssid: str, password: str, authentication: str) -> str:
        ssid_xml = xml_escape(ssid)
        auth_lower = authentication.lower()
        if "open" in auth_lower:
            security = "<security><authEncryption><authentication>open</authentication><encryption>none</encryption><useOneX>false</useOneX></authEncryption></security>"
        else:
            auth = "WPA3SAE" if "wpa3" in auth_lower else "WPA2PSK"
            security = (
                f"<security><authEncryption><authentication>{auth}</authentication><encryption>AES</encryption>"
                "<useOneX>false</useOneX></authEncryption><sharedKey><keyType>passPhrase</keyType>"
                f"<protected>false</protected><keyMaterial>{xml_escape(password)}</keyMaterial></sharedKey></security>"
            )
        return (
            '<?xml version="1.0"?><WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">'
            f"<name>{ssid_xml}</name><SSIDConfig><SSID><name>{ssid_xml}</name></SSID></SSIDConfig>"
            f"<connectionType>ESS</connectionType><connectionMode>manual</connectionMode><MSM>{security}</MSM></WLANProfile>"
        )

    def test_pc_wifi(self, payload: dict) -> dict:
        ssid = str(payload.get("ssid", "")).strip()
        password = str(payload.get("password", ""))
        authentication = str(payload.get("authentication", "WPA2-Personal"))
        if not ssid:
            raise ValueError("Select or enter a Wi-Fi network first.")
        if "enterprise" in authentication.lower() or "802.1x" in authentication.lower():
            raise RuntimeError("Enterprise Wi-Fi credential testing is not supported by this portable application.")
        if "open" not in authentication.lower() and len(password) < 8:
            raise ValueError("The Wi-Fi password must contain at least 8 characters.")
        self.scan_pc_wifi()
        interfaces_before = self._run_netsh("wlan", "show", "interfaces")
        current_match = re.search(r"(?im)^\s*SSID\s*:\s*(.+?)\s*$", interfaces_before)
        previous_ssid = current_match.group(1).strip() if current_match else ""
        with tempfile.TemporaryDirectory(prefix="elma-wifi-") as temp_dir:
            backup_dir = pathlib.Path(temp_dir) / "backup"
            backup_dir.mkdir()
            with contextlib.suppress(RuntimeError):
                self._run_netsh("wlan", "export", "profile", f"name={ssid}", f"folder={backup_dir}", "key=clear")
            backup_profiles = list(backup_dir.glob("*.xml"))
            profile = pathlib.Path(temp_dir) / "elma-wifi-profile.xml"
            profile.write_text(self._wifi_profile_xml(ssid, password, authentication), encoding="utf-8")
            try:
                self._run_netsh("wlan", "add", "profile", f"filename={profile}", "user=current")
                self._run_netsh("wlan", "connect", f"name={ssid}", f"ssid={ssid}")
                deadline = time.monotonic() + 25
                while time.monotonic() < deadline:
                    interfaces = self._run_netsh("wlan", "show", "interfaces")
                    connected = re.search(r"(?im)^\s*state\s*:\s*connected\s*$", interfaces)
                    selected = re.search(r"(?im)^\s*SSID\s*:\s*(.+?)\s*$", interfaces)
                    if connected and selected and selected.group(1).strip() == ssid:
                        self.pc_wifi_connected = True
                        self.pc_wifi_ssid = ssid
                        wifi = self.settings.setdefault("wifi", {})
                        wifi["ssid"] = ssid
                        wifi["password"] = password
                        self.save_designer_settings_async()
                        return {"ok": True, "connected": True, "ssid": ssid}
                    time.sleep(0.75)
            except (RuntimeError, subprocess.TimeoutExpired):
                pass
            for backup in backup_profiles:
                with contextlib.suppress(RuntimeError):
                    self._run_netsh("wlan", "add", "profile", f"filename={backup}", "user=current")
            if previous_ssid:
                with contextlib.suppress(RuntimeError):
                    self._run_netsh("wlan", "connect", f"name={previous_ssid}", f"ssid={previous_ssid}")
        self.pc_wifi_connected = False
        raise RuntimeError(f"Could not connect the PC to {ssid}. Check the Wi-Fi password and signal, then retry.")

    @staticmethod
    def _mqtt_remaining_length(value: int) -> bytes:
        encoded = bytearray()
        while True:
            digit = value % 128
            value //= 128
            if value:
                digit |= 0x80
            encoded.append(digit)
            if not value:
                return bytes(encoded)

    def test_pc_mqtt(self) -> None:
        mqtt = self.settings.get("mqtt", {})
        host = str(mqtt.get("host", "")).strip()
        port = int(mqtt.get("port", 1883) or 1883)
        username = str(mqtt.get("username", ""))
        password = str(mqtt.get("password", ""))
        if not host:
            raise ValueError("Enter an MQTT broker host first.")
        mqtt_string = lambda value: struct.pack("!H", len(value)) + value
        flags = 0x02 | (0x80 if username else 0) | (0x40 if password else 0)
        payload = mqtt_string(f"elma-flasher-test-{uuid.uuid4().hex[:10]}".encode())
        if username:
            payload += mqtt_string(username.encode())
        if password:
            payload += mqtt_string(password.encode())
        variable = mqtt_string(b"MQTT") + bytes((4, flags)) + struct.pack("!H", 15)
        packet = b"\x10" + self._mqtt_remaining_length(len(variable) + len(payload)) + variable + payload
        try:
            connection = socket.create_connection((host, port), timeout=8)
            if bool(mqtt.get("tls", False)):
                connection = ssl.create_default_context().wrap_socket(connection, server_hostname=host)
            with connection:
                connection.settimeout(8)
                connection.sendall(packet)
                reply = b""
                while len(reply) < 4:
                    chunk = connection.recv(4 - len(reply))
                    if not chunk:
                        break
                    reply += chunk
                if len(reply) < 4 or reply[:2] != b"\x20\x02":
                    raise RuntimeError("The broker returned an invalid MQTT response.")
                if reply[3] != 0:
                    reasons = {1: "protocol rejected", 2: "client ID rejected", 3: "broker unavailable", 4: "username/password rejected", 5: "not authorized"}
                    raise RuntimeError(f"MQTT connection failed: {reasons.get(reply[3], f'broker code {reply[3]}')}.")
                connection.sendall(b"\xe0\x00")
        except (OSError, ssl.SSLError) as error:
            self.pc_mqtt_connected = False
            raise RuntimeError(f"MQTT connection failed: {error}") from error
        self.pc_mqtt_connected = True

    def create_job(self, payload: dict) -> DesignerJob:
        compile_only = bool(payload.get("compileOnly", False)) if isinstance(payload, dict) else False
        requested_chip = str(payload.get("chip", "")).strip() if isinstance(payload, dict) else ""
        transport = str(payload.get("transport", "usb")).strip().lower() if isinstance(payload, dict) else "usb"
        if transport not in ("usb", "ip"):
            raise ValueError("Choose USB or IP flashing.")
        if not isinstance(payload, dict) or (not compile_only and transport == "usb" and not str(payload.get("port", "")).strip()):
            raise ValueError("Select a connected USB device")
        if not compile_only and transport == "ip" and not str(payload.get("targetIp", "")).strip():
            raise ValueError("Select or enter an IP device.")
        if compile_only and requested_chip not in CHIP_FAMILIES:
            raise ValueError("Choose ESP32, ESP32-S3, or ESP32-C3 before compiling.")
        with self.lock:
            if any(job.state in ("queued", "running") for job in self.jobs.values()):
                raise ValueError("Another compile or flash job is already running")
            job = DesignerJob()
            job.transport = transport
            self.jobs[job.id] = job
        threading.Thread(target=self.run_job, args=(job, payload), daemon=True).start()
        return job

    def set_job(self, job: DesignerJob, progress: int, status: str) -> None:
        job.progress = progress
        job.status = status
        job.append(status)

    def resolve_profile(self, detected: str, requested: dict, settings: dict, job: DesignerJob, firmware_mode: str = "full") -> str:
        if firmware_mode == "minimal":
            job.compatibility = "Minimal OTA bridge: saved NVS configuration is preserved; Wi-Fi sleep and RSSI cutoffs are disabled; only Wi-Fi and resumable OTA are included."
            return {"esp32": "esp32_ota_bridge", "esp32s3": "esp32s3_ota_bridge", "esp32c3": "esp32c3_ota_bridge"}[detected]
        maximum = bool(requested.get("maximum", True))
        audio_profiles = settings.get("ui", {}).get("peripheralProfiles", {}).get("audioProfiles", []) if isinstance(settings, dict) else []
        configured_audio = any(str(value).strip().lower() not in ("", "none") for value in audio_profiles if value is not None)
        wants_audio = bool(requested.get("audio", False) or configured_audio)
        wants_web = bool(requested.get("webUi", True))
        wants_hacs = bool(requested.get("hacs", True))
        if detected == "esp32c3":
            exclusions = ["I2S network audio (not selected for the C3 memory profile)"]
            if not wants_web:
                exclusions.append("on-device web configurator (explicitly disabled)")
            job.compatibility = "ESP32-C3 maximum-fit profile: Wi-Fi, MQTT/HACS, OTA, motor/GPIO, supported displays, sensors, controls and the compatible web configurator are retained. Excluded: " + "; ".join(exclusions) + "."
            if maximum:
                return "esp32c3_designer_hacs"
            return "esp32c3_designer" + ("_hacs" if wants_hacs else "") + ("_slim" if not wants_web else "")
        if detected == "esp32s3":
            if maximum:
                job.compatibility = "ESP32-S3 full profile selected: all currently supported peripherals and multimedia features are retained."
                return "esp32s3_notifier_hacs"
            job.compatibility = f"ESP32-S3 selected-feature profile: web UI {'included' if wants_web else 'excluded'}, HACS {'included' if wants_hacs else 'excluded'}, audio {'included' if wants_audio else 'excluded'}."
            if wants_audio:
                if wants_hacs:
                    return "esp32s3_notifier_hacs" if wants_web else "esp32s3_notifier_hacs_slim"
                return "esp32s3_notifier" if wants_web else "esp32s3_notifier_slim"
            return "esp32s3_designer_noaudio" + ("_hacs" if wants_hacs else "") + ("_slim" if not wants_web else "")
        if detected == "esp32":
            if maximum:
                job.compatibility = "ESP32 maximum compatible profile selected; the complete classic-ESP32 peripheral catalog is retained."
                return "esp32_notifier_hacs"
            job.compatibility = f"ESP32 selected-feature profile: web UI {'included' if wants_web else 'excluded'}, HACS {'included' if wants_hacs else 'excluded'}, audio {'included' if wants_audio else 'excluded'}."
            if wants_audio:
                if wants_hacs:
                    return "esp32_notifier_hacs" if wants_web else "esp32_notifier_hacs_slim"
                return "esp32_notifier" if wants_web else "esp32_notifier_slim"
            return "esp32_designer_noaudio" + ("_hacs" if wants_hacs else "") + ("_slim" if not wants_web else "")
        raise RuntimeError(f"ELMA firmware generation does not yet support {detected.upper()}.")

    def run_job(self, job: DesignerJob, payload: dict) -> None:
        try:
            job.state = "running"
            job.phase = "preflight"
            compile_only = bool(payload.get("compileOnly", False))
            transport = str(payload.get("transport", "usb")).strip().lower()
            firmware_mode = str(payload.get("firmwareMode", "full")).strip().lower()
            if firmware_mode not in ("full", "minimal"):
                raise RuntimeError("Select Full or Minimal firmware.")
            if firmware_mode == "minimal" and transport != "ip" and not compile_only:
                raise RuntimeError("Minimal recovery firmware must be installed over IP so the existing partition table and NVS configuration remain untouched.")
            port = str(payload.get("port", ""))
            requested_chip = str(payload.get("chip", "auto"))
            if compile_only:
                detected = requested_chip
                flash_size = "8 MB" if detected == "esp32s3" else "4 MB"
                self.set_job(job, 3, f"Preparing {CHIP_FAMILIES[detected]} firmware")
                job.append(f"Compile-only target: {CHIP_FAMILIES[detected]}")
            elif transport == "usb":
                self.set_job(job, 3, f"Detecting the ESP on {port}")
                detected, flash_size = self.flasher._detect_target_chip(port)
                if requested_chip != "auto" and requested_chip != detected:
                    raise RuntimeError(f"Manual target {requested_chip.upper()} does not match detected {detected.upper()}. Nothing was erased.")
                job.append(f"Detected {CHIP_FAMILIES.get(detected, detected)}; flash {flash_size or 'size reported by loader'}")
            else:
                target_ip = str(payload.get("targetIp", "")).strip()
                self.set_job(job, 3, f"Verifying IP device at {target_ip}")
                target = self.probe_network_device({
                    "ip": target_ip,
                    "username": str(payload.get("username", "")),
                    "password": str(payload.get("password", "")),
                })
                detected = str(target.get("chip", ""))
                if detected not in CHIP_FAMILIES:
                    raise RuntimeError("The destination did not report an exact supported ESP chip family. Nothing was compiled or uploaded.")
                if requested_chip != "auto" and requested_chip != detected:
                    raise RuntimeError(
                        f"Destination chip mismatch: device is {detected.upper()}, configured firmware is {requested_chip.upper()}. Nothing was compiled or uploaded."
                    )
                job.target_kind = str(target.get("kind", ""))
                job.target_version = str(target.get("version", ""))
                if job.target_kind != "elma" and not bool(payload.get("confirmedForeignFirmware", False)):
                    raise RuntimeError(f"Replacing {job.target_kind or 'foreign'} firmware requires confirmation before compilation.")
                payload_kind = str(payload.get("targetKind", ""))
                if payload_kind and payload_kind != job.target_kind:
                    raise RuntimeError("Destination firmware identity changed after confirmation. Nothing was uploaded.")
                flash_size = "4 MB"
                job.ip_address = str(target.get("ip", target_ip))
                job.append(
                    f"Verified {job.target_kind.upper()} target at {job.ip_address}: {CHIP_FAMILIES[detected]}"
                )
            profile = self.resolve_profile(detected, payload.get("capabilities", {}), payload.get("settings", {}), job, firmware_mode)
            legacy_capacity = self.legacy_ota_capacity(detected, job.target_kind, job.target_version) if transport == "ip" else None
            if firmware_mode == "full" and legacy_capacity:
                profile = "esp32_notifier_hacs_legacy_ota"
                job.compatibility = (
                    f"Legacy ELMA OTA profile: runtime audio, MQTT/HACS, GPIO, display, controls and OTA APIs are retained; "
                    f"the illustrated on-device frontend is replaced by a small recovery page to fit the {legacy_capacity:,}-byte OTA slot."
                )
                job.append(f"Legacy OTA slot detected from ELMA {job.target_version}: maximum application size {legacy_capacity:,} bytes")
            job.profile = profile
            supplied_settings = payload.get("settings", {})
            supplied_ui = supplied_settings.get("ui", {}) if isinstance(supplied_settings, dict) else {}
            selected_board = str(supplied_ui.get("gpioBoardSelection", "")).strip().lower() if isinstance(supplied_ui, dict) else ""
            if selected_board not in BOARD_PROFILES:
                selected_board = DEFAULT_BOARD_PROFILE[detected]
            board_id, board_chip = BOARD_PROFILES[selected_board]
            if board_chip != detected:
                raise RuntimeError(
                    f"Selected board {selected_board} is not compatible with the {CHIP_FAMILIES[detected]} compile target."
                )
            if firmware_mode == "full":
                validate_gpio_settings(supplied_settings, detected, selected_board)
                job.append(f"Fixed firmware board: {selected_board} (other board illustrations excluded)")
            else:
                job.append("Minimal bridge does not contain board illustrations or peripheral modules; existing NVS settings are read-only and preserved.")
            if job.cancelled:
                raise InterruptedError("Build cancelled")
            project = self.project_root()
            output_name = self.generated_firmware_name(detected, payload.get("capabilities", {}), firmware_mode)
            requested_output = str(payload.get("outputPath", "")).strip()
            output_path = pathlib.Path(requested_output).expanduser().resolve() if requested_output else self.generated_firmware_directory() / output_name
            source_digest = hashlib.sha256()
            source_inputs = [project / "platformio.ini"]
            for source_directory in ("src", "include", "scripts", "partitions", "web"):
                source_inputs.extend(sorted((project / source_directory).glob("**/*")))
            for source_input in source_inputs:
                if source_input.is_file() and not source_input.name.startswith("generated_web_assets"):
                    source_digest.update(source_input.relative_to(project).as_posix().encode())
                    source_digest.update(str(source_input.stat().st_mtime_ns).encode())
                    source_digest.update(str(source_input.stat().st_size).encode())
            signature_payload = {"version": APP_VERSION, "source": source_digest.hexdigest(), "profile": profile, "board": selected_board, "mode": firmware_mode,
                                 "settings": payload.get("settings", {}), "capabilities": payload.get("capabilities", {})}
            build_signature = hashlib.sha256(json.dumps(signature_payload, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
            cache_path = self.portable_home() / "ELMA-Flasher.build-cache.json"
            cached = {}
            try:
                cached = json.loads(cache_path.read_text(encoding="utf-8"))
            except (OSError, ValueError, json.JSONDecodeError):
                pass
            reuse_build = transport == "ip" and cached.get("signature") == build_signature and output_path.is_file()
            job.phase = "compiling"
            self.set_job(job, 12, f"Compiling {profile}" if not reuse_build else "Reusing unchanged compiled firmware")
            if reuse_build:
                application = output_path.read_bytes()
                if chip_family_from_image(application) != detected or hashlib.sha256(application).hexdigest() != cached.get("sha256"):
                    reuse_build = False
                else:
                    job.append("Configuration and build inputs are unchanged; reusing the verified binary from the previous attempt.")
            if not reuse_build:
                command = self.compiler_command() + ["run", "--project-dir", str(project), "--environment", profile]
                job.append(" ".join(command))
                creation_flags = (subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP) if sys.platform == "win32" else 0
                compiler_environment = os.environ.copy()
                compiler_environment["ELMA_PORTABLE_BUILDER"] = "1"
                compiler_environment["ELMA_SELECTED_BOARD_PROFILE"] = selected_board
                compiler_environment["ELMA_SELECTED_BOARD_PROFILE_ID"] = str(board_id)
                wifi_settings = supplied_settings.get("wifi", {})
                compiler_environment["ELMA_STA_TX_POWER_DBM"] = str(wifi_settings.get("staTxPowerDbm", 15.0))
                compiler_environment["ELMA_AP_TX_POWER_DBM"] = str(wifi_settings.get("apTxPowerDbm", 15.0))
                job.process = subprocess.Popen(command, cwd=project, env=compiler_environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", creationflags=creation_flags)
                assert job.process.stdout is not None
                for line in job.process.stdout:
                    job.append(line)
                    if "Compiling" in line and job.progress < 55:
                        job.progress += 1
                    if job.cancelled:
                        self.terminate_job_process(job)
                        raise InterruptedError("Build cancelled")
                code = job.process.wait()
                job.process = None
                if code:
                    raise RuntimeError(f"Firmware compilation failed with exit code {code}. See the build log above.")
                application = (project / ".pio" / "build" / profile / "firmware.bin").read_bytes()
            build = project / ".pio" / "build" / profile
            family = chip_family_from_image(application)
            if family != detected:
                raise RuntimeError("Compiler output chip family does not match the connected target")
            flash_mb = int(re.search(r"(\d+)", flash_size).group(1)) if re.search(r"(\d+)", flash_size) else 4
            job.application_bytes = len(application)
            job.flash_capacity_bytes = min(MAX_APPLICATION_SIZE, flash_mb * 1024 * 1024)
            ram_match = re.search(
                r"RAM:\s*\[[^\]]+\]\s*[\d.]+%\s*\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)",
                "\n".join(job.log), re.IGNORECASE,
            )
            if ram_match:
                job.ram_used_bytes = int(ram_match.group(1))
                job.ram_total_bytes = int(ram_match.group(2))
            if len(application) > min(MAX_APPLICATION_SIZE, flash_mb * 1024 * 1024):
                raise RuntimeError(f"Generated application is {len(application):,} bytes and does not fit this target safely.")
            if legacy_capacity and len(application) > legacy_capacity:
                raise RuntimeError(
                    f"Legacy-compatible application is {len(application):,} bytes but this device's OTA slot is only {legacy_capacity:,} bytes. "
                    "Nothing was uploaded; use USB once to install the current partition table."
                )
            job.append(f"Generated application: {len(application):,} bytes")
            output_path.parent.mkdir(parents=True, exist_ok=True)
            temporary_output = output_path.with_name(f".{output_path.name}.{os.getpid()}.tmp")
            temporary_output.write_bytes(application)
            temporary_output.replace(output_path)
            cache_path.parent.mkdir(parents=True, exist_ok=True)
            cache_path.write_text(json.dumps({"signature": build_signature, "firmware": str(output_path), "sha256": hashlib.sha256(application).hexdigest()}, indent=2), encoding="utf-8")
            job.firmware_file = str(output_path)
            job.append(f"Saved generated firmware: {output_path.name}")
            if compile_only:
                job.progress = 100
                job.status = "Firmware compilation complete"
                job.state = "complete"
                return
            if transport == "ip":
                if job.cancelled:
                    raise InterruptedError("Build cancelled before OTA upload; destination flash was not changed")
                job.phase = "uploading"
                self.set_job(job, 70, f"Starting safe OTA upload to {job.ip_address}")
                username = str(payload.get("username", ""))
                password = str(payload.get("password", ""))
                if job.target_kind == "elma":
                    client = HttpDeviceClient(job.ip_address, username, password, timeout=30)
                    version_numbers = tuple(int(value) for value in re.findall(r"\d+", job.target_version)[:3])
                    if version_numbers and version_numbers < (0, 1, 40):
                        self.upload_elma_legacy_ota(job, client, application, output_path.name)
                        if legacy_capacity:
                            self.apply_settings_after_legacy_ota(job, client, supplied_settings)
                    else:
                        try:
                            self.upload_elma_ota(job, client, application, output_path.name)
                        except RuntimeError as error:
                            if "HTTP 404" not in str(error):
                                raise
                            self.upload_elma_legacy_ota(job, client, application, output_path.name)
                elif job.target_kind == "tasmota":
                    job.append("Replacing Tasmota through its web OTA path. No partition-table erase is performed over IP.")
                    self.upload_tasmota_ota(job, job.ip_address, username, password, application, output_path.name)
                elif job.target_kind == "esphome":
                    job.append("Replacing ESPHome through its Arduino-compatible OTA listener. No partition-table erase is performed over IP.")
                    self.upload_esphome_ota(job, job.ip_address, password, output_path)
                else:
                    raise RuntimeError("Destination OTA type is unsupported.")
                job.progress = 100
                job.status = "Compile and IP flash complete — destination restarting"
                job.state = "complete"
                job.phase = "complete"
                return
            if job.cancelled:
                raise InterruptedError("Build cancelled before erase")
            boot_address = 0 if family in ("esp32s3", "esp32c3") else 0x1000
            parts = [(boot_address, (build / "bootloader.bin").read_bytes()), (0x8000, (build / "partitions.bin").read_bytes()), (APPLICATION_ADDRESS, application)]
            job.phase = "usb-critical-write"
            job.critical_flash = True
            self.set_job(job, 66, "Writing and verifying firmware")

            def append_flash_progress(line: str) -> None:
                job.append(line)
                match = re.search(r"\((\d+)\s*%\)", line)
                if not match:
                    return
                percent = max(0, min(100, int(match.group(1))))
                job.progress = max(job.progress, min(92, 66 + round(percent * 0.26)))
                job.status = f"Writing and verifying firmware — {percent}%"

            self.flasher._write_flash(
                port,
                family,
                parts,
                bool(payload.get("erase", True)),
                append_flash_progress,
            )
            job.critical_flash = False
            if job.cancelled:
                raise InterruptedError("Cancellation completed after the USB firmware write was safely finished and verified")
            job.phase = "provisioning"
            configuration = sanitize_clone_configuration(payload.get("settings", self.settings))
            self.settings = payload.get("settings", self.settings)
            self.set_job(job, 93, "Provisioning Wi-Fi, MQTT, identity and peripheral configuration")
            wifi = configuration.get("wifi", {})
            job.ip_address = self.flasher._provision(port, configuration, bool(str(wifi.get("ssid", "")).strip()))
            job.progress = 100
            job.status = "Compile, flash and configuration complete"
            job.state = "complete"
            job.phase = "complete"
            job.append("Target device identity and MQTT IDs were regenerated from its own hardware ID.")
        except InterruptedError as error:
            job.critical_flash = False
            job.state = "cancelled"
            job.phase = "cancelled"
            job.status = str(error)
            job.append(job.status)
        except BaseException as error:
            job.critical_flash = False
            job.state = "failed"
            job.phase = "failed"
            job.error = friendly_error(error)
            job.status = "Build or flash failed"
            job.append(f"ERROR: {job.error}")


def run_native_designer_window(
    url: str,
    icon_path: pathlib.Path,
    designer_server: DesignerServer | None = None,
    smoke_test: bool = False,
) -> bool:
    """Render the PC designer, compiler and USB flasher in one native ELMA window."""
    from PySide6.QtCore import QByteArray, Qt, QTimer, QUrl, QUrlQuery
    from PySide6.QtGui import QAction, QActionGroup, QIcon, QKeySequence
    from PySide6.QtWebEngineCore import QWebEnginePage, QWebEngineProfile
    from PySide6.QtWebEngineWidgets import QWebEngineView
    from PySide6.QtWidgets import (
        QApplication,
        QFileDialog,
        QInputDialog,
        QMainWindow,
        QMessageBox,
        QProgressDialog,
    )

    origin = urllib.parse.urlparse(url)
    mobile_css_path = resource_path("android_preview/mobile.css")
    if not mobile_css_path.is_file():
        mobile_css_path = pathlib.Path(__file__).resolve().parents[1] / "elma_android" / "web" / "mobile.css"
    mobile_preview_css = mobile_css_path.read_text(encoding="utf-8")

    class LocalDesignerPage(QWebEnginePage):
        def acceptNavigationRequest(self, target: QUrl, navigation_type, is_main_frame: bool) -> bool:
            if not is_main_frame:
                return True
            parsed = urllib.parse.urlparse(target.toString())
            is_local = parsed.scheme in ("http", "https") and parsed.hostname == origin.hostname and parsed.port == origin.port
            if not is_local and parsed.scheme in ("http", "https"):
                webbrowser.open(target.toString())
            return is_local

        def createWindow(self, _window_type):
            return None

    class ResponsiveWebView(QWebEngineView):
        """Keep the full desktop layout intact and scale it as one unit."""

        DESIGN_WIDTH = 1020
        MINIMUM_ZOOM = 0.32

        def __init__(self, parent=None):
            super().__init__(parent)
            self._resize_pending = False
            self.mobile_preview = False
            self.mobile_theme = "light"
            self.loadFinished.connect(lambda _ok: self.applyMobileStyles())

        def applyMobileStyles(self) -> None:
            self.page().runJavaScript(
                "(()=>{const enabled=" + json.dumps(self.mobile_preview) + ";"
                "let style=document.getElementById('elma-desktop-mobile-preview');"
                "if(enabled){if(!style){style=document.createElement('style');style.id='elma-desktop-mobile-preview';document.head.append(style);"
                "document.documentElement.dataset.elmaPreviousTheme=document.documentElement.dataset.elmaTheme||'';}"
                "style.textContent=" + json.dumps(mobile_preview_css) + ";"
                "document.documentElement.dataset.elmaTheme=" + json.dumps(self.mobile_theme) + ";"
                "}else if(style){style.remove();const old=document.documentElement.dataset.elmaPreviousTheme;"
                "if(old)document.documentElement.dataset.elmaTheme=old;else delete document.documentElement.dataset.elmaTheme;"
                "delete document.documentElement.dataset.elmaPreviousTheme;}"
                "document.body.classList.toggle('android-designer',enabled);"
                "window.dispatchEvent(new Event('resize'));})()"
            )

        def resizeEvent(self, event) -> None:
            super().resizeEvent(event)
            if self._resize_pending:
                return
            self._resize_pending = True
            QTimer.singleShot(0, self.applyResponsiveZoom)

        def applyResponsiveZoom(self) -> None:
            self._resize_pending = False
            available_width = max(1, self.width())
            zoom = min(1.0, max(self.MINIMUM_ZOOM, available_width / self.DESIGN_WIDTH))
            if self.mobile_preview:
                zoom = 1.0  # Exercise the actual narrow CSS viewport, not desktop scaling.
            if abs(self.zoomFactor() - zoom) > 0.005:
                self.setZoomFactor(zoom)
            # A wide diagram or transient menu must never expand the document
            # canvas and push the centered application out of view. Vertical
            # overflow remains untouched so the page continues to scroll.
            self.page().runJavaScript(
                "document.documentElement.style.overflowX='hidden';"
                "document.body.style.overflowX='hidden';"
                "document.querySelector('.wrap')?.style.setProperty('margin-inline','auto');"
                "window.scrollTo(0, window.scrollY);"
            )

    qt_app = QApplication.instance() or QApplication(["ELMA Flasher"])
    qt_app.setApplicationName("ELMA Flasher")
    qt_app.setOrganizationName("ELMA IoT")
    qt_app.setApplicationVersion(APP_VERSION)
    qt_app.setStyle("Fusion")
    icon = QIcon(str(icon_path))
    qt_app.setWindowIcon(icon)

    profile = QWebEngineProfile.defaultProfile()
    profile.setPersistentCookiesPolicy(QWebEngineProfile.PersistentCookiesPolicy.NoPersistentCookies)
    profile.setHttpCacheType(QWebEngineProfile.HttpCacheType.MemoryHttpCache)

    window_state_path = DesignerServer.portable_home() / "ELMA-Flasher.window.json"

    def load_window_geometry() -> tuple[bytes, bool] | None:
        if smoke_test:
            return None
        try:
            payload = json.loads(window_state_path.read_text(encoding="utf-8"))
            geometry = base64.b64decode(str(payload.get("geometry", "")), validate=True)
            return geometry, bool(payload.get("maximized", False))
        except (OSError, ValueError, TypeError, json.JSONDecodeError, binascii.Error):
            return None

    class PersistentDesignerWindow(QMainWindow):
        """Native document shell for the shared web-based Device Designer."""

        def __init__(self) -> None:
            super().__init__()
            self._initial_configuration_prompted = False
            self._close_capture_complete = False
            self._compile_job: DesignerJob | None = None
            self._compile_progress: QProgressDialog | None = None
            self._geometry_save_timer = QTimer(self)
            self._geometry_save_timer.setSingleShot(True)
            self._geometry_save_timer.setInterval(350)
            self._geometry_save_timer.timeout.connect(self._save_geometry)
            self._document_state_timer = QTimer(self)
            self._document_state_timer.setInterval(250)
            self._document_state_timer.timeout.connect(self._update_document_title)
            self._document_state_timer.start()
            self._compile_poll_timer = QTimer(self)
            self._compile_poll_timer.setInterval(300)
            self._compile_poll_timer.timeout.connect(self._poll_compile_job)
            self._create_file_menu()

        def _create_file_menu(self) -> None:
            file_menu = self.menuBar().addMenu("&File")
            self.open_action = QAction("&Open Configuration…", self)
            self.open_action.setShortcut(QKeySequence.StandardKey.Open)
            self.open_action.triggered.connect(self._request_open_configuration)
            file_menu.addAction(self.open_action)

            self.save_action = QAction("&Save Configuration", self)
            self.save_action.setShortcut(QKeySequence.StandardKey.Save)
            self.save_action.triggered.connect(lambda: self._request_save_configuration(False))
            file_menu.addAction(self.save_action)

            self.save_as_action = QAction("Save Configuration &As…", self)
            self.save_as_action.setShortcut(QKeySequence.StandardKey.SaveAs)
            self.save_as_action.triggered.connect(lambda: self._request_save_configuration(True))
            file_menu.addAction(self.save_as_action)

            file_menu.addSeparator()
            self.compile_save_action = QAction("&Compile Firmware and Save As…", self)
            self.compile_save_action.setShortcut(QKeySequence("Ctrl+Shift+B"))
            self.compile_save_action.triggered.connect(self._request_compile_and_save)
            file_menu.addAction(self.compile_save_action)

            file_menu.addSeparator()
            self.exit_action = QAction("E&xit", self)
            self.exit_action.setShortcut(QKeySequence.StandardKey.Quit)
            self.exit_action.triggered.connect(self.close)
            file_menu.addAction(self.exit_action)

            self._desktop_geometry = None
            mobile_menu = self.menuBar().addMenu("&Mobile")
            self.preview_group = QActionGroup(self)
            self.preview_group.setExclusive(True)
            self.desktop_view_action = QAction("&Desktop", self)
            self.mobile_view_action = QAction("&Single", self)
            self.fold_view_action = QAction("&Fold", self)
            for action, preset in ((self.desktop_view_action, "desktop"), (self.mobile_view_action, "single"), (self.fold_view_action, "fold")):
                action.setCheckable(True)
                self.preview_group.addAction(action)
                mobile_menu.addAction(action)
                action.triggered.connect(lambda checked, mode=preset: self._set_mobile_view(mode))
            self.desktop_view_action.setChecked(True)
            self.mobile_view_action.setShortcut(QKeySequence("Ctrl+Shift+M"))
            self.mobile_view_action.setToolTip("390-pixel phone viewport for responsive layout debugging")
            self.fold_view_action.setToolTip("740-pixel unfolded phone viewport for responsive layout debugging")
            mobile_menu.addSeparator()
            self.theme_group = QActionGroup(self)
            self.theme_group.setExclusive(True)
            for label, theme in (("Light theme", "light"), ("Dark theme", "dark")):
                action = QAction(label, self)
                action.setCheckable(True)
                action.setChecked(theme == "light")
                self.theme_group.addAction(action)
                mobile_menu.addAction(action)
                action.triggered.connect(lambda checked, value=theme: self._set_mobile_theme(value))

        def _set_mobile_theme(self, theme: str) -> None:
            view = self.centralWidget()
            view.mobile_theme = theme
            view.applyMobileStyles()

        def _set_mobile_view(self, preset: str) -> None:
            view = self.centralWidget()
            enabled = preset != "desktop"
            if enabled:
                if not view.mobile_preview:
                    self._desktop_geometry = self.saveGeometry()
                self.showNormal()
                self.setMinimumSize(360, 420)
                screen = self.screen().availableGeometry()
                self.resize(390 if preset == "single" else min(740, screen.width() - 40), min(844, screen.height() - 60))
                self.move(screen.center() - self.rect().center())
            else:
                self.setMinimumSize(420, 420)
                if self._desktop_geometry is not None:
                    self.restoreGeometry(self._desktop_geometry)
            view.mobile_preview = enabled
            view.applyMobileStyles()
            view.applyResponsiveZoom()

        def _dialog_directory(self) -> pathlib.Path:
            if designer_server is None:
                return DesignerServer.portable_home()
            return designer_server.last_configuration_directory()

        @staticmethod
        def _ensure_extension(path: pathlib.Path, extension: str) -> pathlib.Path:
            return path if path.suffix.lower() == extension else path.with_suffix(extension)

        def _update_document_title(self) -> None:
            if designer_server is None:
                self.setWindowTitle(f"ELMA Flasher v{APP_VERSION}")
                return
            path = designer_server.active_settings_path
            label = path.name if path is not None else "Untitled configuration"
            dirty = " *" if designer_server.configuration_dirty() else ""
            self.setWindowTitle(f"{label}{dirty} — ELMA Flasher v{APP_VERSION}")
            self.save_action.setEnabled(designer_server.configuration_dirty() or path is None)

        def _capture_page_settings(self, callback: Callable[[], None]) -> None:
            if designer_server is None or not designer_view.page():
                callback()
                return

            def captured(value) -> None:
                try:
                    settings = json.loads(value) if isinstance(value, str) else None
                    if isinstance(settings, dict):
                        with designer_server.settings_lock:
                            changed = settings != designer_server.settings
                            designer_server.settings = settings
                        if changed:
                            designer_server.save_designer_settings_async()
                except (TypeError, json.JSONDecodeError):
                    pass
                callback()

            designer_view.page().runJavaScript(
                "window.elmaCollectDesignerSettings ? window.elmaCollectDesignerSettings() : null",
                0,
                captured,
            )

        def _save_configuration(self, save_as: bool = False) -> bool:
            if designer_server is None:
                return False
            destination = designer_server.active_settings_path
            if save_as or destination is None:
                suggested_name = destination.name if destination is not None else "ELMA-Device.config.json"
                selected, _ = QFileDialog.getSaveFileName(
                    self,
                    "Save ELMA configuration",
                    str(self._dialog_directory() / suggested_name),
                    "ELMA configuration (*.json);;JSON files (*.json);;All files (*.*)",
                )
                if not selected:
                    return False
                destination = self._ensure_extension(pathlib.Path(selected), ".json")
            try:
                designer_server.save_designer_settings(path=destination)
                self._update_document_title()
                return True
            except (OSError, TypeError, ValueError) as error:
                QMessageBox.critical(self, "Configuration was not saved", str(error))
                return False

        def _request_save_configuration(self, save_as: bool) -> None:
            self._capture_page_settings(lambda: self._save_configuration(save_as))

        def _confirm_unsaved_changes(self) -> bool:
            if designer_server is None:
                return True
            unnamed = designer_server.active_settings_path is None
            if not designer_server.configuration_dirty() and not unnamed:
                return True
            response = QMessageBox.warning(
                self,
                "Name and save configuration?" if unnamed else "Save configuration changes?",
                "Choose a name and save the current configuration before closing."
                if unnamed
                else "The current configuration has unsaved changes.",
                QMessageBox.StandardButton.Save
                | QMessageBox.StandardButton.Discard
                | QMessageBox.StandardButton.Cancel,
                QMessageBox.StandardButton.Save,
            )
            if response == QMessageBox.StandardButton.Cancel:
                return False
            if response == QMessageBox.StandardButton.Save:
                return self._save_configuration(False)
            return True

        def _open_configuration(self, confirm_changes: bool = True) -> None:
            if designer_server is None or (confirm_changes and not self._confirm_unsaved_changes()):
                return
            selected, _ = QFileDialog.getOpenFileName(
                self,
                "Open ELMA configuration",
                str(self._dialog_directory()),
                "ELMA configuration (*.json);;JSON files (*.json);;All files (*.*)",
            )
            if not selected:
                return
            try:
                designer_server.open_configuration(pathlib.Path(selected))
                designer_view.reload()
                self._update_document_title()
            except (OSError, TypeError, ValueError) as error:
                QMessageBox.critical(self, "Configuration could not be opened", str(error))

        def _request_open_configuration(self) -> None:
            self._capture_page_settings(self._open_configuration)

        def prompt_for_initial_configuration(self) -> None:
            if self._initial_configuration_prompted or smoke_test or designer_server is None:
                return
            self._initial_configuration_prompted = True
            if designer_server.active_settings_path is None:
                self._open_configuration(confirm_changes=False)

        def _request_compile_and_save(self) -> None:
            if designer_server is None or designer_server.flasher is None:
                QMessageBox.critical(self, "Compiler unavailable", "The portable firmware compiler is unavailable.")
                return
            if self._compile_job and self._compile_job.state in ("queued", "running"):
                QMessageBox.information(self, "Compilation in progress", "A firmware compilation is already running.")
                return

            def begin_after_capture() -> None:
                designer_view.page().runJavaScript(
                    "JSON.stringify({"
                    "chip:document.getElementById('localBuilderChip')?.value||'auto',"
                    "firmwareMode:document.getElementById('localBuilderFirmwareMode')?.value||'full',"
                    "maximum:Boolean(document.getElementById('localBuilderMaximum')?.checked),"
                    "webUi:Boolean(document.getElementById('localBuilderWebUi')?.checked),"
                    "hacs:Boolean(document.getElementById('localBuilderHacs')?.checked),"
                    "audio:Boolean(document.getElementById('localBuilderAudio')?.checked)"
                    "})",
                    0,
                    self._begin_compile_and_save,
                )

            self._capture_page_settings(begin_after_capture)

        def _begin_compile_and_save(self, raw_options) -> None:
            if designer_server is None:
                return
            try:
                options = json.loads(raw_options) if isinstance(raw_options, str) else {}
            except json.JSONDecodeError:
                options = {}
            chip = str(options.get("chip", "auto"))
            if chip not in CHIP_FAMILIES:
                labels = list(CHIP_FAMILIES.values())
                selected, accepted = QInputDialog.getItem(
                    self,
                    "Choose firmware target",
                    "Target chip:",
                    labels,
                    0,
                    False,
                )
                if not accepted:
                    return
                chip = next(key for key, label in CHIP_FAMILIES.items() if label == selected)
            capabilities = {
                "maximum": bool(options.get("maximum", True)),
                "webUi": bool(options.get("webUi", True)),
                "hacs": bool(options.get("hacs", True)),
                "audio": bool(options.get("audio", False)) and chip != "esp32c3",
            }
            firmware_mode = str(options.get("firmwareMode", "full"))
            suggested = designer_server.generated_firmware_name(chip, capabilities, firmware_mode)
            selected, _ = QFileDialog.getSaveFileName(
                self,
                "Compile and save firmware",
                str(self._dialog_directory() / suggested),
                "ESP32 firmware (*.bin);;Binary files (*.bin);;All files (*.*)",
            )
            if not selected:
                return
            destination = self._ensure_extension(pathlib.Path(selected), ".bin")
            designer_server.remember_configuration_directory(destination.parent)
            with designer_server.settings_lock:
                settings = json.loads(json.dumps(designer_server.settings))
            try:
                self._compile_job = designer_server.create_job({
                    "compileOnly": True,
                    "firmwareMode": firmware_mode,
                    "chip": chip,
                    "settings": settings,
                    "capabilities": capabilities,
                    "outputPath": str(destination),
                })
            except (RuntimeError, ValueError) as error:
                QMessageBox.critical(self, "Compilation could not start", str(error))
                return
            self._compile_progress = QProgressDialog("Preparing firmware compiler…", "Cancel", 0, 100, self)
            self._compile_progress.setWindowTitle("Compile ELMA firmware")
            self._compile_progress.setWindowModality(Qt.WindowModality.WindowModal)
            self._compile_progress.setMinimumDuration(0)
            self._compile_progress.canceled.connect(self._cancel_compile_job)
            self._compile_progress.show()
            self.compile_save_action.setEnabled(False)
            self._compile_poll_timer.start()

        def _cancel_compile_job(self) -> None:
            if self._compile_job is None:
                return
            self._compile_job.cancelled = True
            if self._compile_job.process and self._compile_job.process.poll() is None:
                self._compile_job.process.terminate()

        def _poll_compile_job(self) -> None:
            job = self._compile_job
            if job is None:
                self._compile_poll_timer.stop()
                return
            if self._compile_progress is not None:
                self._compile_progress.setValue(job.progress)
                self._compile_progress.setLabelText(job.status or "Compiling firmware…")
            if job.state in ("queued", "running"):
                return
            self._compile_poll_timer.stop()
            if self._compile_progress is not None:
                self._compile_progress.close()
                self._compile_progress.deleteLater()
                self._compile_progress = None
            self.compile_save_action.setEnabled(True)
            self._compile_job = None
            if job.state == "complete":
                QMessageBox.information(
                    self,
                    "Firmware compiled",
                    f"Firmware was compiled and saved to:\n{job.firmware_file}",
                )
            elif job.state != "cancelled":
                QMessageBox.critical(self, "Firmware compilation failed", job.error or job.status)

        def _queue_geometry_save(self) -> None:
            if not smoke_test and self.isVisible():
                self._geometry_save_timer.start()

        def _save_geometry(self) -> None:
            if smoke_test:
                return
            if not self.desktop_view_action.isChecked():
                return  # Debug preview must not replace the saved desktop size.
            payload = {
                "geometry": base64.b64encode(bytes(self.saveGeometry())).decode("ascii"),
                "maximized": self.isMaximized(),
            }
            try:
                window_state_path.parent.mkdir(parents=True, exist_ok=True)
                temporary = window_state_path.with_suffix(".tmp")
                temporary.write_text(json.dumps(payload, indent=2), encoding="utf-8")
                temporary.replace(window_state_path)
            except OSError:
                pass

        def moveEvent(self, event) -> None:
            super().moveEvent(event)
            self._queue_geometry_save()

        def resizeEvent(self, event) -> None:
            super().resizeEvent(event)
            self._queue_geometry_save()

        def closeEvent(self, event) -> None:
            if not self._close_capture_complete and not smoke_test:
                event.ignore()
                self._capture_page_settings(self._finish_close_after_capture)
                return
            self._close_capture_complete = False
            if not smoke_test and not self._confirm_unsaved_changes():
                event.ignore()
                return
            if self._compile_job and self._compile_job.state in ("queued", "running"):
                response = QMessageBox.question(
                    self,
                    "Cancel firmware compilation?",
                    "Firmware compilation is still running. Cancel it and close ELMA Flasher?",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                    QMessageBox.StandardButton.No,
                )
                if response != QMessageBox.StandardButton.Yes:
                    event.ignore()
                    return
                self._cancel_compile_job()
            self._geometry_save_timer.stop()
            self._save_geometry()
            super().closeEvent(event)

        def _finish_close_after_capture(self) -> None:
            self._close_capture_complete = True
            self.close()

    window = PersistentDesignerWindow()
    window.setWindowTitle(f"ELMA Flasher v{APP_VERSION}")
    window.setWindowIcon(icon)
    window.resize(1280, 900)
    window.setMinimumSize(420, 420)
    designer_view = ResponsiveWebView(window)
    designer_page = LocalDesignerPage(profile, designer_view)
    designer_view.setPage(designer_page)
    window.setCentralWidget(designer_view)

    result = {
        "designer_loaded": False,
        "responsive_ok": not smoke_test,
        "pc_interface_ok": not smoke_test,
        "file_menu_ok": all(
            action.text()
            for action in (
                window.open_action,
                window.save_action,
                window.save_as_action,
                window.compile_save_action,
            )
        ),
        "responsive_test_started": False,
        "interface_attempts": 0,
    }

    def finish_smoke_test() -> None:
        window.close()
        qt_app.quit()

    def maybe_finish_smoke_test() -> None:
        if not smoke_test or result["responsive_test_started"]:
            return
        if not result["designer_loaded"]:
            return
        result["responsive_test_started"] = True

        def verify_pc_interface(value) -> None:
            try:
                value = json.loads(value) if isinstance(value, str) else value
            except json.JSONDecodeError:
                value = None
            result["pc_interface_ok"] = bool(
                isinstance(value, dict)
                and value.get("mode")
                and value.get("powerHidden")
                and value.get("hardwareEstimate")
                and value.get("storageHidden")
                and value.get("builderVisible")
                and value.get("designerTitle")
                and value.get("controlResponsive")
                and value.get("boardChoiceCount", 0) > 1
                and value.get("boardSelectorEnabled")
                and value.get("boardAutodetectVisible")
                and value.get("boardAutodetectOnBehavior")
                and value.get("boardAutodetectCompact")
                and value.get("boardStartupStateConsistent")
                and value.get("boardDropdownTip")
                and value.get("ipFlashControls")
                and value.get("ipTransportLabel")
                and value.get("singleCancelButton")
                and value.get("motorVisible")
                and value.get("motorControlsStable")
                and value.get("touchAssignmentVisible")
                and value.get("touchAssignmentStable")
                and value.get("interactionResponsive")
                and value.get("tabPaintResponsive")
                and value.get("eyeButtonsBare")
                and value.get("migrationVisible")
                and value.get("migrationControls")
                and value.get("wifiPowerControls")
            )
            if not result["pc_interface_ok"] and result["interface_attempts"] < 20:
                QTimer.singleShot(300, check_pc_interface)
                return
            if not result["pc_interface_ok"]:
                print(f"PC Designer adaptation smoke test failed: {value}")
            window.showNormal()
            window.resize(700, 680)
            QTimer.singleShot(150, verify_responsive_zoom)

        def verify_responsive_zoom() -> None:
            designer_view.applyResponsiveZoom()
            designer_zoom = designer_view.zoomFactor()
            def verify_designer_zoom() -> None:
                result["responsive_ok"] = ResponsiveWebView.MINIMUM_ZOOM <= designer_zoom < 0.85
                desktop_geometry = window.geometry()
                window.mobile_view_action.trigger()
                designer_view.applyResponsiveZoom()
                mobile_ok = designer_view.zoomFactor() == 1.0 and window.width() == 390
                window.fold_view_action.trigger()
                designer_view.applyResponsiveZoom()
                mobile_ok = mobile_ok and designer_view.zoomFactor() == 1.0 and window.width() == min(740, window.screen().availableGeometry().width() - 40)
                window.desktop_view_action.trigger()
                designer_view.applyResponsiveZoom()
                result["responsive_ok"] = result["responsive_ok"] and mobile_ok and window.geometry() == desktop_geometry and designer_view.zoomFactor() < 0.85
                if not result["responsive_ok"]:
                    print(f"Responsive zoom smoke test failed: designer={designer_zoom:.3f}, window={window.width()}")
                finish_smoke_test()

            QTimer.singleShot(150, verify_designer_zoom)

        def check_pc_interface() -> None:
            result["interface_attempts"] += 1
            designer_view.page().runJavaScript(
                "(()=>{"
                "let control=document.querySelector('select[data-peripheral-control-index=\"0\"]');"
                "if(control&&control.value!=='drv8833-dual-motor-driver'){"
                "control.value='drv8833-dual-motor-driver';"
                "control.dispatchEvent(new Event('change',{bubbles:true}));"
                "control=document.querySelector('select[data-peripheral-control-index=\"0\"]');"
                "}"
                "let touchInput=document.querySelector('select[data-peripheral-input-index=\"0\"]');"
                "if(touchInput&&touchInput.value!=='ttp223-touch-button'){"
                "touchInput.value='ttp223-touch-button';"
                "touchInput.dispatchEvent(new Event('change',{bubbles:true}));"
                "}"
                "let duration=document.getElementById('motorChannelAForwardDuration');"
                "let movement=document.getElementById('motorChannelAForwardRole');"
                "let stopSwitch=document.getElementById('motorChannelAForwardLimit');"
                "let boardSelector=document.getElementById('gpioBoardSelector');"
                "let boardAutodetect=document.getElementById('gpioBoardAutodetect');"
                "let flashTransport=document.getElementById('localBuilderTransport');"
                "let flashAction=document.getElementById('localBuilderCompileFlash');"
                "if(flashTransport&&!window.__elmaIpTransportSmoke){"
                "flashTransport.value='ip';flashTransport.dispatchEvent(new Event('change',{bubbles:true}));"
                "window.__elmaIpTransportSmoke=!document.getElementById('localBuilderIpTarget')?.hidden&&flashAction?.textContent.includes('Flash IP Device');"
                "flashTransport.value='usb';flashTransport.dispatchEvent(new Event('change',{bubbles:true}));"
                "}"
                "if(boardSelector&&boardAutodetect&&!window.__elmaBoardModeSmokeStarted){"
                "window.__elmaBoardStartupStateConsistent=boardAutodetect.checked===boardSelector.disabled;"
                "window.__elmaBoardModeSmokeStarted=true;"
                "boardAutodetect.checked=true;"
                "boardAutodetect.dispatchEvent(new Event('change',{bubbles:true}));"
                "setTimeout(()=>{"
                "window.__elmaBoardAutodetectOnBehavior=boardSelector.disabled&&boardSelector.title.includes('Board autodetect is on');"
                "boardAutodetect.checked=false;"
                "boardAutodetect.dispatchEvent(new Event('change',{bubbles:true}));"
                "},500);"
                "}"
                "if(duration&&!window.__elmaMotorControlSmokeStart){"
                "let interactionStarted=performance.now();"
                "duration.value='3000';"
                "duration.dispatchEvent(new Event('input',{bubbles:true}));"
                "duration.dispatchEvent(new Event('change',{bubbles:true}));"
                "duration.focus();"
                "if(movement){movement.value='opening';movement.dispatchEvent(new Event('change',{bubbles:true}));}"
                "if(stopSwitch){stopSwitch.dispatchEvent(new Event('change',{bubbles:true}));}"
                "let touchAction=document.querySelector('select[data-motor-touch-action=\"button1\"]');"
                "if(touchAction){touchAction.value='toggle_open_close';touchAction.dispatchEvent(new Event('change',{bubbles:true}));touchAction.focus();}"
                "window.__elmaMotorInteractionLatency=performance.now()-interactionStarted;"
                "let wifiTab=document.querySelector('[data-tab=wifi]');"
                "let motorTab=document.querySelector('[data-tab=motor]');"
                "if(wifiTab&&motorTab){"
                "wifiTab.click();"
                "setTimeout(()=>{"
                "let tabStarted=performance.now();"
                "motorTab.click();"
                "window.__elmaTabSwitchLatency=performance.now()-tabStarted;"
                "requestAnimationFrame(()=>requestAnimationFrame(()=>{window.__elmaMotorTabPaintLatency=performance.now()-tabStarted;}));"
                "},180);"
                "}"
                "window.__elmaMotorControlSmokeStart=Date.now();"
                "}"
                "return JSON.stringify({"
                "mode:document.body.classList.contains('local-builder-mode'),"
                "powerHidden:getComputedStyle(document.querySelector('.hero-actions')).display==='none',"
                "hardwareEstimate:getComputedStyle(document.querySelector('[data-tab=hardware]')).display!=='none'&&document.querySelector('#tab-hardware h2')?.textContent.includes('Estimate'),"
                "storageHidden:getComputedStyle(document.querySelector('[data-tab=storage-internal]')).display==='none',"
                "builderVisible:getComputedStyle(document.getElementById('localBuilderPanel')).display!=='none',"
                "designerTitle:document.getElementById('deviceTitle')?.textContent.includes('Designer'),"
                "controlResponsive:control?.value==='drv8833-dual-motor-driver',"
                "boardChoiceCount:boardSelector?.options?.length||0,"
                "boardSelectorEnabled:Boolean(window.__elmaBoardAutodetectOnBehavior&&!boardAutodetect?.checked&&!boardSelector?.disabled),"
                "boardAutodetectVisible:!boardAutodetect?.disabled&&!boardAutodetect?.closest('label')?.hidden,"
                "boardAutodetectOnBehavior:Boolean(window.__elmaBoardAutodetectOnBehavior),"
                "boardAutodetectCompact:Boolean(boardAutodetect?.closest('label')?.textContent?.trim()===''&&Math.abs(parseFloat(getComputedStyle(boardAutodetect.closest('label')).width)-parseFloat(getComputedStyle(document.querySelector('.peripheral-profile-action-add')).width))<=2),"
                "boardStartupStateConsistent:Boolean(window.__elmaBoardStartupStateConsistent),"
                "boardDropdownTip:boardSelector?.title?.includes('concrete board'),"
                "ipFlashControls:Boolean(document.getElementById('localBuilderIpDevice')&&document.getElementById('localBuilderIpAddress')&&document.getElementById('localBuilderScanIpDevices')),"
                "ipTransportLabel:Boolean(window.__elmaIpTransportSmoke&&flashAction?.textContent.includes('Flash USB Device')),"
                "singleCancelButton:Boolean(document.getElementById('localBuilderCancel')?.hidden),"
                "motorVisible:getComputedStyle(document.querySelector('[data-tab=motor]')).display!=='none'&&!document.querySelector('[data-tab=motor]').hidden,"
                "motorControlsStable:Boolean(window.__elmaMotorControlSmokeStart&&Date.now()-window.__elmaMotorControlSmokeStart>2200&&duration?.value==='3000'&&movement?.value==='opening'),"
                "touchAssignmentVisible:Boolean(document.querySelector('select[data-motor-touch-action=\"button1\"]')),"
                "touchAssignmentStable:Boolean(window.__elmaMotorControlSmokeStart&&Date.now()-window.__elmaMotorControlSmokeStart>2200&&document.querySelector('select[data-motor-touch-action=\"button1\"]')?.value==='toggle_open_close'),"
                "interactionResponsive:Boolean(window.__elmaMotorInteractionLatency<150&&window.__elmaTabSwitchLatency<250),"
                "tabPaintResponsive:Boolean(window.__elmaMotorTabPaintLatency<500),"
                "eyeButtonsBare:Array.from(document.querySelectorAll('.password-toggle')).every(button=>{let style=getComputedStyle(button);return style.backgroundColor==='rgba(0, 0, 0, 0)'&&parseFloat(style.borderTopWidth)===0}),"
                "migrationVisible:getComputedStyle(document.querySelector('[data-tab=migration]')).display!=='none'&&!document.querySelector('[data-tab=migration]').hidden,"
                "migrationControls:Boolean(document.getElementById('migrationScanButton')&&document.getElementById('migrationInspectButton')&&document.getElementById('migrationApplyButton')&&document.getElementById('migrationYamlFile')),"
                "wifiPowerControls:Boolean(document.querySelectorAll('[data-wifi-power][type=range]').length===2&&!document.getElementById('wifiApplyPowerButton').disabled&&document.getElementById('wifiPowerStatus').textContent.includes('Future ESP device')&&Array.from(document.querySelectorAll('[data-wifi-power]')).every(input=>input.min==='2'&&input.max==='19.5'&&input.step==='0.5'))"
                "});})()",
                0,
                verify_pc_interface,
            )

        QTimer.singleShot(300, check_pc_interface)

    def designer_loaded(ok: bool) -> None:
        result["designer_loaded"] = bool(ok)
        if ok:
            designer_view.applyResponsiveZoom()
            if not smoke_test:
                # Activation is repeated after WebEngine finishes loading because
                # initialization can otherwise return focus to the previous app.
                QTimer.singleShot(0, activate_startup_window)
                QTimer.singleShot(250, activate_startup_window)
            QTimer.singleShot(0, window.prompt_for_initial_configuration)
        maybe_finish_smoke_test()
        if not ok and not smoke_test:
            designer_view.setHtml(
                "<html><body style='font:16px Segoe UI;background:#f7f5ef;color:#2a2926;padding:40px'>"
                "<h1>ELMA Device Designer could not start</h1>"
                "<p>The bundled Designer interface did not load. Close this window and retry.</p>"
                "</body></html>"
            )

    designer_view.loadFinished.connect(designer_loaded)
    saved_window_geometry = load_window_geometry()
    if saved_window_geometry:
        geometry, was_maximized = saved_window_geometry
        window.restoreGeometry(QByteArray(geometry))
    else:
        was_maximized = True

    if smoke_test:
        QTimer.singleShot(15000, finish_smoke_test)
    else:
        window.showMaximized() if was_maximized else window.show()

    def activate_startup_window() -> None:
        # Never pull focus away from the initial Open Configuration dialog (or
        # any later confirmation/file dialog) while a delayed activation retry
        # is pending.
        if smoke_test or not window.isVisible() or qt_app.activeModalWidget() is not None:
            return
        window.raise_()
        window.activateWindow()
        if sys.platform == "win32":
            user32 = ctypes.windll.user32
            handle = int(window.winId())
            if user32.IsIconic(handle):
                user32.ShowWindow(handle, 9)  # SW_RESTORE
            user32.BringWindowToTop(handle)
            user32.SetForegroundWindow(handle)
        window.raise_()
        window.activateWindow()

    if not smoke_test:
        QTimer.singleShot(0, activate_startup_window)
        QTimer.singleShot(150, activate_startup_window)
    designer_url = QUrl(url)
    designer_query = QUrlQuery(designer_url)
    designer_query.addQueryItem("elmaRuntime", "pc-designer")
    designer_url.setQuery(designer_query)
    designer_view.setUrl(designer_url)
    window.showMaximized() if smoke_test else None
    qt_app.exec()
    designer_view.deleteLater()
    window.deleteLater()
    qt_app.processEvents()
    return result["designer_loaded"] and result["pc_interface_ok"] and result["responsive_ok"] and result["file_menu_ok"]


class FlasherApplication:
    def __init__(self, root: tk.Tk, auto_detect_chip: bool = True) -> None:
        self.root = root
        self.auto_detect_chip = auto_detect_chip
        self.designer_server = DesignerServer(self)
        self.events: queue.Queue[tuple] = queue.Queue()
        self.busy = False
        self.last_ip = ""
        self.mode = tk.StringVar(value="clone")
        self.erase = tk.StringVar(value="yes")
        self.source_ip = tk.StringVar(value="")
        self.username = tk.StringVar(value="")
        self.password = tk.StringVar(value="")
        self.firmware_file = tk.StringVar(value="")
        self.port = tk.StringVar(value="")
        self.chip_choice = tk.StringVar(value="auto")
        self.chip_choice_label = tk.StringVar(value=CHIP_CHOICES["auto"])
        self.detected_chip = tk.StringVar(value="Detected: not checked")
        self.firmware_family = tk.StringVar(value="Firmware: not selected")
        self.preconfigure_enabled = tk.BooleanVar(value=False)
        self.preconfigure_summary = tk.StringVar(value="Disabled — device or clone defaults will be used")
        self.preconfigure_values: dict[str, object] = {
            "friendly_name": "",
            "wifi_ssid": "",
            "wifi_password": "",
            "use_static_ip": False,
            "static_ip": "",
            "gateway": "",
            "subnet": "",
            "dns1": "",
            "dns2": "",
            "mqtt_host": "",
            "mqtt_port": "",
            "mqtt_username": "",
            "mqtt_password": "",
            "mqtt_discovery": "default",
            "ap_ssid": "",
            "ap_password": "",
            "ap_fallback": "default",
            "web_auth": "default",
            "web_username": "",
            "web_password": "",
        }
        self.selected_image_family = ""
        self.detected_family = ""
        self.detecting_chip = False
        self.esptool_lock = threading.Lock()
        self.status = tk.StringVar(value="Ready to flash another ESP device")
        self.detail = tk.StringVar(value="Connect the target using a USB data cable, then select its COM port.")
        self.progress = 0
        self._configure_window()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self._build_ui()
        self.refresh_ports()
        self.root.after(80, self._process_events)

    def _configure_window(self) -> None:
        self.root.title(f"ELMA Flasher v{APP_VERSION}")
        self.window_icon_path = resource_path("assets/ELMA-Flasher.ico")
        self.root.iconbitmap(default=str(self.window_icon_path))
        self.window_icon_applied = True
        self.root.geometry("860x760")
        self.root.minsize(760, 690)
        self.root.configure(bg=PAPER)
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure("TCombobox", fieldbackground=WHITE, background=WHITE, padding=8)

    def _build_ui(self) -> None:
        header = tk.Frame(self.root, bg=DARK, height=86)
        header.pack(fill="x")
        header.pack_propagate(False)
        logo = tk.Canvas(header, width=54, height=54, bg=DARK, highlightthickness=0)
        logo.pack(side="left", padx=(24, 14), pady=16)
        logo.create_oval(4, 4, 50, 50, fill=ORANGE, outline=ORANGE)
        logo.create_text(27, 27, text="E", fill=WHITE, font=("Segoe UI", 23, "bold"))
        titles = tk.Frame(header, bg=DARK)
        titles.pack(side="left", pady=14)
        tk.Label(titles, text="ELMA Flasher", bg=DARK, fg=WHITE, font=("Segoe UI", 22, "bold")).pack(anchor="w")
        tk.Label(titles, text="Portable ESP32 / ESP32-S3 / ESP32-C3 designer, compiler and flasher", bg=DARK, fg="#d9d5ce", font=("Segoe UI", 10)).pack(anchor="w")
        tk.Label(header, text=f"v{APP_VERSION}", bg=ORANGE, fg=WHITE, font=("Segoe UI", 10, "bold"), padx=12, pady=6).pack(side="right", padx=24)

        # Reserve the primary action before the expanding body so Windows display
        # scaling can never push the Flash button below the visible client area.
        footer = tk.Frame(self.root, bg=PAPER)
        footer.pack(side="bottom", fill="x", padx=22, pady=(0, 16))
        self.designer_button = tk.Button(footer, text="Open Device Designer — Configure, Compile & Flash", command=self.open_designer, bg=DARK, fg=WHITE, activebackground="#46433d", activeforeground=WHITE, relief="flat", font=("Segoe UI", 11, "bold"), pady=10)
        self.designer_button.pack(fill="x", pady=(0, 8))
        self.flash_button = tk.Button(footer, text="Flash USB Device", command=self.start, bg=ORANGE, fg=WHITE, activebackground="#d97e00", activeforeground=WHITE, disabledforeground="#ddd8ce", relief="flat", font=("Segoe UI", 12, "bold"), pady=11)
        self.flash_button.pack(fill="x")

        body = tk.Frame(self.root, bg=PAPER)
        body.pack(side="top", fill="both", expand=True, padx=22, pady=(18, 12))

        options = tk.LabelFrame(body, text=" Firmware source ", bg=WHITE, fg=DARK, font=("Segoe UI", 11, "bold"), bd=1, relief="solid", padx=14, pady=10)
        options.pack(fill="x")
        tk.Radiobutton(options, variable=self.mode, value="clone", command=self._update_mode, text="Clone Current Device", bg=WHITE, activebackground=WHITE, selectcolor=WHITE, fg=DARK, font=("Segoe UI", 11, "bold")).grid(row=0, column=0, sticky="w")
        tk.Label(options, text="Copies firmware, Wi-Fi, MQTT and configuration; target identity is regenerated from its hardware ID.", bg=WHITE, fg=MUTED, font=("Segoe UI", 9), wraplength=690, justify="left").grid(row=1, column=0, columnspan=4, sticky="w", padx=(24, 0), pady=(0, 8))
        tk.Label(options, text="Device IP", bg=WHITE, fg=DARK).grid(row=2, column=0, sticky="w")
        self.source_entry = tk.Entry(options, textvariable=self.source_ip, relief="solid", bd=1, font=("Segoe UI", 10))
        self.source_entry.grid(row=2, column=1, sticky="ew", padx=(8, 14), ipady=6)
        tk.Label(options, text="Web user", bg=WHITE, fg=DARK).grid(row=2, column=2, sticky="w")
        self.user_entry = tk.Entry(options, textvariable=self.username, relief="solid", bd=1, width=13)
        self.user_entry.grid(row=2, column=3, padx=(8, 10), ipady=6)
        tk.Label(options, text="Password", bg=WHITE, fg=DARK).grid(row=2, column=4, sticky="w")
        self.password_entry = tk.Entry(options, textvariable=self.password, show="•", relief="solid", bd=1, width=13)
        self.password_entry.grid(row=2, column=5, padx=(8, 0), ipady=6)

        tk.Radiobutton(options, variable=self.mode, value="file", command=self._update_mode, text="Flash From File", bg=WHITE, activebackground=WHITE, selectcolor=WHITE, fg=DARK, font=("Segoe UI", 11, "bold")).grid(row=3, column=0, sticky="w", pady=(12, 0))
        self.file_entry = tk.Entry(options, textvariable=self.firmware_file, state="disabled", relief="solid", bd=1, font=("Segoe UI", 9))
        self.file_entry.grid(row=4, column=0, columnspan=5, sticky="ew", padx=(24, 8), ipady=6)
        self.browse_button = tk.Button(options, text="Browse…", command=self.choose_file, state="disabled", bg="#eceae5", fg=DARK, relief="solid", bd=1, padx=14, pady=5)
        self.browse_button.grid(row=4, column=5, sticky="ew")
        self.file_entry.bind("<FocusOut>", lambda _event: self._inspect_selected_firmware(False))
        self.file_entry.bind("<Return>", lambda _event: self._inspect_selected_firmware(True))
        tk.Checkbutton(options, text="Preconfigure target", variable=self.preconfigure_enabled, command=self._update_preconfigure_summary, bg=WHITE, activebackground=WHITE, selectcolor=WHITE, fg=DARK, font=("Segoe UI", 10, "bold")).grid(row=5, column=0, sticky="w", pady=(12, 0))
        tk.Label(options, textvariable=self.preconfigure_summary, bg=WHITE, fg=MUTED, font=("Segoe UI", 8), anchor="w").grid(row=5, column=1, columnspan=4, sticky="ew", padx=(8, 8), pady=(12, 0))
        tk.Button(options, text="Configure…", command=self.open_preconfiguration, bg="#eceae5", fg=DARK, relief="solid", bd=1, padx=14, pady=5).grid(row=5, column=5, sticky="ew", pady=(12, 0))
        options.columnconfigure(1, weight=1)
        options.columnconfigure(3, weight=1)

        target = tk.Frame(body, bg=PAPER)
        target.pack(fill="x", pady=14)
        port_card = tk.LabelFrame(target, text=" Target USB device ", bg=WHITE, fg=DARK, font=("Segoe UI", 11, "bold"), bd=1, relief="solid", padx=12, pady=10)
        port_card.pack(side="left", fill="both", expand=True, padx=(0, 7))
        self.port_combo = ttk.Combobox(port_card, textvariable=self.port, state="readonly", font=("Segoe UI", 10))
        self.port_combo.pack(side="left", fill="x", expand=True)
        self.port_combo.bind("<<ComboboxSelected>>", lambda _event: self.detect_selected_chip())
        tk.Button(port_card, text="Refresh", command=self.refresh_ports, bg="#eceae5", fg=DARK, relief="solid", bd=1, padx=12, pady=6).pack(side="left", padx=(8, 0))

        chip_card = tk.LabelFrame(target, text=" Target chip ", bg=WHITE, fg=DARK, font=("Segoe UI", 11, "bold"), bd=1, relief="solid", padx=12, pady=7)
        chip_card.pack(side="left", fill="both", expand=True, padx=7)
        self.chip_button = tk.Menubutton(chip_card, textvariable=self.chip_choice_label, bg="#eceae5", fg=DARK, activebackground="#e0ddd6", relief="solid", bd=1, padx=10, pady=5, indicatoron=True)
        self.chip_menu = tk.Menu(self.chip_button, tearoff=False)
        self.chip_button.configure(menu=self.chip_menu)
        for family, label in CHIP_CHOICES.items():
            self.chip_menu.add_radiobutton(label=label, variable=self.chip_choice, value=family, command=self._update_chip_choice_label)
        self.chip_button.pack(fill="x")
        self.detected_chip_label = tk.Label(chip_card, textvariable=self.detected_chip, bg=WHITE, fg=MUTED, font=("Segoe UI", 8), anchor="w")
        self.detected_chip_label.pack(fill="x", pady=(4, 0))
        tk.Label(chip_card, textvariable=self.firmware_family, bg=WHITE, fg=MUTED, font=("Segoe UI", 8), anchor="w").pack(fill="x")

        erase_card = tk.LabelFrame(target, text=" Clean target before flashing? ", bg=WHITE, fg=DARK, font=("Segoe UI", 11, "bold"), bd=1, relief="solid", padx=12, pady=8)
        erase_card.pack(side="left", fill="both", expand=True, padx=(7, 0))
        tk.Radiobutton(erase_card, text="Yes — full erase", variable=self.erase, value="yes", bg=WHITE, activebackground=WHITE, selectcolor=WHITE).pack(side="left")
        tk.Radiobutton(erase_card, text="No — preserve", variable=self.erase, value="no", bg=WHITE, activebackground=WHITE, selectcolor=WHITE).pack(side="left", padx=(16, 0))

        progress_card = tk.Frame(body, bg=WHITE, highlightbackground="#d7d3ca", highlightthickness=1)
        progress_card.pack(fill="x", pady=(0, 14))
        self.progress_canvas = tk.Canvas(progress_card, width=130, height=130, bg=WHITE, highlightthickness=0)
        self.progress_canvas.pack(side="left", padx=18, pady=10)
        self.progress_canvas.create_oval(18, 18, 112, 112, outline="#e5e1d8", width=11)
        self.progress_arc = self.progress_canvas.create_arc(18, 18, 112, 112, start=90, extent=0, outline=ORANGE, width=11, style="arc")
        self.progress_text = self.progress_canvas.create_text(65, 64, text="0%", fill=DARK, font=("Segoe UI", 18, "bold"))
        progress_copy = tk.Frame(progress_card, bg=WHITE)
        progress_copy.pack(side="left", fill="both", expand=True, pady=22, padx=(0, 16))
        tk.Label(progress_copy, textvariable=self.status, bg=WHITE, fg=DARK, font=("Segoe UI", 13, "bold"), anchor="w").pack(fill="x")
        tk.Label(progress_copy, textvariable=self.detail, bg=WHITE, fg=MUTED, font=("Segoe UI", 10), anchor="w", justify="left", wraplength=600).pack(fill="x", pady=(6, 0))
        self.open_button = tk.Button(progress_copy, text="Open cloned device", command=self.open_target, bg=GREEN, fg=WHITE, activebackground=GREEN, activeforeground=WHITE, relief="flat", padx=12, pady=7)

        log_frame = tk.Frame(body, bg=WHITE, highlightbackground="#d7d3ca", highlightthickness=1)
        log_frame.pack(fill="both", expand=True)
        self.log = tk.Text(log_frame, height=9, bg="#fbfaf7", fg=DARK, relief="flat", font=("Consolas", 9), padx=12, pady=10, state="disabled", wrap="word")
        self.log.pack(side="left", fill="both", expand=True)
        scrollbar = ttk.Scrollbar(log_frame, command=self.log.yview)
        scrollbar.pack(side="right", fill="y")
        self.log.configure(yscrollcommand=scrollbar.set)

    def _update_mode(self) -> None:
        state = "normal" if self.mode.get() == "file" else "disabled"
        self.file_entry.configure(state=state)
        self.browse_button.configure(state=state)
        clone_state = "normal" if self.mode.get() == "clone" else "disabled"
        for control in (self.source_entry, self.user_entry, self.password_entry):
            control.configure(state=clone_state)
        if self.mode.get() == "file":
            self._inspect_selected_firmware(False)
        else:
            self._set_selected_image_family("")

    def _update_chip_choice_label(self) -> None:
        choice = self.chip_choice.get()
        self.chip_choice_label.set(CHIP_CHOICES.get(choice, CHIP_CHOICES["auto"]))

    def _set_selected_image_family(self, family: str) -> None:
        self.selected_image_family = family if family in CHIP_FAMILIES else ""
        if self.selected_image_family:
            self.firmware_family.set(f"Firmware: {CHIP_FAMILIES[self.selected_image_family]}")
        elif self.mode.get() == "clone":
            self.firmware_family.set("Firmware: determined from source")
        else:
            self.firmware_family.set("Firmware: not selected")
        for index, candidate in enumerate(CHIP_FAMILIES, start=1):
            compatible = not self.selected_image_family or candidate == self.selected_image_family
            self.chip_menu.entryconfigure(index, state="normal" if compatible else "disabled")
        if self.chip_choice.get() not in ("auto", self.selected_image_family) and self.selected_image_family:
            self.chip_choice.set("auto")
            self._update_chip_choice_label()

    def _inspect_selected_firmware(self, show_error: bool) -> None:
        if self.mode.get() != "file":
            return
        value = self.firmware_file.get().strip()
        if not value:
            self._set_selected_image_family("")
            return
        try:
            path = pathlib.Path(value)
            with path.open("rb") as stream:
                family = chip_family_from_image(stream.read(24))
            self._set_selected_image_family(family)
        except (OSError, ValueError) as error:
            self._set_selected_image_family("")
            self.firmware_family.set("Firmware: invalid or unsupported")
            if show_error:
                messagebox.showerror("ELMA Flasher", str(error))

    def _update_preconfigure_summary(self) -> None:
        if not self.preconfigure_enabled.get():
            self.preconfigure_summary.set("Disabled — device or clone defaults will be used")
            return
        try:
            overrides = self._preconfiguration_overrides(self.preconfigure_values)
            fields = sum(len(values) for values in overrides.values() if isinstance(values, dict))
            self.preconfigure_summary.set(f"Enabled — {fields} explicit override{'s' if fields != 1 else ''}; blank fields use device defaults")
        except ValueError:
            self.preconfigure_summary.set("Enabled — configuration needs correction")

    @staticmethod
    def _preconfiguration_overrides(values: dict[str, object]) -> dict:
        text = {key: str(value).strip() for key, value in values.items() if key != "use_static_ip"}
        overrides: dict[str, dict] = {}

        friendly_name = text.get("friendly_name", "")
        if len(friendly_name) > 64:
            raise ValueError("Friendly device name must be 64 characters or fewer.")
        if friendly_name:
            overrides.setdefault("device", {})["friendlyName"] = friendly_name

        ssid = text.get("wifi_ssid", "")
        wifi_password = text.get("wifi_password", "")
        if len(ssid) > 32:
            raise ValueError("Wi-Fi SSID must be 32 characters or fewer.")
        if len(wifi_password) > 63:
            raise ValueError("Wi-Fi password must be 63 characters or fewer.")
        if wifi_password and not ssid:
            raise ValueError("Enter a Wi-Fi SSID when supplying a Wi-Fi password.")
        if ssid:
            wifi = overrides.setdefault("wifi", {})
            wifi["ssid"] = ssid
            wifi["password"] = wifi_password

        use_static_ip = bool(values.get("use_static_ip", False))
        if use_static_ip:
            required = {"staticIp": text.get("static_ip", ""), "gateway": text.get("gateway", ""), "subnet": text.get("subnet", "")}
            if not all(required.values()):
                raise ValueError("Static IP, gateway and subnet are required when static addressing is enabled.")
            for label, value in {**required, "dns1": text.get("dns1", ""), "dns2": text.get("dns2", "")}.items():
                if value:
                    try:
                        if ipaddress.ip_address(value).version != 4:
                            raise ValueError
                    except ValueError as error:
                        raise ValueError(f"{label} must be a valid IPv4 address.") from error
            wifi = overrides.setdefault("wifi", {})
            wifi.update({"useStaticIp": True, **required})
            if text.get("dns1"):
                wifi["dns1"] = text["dns1"]
            if text.get("dns2"):
                wifi["dns2"] = text["dns2"]

        mqtt_fields = {
            "host": text.get("mqtt_host", ""),
            "username": text.get("mqtt_username", ""),
            "password": text.get("mqtt_password", ""),
        }
        for key, value in mqtt_fields.items():
            if value:
                overrides.setdefault("mqtt", {})[key] = value
        mqtt_port = text.get("mqtt_port", "")
        if mqtt_port:
            try:
                port = int(mqtt_port)
            except ValueError as error:
                raise ValueError("MQTT port must be a number from 1 to 65535.") from error
            if not 1 <= port <= 65535:
                raise ValueError("MQTT port must be a number from 1 to 65535.")
            overrides.setdefault("mqtt", {})["port"] = port
        mqtt_discovery = text.get("mqtt_discovery", "default")
        if mqtt_discovery in ("enabled", "disabled"):
            overrides.setdefault("mqtt", {})["discoveryEnabled"] = mqtt_discovery == "enabled"

        ap_ssid = text.get("ap_ssid", "")
        ap_password = text.get("ap_password", "")
        if len(ap_ssid) > 32:
            raise ValueError("Fallback AP name must be 32 characters or fewer.")
        if ap_password and len(ap_password) < 8:
            raise ValueError("Fallback AP password must contain at least 8 characters.")
        if ap_ssid:
            overrides.setdefault("wifi", {})["apSsid"] = ap_ssid
        if ap_password:
            overrides.setdefault("wifi", {})["apPassword"] = ap_password
        ap_fallback = text.get("ap_fallback", "default")
        if ap_fallback in ("enabled", "disabled"):
            overrides.setdefault("wifi", {})["apFallbackEnabled"] = ap_fallback == "enabled"

        web_auth = text.get("web_auth", "default")
        web_username = text.get("web_username", "")
        web_password = text.get("web_password", "")
        if web_auth == "enabled" and (not web_username or not web_password):
            raise ValueError("Web username and password are required when web authentication is enabled.")
        if web_auth in ("enabled", "disabled"):
            web = overrides.setdefault("webAuth", {})
            web["enabled"] = web_auth == "enabled"
            if web_username:
                web["username"] = web_username
            if web_password:
                web["password"] = web_password
        return overrides

    def open_preconfiguration(self) -> None:
        dialog = tk.Toplevel(self.root)
        dialog.title("Preconfigure target — ELMA Flasher")
        dialog.geometry("640x610")
        dialog.minsize(600, 560)
        dialog.configure(bg=PAPER)
        dialog.transient(self.root)
        dialog.grab_set()
        dialog.iconbitmap(default=str(self.window_icon_path))

        header = tk.Frame(dialog, bg=DARK, height=64)
        header.pack(fill="x")
        header.pack_propagate(False)
        tk.Label(header, text="Preconfigure target", bg=DARK, fg=WHITE, font=("Segoe UI", 17, "bold")).pack(anchor="w", padx=20, pady=(10, 0))
        tk.Label(header, text="Blank fields retain the flashed device or cloned configuration defaults.", bg=DARK, fg="#d9d5ce", font=("Segoe UI", 9)).pack(anchor="w", padx=20)

        local: dict[str, tk.Variable] = {}
        for key, value in self.preconfigure_values.items():
            local[key] = tk.BooleanVar(value=bool(value)) if key == "use_static_ip" else tk.StringVar(value=str(value))

        notebook = ttk.Notebook(dialog)
        notebook.pack(fill="both", expand=True, padx=18, pady=16)

        def page(title: str) -> tk.Frame:
            frame = tk.Frame(notebook, bg=WHITE, padx=18, pady=16)
            frame.columnconfigure(1, weight=1)
            notebook.add(frame, text=title)
            return frame

        def field(parent: tk.Frame, row: int, label: str, key: str, secret: bool = False) -> tk.Entry:
            tk.Label(parent, text=label, bg=WHITE, fg=DARK, anchor="w").grid(row=row, column=0, sticky="w", padx=(0, 12), pady=6)
            entry = tk.Entry(parent, textvariable=local[key], show="•" if secret else "", relief="solid", bd=1)
            entry.grid(row=row, column=1, sticky="ew", pady=6, ipady=5)
            return entry

        network = page("Identity & Wi-Fi")
        tk.Label(network, text="Hardware identity", bg=WHITE, fg=DARK, font=("Segoe UI", 10, "bold")).grid(row=0, column=0, sticky="w", pady=(0, 6))
        tk.Label(network, text="Generated from the target chip hardware ID — never cloned or editable", bg=WHITE, fg=GREEN, anchor="w").grid(row=0, column=1, sticky="w", pady=(0, 6))
        field(network, 1, "Friendly device name", "friendly_name")
        field(network, 2, "Wi-Fi SSID", "wifi_ssid")
        field(network, 3, "Wi-Fi password", "wifi_password", True)
        tk.Checkbutton(network, text="Use a new static IP (source IP is never cloned)", variable=local["use_static_ip"], bg=WHITE, activebackground=WHITE, selectcolor=WHITE).grid(row=4, column=0, columnspan=2, sticky="w", pady=(10, 2))
        field(network, 5, "Static IP", "static_ip")
        field(network, 6, "Gateway", "gateway")
        field(network, 7, "Subnet", "subnet")
        field(network, 8, "DNS 1", "dns1")
        field(network, 9, "DNS 2", "dns2")

        mqtt = page("MQTT")
        tk.Label(mqtt, text="MQTT client ID and base topic stay tied to the target hardware ID.", bg=WHITE, fg=GREEN, anchor="w", wraplength=500, justify="left").grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 10))
        field(mqtt, 1, "Broker host", "mqtt_host")
        field(mqtt, 2, "Broker port", "mqtt_port")
        field(mqtt, 3, "Username", "mqtt_username")
        field(mqtt, 4, "Password", "mqtt_password", True)
        tk.Label(mqtt, text="Home Assistant discovery", bg=WHITE, fg=DARK).grid(row=5, column=0, sticky="w", pady=6)
        ttk.Combobox(mqtt, textvariable=local["mqtt_discovery"], state="readonly", values=("default", "enabled", "disabled")).grid(row=5, column=1, sticky="ew", pady=6)

        access = page("Fallback AP & Web")
        field(access, 0, "Fallback AP name", "ap_ssid")
        field(access, 1, "Fallback AP password", "ap_password", True)
        tk.Label(access, text="Fallback AP", bg=WHITE, fg=DARK).grid(row=2, column=0, sticky="w", pady=6)
        ttk.Combobox(access, textvariable=local["ap_fallback"], state="readonly", values=("default", "enabled", "disabled")).grid(row=2, column=1, sticky="ew", pady=6)
        tk.Label(access, text="Web authentication", bg=WHITE, fg=DARK).grid(row=3, column=0, sticky="w", pady=(18, 6))
        ttk.Combobox(access, textvariable=local["web_auth"], state="readonly", values=("default", "enabled", "disabled")).grid(row=3, column=1, sticky="ew", pady=(18, 6))
        field(access, 4, "Web username", "web_username")
        field(access, 5, "Web password", "web_password", True)

        footer = tk.Frame(dialog, bg=PAPER)
        footer.pack(fill="x", padx=18, pady=(0, 16))

        def save() -> None:
            values = {key: variable.get() for key, variable in local.items()}
            try:
                self._preconfiguration_overrides(values)
            except ValueError as error:
                messagebox.showerror("ELMA Flasher", str(error), parent=dialog)
                return
            self.preconfigure_values = values
            self.preconfigure_enabled.set(True)
            self._update_preconfigure_summary()
            dialog.destroy()

        tk.Button(footer, text="Save & Enable", command=save, bg=ORANGE, fg=WHITE, activebackground="#d97e00", activeforeground=WHITE, relief="flat", font=("Segoe UI", 11, "bold"), pady=8).pack(side="right", fill="x", expand=True, padx=(8, 0))
        tk.Button(footer, text="Cancel", command=dialog.destroy, bg="#eceae5", fg=DARK, relief="solid", bd=1, pady=8).pack(side="left", fill="x", expand=True, padx=(0, 8))

    def choose_file(self) -> None:
        value = filedialog.askopenfilename(title="Select ELMA firmware", filetypes=[("ESP32 firmware", "*.bin"), ("All files", "*.*")])
        if value:
            self.firmware_file.set(value)
            self._inspect_selected_firmware(True)

    def _selected_port(self) -> str:
        return self.port.get().split(" — ", 1)[0].strip()

    def refresh_ports(self) -> None:
        ports = sorted(list_ports.comports(), key=lambda item: item.device)
        labels = [f"{item.device} — {item.description}" for item in ports]
        self.port_combo["values"] = labels
        current_device = self.port.get().split(" — ", 1)[0]
        if current_device:
            matching = next((label for label in labels if label.startswith(f"{current_device} —")), "")
            self.port.set(matching)
        if not self.port.get() and labels:
            self.port.set(labels[0])
        if self.port.get() and self.auto_detect_chip:
            self.root.after(50, self.detect_selected_chip)
        if not labels:
            self.port.set("")
            self.detected_family = ""
            self.detected_chip.set("Detected: no serial device")
            self.detected_chip_label.configure(fg=RED)
            self._set_status(0, "No USB serial device detected", "Connect the ESP with a data-capable USB cable, allow Windows to install its driver, then press Refresh.", RED)

    def detect_selected_chip(self) -> None:
        port = self._selected_port()
        if not port or self.detecting_chip or self.busy:
            return
        self.detecting_chip = True
        self.detected_family = ""
        self.detected_chip.set("Detected: checking…")
        self.detected_chip_label.configure(fg=ORANGE)
        threading.Thread(target=self._detect_chip_worker, args=(port,), daemon=True).start()

    def _detect_chip_worker(self, port: str) -> None:
        try:
            model, flash_size = self._detect_target_chip(port)
            self._emit("chip_detected", model, flash_size)
        except BaseException as error:
            self._emit("chip_detection_error", friendly_error(error))

    def _emit(self, kind: str, *values) -> None:
        self.events.put((kind, *values))

    def _log(self, value: str) -> None:
        self._emit("log", value)

    def _set_status(self, percent: int, status: str, detail: str, color: str = ORANGE) -> None:
        self.progress = max(0, min(100, int(percent)))
        self.progress_canvas.itemconfigure(self.progress_arc, extent=-3.6 * self.progress, outline=color)
        self.progress_canvas.itemconfigure(self.progress_text, text=f"{self.progress}%")
        self.status.set(status)
        self.detail.set(detail)

    def _process_events(self) -> None:
        try:
            while True:
                event = self.events.get_nowait()
                if event[0] == "log":
                    self.log.configure(state="normal")
                    self.log.insert("end", f"{event[1]}\n")
                    self.log.see("end")
                    self.log.configure(state="disabled")
                elif event[0] == "status":
                    self._set_status(*event[1:])
                elif event[0] == "chip_detected":
                    self.detecting_chip = False
                    self.detected_family = event[1]
                    model_label = KNOWN_CHIP_MODELS.get(event[1], event[1].upper())
                    flash_label = f" · {event[2]}" if event[2] else ""
                    unsupported = event[1] not in CHIP_FAMILIES
                    self.detected_chip.set(f"Detected: {model_label}{flash_label}{' (unsupported)' if unsupported else ''}")
                    self.detected_chip_label.configure(fg=RED if unsupported else GREEN)
                elif event[0] == "chip_detection_error":
                    self.detecting_chip = False
                    self.detected_family = ""
                    self.detected_chip.set("Detected: unavailable")
                    self.detected_chip_label.configure(fg=RED)
                    self._log(f"Chip detection: {event[1]}")
                elif event[0] == "firmware_family":
                    self._set_selected_image_family(event[1])
                elif event[0] == "complete":
                    self.busy = False
                    self.flash_button.configure(state="normal", text="Flash USB Device")
                    self.last_ip = event[1]
                    if self.last_ip:
                        self.open_button.pack(anchor="w", pady=(10, 0))
                    messagebox.showinfo("ELMA Flasher", event[2])
                elif event[0] == "error":
                    self.busy = False
                    self.flash_button.configure(state="normal", text="Retry Flash")
                    messagebox.showerror("ELMA Flasher", event[1])
        except queue.Empty:
            pass
        self.root.after(80, self._process_events)

    def start(self) -> None:
        if self.busy:
            return
        port = self._selected_port()
        if not port:
            messagebox.showerror("ELMA Flasher", "Select a connected USB serial device.")
            return
        if self.mode.get() == "file" and not self.firmware_file.get().strip():
            messagebox.showerror("ELMA Flasher", "Select an ESP32 application .bin file.")
            return
        try:
            preconfiguration = self._preconfiguration_overrides(self.preconfigure_values) if self.preconfigure_enabled.get() else {}
        except ValueError as error:
            messagebox.showerror("ELMA Flasher", str(error))
            return
        self.busy = True
        self.last_ip = ""
        self.open_button.pack_forget()
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")
        self.flash_button.configure(state="disabled", text="Flashing…")
        job = {
            "mode": self.mode.get(),
            "source_ip": self.source_ip.get(),
            "username": self.username.get(),
            "password": self.password.get(),
            "firmware_file": self.firmware_file.get(),
            "erase": self.erase.get() == "yes",
            "chip_choice": self.chip_choice.get(),
            "preconfiguration": preconfiguration,
        }
        threading.Thread(target=self._flash_worker, args=(port, job), daemon=True).start()

    def _http_path(self, value: str) -> str:
        parsed = urllib.parse.urlparse(value)
        return f"{parsed.path}?{parsed.query}" if parsed.query else parsed.path

    def _prepare_clone(self, job: dict) -> tuple[str, list[tuple[int, bytes]], dict]:
        client = HttpDeviceClient(job["source_ip"], job["username"], job["password"])
        self._emit("status", 4, "Reading source device", "Downloading clone manifest and saved configuration.", ORANGE)
        manifest = client.json("/api/usb-flasher/manifest")
        settings = client.json("/api/settings")
        family = str(manifest.get("chipFamily", "")).lower().replace("-", "")
        if family not in CHIP_FAMILIES:
            raise RuntimeError(f"Source device reported unsupported chip family {family or 'unknown'}.")
        parts: list[tuple[int, bytes]] = []
        source_parts = manifest.get("parts")
        if not isinstance(source_parts, list):
            raise RuntimeError("Source firmware clone manifest does not contain flash parts. Update the source device to a clone-capable ELMA firmware first.")
        for index, part in enumerate(source_parts):
            if not isinstance(part, dict) or part.get("name") not in ("bootloader", "partitions", "application"):
                continue
            source_url = str(part.get("sourceUrl", ""))
            data = client.binary(self._http_path(source_url), int(part.get("size", 0)))
            parts.append((int(part.get("address", 0)), data))
            self._emit("status", 5 + index * 3, "Reading source device", f"Downloaded {part.get('name')} ({len(data):,} bytes).", ORANGE)
        application = next((data for address, data in parts if address == APPLICATION_ADDRESS), b"")
        if chip_family_from_image(application) != family:
            raise RuntimeError("Source application image does not match its declared chip family.")
        return family, parts, sanitize_clone_configuration(settings)

    def _prepare_file(self, job: dict) -> tuple[str, list[tuple[int, bytes]], None]:
        path = pathlib.Path(str(job["firmware_file"]).strip())
        if not path.is_file() or path.suffix.lower() != ".bin":
            raise ValueError("Choose an existing .bin application image.")
        application = path.read_bytes()
        if len(application) > MAX_APPLICATION_SIZE:
            raise ValueError(f"Application image is larger than the {MAX_APPLICATION_SIZE:#x}-byte OTA slot.")
        family = chip_family_from_image(application)
        bootloader_address = 0x0000 if family in ("esp32s3", "esp32c3") else 0x1000
        asset_base = resource_path(f"assets/{family}")
        bootloader = (asset_base / "bootloader.bin").read_bytes()
        partitions = (asset_base / "partitions.bin").read_bytes()
        return family, [(bootloader_address, bootloader), (0x8000, partitions), (APPLICATION_ADDRESS, application)], None

    def _detect_target_chip(self, port: str) -> tuple[str, str]:
        lines: list[str] = []

        def capture(line: str) -> None:
            lines.append(line)
            self._log(line)

        bridge = EsptoolOutput(capture)
        arguments = ["--chip", "auto", "--port", port, "--baud", "115200", "--before", "default-reset", "--after", "hard-reset", "chip-id"]
        try:
            with self.esptool_lock, contextlib.redirect_stdout(bridge), contextlib.redirect_stderr(bridge):
                esptool.main(arguments)
        except SystemExit as error:
            if error.code not in (None, 0):
                raise RuntimeError(f"Automatic chip detection stopped with code {error.code}.") from error
        finally:
            bridge.flush()
        output = "\n".join(lines)
        return chip_family_from_esptool_output(output), flash_size_from_esptool_output(output)

    def _esptool(self, arguments: list[str], line_handler: Callable[[str], None] | None = None) -> None:
        def handle_line(line: str) -> None:
            self._handle_esptool_line(line)
            if line_handler is not None:
                line_handler(line)

        bridge = EsptoolOutput(handle_line)
        try:
            with self.esptool_lock, contextlib.redirect_stdout(bridge), contextlib.redirect_stderr(bridge):
                esptool.main(arguments)
        except SystemExit as error:
            if error.code not in (None, 0):
                raise RuntimeError(f"Espressif flashing engine stopped with code {error.code}.") from error
        finally:
            bridge.flush()

    def _handle_esptool_line(self, line: str) -> None:
        self._log(line)
        match = re.search(r"\((\d+)\s*%\)", line)
        if match:
            self._emit("status", 25 + int(match.group(1)) * 0.65, "Flashing firmware", line, ORANGE)
        elif "Hash of data verified" in line:
            self._emit("status", 90, "Firmware verified", "Flash hashes verified successfully.", ORANGE)

    def _write_flash(
        self,
        port: str,
        family: str,
        parts: list[tuple[int, bytes]],
        erase: bool,
        line_handler: Callable[[str], None] | None = None,
    ) -> None:
        with tempfile.TemporaryDirectory(prefix="elma-flasher-") as temp_dir:
            paths: list[tuple[int, pathlib.Path]] = []
            for index, (address, data) in enumerate(parts):
                path = pathlib.Path(temp_dir) / f"part-{index}.bin"
                path.write_bytes(data)
                paths.append((address, path))
            ota_path = pathlib.Path(temp_dir) / "ota-data-reset.bin"
            ota_path.write_bytes(bytes([0xFF]) * OTA_DATA_SIZE)
            paths.insert(max(0, len(paths) - 1), (OTA_DATA_ADDRESS, ota_path))
            common = ["--chip", family, "--port", port, "--baud", str(FLASH_BAUD), "--before", "default-reset"]
            if erase:
                self._emit("status", 18, "Erasing target flash", "Performing the requested full-chip erase.", ORANGE)
                self._esptool(common + ["--after", "no-reset", "erase-flash"], line_handler)
            self._emit("status", 25, "Flashing firmware", "Writing bootloader, partitions, OTA selection and application.", ORANGE)
            write_arguments = common + ["--after", "hard-reset", "write-flash", "--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "detect"]
            for address, path in paths:
                write_arguments.extend([f"0x{address:X}", str(path)])
            self._esptool(write_arguments, line_handler)

    def _provision(self, port: str, configuration: dict, expect_wifi_ip: bool) -> str:
        payload = json.dumps(configuration, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        checksum = crc32(payload)
        header = f"ELMA_CLONE_CONFIG {len(payload)} {checksum:08X}\n".encode()
        deadline = time.monotonic() + PROVISION_TIMEOUT_SECONDS
        header_sent = False
        sent = False
        applied = False
        self._emit("status", 93, "Waiting for target firmware", "Opening the target's 115200-baud provisioning channel.", ORANGE)
        while time.monotonic() < deadline:
            try:
                with serial.Serial(port, CONSOLE_BAUD, timeout=1, write_timeout=5) as connection:
                    connection.reset_input_buffer()
                    connection.write(b"ELMA_CLONE_PING\n")
                    connection.flush()
                    while time.monotonic() < deadline:
                        raw = connection.readline()
                        if not raw:
                            connection.write(b"ELMA_CLONE_PING\n")
                            connection.flush()
                            continue
                        line = raw.decode("utf-8", "replace").strip()
                        if not line:
                            continue
                        self._log(line)
                        if not header_sent and "[clone] provisioning ready" in line:
                            self._emit("status", 94, "Copying configuration", "Sending Wi-Fi, MQTT, GPIO and peripheral settings.", ORANGE)
                            connection.write(header)
                            connection.flush()
                            header_sent = True
                        if header_sent and not sent and "[clone] receiving configuration bytes=" in line:
                            # Wait for the target to allocate and acknowledge its
                            # receive buffer, then pace chunks so USB CDC/UART
                            # buffers cannot drop bytes from a large designer
                            # configuration and silently replace them with later
                            # ping traffic before the declared length is reached.
                            for offset in range(0, len(payload), 128):
                                connection.write(payload[offset:offset + 128])
                                connection.flush()
                                time.sleep(0.005)
                            sent = True
                        if "[clone] error=" in line:
                            raise RuntimeError(line.split("error=", 1)[1])
                        if "[clone] configuration applied" in line:
                            applied = True
                            if not expect_wifi_ip:
                                return ""
                            self._emit("status", 97, "Configuration saved", "Target identity regenerated; waiting for Wi-Fi.", ORANGE)
                        match = re.search(r"\[wifi\].*station connected.*\bip=(\d{1,3}(?:\.\d{1,3}){3})", line, re.I)
                        if applied and match:
                            return match.group(1)
            except (serial.SerialException, OSError) as error:
                self._log(f"Serial reconnect: {error}")
                time.sleep(1)
        if not header_sent:
            raise RuntimeError("The new firmware booted, but its serial provisioning channel did not become ready.")
        if not sent:
            raise RuntimeError("The target accepted the provisioning header but did not request the configuration payload.")
        if not applied:
            raise RuntimeError("The target did not confirm that cloned configuration was saved.")
        raise RuntimeError("Configuration was cloned, but the target did not report a Wi-Fi IP address within two minutes.")

    def _flash_worker(self, port: str, job: dict) -> None:
        try:
            self._log(f"ELMA Flasher v{APP_VERSION}")
            self._log(f"Target port: {port}")
            if job["mode"] == "clone":
                family, parts, configuration = self._prepare_clone(job)
            else:
                family, parts, configuration = self._prepare_file(job)
            self._emit("firmware_family", family)
            self._log(f"Image target: {CHIP_FAMILIES[family]}")
            overrides = job.get("preconfiguration", {})
            if overrides:
                configuration = merge_configuration(configuration or {}, overrides)
                self._log("Optional target preconfiguration will be applied after flashing.")
            requested_family = str(job.get("chip_choice", "auto"))
            if requested_family != "auto" and requested_family != family:
                raise RuntimeError(
                    f"Manual target selection {CHIP_FAMILIES.get(requested_family, requested_family)} is incompatible with "
                    f"this {CHIP_FAMILIES[family]} firmware. Choose Auto-detect or {CHIP_FAMILIES[family]}."
                )
            self._emit("status", 14, "Detecting target chip", f"Checking the ESP connected on {port} before any erase or write.", ORANGE)
            try:
                detected_family, detected_flash_size = self._detect_target_chip(port)
                self._emit("chip_detected", detected_family, detected_flash_size)
            except BaseException:
                if requested_family == "auto":
                    raise
                detected_family = requested_family
                detected_flash_size = ""
                self._log(f"Automatic detection unavailable; using manual {CHIP_FAMILIES[requested_family]} override. Espressif's loader will still verify the chip before writing.")
            if detected_family != family:
                detected_label = KNOWN_CHIP_MODELS.get(detected_family, detected_family.upper())
                flash_detail = f" with {detected_flash_size} flash" if detected_flash_size else ""
                raise RuntimeError(
                    f"Connected target reports {detected_label}{flash_detail}, but the selected firmware is compiled for {CHIP_FAMILIES[family]}. "
                    "Flash capacity does not make different ESP chip architectures compatible. Nothing was erased or written."
                )
            self._write_flash(port, family, parts, bool(job["erase"]))
            if configuration is not None:
                wifi = configuration.get("wifi", {}) if isinstance(configuration, dict) else {}
                expect_wifi_ip = isinstance(wifi, dict) and bool(str(wifi.get("ssid", "")).strip())
                ip_address = self._provision(port, configuration, expect_wifi_ip)
                operation = "Clone" if job["mode"] == "clone" else "Flash and configuration"
                detail = f"The target connected using its own hardware identity at {ip_address}." if ip_address else "Configuration saved; the target retained its own hardware identity."
                message = f"{operation} completed successfully."
                if ip_address:
                    message += f"\n\nNew device IP: {ip_address}"
                self._emit("status", 100, f"{operation} complete", detail, GREEN)
                self._emit("complete", ip_address, message)
            else:
                self._emit("status", 100, "Flash complete", "Firmware was written, verified and the target was reset.", GREEN)
                self._emit("complete", "", "Firmware flash completed successfully.")
        except BaseException as error:
            message = friendly_error(error)
            self._log(f"ERROR: {message}")
            self._emit("status", self.progress, "Flashing failed", message, RED)
            self._emit("error", message)

    def open_target(self) -> None:
        if self.last_ip:
            webbrowser.open(f"http://{self.last_ip}/")

    def open_designer(self) -> None:
        """Switch to the bundled native Designer view without launching a browser."""
        try:
            url = self.designer_server.start()
            self._log("Opening the bundled ELMA Device Designer window")
            self.root.withdraw()
            run_native_designer_window(url, self.window_icon_path, self.designer_server)
        except BaseException as error:
            messagebox.showerror("ELMA Device Designer", friendly_error(error))
        finally:
            self.root.deiconify()
            self.root.lift()
            self.root.focus_force()

    def close(self) -> None:
        self.designer_server.stop()
        self.root.destroy()


def self_test() -> int:
    required = [
        resource_path("assets/ELMA-Flasher.ico"),
        resource_path("assets/esp32/bootloader.bin"),
        resource_path("assets/esp32/partitions.bin"),
        resource_path("assets/esp32s3/bootloader.bin"),
        resource_path("assets/esp32s3/partitions.bin"),
        resource_path("assets/esp32c3/bootloader.bin"),
        resource_path("assets/esp32c3/partitions.bin"),
    ]
    if getattr(sys, "frozen", False):
        required.extend([
            resource_path("ELMA-Compiler-Core.exe"),
            resource_path("web/index.html"),
            resource_path("web/modules/local-builder.js"),
            resource_path("web/modules/device-migration-tab.js"),
            resource_path("builder_project/platformio.ini"),
            resource_path("builder_project/src/generated_web_assets.cpp"),
        ])
    missing = [str(path) for path in required if not path.is_file() or path.stat().st_size == 0]
    if missing:
        return 2
    if not getattr(esptool, "main", None) or not getattr(serial, "Serial", None):
        return 3
    esp32_header = bytearray(24)
    esp32_header[0] = 0xE9
    esp32s3_header = bytearray(esp32_header)
    esp32s3_header[12] = 0x09
    esp32c3_header = bytearray(esp32_header)
    esp32c3_header[12] = 0x05
    if chip_family_from_image(esp32_header) != "esp32" or chip_family_from_image(esp32s3_header) != "esp32s3" or chip_family_from_image(esp32c3_header) != "esp32c3":
        return 5
    if chip_family_from_esptool_output("Chip is ESP32-S3") != "esp32s3" or chip_family_from_esptool_output("Chip is ESP32") != "esp32":
        return 5
    c3_output = "Chip type: ESP32-C3 (revision v0.4)\nFeatures: Wi-Fi, Embedded Flash 4MB (XMC)"
    if chip_family_from_esptool_output(c3_output) != "esp32c3" or flash_size_from_esptool_output(c3_output) != "4 MB":
        return 5
    clone = sanitize_clone_configuration({
        "device": {"deviceName": "source", "friendlyName": "Source"},
        "mqtt": {"host": "broker", "clientId": "source", "baseTopic": "source/topic"},
        "wifi": {"ssid": "mesh", "useStaticIp": True, "staticIp": "192.168.1.40"},
    })
    overrides = FlasherApplication._preconfiguration_overrides({
        "friendly_name": "Kitchen", "wifi_ssid": "mesh", "wifi_password": "secret", "use_static_ip": True,
        "static_ip": "192.168.1.42", "gateway": "192.168.1.1", "subnet": "255.255.255.0", "dns1": "", "dns2": "",
        "mqtt_host": "broker", "mqtt_port": "1883", "mqtt_username": "", "mqtt_password": "", "mqtt_discovery": "enabled",
        "ap_ssid": "ELMA-Setup", "ap_password": "12345678", "ap_fallback": "enabled",
        "web_auth": "default", "web_username": "", "web_password": "",
    })
    configured = merge_configuration(clone, overrides)
    if configured["device"].get("friendlyName") != "Kitchen" or "deviceName" in configured["device"]:
        return 6
    if configured["wifi"].get("staticIp") != "192.168.1.42" or "clientId" in configured["mqtt"] or "baseTopic" in configured["mqtt"]:
        return 6
    return 0


def main() -> int:
    if "--android-build-server" in sys.argv:
        from android_build_server import run_window
        return run_window(DesignerServer, BOARD_PROFILES, sanitize_clone_configuration)
    configure_windows_identity()
    if "--designer-only" in sys.argv:
        server = DesignerServer(None)  # Native flash is unavailable only in this test/server mode.
        print(server.start(), flush=True)
        try:
            assert server.httpd is not None
            server.thread.join()
        except KeyboardInterrupt:
            server.httpd.shutdown()
        return 0
    if "--self-test" in sys.argv:
        return self_test()
    if "--board-gpio-smoke-test" in sys.argv:
        from board_gpio_smoke_test import run_board_gpio_smoke_test
        return run_board_gpio_smoke_test(DesignerServer(None))
    if "--ui-smoke-test" in sys.argv:
        result = self_test()
        if result:
            return result
        root = tk.Tk()
        root.attributes("-alpha", 0.0)
        app = FlasherApplication(root, auto_detect_chip=False)
        root.update()
        button_bottom = app.flash_button.winfo_rooty() + app.flash_button.winfo_height()
        window_bottom = root.winfo_rooty() + root.winfo_height()
        icon_ok = app.window_icon_path.is_file() and app.window_icon_applied and window_has_windows_icon(root)
        app._set_selected_image_family("esp32")
        compatibility_ok = self_test() == 0 and app.chip_menu.entrycget(1, "state") == "normal" and app.chip_menu.entrycget(2, "state") == "disabled" and app.chip_menu.entrycget(3, "state") == "disabled"
        designer_ok = False
        try:
            designer_url = app.designer_server.start()
            with urllib.request.urlopen(f"{designer_url}api/builder/status", timeout=3) as response:
                designer_status = json.loads(response.read().decode("utf-8"))
            designer_ok = designer_status.get("active") is True and designer_status.get("version") == APP_VERSION
        finally:
            app.designer_server.stop()
        layout_ok = app.flash_button.winfo_ismapped() and button_bottom <= window_bottom
        root.destroy()
        return 0 if layout_ok and icon_ok and compatibility_ok and designer_ok else 4
    if "--native-designer-smoke-test" in sys.argv:
        result = self_test()
        if result:
            return result
        root = tk.Tk()
        root.withdraw()
        app = FlasherApplication(root, auto_detect_chip=False)
        root.withdraw()
        try:
            designer_url = app.designer_server.start()
            native_designer_ok = run_native_designer_window(
                designer_url,
                app.window_icon_path,
                app.designer_server,
                smoke_test=True,
            )
        finally:
            app.designer_server.stop()
            root.destroy()
        return 0 if native_designer_ok else 7
    if "--portable-builder-compile-test" in sys.argv:
        if not getattr(sys, "frozen", False):
            return 8
        server = DesignerServer(None)
        project = server.project_root()
        if (project / "tools").exists() or (project / "README.md").exists():
            return 8
        compiler_environment = os.environ.copy()
        compiler_environment["ELMA_PORTABLE_BUILDER"] = "1"
        creation_flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        profiles = (
            ("esp32c3_designer_hacs", "esp32c3", "esp32-c3", True, None),
            ("esp32s3_notifier_hacs", "esp32s3", "esp32-s3-super-mini", True, None),
            ("esp32_notifier_hacs", "esp32", "esp32-wroom", True, None),
            ("esp32_notifier_hacs_legacy_ota", "esp32", "wemos-lolin32-mini", False, 0x190000),
        )
        for profile, expected_family, board_profile, expects_board_asset, maximum_size in profiles:
            board_id, _ = BOARD_PROFILES[board_profile]
            compiler_environment["ELMA_SELECTED_BOARD_PROFILE"] = board_profile
            compiler_environment["ELMA_SELECTED_BOARD_PROFILE_ID"] = str(board_id)
            command = server.compiler_command() + [
                "run", "--project-dir", str(project), "--environment", profile,
            ]
            completed = subprocess.run(
                command,
                cwd=project,
                env=compiler_environment,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                creationflags=creation_flags,
                timeout=600,
                check=False,
            )
            firmware = project / ".pio" / "build" / profile / "firmware.bin"
            if completed.returncode or not firmware.is_file():
                return 9
            try:
                firmware_bytes = firmware.read_bytes()
                if chip_family_from_image(firmware_bytes[:24]) != expected_family:
                    return 9
                if maximum_size is not None and len(firmware_bytes) > maximum_size:
                    return 9
                selected_asset = BOARD_ASSET_FILES[board_profile].encode()
                if expects_board_asset and selected_asset not in firmware_bytes:
                    return 9
                if expects_board_asset and any(
                    asset.encode() in firmware_bytes
                    for profile_name, asset in BOARD_ASSET_FILES.items()
                    if profile_name != board_profile
                ):
                    return 9
            except (OSError, ValueError):
                return 9
        return 0
    # Start directly in the unified native window. A hidden controller supplies
    # the proven serial detection, erase, flash and provisioning implementation
    # to the loopback-only Designer backend.
    root = tk.Tk()
    root.withdraw()
    app = FlasherApplication(root)
    root.withdraw()
    try:
        designer_url = app.designer_server.start()
        run_native_designer_window(designer_url, app.window_icon_path, app.designer_server)
    finally:
        app.designer_server.stop()
        root.destroy()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
