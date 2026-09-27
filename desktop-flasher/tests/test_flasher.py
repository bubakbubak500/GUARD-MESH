# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from guard_mesh_flasher.firmware import ImageError, inspect_bytes, TDECK
from guard_mesh_flasher.catalog import Library, inspect_entry
from guard_mesh_flasher.transport import FlashRequest, prepare, write_arguments, worker, flash, check_flash_capacity
from guard_mesh_flasher.bundle import compose


def image(application=False):
    header = bytearray(24)
    header[0:4] = bytes([0xE9, 1, 2, 0x40])
    header[12] = 9
    header[23] = 1
    payload = bytearray(256)
    if application:
        struct.pack_into("<I", payload, 0, 0xABCD5432)
        payload[16:20], payload[48:52] = b"v1.0", b"test"
    else:
        payload[:] = b"b" * len(payload)
    data = header + struct.pack("<II", 0x3C000020, len(payload)) + payload
    checksum = 0xEF
    for byte in payload:
        checksum ^= byte
    data += b"\0" * (15 - len(data) % 16) + bytes([checksum])
    return bytes(data) + hashlib.sha256(data).digest()


def partitions(overlap=False):
    entries = ((1, 2, 0x9000, 0x5000), (1, 0, 0x9000 if overlap else 0xE000, 0x2000),
               (0, 0x10, 0x10000, 0x3E0000), (0, 0x11, 0x3F0000, 0x3E0000),
               (1, 0x83, 0x7D0000, 0x4C0000), (1, 0x82, 0xC90000, 0x360000),
               (1, 3, 0xFF0000, 0x10000))
    table = b"".join(struct.pack("<HBBII16sI", 0x50AA, *entry, b"test", 0) for entry in entries)
    table += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(table).digest()
    return table + b"\xff" * (4096 - len(table))


def merged(overlap=False):
    app = image(True)
    result = bytearray(b"\xff" * (0x10000 + len(app)))
    boot = image()
    result[:len(boot)] = boot
    result[0x8000:0x9000] = partitions(overlap)
    result[0x10000:] = app
    return bytes(result)


class FirmwareTests(unittest.TestCase):
    def test_merged_and_application_are_distinct(self):
        full = inspect_bytes(merged())
        app = inspect_bytes(image(True))
        self.assertEqual((full.kind, full.app_offset, full.version), ("merged", 0x10000, "v1.0"))
        self.assertEqual(app.kind, "application")
        self.assertEqual(full.sha256, hashlib.sha256(merged()).hexdigest())

    def test_damage_and_truncation_rejected(self):
        for offset in (0, 12, 32, 303, 304, 0x8000, 0x8050, 0x80F0, 0x10020):
            data = bytearray(merged())
            data[offset] ^= 0x04
            with self.subTest(offset=offset), self.assertRaises(ImageError):
                inspect_bytes(data)
        for length in (0, 23, 150, 0x8010, 0x10080, len(merged()) - 1):
            with self.subTest(length=length), self.assertRaises(ImageError):
                inspect_bytes(merged()[:length])

    def test_overlap_rejected_even_with_valid_table_md5(self):
        with self.assertRaisesRegex(ImageError, "překrývají"):
            inspect_bytes(merged(overlap=True))

    def test_capacity_must_be_known_and_sufficient(self):
        check_flash_capacity("Detected flash size: 16MB", 16 * 1024 * 1024)
        for text in ("Detected flash size: 4MB", "Unknown", ""):
            with self.assertRaises(ImageError):
                check_flash_capacity(text, 16 * 1024 * 1024)


class StorageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.file = self.root / "verze s mezerou a žluťoučký.bin"
        self.file.write_bytes(merged())

    def tearDown(self):
        self.tmp.cleanup()

    def test_library_survives_restart_and_removal_preserves_files(self):
        library = Library(self.root / "data")
        library.add(self.file)
        library.add(self.file)
        library.add(self.root)
        library = Library(library.home)
        self.assertEqual(library.paths(), [self.file])
        library.remove(str(self.file))
        library.remove(str(self.root))
        self.assertTrue(self.file.is_file())
        self.assertEqual(library.paths(), [])

    def test_corrupt_config_and_missing_firmware(self):
        library = Library(self.root)
        for value in ("invalid", "[]", "null", '{"files": 42, "folders": [null]}'):
            library.config.write_text(value, encoding="utf-8")
            self.assertEqual(Library(self.root).paths(), [])
        self.assertIsNone(inspect_entry(self.root / "missing.bin").info)

    def test_sidecar_checksum_and_web_archive(self):
        sidecar = self.file.with_suffix(".json")
        sidecar.write_text(json.dumps({"sha256": "0" * 64}))
        self.assertIsNone(inspect_entry(self.file).info)
        sidecar.unlink()
        records = self.root / "builds"
        records.mkdir()
        (records / sidecar.name).write_text(json.dumps({"sha256": hashlib.sha256(merged()).hexdigest(),
                   "environment": "LilyGo_TDeck_companion_radio_touch", "source": "revision"}))
        self.assertEqual(inspect_entry(self.file).board, TDECK)

    def test_immutable_copy_and_changed_source_rejected(self):
        request = FlashRequest(str(self.file), inspect_entry(self.file).info.sha256, "COM_TEST", 460800, TDECK)
        job = self.root / "job"
        job.mkdir()
        manifest = prepare(request, job)
        self.file.write_bytes(image(True))
        self.assertEqual((job / "image.bin").read_bytes(), merged())
        self.assertEqual(json.loads(manifest.read_text())["port"], "COM_TEST")
        with self.assertRaises(ImageError):
            prepare(request, job)

    def test_changed_source_never_launches_process(self):
        request = FlashRequest(str(self.file), "0" * 64, "COM_TEST", 115200, TDECK)
        with patch("subprocess.Popen") as launch:
            with self.assertRaises(ImageError):
                flash(request, lambda *_: None, self.root / "logs", launcher=launch)
            launch.assert_not_called()

    def test_raw_app_rejected_and_fixed_command(self):
        self.file.write_bytes(image(True))
        request = FlashRequest(str(self.file), inspect_entry(self.file).info.sha256, "COM_TEST", 115200, TDECK)
        with self.assertRaisesRegex(ImageError, "Samotnou"):
            prepare(request, self.root)
        args = write_arguments({"port": "COM_TEST", "baud": 115200, "image": str(self.file)})
        self.assertEqual(args[-2:], ["0x0", str(self.file)])
        self.assertEqual(args[args.index("--before") + 1], "no_reset")
        self.assertNotIn("erase_flash", args)
        self.assertNotIn("--force", args)

    def test_worker_checks_capacity_before_write_and_handles_failure(self):
        request = FlashRequest(str(self.file), inspect_entry(self.file).info.sha256, "COM_TEST", 115200, TDECK)
        manifest = prepare(request, self.root)
        with patch("esptool.main", side_effect=lambda args: print("Detected flash size: 4MB")) as tool:
            self.assertEqual(worker(manifest), 1)
            self.assertEqual(tool.call_count, 1)
            self.assertEqual(tool.call_args.args[0][-1], "flash_id")
            self.assertEqual(tool.call_args.args[0][tool.call_args.args[0].index("--after") + 1], "no_reset_stub")
        def failing_write(args):
            if args[-1] == "flash_id":
                print("Detected flash size: 16MB")
            else:
                raise RuntimeError("USB disconnected")
        with patch("esptool.main", side_effect=failing_write):
            self.assertEqual(worker(manifest), 1)
        self.assertFalse(json.loads((self.root / "result.json").read_text())["ok"])
        with patch("esptool.main", side_effect=lambda args: print("Detected flash size: 16MB")) as tool:
            self.assertEqual(worker(manifest), 0)
            self.assertEqual(tool.call_count, 2)
        self.assertTrue(json.loads((self.root / "result.json").read_text())["ok"])

    def test_composer_validates_layout_and_preserves_sources(self):
        files = [self.root / name for name in ("boot.bin", "part.bin", "ota.bin", "app.bin")]
        parts = (image(), partitions(), b"\xff" * 0x2000, image(True))
        for path, data in zip(files, parts):
            path.write_bytes(data)
        result = compose(*files, self.root / "complete.bin")
        self.assertEqual(inspect_entry(result).board, TDECK)
        with self.assertRaises(ImageError):
            compose(*files, files[0])
        files[1].write_bytes(partitions(overlap=True))
        with self.assertRaisesRegex(ImageError, "rozložení"):
            compose(*files, result)


if __name__ == "__main__":
    unittest.main()
