# SPDX-License-Identifier: GPL-3.0-or-later
"""Persistent folder/file references; imported firmware is never moved or deleted."""
from dataclasses import dataclass
import json
import os
from pathlib import Path
import sys

from .firmware import ImageError, inspect_file, TDECK


def data_directory():
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    elif sys.platform == "darwin":
        base = Path.home() / "Library/Application Support"
    else:
        base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))
    return base / "Guard-Mesh-Flasher"


def resource_directory():
    return Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[1]))


class Library:
    def __init__(self, home=None):
        self.home = Path(home) if home else data_directory()
        self.config = self.home / "library.json"
        self.folders, self.files = [], []
        try:
            data = json.loads(self.config.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ValueError("Library configuration must be an object")
            for key in ("folders", "files"):
                values = data.get(key, [])
                if isinstance(values, list):
                    setattr(self, key, [str(Path(v).resolve()) for v in values if isinstance(v, str)])
        except (OSError, ValueError, TypeError):
            pass

    def save(self):
        self.home.mkdir(parents=True, exist_ok=True)
        tmp = self.config.with_suffix(".tmp")
        tmp.write_text(json.dumps({"folders": self.folders, "files": self.files},
                                  ensure_ascii=False, indent=2), encoding="utf-8")
        tmp.replace(self.config)

    def add(self, path):
        path = Path(path).resolve()
        target = self.folders if path.is_dir() else self.files
        if str(path) not in target:
            target.append(str(path))
        self.save()

    def remove(self, path):
        for values in (self.folders, self.files):
            if path in values:
                values.remove(path)
        self.save()

    def paths(self, defaults=()):
        paths = {Path(p) for p in self.files}
        for folder in [*defaults, *self.folders]:
            try:
                paths.update(Path(folder).glob("*.bin"))
            except OSError:
                pass
        return sorted(paths, key=lambda p: str(p).casefold())


@dataclass(frozen=True)
class Entry:
    path: Path
    info: object
    title: str
    board: str
    modified: float
    error: str = ""


def inspect_entry(path):
    path = Path(path)
    try:
        info = inspect_file(path)
        title, board = path.stem, ""
        sidecar = path.with_suffix(".json")
        if sidecar.is_file():
            metadata = json.loads(sidecar.read_text(encoding="utf-8"))
            expected = metadata.get("sha256")
            if expected and expected != info.sha256:
                raise ImageError("SHA-256 nesouhlasí s uloženými údaji sestavení.")
            title = str(metadata.get("title") or metadata.get("version") or title)
            board = str(metadata.get("board") or "")
        # Web flasher archives keep their sidecars in builds/, not beside the bin.
        web_record = path.parent / "builds" / (path.stem + ".json")
        if web_record.is_file():
            metadata = json.loads(web_record.read_text(encoding="utf-8"))
            if metadata.get("sha256") != info.sha256:
                raise ImageError("Kontrolní součet staršího webového sestavení nesouhlasí.")
            if metadata.get("environment") == "LilyGo_TDeck_companion_radio_touch":
                board = TDECK
                title = f"T-Deck · {metadata.get('source', 'local')} · {path.stem[:8]}"
        return Entry(path, info, title, board, path.stat().st_mtime)
    except (OSError, ValueError, TypeError, AttributeError) as error:
        return Entry(path, None, path.stem, "", 0, str(error))


def default_folders():
    folders = [resource_directory() / "firmware"]
    executable = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else resource_directory()
    folders.append(executable / "firmware")
    repo = Path(__file__).resolve().parents[2]
    if (repo / "platformio.ini").is_file():
        folders.extend([repo / "out", repo / ".flasher-cache"])
    return list(dict.fromkeys(folders))
