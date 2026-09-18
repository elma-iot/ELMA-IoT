from pathlib import Path
from functools import lru_cache
import gzip
import os
import re
import shutil
import subprocess
import sys
import json

Import("env")

ROOT = Path(env["PROJECT_DIR"])
sys.path.insert(0,str(ROOT / "scripts"))
from project_defaults import generate_defaults
defaults_file=os.environ.get("ELMA_PROJECT_DEFAULTS_FILE","")
generate_defaults(ROOT,Path(defaults_file).read_text(encoding="utf-8") if defaults_file else os.environ.get("ELMA_PROJECT_DEFAULTS_JSON","{}"))
WEB_DIR = ROOT / "web"
BUILD_WEB_DIR = ROOT / ".web-build"
TMP_WEB_DIR = ROOT / ".tmp-webbundle"
HEADER = ROOT / "include" / "generated_web_assets.h"
SOURCE = ROOT / "src" / "generated_web_assets.cpp"
SKIPPED_WEB_ASSETS = {
    "esp32-c2-esp8684-breadboard.svg",
    "wemos-s2-mini-breadboard.svg",
    "wemos-d1-mini-lite-breadboard.svg",
    "wemos-d1-mini-esp32-breadboard.svg",
    "esp8266-esp12f-breadboard.svg",
    "esp8266-esp12e-breadboard.svg",
    "esp8266-esp01-breadboard.svg",
    "i18n.js",
    "desktop-preferences.js",
    "desktop-locales.json",
    "favicon.ico",
    # The SVG logo is the canonical favicon. This legacy multi-resolution ICO
    # costs about 87 KiB even after gzip and is not needed by modern browsers.
    "elma_iot_favicon.ico",
    # Unreferenced UI assets still cost flash when embedded.
    "esp32-38pinwide-breadboard.svg",
    "esp32-s3-devkit-c1-n8r8-v1-schematic.svg",
    "microsd-spi-breadboard.svg",
    "ttp223-touch-icon.svg",
}

BOARD_ASSET_IDS = {
    "esp32-s3-supermini-breadboard.svg": 1,
    "esp32-s3-zero-breadboard.svg": 2,
    "esp32-s3-psram-breadboard.svg": 3,
    "esp32-spk-n16r8-breadboard.svg": 4,
    "esp32-s3-devkit-c1-n8r8-v1-breadboard.svg": 5,
    "esp32-s3-cam-module-breadboard.svg": 6,
    "esp32-wrover-breadboard.svg": 7,
    "esp32-wroom-breadboard.svg": 8,
    "esp32-mini-breadboard.svg": 9,
    "wemos-lolin32-mini-breadboard.svg": 10,
    "esp32-c3-breadboard.svg": 11,
    "esp32-s2-mini-breadboard.svg": 12,
    "esp32-c6-mini-breadboard.svg": 13,
}

selected_board_id_text = os.environ.get("ELMA_SELECTED_BOARD_PROFILE_ID", "0").strip()
if not selected_board_id_text.isdigit():
    raise SystemExit("ELMA_SELECTED_BOARD_PROFILE_ID must be a numeric board identifier")
selected_board_id = int(selected_board_id_text)
if selected_board_id and selected_board_id not in BOARD_ASSET_IDS.values():
    raise SystemExit(f"Unknown ELMA selected board identifier: {selected_board_id}")
env.Append(CPPDEFINES=[("APP_COMPILED_BOARD_PROFILE_ID", selected_board_id)])
sys.path.insert(0,str(ROOT / "scripts"))
from compact_peripheral_assets import load_manifest,active_mask,block_svg
peripheral_svg_manifest=load_manifest(ROOT)
peripheral_svg_paths=sorted(peripheral_svg_manifest)
active_profiles_text=os.environ.get("ELMA_ACTIVE_PERIPHERAL_PROFILES")
# No configured peripherals means no detailed illustrations; explicit project masks still win.
active_profiles=json.loads(active_profiles_text) if active_profiles_text is not None else []
peripheral_svg_mask=active_mask(peripheral_svg_manifest,active_profiles)
env.Append(CPPDEFINES=[("APP_PERIPHERAL_SVG_MASK",peripheral_svg_mask)])
if active_profiles is not None:print(f"[web-assets] Detailed peripheral SVGs: {bin(peripheral_svg_mask).count(chr(49))} of {len(peripheral_svg_paths)}; others use blocks with identical I/O")


if env.get("PIOENV") == "esp32_notifier_hacs_legacy_ota":
    env.Append(LINKFLAGS=["-flto"])

language_codes = ["en","es","zh","hi","ar","pt","bn","ru","ja","de","fr","ko","tr","it","id","pl","uk","vi","th","fa"]
language = os.environ.get("ELMA_COMPILED_LANGUAGE", "en")
env.Append(CPPDEFINES=[("APP_COMPILED_LANGUAGE_ID", language_codes.index(language)), ("APP_COMPILED_THEME_ID", {"automatic":0,"light":1,"dark":2}[os.environ.get("ELMA_COMPILED_THEME", "automatic")])])

if os.environ.get("ELMA_PORTABLE_BUILDER") == "1" and HEADER.is_file() and SOURCE.is_file():
    selected_label = os.environ.get("ELMA_SELECTED_BOARD_PROFILE", "all supported boards")
    print(f"[web-assets] portable builder is using the prebundled configurator for {selected_label}")
    Return()


def is_gzip_payload(data: bytes) -> bool:
    return len(data) >= 2 and data[0] == 0x1F and data[1] == 0x8B


def c_array(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        rows.append(", ".join(f"0x{byte:02x}" for byte in chunk))
    return ",\n    ".join(rows)


def minify_html(text: str) -> str:
    text = re.sub(r"<!--(?!\s*\[if).*?-->", "", text, flags=re.S)
    text = re.sub(r">\s+<", "><", text)
    text = re.sub(r"\s{2,}", " ", text)
    return text.strip()


def minify_css(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"\s+", " ", text)
    text = re.sub(r"\s*([{}:;,>])\s*", r"\1", text)
    text = text.replace(";}", "}")
    return text.strip()


def minify_svg(text: str) -> str:
    text = re.sub(r"<\?xml.*?\?>", "", text, flags=re.S)
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    text = re.sub(r"<metadata\b.*?</metadata>", "", text, flags=re.S)
    text = re.sub(r"<sodipodi:namedview\b.*?</sodipodi:namedview>", "", text, flags=re.S)
    text = re.sub(r"\s+xmlns:(?:inkscape|sodipodi)=\"[^\"]*\"", "", text)
    text = re.sub(r"\s+(?:inkscape|sodipodi):[\w.-]+=\"[^\"]*\"", "", text)
    text = re.sub(r">\s+<", "><", text)
    text = re.sub(r"\s{2,}", " ", text)
    return text.strip()


def npx_command() -> list[str]:
    return ["npx.cmd"] if os.name == "nt" else ["npx"]


def tool_command(name: str) -> list[str]:
    node = os.environ.get("ELMA_NODE_EXECUTABLE")
    if node:
        cli = {"esbuild": "esbuild/bin/esbuild", "svgo": "svgo/bin/svgo"}[name]
        return [node, str(ROOT / "node_modules" / cli)]
    return [*npx_command(), "--no-install", name]


@lru_cache(maxsize=1)
def has_svgo() -> bool:
    try:
        completed = subprocess.run(
            [*tool_command("svgo"), "--version"],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError:
        return False
    return completed.returncode == 0


def optimize_svg(path: Path) -> bytes:
    optimized = minify_svg(path.read_text(encoding="utf-8"))
    if not has_svgo():
        return optimized.encode("utf-8")

    relative_path = path.relative_to(WEB_DIR)
    input_path = TMP_WEB_DIR / relative_path
    output_path = input_path.with_suffix(f"{input_path.suffix}.optimized")
    input_path.parent.mkdir(parents=True, exist_ok=True)
    input_path.write_text(optimized, encoding="utf-8")

    svgo_cmd = [
        *tool_command("svgo"),
        "--input",
        str(input_path),
        "--output",
        str(output_path),
        "--multipass",
    ]
    try:
        completed = subprocess.run(
            svgo_cmd,
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError:
        return optimized.encode("utf-8")

    if completed.returncode != 0 or not output_path.exists():
        return optimized.encode("utf-8")
    return output_path.read_bytes()


def build_web_assets() -> None:
    if BUILD_WEB_DIR.exists():
        shutil.rmtree(BUILD_WEB_DIR)
    if TMP_WEB_DIR.exists():
        shutil.rmtree(TMP_WEB_DIR)
    BUILD_WEB_DIR.mkdir(parents=True, exist_ok=True)

    for code in language_codes:
        locale_environment = dict(os.environ, ELMA_COMPILED_LANGUAGE=code)
        subprocess.run([os.environ.get("ELMA_NODE_EXECUTABLE", "node"), str(ROOT / "scripts" / "build_ui_locales.mjs"), str(ROOT), str(BUILD_WEB_DIR)], cwd=ROOT, env=locale_environment, check=True)
        target = BUILD_WEB_DIR / "__locales" / code / "firmware-i18n.js"
        target.parent.mkdir(parents=True, exist_ok=True)
        (BUILD_WEB_DIR / "firmware-i18n.js").replace(target)

    esbuild_cmd = [
        *tool_command("esbuild"),
        str(WEB_DIR / "app.js"),
        "--bundle",
        "--format=esm",
        "--minify",
        f"--outfile={BUILD_WEB_DIR / 'app.js'}",
    ]
    try:
        subprocess.run(esbuild_cmd, cwd=ROOT, check=True)
    except FileNotFoundError as exc:
        raise SystemExit("Node.js and npm are required to build the bundled web UI.") from exc
    except subprocess.CalledProcessError as exc:
        raise SystemExit(
            "Bundling web assets failed. Run `npm install` in the project root to install frontend build dependencies."
        ) from exc

    for path in sorted(WEB_DIR.rglob("*")):
        if not path.is_file():
            continue

        relative_path = path.relative_to(WEB_DIR)
        relative_path_str = relative_path.as_posix()
        if relative_path_str in ("app.js", "firmware-i18n.js") or relative_path_str.startswith(("modules/", "i18n/")) or relative_path_str in SKIPPED_WEB_ASSETS:
            continue

        target_path = BUILD_WEB_DIR / relative_path
        target_path.parent.mkdir(parents=True, exist_ok=True)
        target_path.write_bytes(prepare_payload(path))

    subprocess.run(
        ["node", str(ROOT / "scripts" / "build_board_web.mjs"), str(ROOT), str(BUILD_WEB_DIR)],
        cwd=ROOT, check=True,
    )


def prepare_payload(path: Path) -> bytes:
    raw = path.read_bytes()
    if path.suffix.lower() == ".html":
        return minify_html(raw.decode("utf-8")).encode("utf-8")
    if path.suffix.lower() == ".css":
        return minify_css(raw.decode("utf-8")).encode("utf-8")
    if path.suffix.lower() == ".svg":
        return optimize_svg(path)
    return raw


build_web_assets()
(BUILD_WEB_DIR / "desktop-locales.json").unlink(missing_ok=True)


assets = []
for path in sorted(BUILD_WEB_DIR.rglob("*")):
    if not path.is_file():
        continue
    relative_path = path.relative_to(BUILD_WEB_DIR).as_posix()
    raw = path.read_bytes()
    if path.suffix == ".html":
        raw = raw.replace(b'<script type="module" src="/desktop-preferences.js"></script>', b"")
    gzip_encoded = is_gzip_payload(raw)
    payload = raw if gzip_encoded else gzip.compress(raw, compresslevel=9)
    mime = {
        ".html": "text/html; charset=utf-8",
        ".css": "text/css; charset=utf-8",
        ".js": "application/javascript; charset=utf-8",
        ".svg": "image/svg+xml",
    }.get(path.suffix.lower(), "application/octet-stream")
    symbol_base = re.sub(r"[^0-9a-zA-Z_]+", "_", relative_path)
    symbol = symbol_base.strip("_") or "asset"
    assets.append((relative_path, symbol, mime, payload, gzip_encoded or is_gzip_payload(payload)))

total_embedded_bytes = sum(len(payload) for _, _, _, payload, _ in assets)
print(f"[web-assets] embedded gzip payload: {total_embedded_bytes / 1024:.1f} KiB across {len(assets)} files")
for asset_path, _, _, payload, _ in sorted(assets, key=lambda item: len(item[3]), reverse=True)[:10]:
    print(f"[web-assets] {len(payload) / 1024:7.1f} KiB  {asset_path}")

header_lines = [
    "#pragma once",
    "",
    "#include <Arduino.h>",
    "#include <pgmspace.h>",
    "",
    "struct EmbeddedWebAsset {",
    "    const char* path;",
    "    const char* contentType;",
    "    const uint8_t* data;",
    "    size_t size;",
    "    bool gzip;",
    "};",
    "",
]

source_lines = [
    '#include "generated_web_assets.h"',
    "",
]

def asset_guard(asset_path: str):
    locale = re.match(r"__locales/([^/]+)/", asset_path)
    if locale:
        return f"#if APP_COMPILED_LANGUAGE_ID == {language_codes.index(locale.group(1))}"

    variant = re.match(r"__boards/(\d+)/", asset_path)
    if variant:
        return f"#if APP_COMPILED_BOARD_PROFILE_ID == {variant.group(1)}"
    if asset_path in ("app.js", "index.html"):
        return "#if APP_COMPILED_BOARD_PROFILE_ID == 0"
    board_asset_id = BOARD_ASSET_IDS.get(asset_path)
    if board_asset_id:
        return f"#if APP_COMPILED_BOARD_PROFILE_ID == 0 || APP_COMPILED_BOARD_PROFILE_ID == {board_asset_id}"
    return None


for asset_path, symbol, _, payload, _ in assets:
    guard = asset_guard(asset_path)
    if guard:
        header_lines.append(guard)
        source_lines.append(guard)
    header_lines.append(f"extern const uint8_t {symbol}[];")
    header_lines.append(f"extern const size_t {symbol}_len;")
    compact_asset=asset_path in peripheral_svg_manifest
    if compact_asset:
        source_lines.append(f"#if APP_PERIPHERAL_SVG_MASK & {1 << peripheral_svg_paths.index(asset_path)}")
    source_lines.append(f"const uint8_t {symbol}[] PROGMEM = {{")
    source_lines.append(f"    {c_array(payload)}")
    source_lines.append("};")
    if compact_asset:
        compact_payload=gzip.compress(block_svg(peripheral_svg_manifest[asset_path]).encode("utf-8"),compresslevel=9,mtime=0)
        source_lines.extend(["#else",f"const uint8_t {symbol}[] PROGMEM = {{",f"    {c_array(compact_payload)}","};","#endif"])
    source_lines.append(f"const size_t {symbol}_len = sizeof({symbol});")
    if guard:
        header_lines.append("#endif")
        source_lines.append("#endif")
    source_lines.append("")

header_lines.append("extern const EmbeddedWebAsset WEB_ASSETS[];")
header_lines.append("extern const size_t WEB_ASSET_COUNT;")

source_lines.append("const EmbeddedWebAsset WEB_ASSETS[] = {")
for asset_path, symbol, mime, _, gzip_encoded in assets:
    guard = asset_guard(asset_path)
    if guard:
        source_lines.append(guard)
    route_path = re.sub(r"^(?:__boards/\d+|__locales/[^/]+)/", "", asset_path)
    source_lines.append(
        f'    {{"/{route_path}", "{mime}", {symbol}, {symbol}_len, {str(gzip_encoded).lower()}}},'
    )
    if guard:
        source_lines.append("#endif")
source_lines.append("};")
source_lines.append("const size_t WEB_ASSET_COUNT = sizeof(WEB_ASSETS) / sizeof(WEB_ASSETS[0]);")

HEADER.write_text("\n".join(header_lines) + "\n", encoding="utf-8")
SOURCE.write_text("\n".join(source_lines) + "\n", encoding="utf-8")
