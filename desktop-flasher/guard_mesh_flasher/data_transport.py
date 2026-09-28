# SPDX-License-Identifier: GPL-3.0-or-later
"""Backup/restore transaction, with the ESP kept in its stub until completion."""
from contextlib import redirect_stdout
from pathlib import Path
import re
import time
import uuid

from .backup import compatible, data_partitions, load_backup, save_backup, restore_sd, sd_profile
from .firmware import ImageError, TDECK, inspect_file


class EsptoolSession:
    """Retain one stub and serial handle, including its selected baud rate."""
    def __init__(self, payload):
        self.payload, self.esp = payload, None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        if self.esp is not None:
            self.esp._port.close()

    def __call__(self, args):
        import esptool
        if self.esp is None:
            self.esp = esptool.get_default_connected_device(
                [self.payload["port"]], port=self.payload["port"], connect_attempts=7,
                initial_baud=115200, chip="esp32s3", before="default_reset")
            if self.esp is None or self.esp.secure_download_mode:
                raise ImageError("Zařízení není dostupné v podporovaném nezabezpečeném režimu.")
            self.esp = self.esp.run_stub()
            if self.payload["baud"] != 115200:
                self.esp.change_baud(self.payload["baud"])
        args = list(args)
        # The stub is already loaded. Do not upload it again or reopen the port
        # at ROM baud between commands. esptool retains externally owned handles.
        args[args.index("--before") + 1] = "no_reset_no_sync"
        args.insert(0, "--no-stub")
        if "write_flash" in args:
            args.insert(args.index("write_flash") + 1, "--compress")
        esptool.main(args, esp=self.esp)


def execute(payload, stream, work):
    with EsptoolSession(payload) as session:
        return _execute(payload, stream, work, session)


def _execute(payload, stream, work, session):
    from .transport import _ProbeLog, check_flash_capacity, write_arguments
    work = Path(work)
    if payload["board"] != TDECK:
        raise ImageError("Záloha a obnova jsou zatím ověřené jen pro T-Deck / T-Deck Plus s GUARD-MESH nebo WadaMesh.")
    prefix = ["--chip", "esp32s3", "--port", payload["port"], "--baud", str(payload["baud"])]

    def call(command, first=False, reset=False):
        capture = _ProbeLog(stream)
        with redirect_stdout(capture):
            session(prefix + ["--before", "default_reset" if first else "no_reset",
                              "--after", "hard_reset" if reset else "no_reset_stub"] + command)
        return capture.getvalue()

    def read(offset, size, name):
        path = work / name
        call(["read_flash", hex(offset), hex(size), str(path)])
        data = path.read_bytes()
        if len(data) != size:
            raise ImageError("Neúplná data načtená ze zařízení; zápis nebyl spuštěn.")
        return data

    def files_for(parts, blobs, suffix):
        args = []
        for part in parts:
            path = work / (part.name + suffix + ".bin")
            path.write_bytes(blobs[part.name])
            args += [hex(part.offset), str(path)]
        return args

    operation = payload["operation"]
    archive = payload.get("archive", "")
    restored = load_backup(archive) if operation == "restore" else None
    if restored and restored[0]["sd_included"] and not payload.get("sd_root"):
        raise ImageError("Záloha obsahuje SD data. Nejdříve vyber kartu připojenou k PC.")
    sd_source = payload.get("sd_root", "")
    if sd_source and operation == "restore" and not (Path(sd_source) / "meshcomod").exists():
        if not Path(sd_source).is_dir():
            raise ImageError("Cílová SD karta není dostupná.")
        sd_source = "" # Empty destination has no existing SD profile to safeguard.
    if sd_source:
        # A readable, known data folder is required before touching the device.
        sd_profile(sd_source)
    image = inspect_file(payload["image"]) if operation == "flash" else None
    if image and (image.kind != "merged" or image.sha256 != payload["sha256"]):
        raise ImageError("Pracovní kopie firmwaru se změnila.")
    print("Kontroluji zařízení, zabezpečení a datové oddíly…", flush=True)
    probe = call(["flash_id"], first=True)
    capacity_match = re.search(r"Detected flash size:\s*(\d+)MB", probe)
    if not capacity_match or int(capacity_match[1]) != 16:
        raise ImageError("Záloha T-Decku vyžaduje ověřenou 16MiB flash.")
    capacity = 16 << 20
    if restored and restored[0]["flash_size"] != capacity:
        raise ImageError("Záloha má jinou velikost flash než zařízení.")
    check_flash_capacity(probe, image.flash_size if image else capacity)
    identity = re.search(r"\bMAC:\s*((?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2})", probe)
    if not identity:
        identity = re.search(r"\bMAC:\s*((?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2})", call(["read_mac"]))
    if not identity:
        raise ImageError("Nelze ověřit MAC zařízení. Nic nebylo zapsáno.")
    mac = identity[1].lower()
    if restored and (restored[0]["mac"] != mac or restored[0]["board"] != payload["board"]):
        raise ImageError("Záloha patří jinému zařízení. Obnova identity na jiné rádio není povolena.")
    security = call(["get_security_info"])
    if not all(re.search(rf"^{name}: Disabled\s*$", security, re.M) for name in ("Flash Encryption", "Secure Boot")):
        raise ImageError("Zabezpečenou flash tato obnova nepodporuje. Nic nebylo zapsáno.")
    table = read(0x8000, 4096, "device-partitions.bin")
    parts = data_partitions(table, capacity)
    if operation == "flash":
        target_table = Path(payload["image"]).read_bytes()[0x8000:0x9000]
        compatible(table, target_table, capacity)
    elif restored:
        compatible(restored[1], table, capacity)
    print("Načítám nastavení, identitu a interní zprávy…", flush=True)
    blobs = {p.name: read(p.offset, p.size, "read-" + p.name + ".bin") for p in parts}
    call(["verify_flash"] + files_for(parts, blobs, "-before"))
    backup_path = payload.get("backup_path")
    if operation == "restore":
        backup_path = str(Path(payload["backup_directory"]) /
                          (time.strftime("before-restore-%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:8] + ".gmbak"))
    saved = save_backup(backup_path, table, blobs, mac, capacity, payload["board"], sd_source)
    print(f"Ověřená záloha je uložená: {saved}", flush=True)
    if not payload.get("sd_root"):
        print("SD karta není součástí této zálohy; její obsah zůstává beze změny.", flush=True)
    try:
        if operation == "flash":
            print("Instaluji firmware; do obnovení dat se rádio nespustí…", flush=True)
            args = write_arguments(payload)
            args[args.index("--after") + 1] = "no_reset_stub"
            session(args)
        if operation in ("flash", "restore"):
            print("Obnovuji a ověřuji původní datové oddíly…", flush=True)
            restore_blobs = restored[2] if restored else blobs
            args = files_for(parts, restore_blobs, "-restore")
            call(["write_flash", "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"] + args)
            call(["verify_flash"] + args)
            if restored and restored[0]["sd_included"]:
                previous = restore_sd(archive, payload["sd_root"])
                print(f"SD data obnovena; původní SD složka: {previous}", flush=True)
        call(["read_mac"], reset=True)
        return {"ok": True, "operation": operation, "backup": str(saved)}
    except BaseException as error:
        raise RuntimeError(f"{error}\nObnova/instalace nebyla dokončena. Záloha zůstává uložená: {saved}") from error
