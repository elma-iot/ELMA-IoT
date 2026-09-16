"""Preflight GPIO checks shared by USB, IP and Compile/Save jobs. No mutation."""
import json


def valid_pins(chip, output=False, board=""):
    pins = {
        "esp32": set(range(6)) | set(range(12, 20)) | {21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39},
        "esp32s3": set(range(22)) | set(range(33, 49)),
        "esp32c3": set(range(11)) | {20, 21},
    }.get(chip, set())
    if output:
        pins -= set(range(34, 40)) if chip == "esp32" else ({46} if chip == "esp32s3" else set())
    if board == "esp32-wrover":
        pins -= {16, 17}
    if board in ("esp32-s3-psram", "esp32-spk-n16r8", "esp32-s3-devkit-c1", "esp32-s3-cam-module"):
        pins -= set(range(33, 38))
    return pins


def validate_gpio_settings(settings, chip, board):
    issues = []

    def check(label, value, output=True, disabled=None):
        if value is None or value == "" or value == disabled:
            return
        try:
            pin = int(value)
            if float(value) != pin or pin not in valid_pins(chip, output, board):
                raise ValueError()
        except (TypeError, ValueError, OverflowError):
            issues.append(f"{label}: GPIO{value} is invalid/reserved for {board}")

    check("Status LED", settings.get("device", {}).get("statusLedPin"))
    audio = settings.get("audio", {})
    if audio.get("enabled", True) and chip != "esp32c3":
        audio_pins = [audio.get(key) for key in ("bclkPin", "wsPin", "doutPin")]
        for key in ("bclkPin", "wsPin", "doutPin"):
            check(f"Audio {key}", audio.get(key))
        assigned = [pin for pin in audio_pins if pin is not None]
        if len(set(assigned)) != len(assigned):
            issues.append("Audio BCLK, WS and DIN must use different GPIOs")
    oled = settings.get("oled", {})
    if oled.get("enabled", False):
        for key in (("wapeTriggerPin",) if oled.get("displayType") == "wape" else ("sdaPin", "sclPin", "resetPin")):
            check(f"Display {key}", oled.get(key), disabled=-1 if key == "resetPin" else None)
    sd = settings.get("sd", {})
    if sd.get("enabled", False):
        for key in ("csPin", "sckPin", "mosiPin", "misoPin"):
            check(f"SD {key}", sd.get(key), output=key != "misoPin")
    check("Battery ADC", settings.get("battery", {}).get("adcPin"), output=False, disabled=0)
    bindings = settings.get("ui", {}).get("peripheralHelperBindings", {})
    if isinstance(bindings, str):
        try:
            bindings = json.loads(bindings or "{}")
        except ValueError:
            raise ValueError("Invalid peripheral GPIO configuration JSON")
    if isinstance(bindings, dict):
        profiles = settings.get("ui", {}).get("peripheralProfiles", {})
        if isinstance(profiles, str):
            try:
                profiles = json.loads(profiles)
            except ValueError:
                profiles = {}
        profile_keys = {"audio": "audioProfiles", "audioIn": "audioInProfiles", "display": "displayProfiles", "sensor": "sensors", "input": "inputs", "control": "controls", "expansion": "expansions", "storage": "storage", "communication": "communication", "power": "power"}
        for slot, signals in bindings.items():
            if not isinstance(signals, dict):
                continue
            group, _, index = slot.partition(":")
            selected = profiles.get(profile_keys.get(group, "")) if isinstance(profiles, dict) else None
            if isinstance(selected, list) and index.isdigit() and (int(index) >= len(selected) or selected[int(index)] == "none"):
                continue  # Removed peripherals may retain bindings for later reuse.
            for signal, value in signals.items():
                if signal.upper() in ("MAIN_CONTROL", "CONTACT", "SOURCE", "INPUT_VOLTAGE", "OUTPUT_VOLTAGE"):
                    continue
                check(f"{slot} {signal}", value, output=not slot.startswith(("input:", "sensor:", "audioIn:")))
    if issues:
        raise ValueError("Correct GPIO assignments before compiling: " + "; ".join(issues))
