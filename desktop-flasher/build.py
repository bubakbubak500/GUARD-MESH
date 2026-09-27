# SPDX-License-Identifier: GPL-3.0-or-later
"""Build on the target OS; Windows optionally builds the Inno Setup installer."""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
PROJECT = ROOT.parent
CACHE = PROJECT / ".flasher-cache" / "desktop-package"
OUTPUT = PROJECT / "out" / "flasher"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware-dir", type=Path)
    parser.add_argument("--iscc", type=Path, help="Path to the installed Inno Setup compiler")
    args = parser.parse_args()
    CACHE.mkdir(parents=True, exist_ok=True)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    notices = CACHE / "licenses"
    notices.mkdir(exist_ok=True)
    for name in ("esptool", "pyserial", "cryptography", "cffi", "ecdsa", "reedsolo", "bitstring", "bitarray", "intelhex", "PyYAML", "pycparser", "tibs", "six"):
        distribution = importlib.metadata.distribution(name)
        for file in distribution.files or ():
            if "license" in file.name.lower() or "copying" in file.name.lower():
                source = Path(distribution.locate_file(file))
                if source.is_file():
                    shutil.copyfile(source, notices / (name + "-" + file.name))
    shutil.copyfile(PROJECT / "LICENSE", notices / "GUARD-MESH-LICENSE.txt")
    from guard_mesh_flasher.firmware import inspect_file
    command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--windowed", "--onedir",
               "--name", "Guard-Mesh-Flasher", "--distpath", str(CACHE / "dist"),
               "--workpath", str(CACHE / "work"), "--specpath", str(CACHE),
               "--paths", str(ROOT), "--collect-all", "esptool", "--collect-all", "espefuse",
               "--hidden-import", "serial.tools.list_ports", "--add-data", f"{notices}:licenses",
               "--add-data", f"{ROOT / 'README.md'}:."]
    if args.firmware_dir:
        firmware = args.firmware_dir.resolve()
        files = list(firmware.glob("*.bin"))
        if not files:
            raise ValueError("Firmware directory is empty")
        for file in files:
            if inspect_file(file).kind != "merged":
                raise ValueError(f"Not an installation image: {file}")
        command.extend(["--add-data", f"{firmware}:firmware"])
    command.append(str(ROOT / "main.py"))
    subprocess.run(command, check=True, cwd=ROOT)
    folder = CACHE / "dist/Guard-Mesh-Flasher"
    system = "Windows-x64" if sys.platform == "win32" else sys.platform
    archive = shutil.make_archive(str(OUTPUT / f"Guard-Mesh-Flasher-1.0.0-{system}-portable"), "zip", folder.parent, folder.name)
    outputs = [Path(archive)]
    if args.iscc:
        subprocess.run([str(args.iscc.resolve()), f"/DAppSource={folder}", f"/DOutputPath={OUTPUT}",
                        str(ROOT / "packaging/windows.iss")], check=True)
        outputs.append(OUTPUT / "Guard-Mesh-Flasher-1.0.0-Windows-x64-Setup.exe")
    checksums = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in outputs}
    (OUTPUT / "SHA256SUMS.json").write_text(json.dumps(checksums, indent=2), encoding="utf-8")
    print("Build ready:", OUTPUT)


if __name__ == "__main__":
    main()
