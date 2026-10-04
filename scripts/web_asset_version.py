"""Detect stale portable UI bundles without requiring Node on end-user PCs."""
import hashlib
from pathlib import Path

PREFIX = '// ELMA_WEB_SOURCE_SHA256: '

def source_digest(root):
    root = Path(root)
    paths = [p for p in (root / 'web').rglob('*') if p.is_file()
             and 'esp8266' not in p.relative_to(root / 'web').parts
             and p.name not in ('firmware-i18n.js', 'desktop-locales.json')]
    paths += [root / 'package-lock.json']
    paths += [root / 'scripts' / name for name in (
        'asset_embed.py', 'web_asset_version.py', 'build_board_web.mjs',
        'build_ui_locales.mjs', 'firmware_i18n_runtime.mjs',
        'mainboard-svg-manifest.json', 'runtime-board-catalog.json')]
    digest = hashlib.sha256()
    for path in sorted(paths):
        if path.is_file():
            digest.update(path.relative_to(root).as_posix().encode())
            digest.update(b'\0')
            digest.update(path.read_bytes())
    return digest.hexdigest()

def matches(source, digest):
    with Path(source).open(encoding='utf-8') as stream:
        return stream.readline().strip() == PREFIX + digest

if __name__ == '__main__':
    import sys
    root = Path(sys.argv[1]).resolve()
    source = root / 'src/generated_web_assets.cpp'
    if not source.exists() or not matches(source, source_digest(root)):
        raise SystemExit('Embedded web assets are stale. Run a firmware build with Node.js available before packaging the Windows application.')
    print('Embedded web assets match the current interface source.')
