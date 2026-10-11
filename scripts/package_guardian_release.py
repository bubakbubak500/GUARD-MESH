#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate and package local Guardian builds; never flash or publish a release."""

import argparse
from datetime import date
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import tempfile
import zipfile


PROJECT_ROOT = Path(__file__).resolve().parent.parent
APP_OFFSET = 0x10000
SLOT_LIMIT = 0x3E0000
CHIP_ID = 9
CHUNK_SIZE = 1024 * 1024
BOARDS = (
    ("TDeck", "LilyGo T-Deck / T-Deck Plus", "LilyGo_TDeck_companion_radio_touch"),
    ("Heltec-V4-TFT", "Heltec V4 TFT", "heltec_v4_tft_companion_radio_usb_tcp_touch"),
)


def release_tag(value):
    match = re.fullmatch(r"guardian-(\d{4})\.(\d{2})\.(\d{2})(?:\.\d{1,6})?", value)
    if not match:
        raise argparse.ArgumentTypeError("expected guardian-YYYY.MM.DD[.N]")
    try:
        date(*(int(part) for part in match.groups()))
    except ValueError as error:
        raise argparse.ArgumentTypeError("release tag has an invalid date") from error
    return value


def source_commit(value):
    if not re.fullmatch(r"[0-9a-fA-F]{40}", value):
        raise argparse.ArgumentTypeError("source commit must be a full 40-character Git SHA")
    return value.lower()


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(CHUNK_SIZE), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_image_header(header, description):
    if len(header) < 24 or header[0] != 0xE9:
        raise ValueError("%s: missing ESP image header" % description)
    if struct.unpack_from("<H", header, 12)[0] != CHIP_ID:
        raise ValueError("%s: image is not for ESP32-S3 (chip ID 9)" % description)


def inspect_build(build_dir, tag, board, environment):
    app_path = build_dir / "firmware.bin"
    merged_path = build_dir / "firmware-merged.bin"
    elf_path = build_dir / "firmware.elf"
    if not 24 <= app_path.stat().st_size <= SLOT_LIMIT:
        raise ValueError("%s: app size is outside the 0x3E0000 OTA slot" % board)
    app = app_path.read_bytes()
    app_bytes = len(app)
    if not 24 <= app_bytes <= SLOT_LIMIT:
        raise ValueError("%s: app changed size during validation" % board)
    check_image_header(app[:24], str(app_path))
    if tag.encode("ascii") + b"\0" not in app:
        raise ValueError("%s: exact release tag %s is not embedded in the app" % (board, tag))
    merged = merged_path.read_bytes()
    merged_bytes = len(merged)
    if merged_bytes < APP_OFFSET + app_bytes:
        raise ValueError("%s: merged image is too short for the complete app at 0x10000" % board)
    check_image_header(merged[:24], str(merged_path))
    if merged[APP_OFFSET:APP_OFFSET + app_bytes] != app:
        raise ValueError("%s: merged app at 0x10000 differs from firmware.bin" % board)
    with elf_path.open("rb") as elf:
        if elf.read(4) != b"\x7fELF":
            raise ValueError("%s: firmware.elf is not an ELF file" % board)
    return {
        "board": board,
        "environment": environment,
        "app_bytes": app_bytes,
        "slot_limit": SLOT_LIMIT,
        "slot_free": SLOT_LIMIT - app_bytes,
        "app_sha256": hashlib.sha256(app).hexdigest(),
        "merged_bytes": merged_bytes,
        "merged_sha256": hashlib.sha256(merged).hexdigest(),
        "app_offset": APP_OFFSET,
        "image_chip_id": CHIP_ID,
        "embedded_release_tag_verified": True,
        "merged_app_matches": True,
        "elf_bytes": elf_path.stat().st_size,
        "elf_sha256": sha256_file(elf_path),
    }


def copy_checked(source, target, expected_hash):
    with source.open("rb") as original, target.open("xb") as output:
        shutil.copyfileobj(original, output, CHUNK_SIZE)
    if sha256_file(target) != expected_hash:
        raise ValueError("%s changed during packaging; finish all builds before retrying" % source)


def write_json(path, value):
    with path.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(value, output, ensure_ascii=False, indent=2)
        output.write("\n")


def write_elf_zip(source, target, expected_hash):
    # Fixed metadata and streamed contents make repeated packages of the same ELF
    # byte-identical without depending on its filesystem timestamps or permissions.
    info = zipfile.ZipInfo("firmware.elf", date_time=(1980, 1, 1, 0, 0, 0))
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    info.file_size = source.stat().st_size
    digest = hashlib.sha256()
    with zipfile.ZipFile(target, "x") as archive:
        with source.open("rb") as original, archive.open(info, "w") as output:
            for chunk in iter(lambda: original.read(CHUNK_SIZE), b""):
                digest.update(chunk)
                output.write(chunk)
    if digest.hexdigest() != expected_hash:
        raise ValueError("%s changed during packaging; finish all builds before retrying" % source)


def package(args):
    build_dirs = (args.tdeck_build_dir.resolve(), args.heltec_build_dir.resolve())
    boards = [inspect_build(directory, args.tag, board, environment)
              for directory, (_, board, environment) in zip(build_dirs, BOARDS)]
    output_root = args.output_root.resolve()
    destination = output_root / args.tag
    if destination.exists() or destination.is_symlink():
        raise ValueError("refusing to replace existing release directory: %s" % destination)
    output_root.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".%s-" % args.tag, dir=output_root))
    staged_files = []
    try:
        artifacts = []
        for (asset_board, board, _), directory, validation in zip(BOARDS, build_dirs, boards):
            prefix = "Guard-Mesh-%s-%s-" % (asset_board, args.tag)
            app = staging / (prefix + "app-ota.bin")
            merged = staging / (prefix + "merged.bin")
            debug = staging / (prefix + "debug-elf.zip")
            manifest = staging / (prefix + "merged.json")
            staged_files.extend((app, merged, debug, manifest))
            copy_checked(directory / "firmware.bin", app, validation["app_sha256"])
            copy_checked(directory / "firmware-merged.bin", merged, validation["merged_sha256"])
            write_elf_zip(directory / "firmware.elf", debug, validation["elf_sha256"])
            write_json(manifest, {
                "title": "GUARD-MESH %s - %s" % (board, args.tag),
                "board": board,
                "sha256": validation["merged_sha256"],
                "version": args.tag,
                "format": "merged",
                "offset": 0,
                "source_commit": args.source_commit,
            })
            for path in (app, merged, debug, manifest):
                artifacts.append({"name": path.name, "bytes": path.stat().st_size,
                                  "sha256": sha256_file(path)})
        artifacts.sort(key=lambda item: item["name"])
        sums = staging / "SHA256SUMS.txt"
        validation_path = staging / "validation.json"
        staged_files.extend((sums, validation_path))
        with sums.open("x", encoding="utf-8", newline="\n") as output:
            for artifact in artifacts:
                output.write("%s  %s\n" % (artifact["sha256"], artifact["name"]))
        write_json(validation_path, {
            "tag": args.tag,
            "source_commit": args.source_commit,
            "hardware_tested": False,
            "boards": boards,
            "artifacts": artifacts,
        })
        if destination.exists() or destination.is_symlink():
            raise ValueError("release directory appeared during packaging: %s" % destination)
        staging.rename(destination)
        return destination
    finally:
        # Remove only this invocation's known temporary files, never a release
        # directory or the user's build tree; no recursive deletion is needed.
        for path in staged_files:
            path.unlink(missing_ok=True)
        if staging.exists():
            staging.rmdir()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True, type=release_tag,
                        help="numeric release tag, e.g. guardian-2026.10.11")
    parser.add_argument("--source-commit", required=True, type=source_commit,
                        help="full Git commit of the source used for both builds")
    parser.add_argument("--tdeck-build-dir", type=Path,
                        default=PROJECT_ROOT / ".pio/build/LilyGo_TDeck_companion_radio_touch")
    parser.add_argument("--heltec-build-dir", type=Path,
                        default=PROJECT_ROOT / ".pio/build/heltec_v4_tft_companion_radio_usb_tcp_touch")
    parser.add_argument("--output-root", type=Path, default=PROJECT_ROOT / "out/releases",
                        help="parent directory for the new release; existing releases are never replaced")
    args = parser.parse_args()
    try:
        destination = package(args)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, "Packaging failed: %s\n" % error)
    print("Local package created: %s (hardware_tested=false; not published)" % destination)


if __name__ == "__main__":
    main()
