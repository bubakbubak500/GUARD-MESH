# SPDX-License-Identifier: GPL-3.0-or-later
"""Checked, device-bound data archives. Never restore code or OTA selection."""
from dataclasses import dataclass, asdict
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import uuid
import zipfile

from .firmware import ImageError, TDECK

MAX_ARCHIVE = 544 * 1024 * 1024
MAX_FILES = 10000
DATA_TYPES = {"nvs": 2, "spiffs": 0x82}


@dataclass(frozen=True)
class Partition:
    name: str
    kind: int
    subtype: int
    offset: int
    size: int
    flags: int


def partitions(table, capacity):
    if len(table) != 4096 or capacity not in (4 << 20, 8 << 20, 16 << 20, 32 << 20):
        raise ImageError("Neplatná tabulka oddílů nebo velikost flash.")
    found = []
    for pos in range(0, 0xC00, 32):
        magic = table[pos:pos + 2]
        if magic == b"\xeb\xeb":
            if not found or hashlib.md5(table[:pos]).digest() != table[pos + 16:pos + 32]:
                raise ImageError("Kontrolní součet tabulky oddílů nesouhlasí.")
            return found
        if magic != b"\xaa\x50":
            break
        _, kind, subtype, offset, size, label, flags = struct.unpack_from("<HBBII16sI", table, pos)
        try:
            name = label.split(b"\0", 1)[0].decode("ascii")
        except UnicodeDecodeError as error:
            raise ImageError("Neplatný název oddílu.") from error
        if (not re.fullmatch(r"[A-Za-z0-9_-]{1,16}", name) or not size or size % 4096
                or offset < 0x9000 or offset % 4096 or offset + size > capacity
                or (kind == 0 and offset % 0x10000)):
            raise ImageError("Neplatný rozsah nebo název oddílu.")
        if any(p.name == name or offset < p.offset + p.size and p.offset < offset + size for p in found):
            raise ImageError("Duplicitní nebo překrývající se oddíly.")
        found.append(Partition(name, kind, subtype, offset, size, flags))
    raise ImageError("Chybí kontrolní součet tabulky oddílů.")


def data_partitions(table, capacity):
    parts = partitions(table, capacity)
    selected = [p for p in parts if p.name in DATA_TYPES]
    if (len(selected) != 2 or any(p.kind != 1 or p.subtype != DATA_TYPES[p.name] or p.flags for p in selected)):
        raise ImageError("Záloha vyžaduje nešifrované oddíly nvs a spiffs GUARD-MESH / WadaMesh.")
    return sorted(selected, key=lambda p: p.name)


def compatible(source_table, target_table, capacity):
    source, target = data_partitions(source_table, capacity), data_partitions(target_table, capacity)
    if source != target:
        raise ImageError("Datové oddíly původního a cílového firmwaru se liší. Nic nebylo zapsáno; automatická migrace tohoto rozložení není podporována.")
    return target


def file_hash(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _ordinary(path):
    st = path.lstat()
    if path.is_symlink() or getattr(st, "st_file_attributes", 0) & 0x400:
        raise ImageError("SD složka nesmí obsahovat odkazy ani junctions.")
    if not (stat.S_ISDIR(st.st_mode) or stat.S_ISREG(st.st_mode)):
        raise ImageError("SD záloha obsahuje nepodporovaný typ souboru.")
    return st


def sd_profile(root):
    root = Path(root).resolve(strict=True)
    profile = root / "meshcomod"
    _ordinary(profile)
    if not profile.is_dir():
        raise ImageError("Vyber kořen SD karty se složkou meshcomod.")
    return profile


def _sd_name(name):
    p = PurePosixPath(name)
    if (not name.startswith("sd/meshcomod/") or re.search(r'[\\:<>"|?*\x00-\x1f]', name)
            or any(x in ("", ".", "..") or x.endswith((".", " ")) for x in name.split("/"))
            or any(re.fullmatch(r"(?i)(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?", x) for x in p.parts)):
        raise ImageError("Neplatná cesta v SD záloze.")
    return Path(*p.parts[2:])


def save_backup(path, table, blobs, mac, capacity, board, sd_root=""):
    path = Path(path)
    if path.exists():
        raise ImageError("Soubor zálohy už existuje. Vyber nový název.")
    selected = data_partitions(table, capacity)
    if board != TDECK or not re.fullmatch(r"(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", mac):
        raise ImageError("Chybí podporovaná deska nebo identita zařízení.")
    manifest = {"format": "guard-mesh-backup", "version": 1, "board": board, "mac": mac,
                "flash_size": capacity, "created": datetime.now(timezone.utc).isoformat(),
                "partitions": [asdict(p) for p in selected], "files": {}, "sd_included": bool(sd_root)}
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    total = 0
    try:
        with zipfile.ZipFile(tmp, "x", compression=zipfile.ZIP_DEFLATED) as z:
            def put(name, data):
                nonlocal total
                total += len(data)
                if total > MAX_ARCHIVE or len(manifest["files"]) >= MAX_FILES:
                    raise ImageError("Záloha překračuje limit 544 MiB / 10 000 souborů.")
                z.writestr(name, data)
                manifest["files"][name] = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            put("partition-table.bin", table)
            for p in selected:
                data = blobs[p.name]
                if len(data) != p.size:
                    raise ImageError("Neúplně načtený datový oddíl.")
                put(f"flash/{p.name}.bin", data)
            if sd_root:
                profile = sd_profile(sd_root)
                def walk_error(error):
                    raise error
                for directory, dirs, files in os.walk(profile, followlinks=False, onerror=walk_error):
                    for child in dirs + files:
                        _ordinary(Path(directory) / child)
                    for filename in sorted(files):
                        source = Path(directory) / filename
                        before = source.stat()
                        if before.st_size > MAX_ARCHIVE - total:
                            raise ImageError("SD data jsou pro tento archiv příliš velká.")
                        name = "sd/meshcomod/" + source.relative_to(profile).as_posix()
                        _sd_name(name)
                        data = source.read_bytes()
                        after = source.stat()
                        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                            raise ImageError("SD data se během zálohování změnila. Záloha nebyla dokončena.")
                        put(name, data)
            z.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False).encode("utf-8"))
        with tmp.open("r+b") as f:
            os.fsync(f.fileno())
        load_backup(tmp)
        # Exclusive destination: never overwrite an earlier recovery archive.
        with path.open("xb") as target, tmp.open("rb") as source:
            import shutil
            try:
                shutil.copyfileobj(source, target)
                target.flush()
                os.fsync(target.fileno())
            except BaseException:
                target.close()
                path.unlink()
                raise
        load_backup(path)
        return path
    finally:
        if tmp.exists():
            tmp.unlink()


def load_backup(path):
    path = Path(path)
    if path.stat().st_size > MAX_ARCHIVE + 8 * 1024 * 1024:
        raise ImageError("Záloha je příliš velká.")
    try:
        with zipfile.ZipFile(path) as z:
            entries = z.infolist()
            names = [i.filename for i in entries]
            if (len(names) != len(set(n.casefold() for n in names)) or len(names) > MAX_FILES + 1
                    or sum(i.file_size for i in entries) > MAX_ARCHIVE + 2 * 1024 * 1024
                    or z.getinfo("manifest.json").file_size > 2 * 1024 * 1024):
                raise ImageError("Neplatná nebo příliš velká záloha.")
            m = json.loads(z.read("manifest.json"))
            if (m.get("format") != "guard-mesh-backup" or m.get("version") != 1 or m.get("board") != TDECK
                    or not isinstance(m.get("sd_included"), bool)
                    or not isinstance(m.get("created"), str) or not m["created"]
                    or not re.fullmatch(r"(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", m.get("mac", ""))):
                raise ImageError("Nepodporovaný formát zálohy.")
            expected = m["files"]
            if set(names) != set(expected) | {"manifest.json"}:
                raise ImageError("Obsah zálohy nesouhlasí s manifestem.")
            for name, meta in expected.items():
                if name not in ("partition-table.bin", "flash/nvs.bin", "flash/spiffs.bin"):
                    _sd_name(name)
                    if not m["sd_included"]:
                        raise ImageError("Nekonzistentní SD záloha.")
                item = z.getinfo(name)
                if item.is_dir() or item.file_size != meta["size"] or item.flag_bits & 1:
                    raise ImageError("Neplatný soubor v záloze.")
                digest = hashlib.sha256()
                with z.open(name) as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        digest.update(block)
                if digest.hexdigest() != meta["sha256"]:
                    raise ImageError("Záloha je poškozená: nesouhlasí SHA-256.")
            table = z.read("partition-table.bin")
            selected = data_partitions(table, m["flash_size"])
            if m["partitions"] != [asdict(p) for p in selected]:
                raise ImageError("Datové oddíly nesouhlasí s původní tabulkou.")
            blobs = {p.name: z.read(f"flash/{p.name}.bin") for p in selected}
            if any(len(blobs[p.name]) != p.size for p in selected):
                raise ImageError("Neúplná data zálohy.")
            return m, table, blobs
    except (KeyError, TypeError, ValueError, AttributeError, zipfile.BadZipFile, RuntimeError) as error:
        if isinstance(error, ImageError):
            raise
        raise ImageError("Záloha je poškozená nebo má neplatný formát.") from error


def restore_sd(archive, root):
    m, _, _ = load_backup(archive)
    if not m["sd_included"]:
        return None
    if not root:
        raise ImageError("Záloha obsahuje SD data. Vyber SD kartu připojenou k PC.")
    root = Path(root).resolve(strict=True)
    current = root / "meshcomod"
    if current.exists():
        _ordinary(current)
        if not current.is_dir():
            raise ImageError("Cíl meshcomod není složka.")
    stage = root / (".guardian-restore-" + uuid.uuid4().hex)
    previous = root / ("meshcomod-before-restore-" + uuid.uuid4().hex)
    # All directory moves are between verified direct children of the selected SD root.
    if any(p.resolve().parent != root for p in (current, stage, previous)):
        raise ImageError("Neplatná cílová SD cesta.")
    stage.mkdir()
    with zipfile.ZipFile(archive) as z:
        for name in m["files"]:
            if not name.startswith("sd/"):
                continue
            dest = stage / _sd_name(name)
            dest.parent.mkdir(parents=True, exist_ok=True)
            if not dest.resolve().is_relative_to(stage.resolve()):
                raise ImageError("Soubor překračuje cílovou SD složku.")
            with z.open(name) as source, dest.open("xb") as output:
                import shutil
                shutil.copyfileobj(source, output)
                output.flush()
                os.fsync(output.fileno())
            if file_hash(dest) != m["files"][name]["sha256"]:
                raise ImageError("Ověření zapsaných SD dat selhalo.")
    moved = False
    try:
        if current.exists():
            current.rename(previous)
            moved = True
        stage.rename(current)
    except BaseException:
        if moved and not current.exists():
            previous.rename(current)
        raise
    return previous if moved else None
