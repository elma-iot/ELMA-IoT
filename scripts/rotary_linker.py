"""Keep the RGB restart interrupt's GDMA helpers in internal executable RAM.

Only rotary profiles load this script. Never modify the shared SDK package.
Arduino 3.1.3's linker places these helpers in flash although RGB restart calls
them from its IRAM interrupt during NVS/filesystem writes.
"""
from pathlib import Path
import os
Import("env")

# Android replays standalone cc1/as/ld commands through its bundled runtime.
# Its dependency archives must contain native objects, not host LTO bytecode.
if os.environ.get('ELMA_ANDROID_COMPILER_PLAN') == '1':
    env.Append(CCFLAGS=['-fno-lto'], LINKFLAGS=['-fno-lto'])

sdk = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"))
source = sdk / "esp32s3" / "qio_opi" / "sections.ld"
text = source.read_text(encoding="utf-8")
anchor = "    *(.iram1 .iram1.*)"
if text.count(anchor) != 1:
    raise RuntimeError("Rotary linker: unexpected SDK IRAM layout")
text = text.replace(anchor, anchor + "\n"
    "    *libhal.a:gdma_hal_top.*(.literal .literal.* .text .text.*)\n"
    "    *libhal.a:gdma_hal_ahb_v1.*(.literal .literal.* .text .text.*)")
destination = Path(env.subst("$BUILD_DIR")) / "rotary_sections.ld"
destination.parent.mkdir(parents=True, exist_ok=True)
if not destination.is_file() or destination.read_text(encoding="utf-8") != text:
    destination.write_text(text, encoding="utf-8")
flags = list(env["LINKFLAGS"])
replaced = False
for index, flag in enumerate(flags):
    if str(flag) == "sections.ld":
        flags[index] = str(destination)
        replaced = True
    elif str(flag) == "-Tsections.ld":
        flags[index] = "-T" + str(destination)
        replaced = True
if not replaced:
    raise RuntimeError("Rotary linker: sections.ld link option not found")
env.Replace(LINKFLAGS=flags)
env.Depends("$BUILD_DIR/${PROGNAME}.elf", str(destination))
