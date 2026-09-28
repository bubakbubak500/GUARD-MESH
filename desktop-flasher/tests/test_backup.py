# SPDX-License-Identifier: GPL-3.0-or-later
"""Real archives plus simulated ESP flash: no physical serial device is opened."""
import hashlib
import io
from contextlib import redirect_stdout
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch, MagicMock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from guard_mesh_flasher.backup import compatible, data_partitions, file_hash, load_backup, save_backup, restore_sd
from guard_mesh_flasher.firmware import ImageError, TDECK
from guard_mesh_flasher.transport import DataRequest, FlashRequest, prepare, worker
from guard_mesh_flasher.data_transport import EsptoolSession
from test_flasher import merged

MAC = "80:b5:4e:f0:7d:89"
CAPACITY = 16 << 20


def table(old=False, changed=False):
    # Current T-Deck CSV; older single-app images retain the same data ranges.
    entries = [("nvs", 1, 2, 0x9000, 0x5000), ("otadata", 1, 0, 0xE000, 0x2000)]
    entries += ([("app0", 0, 0x10, 0x10000, 0x7C0000)] if old else
                [("app0", 0, 0x10, 0x10000, 0x3E0000), ("app1", 0, 0x11, 0x3F0000, 0x3E0000)])
    entries += [("tiles", 1, 0x83, 0x7D0000, 0x4C0000),
                ("spiffs", 1, 0x82, 0xC90000, 0x350000 if changed else 0x360000),
                ("coredump", 1, 3, 0xFF0000, 0x10000)]
    result = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, sub, off, size, name.encode(), 0)
                      for name, kind, sub, off, size in entries)
    result += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(result).digest()
    return result.ljust(4096, b"\xff")


def blobs():
    return {"nvs": b"N" * 0x5000, "spiffs": b"M" * 0x360000}


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.archive = self.root / "záloha rádia.gmbak"

    def tearDown(self):
        self.tmp.cleanup()

    def save(self, sd=""):
        return save_backup(self.archive, table(), blobs(), MAC, CAPACITY, TDECK, sd)

    def rewrite(self, change):
        with zipfile.ZipFile(self.archive) as z:
            contents = {name: z.read(name) for name in z.namelist()}
        change(contents)
        with zipfile.ZipFile(self.archive, "w", zipfile.ZIP_DEFLATED) as z:
            for name, data in contents.items():
                z.writestr(name, data)

    def test_round_trip_contains_only_selected_data(self):
        self.save()
        m, t, b = load_backup(self.archive)
        self.assertEqual(b, blobs())
        self.assertEqual(t, table())
        self.assertEqual(m["mac"], MAC)
        self.assertFalse(m["sd_included"])
        self.assertEqual(set(m["files"]), {"partition-table.bin", "flash/nvs.bin", "flash/spiffs.bin"})
        with self.assertRaises(ImageError):
            self.save()
        self.assertEqual(load_backup(self.archive)[2], blobs())

    def test_old_app_layout_allowed_but_changed_data_or_bad_md5_refused(self):
        self.assertEqual(compatible(table(old=True), table(), CAPACITY), data_partitions(table(), CAPACITY))
        with self.assertRaises(ImageError):
            compatible(table(changed=True), table(), CAPACITY)
        corrupted = bytearray(table())
        corrupted[8] ^= 1
        with self.assertRaises(ImageError):
            data_partitions(corrupted, CAPACITY)

    def test_corrupt_hash_and_missing_metadata_refused(self):
        self.save()
        self.rewrite(lambda files: files.update({"flash/nvs.bin": b"X" * 0x5000}))
        with self.assertRaisesRegex(ImageError, "SHA-256"):
            load_backup(self.archive)
        self.archive.unlink()
        self.save()
        def remove_date(files):
            m = json.loads(files["manifest.json"])
            del m["created"]
            files["manifest.json"] = json.dumps(m).encode()
        self.rewrite(remove_date)
        with self.assertRaises(ImageError):
            load_backup(self.archive)

    def test_sd_restore_keeps_previous_profile_and_unrelated_files(self):
        sd = self.root / "sd"
        (sd / "meshcomod/msgs").mkdir(parents=True)
        msg = sd / "meshcomod/msgs/message"
        msg.write_bytes(b"old messages")
        (sd / "map.bin").write_bytes(b"map untouched")
        self.save(str(sd))
        msg.write_bytes(b"new messages")
        previous = restore_sd(self.archive, sd)
        self.assertEqual(msg.read_bytes(), b"old messages")
        self.assertEqual((previous / "msgs/message").read_bytes(), b"new messages")
        self.assertEqual((sd / "map.bin").read_bytes(), b"map untouched")
        self.assertEqual(previous.parent, sd)

    def test_archive_traversal_refused_even_with_matching_hash(self):
        self.save()
        def malicious(files):
            name = "sd/meshcomod/../../escape.txt"
            data = b"no"
            m = json.loads(files["manifest.json"])
            m["sd_included"] = True
            m["files"][name] = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            files[name] = data
            files["manifest.json"] = json.dumps(m).encode()
        self.rewrite(malicious)
        with self.assertRaises(ImageError):
            restore_sd(self.archive, self.root)
        self.assertFalse((self.root / "escape.txt").exists())

    def test_unreadable_sd_walk_aborts_archive(self):
        sd = self.root / "sd"
        (sd / "meshcomod").mkdir(parents=True)
        def fail_walk(*args, **kwargs):
            kwargs["onerror"](PermissionError("unreadable subfolder"))
            return iter(())
        with patch("guard_mesh_flasher.backup.os.walk", side_effect=fail_walk):
            with self.assertRaises(PermissionError):
                self.save(str(sd))
        self.assertFalse(self.archive.exists())

    def test_selected_archive_changed_before_job_refused(self):
        self.save()
        request = DataRequest("restore", str(self.archive), "COM_TEST", 115200, TDECK,
                              file_hash(self.archive), backup_directory=str(self.root / "backups"))
        self.archive.write_bytes(b"changed")
        with self.assertRaises(ImageError):
            prepare(request, self.root)


class Device:
    def __init__(self, backup):
        self.memory = bytearray(b"\xff" * CAPACITY)
        self.memory[0x8000:0x9000] = table(old=True)
        for p in data_partitions(table(), CAPACITY):
            self.memory[p.offset:p.offset + p.size] = blobs()[p.name]
        self.backup = backup
        self.mac = MAC
        self.secure = False
        self.fail = None
        self.calls = []
        self.writes = []

    def __call__(self, args, esp=None):
        op = next(x for x in args if x in ("flash_id", "read_mac", "get_security_info", "read_flash", "write_flash", "verify_flash"))
        self.calls.append((op, args))
        if self.fail == op:
            raise RuntimeError("simulated failure " + op)
        if op == "flash_id":
            print("Detected flash size: 16MB\nMAC: " + self.mac)
        elif op == "read_mac":
            print("MAC: " + self.mac)
        elif op == "get_security_info":
            print("Secure Boot: Disabled\nFlash Encryption: " + ("Enabled" if self.secure else "Disabled"))
        elif op == "read_flash":
            off, size = int(args[-3], 0), int(args[-2], 0)
            Path(args[-1]).write_bytes(self.memory[off:off + size])
        else:
            rest = args[args.index(op) + 1:]
            if rest and rest[0] == "--compress":
                rest = rest[1:]
            while rest and rest[0].startswith("--"):
                rest = rest[2:]
            for i in range(0, len(rest), 2):
                off, data = int(rest[i], 0), Path(rest[i + 1]).read_bytes()
                if op == "write_flash":
                    # No write can happen before a durable, independently readable archive exists.
                    load_backup(self.backup)
                    self.writes.append((off, len(data)))
                    self.memory[off:off + len(data)] = data
                elif self.memory[off:off + len(data)] != data:
                    raise RuntimeError("verify mismatch")


class TransactionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.archive = self.root / "previous.gmbak"
        self.image = self.root / "fw.bin"
        data = bytearray(merged())
        data[0x8000:0x9000] = table()
        self.image.write_bytes(data)
        self.device = Device(self.archive)
        self.request = FlashRequest(str(self.image), file_hash(self.image), "COM_TEST", 460800, TDECK, True, str(self.archive))

    def tearDown(self):
        self.tmp.cleanup()

    def run_job(self, request=None):
        job = self.root / "job"
        job.mkdir(exist_ok=True)
        manifest = prepare(request or self.request, job)
        esp = MagicMock()
        esp.secure_download_mode = False
        esp.run_stub.return_value = esp
        with patch("esptool.get_default_connected_device", return_value=esp) as connect, patch("esptool.main", side_effect=self.device) as main:
            result = worker(manifest)
        connect.assert_called_once()
        esp._port.close.assert_called_once()
        for call in main.call_args_list:
            self.assertIs(call.kwargs["esp"], esp)
            self.assertIn("--no-stub", call.args[0])
            self.assertIn("no_reset_no_sync", call.args[0])
        return result, json.loads((job / "result.json").read_text())

    def test_flash_backs_up_before_writing_and_restores_before_boot(self):
        code, result = self.run_job()
        self.assertEqual(code, 0, result)
        self.assertEqual(load_backup(self.archive)[2], blobs())
        self.assertEqual([o for o, _ in self.device.writes], [0, 0x9000, 0xC90000])
        for p in data_partitions(table(), CAPACITY):
            self.assertEqual(self.device.memory[p.offset:p.offset + p.size], blobs()[p.name])
        self.assertEqual(self.device.calls[-2][0], "verify_flash")
        self.assertEqual(self.device.calls[-1][0], "read_mac")
        for _, args in self.device.calls[:-1]:
            self.assertEqual(args[args.index("--after") + 1], "no_reset_stub")
        self.assertEqual(self.device.calls[-1][1][-2:], ["hard_reset", "read_mac"])
        self.assertEqual(self.device.memory[0x7D0000:0xC90000], b"\xff" * 0x4C0000)

    def test_read_and_verify_failures_never_write(self):
        for op in ("read_flash", "verify_flash"):
            with self.subTest(op=op):
                self.device.fail = op
                code, result = self.run_job()
                self.assertEqual(code, 1)
                self.assertFalse(result["ok"])
                self.assertEqual(self.device.writes, [])
                self.assertFalse(self.archive.exists())

    def test_disk_failure_never_writes(self):
        with patch("guard_mesh_flasher.data_transport.save_backup", side_effect=OSError("disk full")):
            self.assertEqual(self.run_job()[0], 1)
        self.assertEqual(self.device.writes, [])

    def test_layout_mismatch_and_encryption_never_write(self):
        self.device.memory[0x8000:0x9000] = table(changed=True)
        self.assertEqual(self.run_job()[0], 1)
        self.device.memory[0x8000:0x9000] = table()
        self.device.secure = True
        self.assertEqual(self.run_job()[0], 1)
        self.assertEqual(self.device.writes, [])

    def test_failed_write_keeps_recovery_archive_and_reports_path(self):
        self.device.fail = "write_flash"
        code, result = self.run_job()
        self.assertEqual(code, 1)
        self.assertIn(str(self.archive), result["error"])
        self.assertEqual(load_backup(self.archive)[2], blobs())
        self.assertNotEqual(self.device.calls[-1][0], "read_mac")

    def test_verification_failure_after_write_preserves_backup_and_does_not_boot(self):
        device = self.device
        def corrupt_after_write(args, esp=None):
            device(args, esp)
            if "write_flash" in args:
                device.memory[0x9000] ^= 1
        self.device = corrupt_after_write
        code, result = self.run_job()
        self.assertEqual(code, 1)
        self.assertIn(str(self.archive), result["error"])
        self.assertEqual(load_backup(self.archive)[2], blobs())
        self.assertNotEqual(device.calls[-1][0], "read_mac")

    def test_backup_does_not_write_then_restore_rejects_other_radio(self):
        request = DataRequest("backup", str(self.archive), "COM_TEST", 115200, TDECK)
        self.assertEqual(self.run_job(request)[0], 0)
        self.assertEqual(self.device.writes, [])
        self.device.mac = "00:11:22:33:44:55"
        restore = DataRequest("restore", str(self.archive), "COM_TEST", 115200, TDECK,
                              file_hash(self.archive), backup_directory=str(self.root / "recovery"))
        self.assertEqual(self.run_job(restore)[0], 1)
        self.assertEqual(self.device.writes, [])

    def test_restore_saves_current_data_and_writes_only_data(self):
        save_backup(self.archive, table(), blobs(), MAC, CAPACITY, TDECK)
        self.device.memory[0x9000:0xE000] = b"C" * 0x5000
        recovery = self.root / "recovery"
        restore = DataRequest("restore", str(self.archive), "COM_TEST", 115200, TDECK,
                              file_hash(self.archive), backup_directory=str(recovery))
        original_call = self.device
        def checked(args, esp=None):
            if "write_flash" in args:
                archives = list(recovery.glob("*.gmbak"))
                self.assertEqual(len(archives), 1)
                self.assertEqual(load_backup(archives[0])[2]["nvs"], b"C" * 0x5000)
            original_call(args)
        self.device = checked
        code, result = self.run_job(restore)
        self.assertEqual(code, 0, result)
        self.assertEqual([o for o, _ in original_call.writes], [0x9000, 0xC90000])
        self.assertEqual(original_call.memory[0x9000:0xE000], blobs()["nvs"])


class SessionTests(unittest.TestCase):
    def test_real_esptool_parser_retains_external_stub_and_baud(self):
        # Exercise esptool.main itself, only hardware responses/write command are faked.
        esp = MagicMock()
        esp.secure_download_mode = False
        esp.IS_STUB = True
        esp.CHIP_NAME = "ESP32-S3"
        esp.EFUSE_MAX_KEY = 5
        esp.KEY_PURPOSES = {0: "USER"}
        esp.run_stub.return_value = esp
        esp.get_chip_features.return_value = ["WiFi", "BLE"]
        esp.read_mac.return_value = tuple(bytes.fromhex(MAC.replace(":", "")))
        esp.flash_id.return_value = 0x1840EF
        esp.read_spiflash_sfdp.return_value = 0
        esp.get_security_info.return_value = {"flags": 0, "flash_crypt_cnt": 0,
                                              "key_purposes": [0] * 7, "chip_id": 9, "api_version": 1}
        writes = []
        def write_flash(esp, args):
            writes.append((esp, args.compress, args.addr_filename[0][0]))
        payload = {"port": "COM_TEST", "baud": 460800}
        prefix = ["--chip", "esp32s3", "--port", "COM_TEST", "--baud", "460800",
                  "--before", "no_reset", "--after", "no_reset_stub"]
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()) as output, \
             patch("esptool.get_default_connected_device", return_value=esp) as connect, \
             patch("esptool.write_flash", new=write_flash):
            binary = Path(folder) / "data.bin"
            binary.write_bytes(b"data")
            with EsptoolSession(payload) as session:
                session(prefix + ["flash_id"])
                session(prefix + ["get_security_info"])
                session(prefix + ["write_flash", "--flash_size", "keep", "0x9000", str(binary)])
            self.assertIn("Detected flash size: 16MB", output.getvalue())
            self.assertIn("Flash Encryption: Disabled", output.getvalue())
        connect.assert_called_once()
        esp.run_stub.assert_called_once()
        esp.change_baud.assert_called_once_with(460800)
        esp._port.close.assert_called_once()
        esp.hard_reset.assert_not_called()
        self.assertEqual(writes, [(esp, True, 0x9000)])


if __name__ == "__main__":
    unittest.main()
