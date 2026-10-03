"""Apply only the size-fit's explicitly approved unused driver exclusions."""
import json
import os
import re


def apply(env):
    excluded = json.loads(os.environ.get("ELMA_EXCLUDED_FEATURES", "[]"))
    if not isinstance(excluded, list) or any(x not in ("audio", "display", "sd") for x in excluded):
        raise ValueError("Invalid unused-feature build policy")
    flags = env.GetProjectOption("build_flags", [])
    if isinstance(flags, str):
        flags = [flags]
    # Source profiles (including C3 and *_noaudio) can already disable audio
    # without a size-fit retry. Do not compile an explicitly listed decoder
    # merely because lib_deps is inherited from the full profile.
    audio_disabled = "audio" in excluded or any(
        re.search(r"(?:^|\s)-D\s*APP_DISABLE_AUDIO(?:[=\s]|$)", flag)
        for flag in flags
    )
    libraries = ["ESP32-audioI2S", "ESP32-audioI2S-master"] if audio_disabled else []
    for feature in excluded:
        env.Append(CPPDEFINES=[("APP_DISABLE_" + feature.upper(), 1)])
        libraries += {
            # The pinned archive's library.properties uses the -master name;
            # registry/git installations can use the name without that suffix.
            "audio": [],  # Both installation names are handled above.
            "display": ["Adafruit GFX Library", "Adafruit SSD1306", "Adafruit SH110X"],
            # Audio.h includes both headers unconditionally. Disable our SD
            # backend independently, but prune its libraries only when audio
            # is excluded too. Keep transitive dependencies for active audio.
            "sd": ["SD", "SD_MMC"] if audio_disabled else [],
        }[feature]
    if "sd" in excluded and not audio_disabled:
        print("[size-fit] SD backend disabled; retaining SD/SD_MMC dependencies required by audio")
    if libraries:
        config = env.GetProjectConfig()
        ignored = list(env.GetProjectOption("lib_ignore", []))
        config.set("env:" + env["PIOENV"], "lib_ignore", ignored + libraries)
        print("[size-fit] Excluded unused drivers/libraries: " + ", ".join(libraries))
