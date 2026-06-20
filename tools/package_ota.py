#!/usr/bin/env python3
"""Build and package FollowBox firmware for cloud OTA publication."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE_DIR = ROOT / "firmware"
CLOUD_FIRMWARE_DIR = ROOT / "cloud" / "firmware"
DEFAULT_ENV = "esp32-s3-devkitc-1"


def run(cmd: list[str], cwd: Path) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=str(cwd), check=True)


def firmware_version() -> str:
    header = FIRMWARE_DIR / "include" / "config" / "ota_config.h"
    text = header.read_text(encoding="utf-8")
    match = re.search(r'#define\s+FOLLOWBOX_FIRMWARE_VERSION\s+"([^"]+)"', text)
    if not match:
        raise RuntimeError(f"Cannot find FOLLOWBOX_FIRMWARE_VERSION in {header}")
    return match.group(1)


def file_md5(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build firmware.bin and write cloud OTA manifest.json")
    parser.add_argument("--env", default=os.environ.get("FOLLOWBOX_PIO_ENV", DEFAULT_ENV), help="PlatformIO env to build")
    parser.add_argument("--pio", default=os.environ.get("PIO", "pio"), help="PlatformIO executable")
    parser.add_argument("--version", default=os.environ.get("FOLLOWBOX_OTA_VERSION", ""), help="Manifest version")
    parser.add_argument("--notes", default=os.environ.get("FOLLOWBOX_OTA_NOTES", ""), help="Manifest release notes")
    parser.add_argument("--force", action="store_true", help="Set manifest force=true without installing on devices")
    parser.add_argument("--skip-build", action="store_true", help="Package the existing PlatformIO firmware.bin")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.skip_build:
        run([args.pio, "run", "-d", str(FIRMWARE_DIR), "-e", args.env], cwd=ROOT)

    source_bin = FIRMWARE_DIR / ".pio" / "build" / args.env / "firmware.bin"
    if not source_bin.is_file():
        raise FileNotFoundError(f"Missing built firmware binary: {source_bin}")

    CLOUD_FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    target_bin = CLOUD_FIRMWARE_DIR / "firmware.bin"
    shutil.copy2(source_bin, target_bin)

    manifest = {
        "version": args.version or firmware_version(),
        "file": "firmware.bin",
        "md5": file_md5(target_bin),
        "size": target_bin.stat().st_size,
        "force": bool(args.force),
    }
    if args.notes:
        manifest["notes"] = args.notes

    manifest_path = CLOUD_FIRMWARE_DIR / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    reloaded = json.loads(manifest_path.read_text(encoding="utf-8"))
    if reloaded["size"] != target_bin.stat().st_size or reloaded["md5"] != file_md5(target_bin):
        raise RuntimeError("Manifest verification failed: size or md5 does not match firmware.bin")

    print(json.dumps(manifest, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
