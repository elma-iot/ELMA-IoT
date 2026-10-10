"""Generate native-object archives for the Android standalone compiler plan."""
import os
Import("env")
if os.environ.get("ELMA_ANDROID_COMPILER_PLAN") == "1":
    env.Append(BUILD_UNFLAGS=["-flto"], BUILD_FLAGS=["-fno-lto"])
