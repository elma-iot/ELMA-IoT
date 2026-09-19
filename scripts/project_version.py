"""Validate the single firmware version source against release-facing files."""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path


VERSION_PATTERN = re.compile(r'^#define APP_VERSION "(\d+\.\d+\.\d+)"$', re.MULTILINE)
FLASHER_VERSION_PATTERN = re.compile(r'^APP_VERSION = "(\d+\.\d+\.\d+)"$', re.MULTILINE)
ASSET_PREFIXES = (
    "esp32-notifier",
    "esp32-notifier-hacs",
    "esp32-notifier-hacs-slim",
    "esp32-notifier-hacs-legacy-ota",
    "esp32s3-notifier",
    "esp32s3-notifier-hacs",
    "esp32s3-notifier-hacs-slim",
    "esp32c3-notifier-hacs",
    "esp32-ota-bridge",
    "esp32s3-ota-bridge",
    "esp32c3-ota-bridge",
)


def firmware_version(project_dir: Path) -> str:
    version_header = project_dir / "include" / "version.h"
    match = VERSION_PATTERN.search(version_header.read_text(encoding="utf-8"))
    if not match:
        raise RuntimeError(f"APP_VERSION is missing or malformed in {version_header}")
    return match.group(1)


def validate(project_dir: Path, expected_tag: str = "", validate_release_metadata: bool = True) -> str:
    version = firmware_version(project_dir)
    tag = f"v{version}"
    errors: list[str] = []
    if expected_tag and expected_tag != tag:
        errors.append(f"release tag {expected_tag!r} does not match firmware APP_VERSION {tag!r}")

    if not validate_release_metadata:
        return version

    flasher_path = project_dir / "tools" / "elma_flasher" / "elma_flasher.py"
    if not flasher_path.is_file():
        flasher_path = project_dir.parent / "Windows" / "elma_flasher.py"
    flasher_source = flasher_path.read_text(encoding="utf-8")
    flasher_match = FLASHER_VERSION_PATTERN.search(flasher_source)
    if not flasher_match or flasher_match.group(1) != version:
        errors.append("ELMA Flasher APP_VERSION is missing or does not match firmware APP_VERSION")

    desktop_version=None
    metadata=project_dir.parent/"Windows"/"app_metadata.py"
    if metadata.is_file():
        text=metadata.read_text(encoding="utf-8")
        desktop=FLASHER_VERSION_PATTERN.search(text)
        payload=re.search(r'^FIRMWARE_VERSION = "(\d+\.\d+\.\d+)"$',text,re.MULTILINE)
        if desktop:desktop_version=desktop.group(1)
        if not payload or payload.group(1)!=version:errors.append("Windows bundled firmware version does not match firmware source")
    readme = (project_dir / "README.md").read_text(encoding="utf-8")
    required_readme_lines = (
        f"- Firmware version: `{tag}`",
        f"- Default ESP32-S3 HACS asset: `esp32s3-notifier-hacs-{tag}.bin`",
        f"Release asset names for `{tag}`:",
        f"- [release-assets/{tag}/release-notes.md](release-assets/{tag}/release-notes.md)",
    )
    for line in required_readme_lines:
        if line not in readme:
            errors.append(f"README is not synchronized; missing: {line}")

    notes_path = project_dir / "release-assets" / tag / "release-notes.md"
    if not notes_path.is_file():
        errors.append(f"missing current release notes: {notes_path}")
    else:
        notes = notes_path.read_text(encoding="utf-8")
        for prefix in ASSET_PREFIXES:
            asset = f"{prefix}-{tag}.bin"
            if asset not in notes:
                errors.append(f"release notes do not list {asset}")
        # The public firmware checkout intentionally has no private Windows sibling
        # in CI. Validate its independently versioned asset only when that metadata
        # is actually available in the local multi-repository workspace.
        if desktop_version:
            flasher_asset = f"ELMA-Flasher-v{desktop_version}.exe"
            if flasher_asset not in notes:
                errors.append(f"release notes do not list {flasher_asset}")

    if errors:
        raise RuntimeError("Project version validation failed:\n- " + "\n- ".join(errors))
    return version


def cli() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-dir", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--tag", default="", help="Release tag that must match APP_VERSION (for example v0.1.27)")
    parser.add_argument("--print", action="store_true", dest="print_version")
    args = parser.parse_args()
    portable_builder = os.environ.get("ELMA_PORTABLE_BUILDER") == "1"
    try:
        version = validate(args.project_dir.resolve(), args.tag, validate_release_metadata=not portable_builder)
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    if args.print_version:
        print(version)
    else:
        print(f"Project version v{version} is synchronized.")
    return 0


try:
    Import("env")  # type: ignore[name-defined]  # PlatformIO/SCons injects Import.
except NameError:
    if __name__ == "__main__":
        raise SystemExit(cli())
else:
    validate(  # type: ignore[name-defined]
        Path(env.subst("$PROJECT_DIR")).resolve(),
        validate_release_metadata=os.environ.get("ELMA_PORTABLE_BUILDER") != "1",
    )
