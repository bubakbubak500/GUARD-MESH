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


if __name__ == "__main__":
    unittest.main()
