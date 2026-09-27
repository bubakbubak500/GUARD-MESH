# SPDX-License-Identifier: GPL-3.0-or-later
"""Create a T-Deck install image from explicitly selected matching build parts."""
import json
import struct
from pathlib import Path
from .firmware import ImageError, inspect_bytes, read_image, TDECK


def compose(bootloader, partitions, boot_app0, application, destination, title=""):
    sources = [Path(p).resolve() for p in (bootloader, partitions, boot_app0, application)]
    destination = Path(destination).resolve()
    if destination in sources:
        raise ImageError("Výstup nesmí přepsat žádnou ze vstupních částí.")
    parts = [read_image(p) for p in sources]
    for data, maximum, name in zip(parts, (0x8000, 0x1000, 0x2000, 0x3E0000),
                                    ("bootloader", "partitions", "boot_app0", "application")):
        if not data or len(data) > maximum:
            raise ImageError(f"Nesprávná velikost části {name}.")
    if len(parts[2]) != 0x2000:
        raise ImageError("T-Deck boot_app0.bin musí mít 8192 bajtů.")
    expected = ((1, 2, 0x9000, 0x5000), (1, 0, 0xE000, 0x2000),
                (0, 0x10, 0x10000, 0x3E0000), (0, 0x11, 0x3F0000, 0x3E0000),
                (1, 0x83, 0x7D0000, 0x4C0000), (1, 0x82, 0xC90000, 0x360000),
                (1, 3, 0xFF0000, 0x10000))
    if len(parts[1]) < len(expected) * 32 or any(
            struct.unpack_from("<BBII", parts[1], i * 32 + 2) != entry
            for i, entry in enumerate(expected)):
        raise ImageError("Tabulka neodpovídá podporovanému rozložení GUARD-MESH T-Decku.")
    data = bytearray(b"\xff" * (0x10000 + len(parts[3])))
    for offset, part in zip((0, 0x8000, 0xE000, 0x10000), parts):
        data[offset:offset + len(part)] = part
    info = inspect_bytes(data)
    if info.kind != "merged" or info.app_offset != 0x10000 or info.flash_size != 16 * 1024 * 1024:
        raise ImageError("Části neodpovídají instalačnímu obrazu T-Decku (16 MiB, aplikace na 0x10000).")
    destination.parent.mkdir(parents=True, exist_ok=True)
    tmp = destination.with_suffix(".bin.tmp")
    tmp.write_bytes(data)
    tmp.replace(destination)
    metadata = {"title": title or destination.stem, "board": TDECK, "sha256": info.sha256,
                "version": info.version, "format": "merged", "offset": 0}
    destination.with_suffix(".json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
    return destination
