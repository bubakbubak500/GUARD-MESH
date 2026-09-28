# SPDX-License-Identifier: GPL-3.0-or-later
"""Real Tk widget tests; serial ports and flashing are replaced with fakes."""
import queue
import tempfile
import tkinter as tk
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from guard_mesh_flasher.app import Application
from guard_mesh_flasher.catalog import Library, Entry
from guard_mesh_flasher.firmware import inspect_bytes, TDECK
from test_flasher import merged


class GuiTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = tk.Tk()
        self.root.withdraw()
        with patch.object(Application, "scan"), patch("guard_mesh_flasher.app.serial_ports", return_value=[("COM_TEST", "COM_TEST — fake")]):
            self.app = Application(self.root, Library(self.tmp.name))
        entry = Entry(Path(self.tmp.name) / "test.bin", inspect_bytes(merged()), "Test version", TDECK, 12345)
        self.app.entries = {"0": entry, "1": Entry(Path("bad.bin"), None, "Bad image", "", 0, "bad")}
        self.app.render()
        self.root.update()

    def tearDown(self):
        self.app.destroy()
        self.tmp.cleanup()

    def test_selection_board_port_and_search_invalidate_confirmation(self):
        a = self.app
        self.assertEqual(a.selected.title, "Test version")
        a.confirm.set(True)
        a.controls()
        self.assertEqual(str(a.flash_button["state"]), "normal")
        a.board.set("LilyGo T-Deck Pro")
        self.assertFalse(a.confirm.get())
        a.confirm.set(True)
        a.controls()
        self.assertEqual(str(a.flash_button["state"]), "disabled")
        a.board.set(TDECK)
        a.confirm.set(True)
        a.baud.set("115200")
        self.assertFalse(a.confirm.get())
        a.search.set("nothing matches")
        self.root.update()
        self.assertIsNone(a.selected)
        self.assertEqual(str(a.flash_button["state"]), "disabled")

    def test_cancel_does_not_start_and_active_job_blocks_close(self):
        a = self.app
        a.confirm.set(True)
        a.controls()
        with patch("guard_mesh_flasher.app.messagebox.askokcancel", return_value=False), patch.object(a, "background") as start:
            a.start_flash()
            start.assert_not_called()
        with patch("guard_mesh_flasher.app.messagebox.askokcancel", return_value=True), patch.object(a, "background") as start:
            a.start_flash()
            start.assert_called_once()
        self.assertTrue(a.flashing)
        self.assertEqual(str(a.import_button["state"]), "disabled")
        with patch("guard_mesh_flasher.app.messagebox.showinfo"), patch.object(self.root, "destroy") as destroy:
            a.close()
            destroy.assert_not_called()

    def test_error_never_marks_success(self):
        a = self.app
        a.flashing = True
        a.emit("error", "USB disconnected")
        with patch("guard_mesh_flasher.app.messagebox.showerror") as dialog:
            a.poll()
            dialog.assert_called_once()
        self.assertFalse(a.flashing)
        self.assertFalse(a.confirm.get())
        self.assertEqual(str(a.flash_button["state"]), "disabled")
        self.assertEqual(float(a.progress["value"]), 0)

    def test_preservation_default_and_flash_request(self):
        a = self.app
        self.assertTrue(a.preserve.get())
        a.confirm.set(True)
        a.controls()
        with patch("guard_mesh_flasher.app.messagebox.askokcancel", return_value=True), patch.object(a, "begin_job") as start:
            a.start_flash()
        request = start.call_args.args[0]
        self.assertTrue(request.preserve)
        self.assertEqual(Path(request.backup_path).parent, a.library.home / "backups")
        a.preserve.set(False)
        self.assertFalse(a.confirm.get())

    def test_data_actions_without_firmware_and_busy_disabled(self):
        a = self.app
        a.search.set("nothing matches")
        self.assertIsNone(a.selected)
        self.assertEqual(str(a.backup_button["state"]), "normal")
        with patch("guard_mesh_flasher.app.filedialog.asksaveasfilename", return_value=""), patch.object(a, "begin_job") as start:
            a.start_backup()
            start.assert_not_called()
        a.flashing = True
        a.controls()
        for widget in (a.backup_button, a.restore_button, a.sd_button, a.preserve_box):
            self.assertEqual(str(widget["state"]), "disabled")

    def test_restore_requires_sd_and_cancel_does_not_start(self):
        a = self.app
        meta = {"sd_included": True, "mac": "80:b5:4e:f0:7d:89", "created": "2026-09-28"}
        with patch("guard_mesh_flasher.app.filedialog.askopenfilename", return_value="example.gmbak"), \
             patch("guard_mesh_flasher.app.file_hash", return_value="0" * 64), \
             patch("guard_mesh_flasher.app.load_backup", return_value=(meta, b"", {})), \
             patch("guard_mesh_flasher.app.messagebox.showerror") as error, \
             patch("guard_mesh_flasher.app.messagebox.askokcancel", return_value=False) as confirm, \
             patch.object(a, "begin_job") as start:
            a.start_restore()
            error.assert_called_once()
            confirm.assert_not_called()
            start.assert_not_called()
            a.sd_root.set(self.tmp.name)
            a.start_restore()
            confirm.assert_called_once()
            start.assert_not_called()


if __name__ == "__main__":
    unittest.main()
