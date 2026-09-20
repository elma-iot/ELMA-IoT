"""Make every inactive peripheral route share one tiny fallback SVG."""
from pathlib import Path
import gzip
import re

from compact_peripheral_assets import default_svg

BEGIN = "// ELMA_SHARED_PERIPHERAL_FALLBACK_BEGIN"
END = "// ELMA_SHARED_PERIPHERAL_FALLBACK_END"


def _array(data):
    return ",\n    ".join(", ".join(f"0x{byte:02x}" for byte in data[offset:offset + 16]) for offset in range(0, len(data), 16))


def install_shared_fallback(source_path, manifest):
    source_path = Path(source_path)
    source = source_path.read_text(encoding="utf-8")
    payload = gzip.compress(default_svg().encode("utf-8"), compresslevel=9, mtime=0)
    fallback = (
        f"{BEGIN}\nconst uint8_t elma_default_peripheral_svg[] PROGMEM = {{\n"
        f"    {_array(payload)}\n}};\n"
        "const size_t elma_default_peripheral_svg_len = sizeof(elma_default_peripheral_svg);\n"
        f"{END}\n"
    )
    if BEGIN in source:
        updated=re.sub(re.escape(BEGIN)+r"[\s\S]*?"+re.escape(END)+r"\r?\n?",fallback,source,count=1)
        if updated!=source:source_path.write_text(updated,encoding="utf-8")
        return updated!=source
    # Current full generators already emit the shared fallback and guarded
    # routes directly. Add the maintenance markers once so portable builds can
    # refresh the tiny payload without trying to migrate the old per-route form.
    direct_definition=re.compile(
        r'const uint8_t elma_default_peripheral_svg\[\] PROGMEM = \{[\s\S]*?\n\};\r?\n'
        r'const size_t elma_default_peripheral_svg_len = sizeof\(elma_default_peripheral_svg\);\r?\n?'
    )
    routes_ready=all(
        f'{{"/{asset_path}", "image/svg+xml", elma_default_peripheral_svg, elma_default_peripheral_svg_len, true}}' in source
        for asset_path in manifest
    )
    if direct_definition.search(source) and routes_ready:
        source=direct_definition.sub(fallback,source,count=1)
        source_path.write_text(source,encoding="utf-8")
        return True
    source, count = re.subn(r'(#include "generated_web_assets\.h"\r?\n)', lambda match: match.group(1) + fallback, source, count=1)
    if count != 1:
        raise RuntimeError("Generated web asset include is missing")
    paths = sorted(manifest)
    for index, asset_path in enumerate(paths):
        route_pattern = re.compile(rf'^\s*\{{"/{re.escape(asset_path)}", "image/svg\+xml", (\w+), (\w+), true\}},\s*$', re.M)
        route_match = route_pattern.search(source)
        if not route_match:
            raise RuntimeError(f"Peripheral route is missing: {asset_path}")
        symbol, length_symbol = route_match.groups()
        bit = 1 << index
        definition_pattern = re.compile(
            rf'#if APP_PERIPHERAL_SVG_MASK & {bit}\r?\n'
            rf'(const uint8_t {symbol}\[\] PROGMEM = \{{[\s\S]*?\n\}};)\r?\n'
            rf'#else[\s\S]*?#endif\r?\n'
            rf'(const size_t {length_symbol} = sizeof\({symbol}\);)'
        )
        source, definition_count = definition_pattern.subn(
            f"#if APP_PERIPHERAL_SVG_MASK & {bit}\n\\1\n\\2\n#endif", source, count=1
        )
        if definition_count != 1:
            raise RuntimeError(f"Peripheral definition is not in the expected compact form: {asset_path}")
        route = route_match.group(0).strip()
        replacement = (
            f"#if APP_PERIPHERAL_SVG_MASK & {bit}\n    {route}\n#else\n"
            f'    {{"/{asset_path}", "image/svg+xml", elma_default_peripheral_svg, elma_default_peripheral_svg_len, true}},\n#endif'
        )
        source = route_pattern.sub(replacement, source, count=1)
    source_path.write_text(source, encoding="utf-8")
    return True
