# SPDX-License-Identifier: GPL-3.0-or-later
"""Immutable flash jobs and an isolated esptool process; no shell commands."""
from dataclasses import dataclass
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from .firmware import ImageError, inspect_bytes, read_image

BAUD_RATES = (115200, 460800, 921600)


def serial_ports():
    from serial.tools import list_ports
    return [(p.device, f"{p.device} — {p.description}") for p in list_ports.comports()]


@dataclass(frozen=True)
class FlashRequest:
    path: str
    sha256: str
    port: str
    baud: int
    board: str

    def validate(self):
        if not self.port or self.port.startswith("-") or any(c in self.port for c in "\r\n\0"):
            raise ValueError("Vyber platný sériový port.")
        if self.baud not in BAUD_RATES or not self.board:
            raise ValueError("Vyber desku a podporovanou rychlost.")
        if not re.fullmatch(r"[0-9a-f]{64}", self.sha256):
            raise ValueError("Chybí kontrolní součet vybraného obrazu.")


def prepare(request, directory):
    request.validate()
    data = read_image(request.path)
    info = inspect_bytes(data)
    if info.kind != "merged":
        raise ImageError("Samotnou aplikaci nelze zapisovat od adresy 0. Vyber kompletní obraz.")
    if info.sha256 != request.sha256:
        raise ImageError("Soubor se od výběru změnil. Obnov seznam a vyber jej znovu.")
    directory = Path(directory)
    image = directory / "image.bin"
    image.write_bytes(data)
    payload = {"image": str(image), "sha256": info.sha256, "port": request.port,
               "baud": request.baud, "board": request.board}
    manifest = directory / "request.json"
    manifest.write_text(json.dumps(payload), encoding="utf-8")
    return manifest


def write_arguments(payload):
    return ["--chip", "esp32s3", "--port", payload["port"], "--baud", str(payload["baud"]),
            "--before", "no_reset", "--after", "hard_reset", "write_flash",
            "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep",
            "0x0", payload["image"]]


def check_flash_capacity(text, required):
    match = re.search(r"Detected flash size:\s*(\d+)(MB|KB)", text)
    if not match:
        raise ImageError("Nelze ověřit velikost flash zařízení; zápis nebyl spuštěn.")
    actual = int(match[1]) * (1024 * 1024 if match[2] == "MB" else 1024)
    if actual < required:
        raise ImageError(f"Obraz vyžaduje {required // 1048576} MiB flash, zařízení má {actual // 1048576} MiB.")


class _ProbeLog(io.StringIO):
    def __init__(self, target):
        super().__init__()
        self.target = target

    def write(self, text):
        self.target.write(text)
        self.target.flush()
        return super().write(text)

    def flush(self):
        self.target.flush()


def worker(manifest):
    """Called in a child process, also supported by the frozen windowed EXE."""
    from contextlib import redirect_stdout, redirect_stderr
    manifest = Path(manifest)
    result = {"ok": False}
    with (manifest.parent / "output.log").open("w", encoding="utf-8", buffering=1) as stream:
        with redirect_stdout(stream), redirect_stderr(stream):
            try:
                payload = json.loads(manifest.read_text(encoding="utf-8"))
                request = FlashRequest(payload["image"], payload["sha256"], payload["port"],
                                       payload["baud"], payload["board"])
                request.validate()
                image = inspect_bytes(read_image(request.path))
                if image.kind != "merged" or image.sha256 != request.sha256:
                    raise ImageError("Pracovní kopie firmwaru neprošla kontrolou.")
                import esptool
                print(f"ESP32-S3 · {request.board} · {request.port} · {request.baud} baud", flush=True)
                print(f"SHA-256: {request.sha256}", flush=True)
                print("Ověřuji čip a kapacitu flash před zápisem…", flush=True)
                probe = _ProbeLog(stream)
                with redirect_stdout(probe):
                    esptool.main(["--chip", "esp32s3", "--port", request.port,
                                  "--before", "default_reset", "--after", "no_reset_stub", "flash_id"])
                # Keep the stub running between probe and write. Booting the app
                # here would re-enumerate native USB and invalidate the chosen port.
                check_flash_capacity(probe.getvalue(), image.flash_size)
                esptool.main(write_arguments(payload))
                result = {"ok": True}
            except BaseException as error:
                print(f"\nCHYBA: {error}", flush=True)
                result = {"ok": False, "error": str(error)}
    (manifest.parent / "result.json").write_text(json.dumps(result), encoding="utf-8")
    return 0 if result["ok"] else 1


def worker_command(manifest):
    if getattr(sys, "frozen", False):
        return [sys.executable, "--worker", str(manifest)]
    return [sys.executable, str(Path(__file__).resolve().parents[1] / "main.py"), "--worker", str(manifest)]


def flash(request, emit, log_directory, launcher=subprocess.Popen):
    """Runs on the GUI's worker thread. All UI updates go through emit(queue)."""
    log_directory = Path(log_directory)
    log_directory.mkdir(parents=True, exist_ok=True)
    log_path = log_directory / (time.strftime("flash-%Y%m%d-%H%M%S-") + os.urandom(3).hex() + ".log")
    with tempfile.TemporaryDirectory(prefix="guard-mesh-flash-") as tmp:
        manifest = prepare(request, tmp)
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        process = launcher(worker_command(manifest), creationflags=flags,
                           stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        offset = 0
        with log_path.open("w", encoding="utf-8") as saved:
            saved.write(f"Firmware: {request.path}\nBoard: {request.board}\nPort: {request.port}\nSHA-256: {request.sha256}\n")
            while True:
                source = Path(tmp) / "output.log"
                if source.exists():
                    with source.open("r", encoding="utf-8", errors="replace") as output:
                        output.seek(offset)
                        text = output.read()
                        offset = output.tell()
                    if text:
                        saved.write(text)
                        saved.flush()
                        emit("log", text)
                if process.poll() is not None:
                    # Drain the final bytes after process termination before reading result.
                    if source.exists():
                        with source.open("r", encoding="utf-8", errors="replace") as output:
                            output.seek(offset)
                            text = output.read()
                        saved.write(text)
                        emit("log", text)
                    break
                time.sleep(0.1)
        result_path = Path(tmp) / "result.json"
        result = json.loads(result_path.read_text(encoding="utf-8")) if result_path.exists() else {}
        if process.returncode != 0 or result.get("ok") is not True:
            raise RuntimeError(f"{result.get('error') or 'Flashování selhalo; podrobnosti jsou v protokolu.'}\nProtokol: {log_path}")
        return log_path
