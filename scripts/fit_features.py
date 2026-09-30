"""Apply only the size-fit's explicitly approved unused driver exclusions."""
import json
import os


def apply(env):
    excluded = json.loads(os.environ.get("ELMA_EXCLUDED_FEATURES", "[]"))
    if not isinstance(excluded, list) or any(x not in ("audio", "display", "sd") for x in excluded):
        raise ValueError("Invalid unused-feature build policy")
    libraries = []
    for feature in excluded:
        env.Append(CPPDEFINES=[("APP_DISABLE_" + feature.upper(), 1)])
        libraries += {
            "audio": ["ESP32-audioI2S"],
            "display": ["Adafruit GFX Library", "Adafruit SSD1306", "Adafruit SH110X"],
            "sd": ["SD", "SD_MMC"],
        }[feature]
    if libraries:
        config = env.GetProjectConfig()
        ignored = list(env.GetProjectOption("lib_ignore", []))
        config.set("env:" + env["PIOENV"], "lib_ignore", ignored + libraries)
        print("[size-fit] Excluded unused drivers/libraries: " + ", ".join(libraries))
