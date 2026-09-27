# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline ESP32-S3 image inspection. No device or UI dependencies."""
from dataclasses import dataclass
import hashlib
from pathlib import Path
import struct

MAX_BYTES = 32 * 1024 * 1024
TDECK = "LilyGo T-Deck / T-Deck Plus"


class ImageError(ValueError):
    pass


@dataclass(frozen=True)
class ImageInfo:
    kind: str
    size: int
    sha256: str
    version: str
    project: str
    built: str
    flash_size: int
    app_offset: int


def read_image(path):
    with Path(path).open("rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ImageError("Soubor je větší než 32 MiB.")
    return data


def _image(data, start, limit):
    """Validate complete segment framing, XOR checksum and optional SHA-256."""
    if start + 24 > limit or data[start] != 0xE9:
        raise ImageError("Chybí platná hlavička obrazu ESP32-S3.")
    count = data[start + 1]
    if not 1 <= count <= 16 or struct.unpack_from("<H", data, start + 12)[0] != 9:
        raise ImageError("Obraz není pro podporovaný čip ESP32-S3.")
    if data[start + 23] not in (0, 1):
        raise ImageError("Neplatný příznak kontrolního součtu obrazu.")
    pos, checksum = start + 24, 0xEF
    first = b""
    for index in range(count):
        if pos + 8 > limit:
            raise ImageError("Obraz má neúplnou hlavičku segmentu.")
        length = struct.unpack_from("<I", data, pos + 4)[0]
        pos += 8
        if pos + length > limit:
            raise ImageError("Obraz je neúplný nebo přesahuje aplikační oddíl.")
        segment = data[pos:pos + length]
        if index == 0:
            first = segment
        for value in segment:
            checksum ^= value
        pos += length
    end = start + ((pos - start) // 16 + 1) * 16
    if end > limit or data[end - 1] != checksum:
        raise ImageError("Kontrolní součet segmentů nesouhlasí.")
    if data[start + 23]:
        if end + 32 > limit or hashlib.sha256(data[start:end]).digest() != data[end:end + 32]:
            raise ImageError("SHA-256 v obrazu nesouhlasí; soubor je poškozený.")
        end += 32
    fields = ("", "", "")
    if len(first) >= 112 and struct.unpack_from("<I", first)[0] == 0xABCD5432:
        def text(offset, size):
            return first[offset:offset + size].split(b"\0", 1)[0].decode("utf-8", "replace")
        fields = (text(16, 32), text(48, 32), f"{text(96, 16)} {text(80, 16)}".strip())
    return end, fields


def inspect_bytes(data):
    if not 24 <= len(data) <= MAX_BYTES:
        raise ImageError("Očekávám firmware .bin o velikosti 24 B až 32 MiB.")
    end, fields = _image(data, 0, len(data))
    size_code = data[3] >> 4
    if size_code > 5:
        raise ImageError("Nepodporovaná velikost flash v hlavičce.")
    flash_size = (1 << size_code) * 1024 * 1024
    digest = hashlib.sha256(data).hexdigest()
    # An application image at zero is NOT a full installation image.
    if len(data) < 0x8020 or data[0x8000:0x8002] != b"\xaa\x50":
        if fields[1]:
            return ImageInfo("application", len(data), digest, *fields, flash_size, 0)
        raise ImageError("Chybí tabulka oddílů na 0x8000. Vyber kompletní instalační obraz.")
    if end > 0x8000:
        raise ImageError("Bootloader zasahuje do tabulky oddílů.")
    if len(data) > flash_size:
        raise ImageError("Obraz přesahuje velikost flash uvedenou v hlavičce.")
    entries, app, md5_found = [], None, False
    for pos in range(0x8000, 0x8C00, 32):
        if pos + 32 > len(data):
            raise ImageError("Neúplná tabulka oddílů.")
        magic = data[pos:pos + 2]
        if magic == b"\xff\xff":
            break
        if magic == b"\xeb\xeb":
            if hashlib.md5(data[0x8000:pos]).digest() != data[pos + 16:pos + 32]:
                raise ImageError("MD5 tabulky oddílů nesouhlasí.")
            md5_found = True
            break
        if magic != b"\xaa\x50":
            raise ImageError("Neplatná tabulka oddílů.")
        kind, subtype, offset, size = struct.unpack_from("<BBII", data, pos + 2)
        if not size or offset < 0x9000 or offset % 4096 or offset + size > flash_size:
            raise ImageError("Neplatný rozsah oddílu.")
        if any(offset < other + length and other < offset + size for other, length in entries):
            raise ImageError("Oddíly v obrazu se překrývají.")
        entries.append((offset, size))
        if kind == 0 and subtype in (0, 0x10) and offset < len(data):
            if offset % 0x10000:
                raise ImageError("Aplikační oddíl není správně zarovnaný.")
            _, app_fields = _image(data, offset, min(len(data), offset + size))
            app = (offset, app_fields)
    if not entries or not app:
        raise ImageError("Chybí úplná startovací aplikace podle tabulky oddílů.")
    if not md5_found:
        raise ImageError("Chybí kontrolní součet tabulky oddílů.")
    return ImageInfo("merged", len(data), digest, *app[1], flash_size, app[0])


def inspect_file(path):
    return inspect_bytes(read_image(path))
