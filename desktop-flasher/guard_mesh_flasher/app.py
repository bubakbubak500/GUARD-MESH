# SPDX-License-Identifier: GPL-3.0-or-later
"""Tk desktop UI. Background work publishes events; only the Tk thread owns widgets."""
from datetime import datetime
import json
from pathlib import Path
import queue
import re
import threading
import uuid
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from . import __version__
from .catalog import Library, default_folders, inspect_entry
from .firmware import TDECK
from .transport import BAUD_RATES, FlashRequest, DataRequest, flash, serial_ports
from .backup import load_backup, file_hash

BOARDS = (TDECK, "LilyGo T-Deck Pro", "Heltec LoRa V4 R8", "LilyGo T-LoRa Pager",
          "ThinkNode M9", "Jiná deska ESP32-S3")


class Application:
    def __init__(self, root, library, smoke_report=None):
        self.root, self.library, self.smoke_report = root, library, smoke_report
        self.events = queue.Queue()
        self.entries, self.ports = {}, {}
        self.scanning, self.flashing = False, False
        self.selected = None
        self.search = tk.StringVar()
        self.ready_only = tk.BooleanVar(value=True)
        self.board = tk.StringVar(value=TDECK)
        self.port = tk.StringVar()
        self.baud = tk.StringVar(value="460800")
        self.status = tk.StringVar(value="Načítám místní sestavení…")
        self.summary = tk.StringVar(value="Vyber firmware ze seznamu.")
        self.details = tk.StringVar()
        self.confirm = tk.BooleanVar()
        self.preserve = tk.BooleanVar(value=True)
        self.sd_root = tk.StringVar()
        self._layout()
        self.search.trace_add("write", lambda *_: self.render())
        self.board.trace_add("write", lambda *_: self.invalidate())
        self.port.trace_add("write", lambda *_: self.invalidate())
        self.baud.trace_add("write", lambda *_: self.invalidate())
        self.preserve.trace_add("write", lambda *_: self.invalidate())
        self.sd_root.trace_add("write", lambda *_: self.invalidate())
        root.protocol("WM_DELETE_WINDOW", self.close)
        self._poll_token = root.after(80, self.poll)
        self.refresh_ports()
        self.scan()

    def _layout(self):
        root = self.root
        root.title(f"Guard-Mesh-Flasher {__version__}")
        root.geometry("1080x940")
        root.minsize(880, 880)
        root.configure(bg="#eef2f5")
        style = ttk.Style(root)
        style.theme_use("clam")
        style.configure(".", font=("Segoe UI", 10), background="#eef2f5", foreground="#152536")
        style.configure("TButton", padding=(12, 7))
        style.configure("Accent.TButton", background="#126f67", foreground="white", font=("Segoe UI", 11, "bold"))
        style.map("Accent.TButton", background=[("disabled", "#ccd4d9"), ("active", "#0b5a54")],
                  foreground=[("disabled", "#667784")])
        style.configure("Treeview", rowheight=29, background="white", fieldbackground="white")
        style.configure("Treeview.Heading", font=("Segoe UI", 10, "bold"), padding=6)
        style.map("Treeview", background=[("selected", "#126f67")], foreground=[("selected", "white")])
        style.configure("TLabelframe.Label", font=("Segoe UI", 10, "bold"))
        header = tk.Frame(root, bg="#142938", padx=22, pady=17)
        header.pack(fill="x")
        tk.Label(header, text="GUARD-MESH", font=("Segoe UI", 23, "bold"), fg="#ffffff", bg="#142938").pack(side="left")
        tk.Label(header, text="FLASHER", font=("Segoe UI", 13), fg="#69d0be", bg="#142938", padx=14).pack(side="left")
        tk.Label(header, text=f"Lokálně • přes USB  |  {__version__}", font=("Segoe UI", 10), fg="#c6d4dd", bg="#142938").pack(side="right")
        content = ttk.Frame(root, padding=(20, 14))
        content.pack(fill="both", expand=True)
        toolbar = ttk.Frame(content)
        toolbar.pack(fill="x", pady=(0, 10))
        self.import_button = ttk.Button(toolbar, text="Přidat firmware…", command=self.import_files)
        self.import_button.pack(side="left")
        self.folder_button = ttk.Button(toolbar, text="Přidat složku…", command=self.import_folder)
        self.folder_button.pack(side="left", padx=6)
        self.refresh_button = ttk.Button(toolbar, text="Obnovit", command=self.scan)
        self.refresh_button.pack(side="left")
        self.compose_button = ttk.Button(toolbar, text="Složit obraz…", command=self.compose)
        self.compose_button.pack(side="left", padx=6)
        ttk.Button(toolbar, text="Nápověda", command=self.help).pack(side="right")
        self.sources_button = ttk.Button(toolbar, text="Zdroje…", command=self.sources)
        self.sources_button.pack(side="right", padx=6)
        filters = ttk.Frame(content)
        filters.pack(fill="x", pady=(0, 8))
        ttk.Label(filters, text="Místní verze", font=("Segoe UI", 12, "bold")).pack(side="left")
        ttk.Checkbutton(filters, text="Jen instalační obrazy", variable=self.ready_only, command=self.render).pack(side="right")
        ttk.Entry(filters, textvariable=self.search, width=28).pack(side="right", padx=12)
        ttk.Label(filters, text="Hledat:").pack(side="right")
        table = ttk.Frame(content)
        table.pack(fill="both", expand=True)
        self.tree = ttk.Treeview(table, columns=("version", "board", "date", "size", "kind"), show="headings", height=4, selectmode="browse")
        for name, label, width in (("version", "Verze / sestavení", 320), ("board", "Deska", 180),
                                   ("date", "Datum souboru", 135), ("size", "Velikost", 80), ("kind", "Typ", 135)):
            self.tree.heading(name, text=label)
            self.tree.column(name, width=width, minwidth=65)
        scrollbar = ttk.Scrollbar(table, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set)
        self.tree.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        self.tree.bind("<<TreeviewSelect>>", self.select)
        self.tree.tag_configure("unavailable", foreground="#8a5151")
        selection = ttk.LabelFrame(content, text="Vybraný firmware", padding=10)
        selection.pack(fill="x", pady=(10, 8))
        ttk.Label(selection, textvariable=self.summary, font=("Segoe UI", 10, "bold"), wraplength=990).pack(anchor="w")
        ttk.Label(selection, textvariable=self.details, wraplength=990, font=("Segoe UI", 9)).pack(anchor="w", pady=(4, 0))
        connection = ttk.Frame(content)
        connection.pack(fill="x", pady=5)
        ttk.Label(connection, text="Deska:").grid(row=0, column=0, sticky="w")
        self.board_box = ttk.Combobox(connection, textvariable=self.board, values=BOARDS, state="readonly", width=29)
        self.board_box.grid(row=1, column=0, sticky="ew", padx=(0, 10))
        ttk.Label(connection, text="USB / sériový port:").grid(row=0, column=1, sticky="w")
        self.port_box = ttk.Combobox(connection, textvariable=self.port, state="readonly", width=34)
        self.port_box.grid(row=1, column=1, sticky="ew")
        self.port_button = ttk.Button(connection, text="Obnovit porty", command=self.refresh_ports)
        self.port_button.grid(row=1, column=2, padx=8)
        ttk.Label(connection, text="Rychlost:").grid(row=0, column=3, sticky="w")
        self.baud_box = ttk.Combobox(connection, textvariable=self.baud, values=BAUD_RATES, state="readonly", width=10)
        self.baud_box.grid(row=1, column=3)
        connection.columnconfigure(1, weight=1)
        data = ttk.LabelFrame(content, text="Nastavení a zprávy · GUARD-MESH / WadaMesh na T-Decku", padding=8)
        data.pack(fill="x", pady=(8, 0))
        self.preserve_box = ttk.Checkbutton(data, text="Před instalací zálohovat a automaticky obnovit nastavení a interní zprávy", variable=self.preserve)
        self.preserve_box.pack(anchor="w")
        data_actions = ttk.Frame(data)
        data_actions.pack(fill="x", pady=4)
        self.backup_button = ttk.Button(data_actions, text="Zálohovat data…", command=self.start_backup)
        self.backup_button.pack(side="left")
        self.restore_button = ttk.Button(data_actions, text="Obnovit zálohu…", command=self.start_restore)
        self.restore_button.pack(side="left", padx=6)
        self.sd_button = ttk.Button(data_actions, text="Připojit SD složku…", command=self.choose_sd)
        self.sd_button.pack(side="left")
        self.sd_clear = ttk.Button(data_actions, text="Bez SD", command=lambda: self.sd_root.set(""))
        self.sd_clear.pack(side="left", padx=6)
        ttk.Label(data_actions, textvariable=self.sd_root, width=35).pack(side="left")
        ttk.Label(data, text="USB záloha čte NVS a SPIFFS. Data na SD zůstávají na kartě; pro jejich zálohu připoj kartu k PC.",
                  wraplength=990, font=("Segoe UI", 9)).pack(anchor="w")
        self.warning = ttk.Label(content, text="Bez obnovy může kompletní instalace přepsat identitu a nastavení. Záloha a obnova kontrolují stejné rádio a datové oddíly.",
                                 foreground="#914515", wraplength=990)
        self.warning.pack(anchor="w", pady=(10, 4))
        self.confirm_box = ttk.Checkbutton(content, text="Ověřil(a) jsem správnou desku a počítám s možným přepsáním dat.",
                                          variable=self.confirm, command=self.controls)
        self.confirm_box.pack(anchor="w")
        action = ttk.Frame(content)
        action.pack(fill="x", pady=(8, 8))
        self.flash_button = ttk.Button(action, text="Nahrát firmware", style="Accent.TButton", command=self.start_flash)
        self.flash_button.pack(side="right")
        self.progress = ttk.Progressbar(action, mode="determinate", maximum=100)
        self.progress.pack(side="left", fill="x", expand=True, padx=(0, 16))
        ttk.Label(content, textvariable=self.status, wraplength=990).pack(anchor="w")
        log_frame = ttk.LabelFrame(content, text="Průběh / protokol", padding=4)
        log_frame.pack(fill="x", pady=(8, 0))
        self.log = tk.Text(log_frame, height=4, bg="#142938", fg="#d1e6e4", insertbackground="white",
                           font=("Consolas", 9), wrap="word", state="disabled", relief="flat")
        log_scroll = ttk.Scrollbar(log_frame, command=self.log.yview)
        self.log.configure(yscrollcommand=log_scroll.set)
        log_scroll.pack(side="right", fill="y")
        self.log.pack(fill="x", expand=True)
        self.controls()

    def emit(self, kind, value):
        self.events.put((kind, value))

    def background(self, operation, success):
        def work():
            try:
                self.emit(success, operation())
            except Exception as error:
                self.emit("error", str(error))
        threading.Thread(target=work, daemon=True).start()

    def scan(self):
        if self.flashing or self.scanning:
            return
        self.scanning = True
        self.invalidate()
        self.status.set("Kontroluji místní firmware a jeho kontrolní součty…")
        paths = self.library.paths(default_folders())
        self.background(lambda: sorted((inspect_entry(p) for p in paths), key=lambda e: e.modified, reverse=True), "catalog")

    def render(self):
        if self.flashing:
            return
        selected = self.selected.path if self.selected else None
        self.tree.delete(*self.tree.get_children())
        query = self.search.get().casefold().strip()
        for key, entry in self.entries.items():
            ready = entry.info and entry.info.kind == "merged"
            if self.ready_only.get() and not ready:
                continue
            if query and query not in f"{entry.title} {entry.path.name} {entry.board} {entry.info.version if entry.info else ''}".casefold():
                continue
            kind = "Instalační obraz" if ready else "Jen aplikace" if entry.info else "Chyba souboru"
            self.tree.insert("", "end", iid=key, values=(entry.title, entry.board or "Vyber ručně",
                             datetime.fromtimestamp(entry.modified).strftime("%d.%m.%Y %H:%M") if entry.modified else "—",
                             f"{entry.info.size / 1048576:.2f} MiB" if entry.info else "—", kind),
                             tags=() if ready else ("unavailable",))
            if entry.path == selected:
                self.tree.selection_set(key)
        if not self.tree.selection():
            children = self.tree.get_children()
            if children:
                self.tree.selection_set(children[0])
            else:
                self.selected = None
                self.summary.set("Žádný instalační obraz. Přidej soubor nebo složku, případně vypni filtr.")
                self.details.set("")
                self.invalidate()

    def select(self, _event=None):
        if self.flashing:
            return
        selection = self.tree.selection()
        self.selected = self.entries.get(selection[0]) if selection else None
        self.invalidate()
        entry = self.selected
        if not entry:
            return
        if entry.board in BOARDS:
            self.board.set(entry.board)
        if not entry.info:
            self.summary.set("Soubor nelze instalovat: " + entry.error)
            self.details.set(str(entry.path))
        else:
            info = entry.info
            self.summary.set(entry.title + (" — připraveno" if info.kind == "merged" else " — samotná aplikace; použij Složit obraz…"))
            label = "Metadata SDK" if info.project == "arduino-lib-builder" else "Metadata aplikace"
            self.details.set(f"{entry.path}\n{label}: {info.project or 'ESP32-S3'} · {info.version or 'verze neuvedena'} · {info.built or 'datum neuvedeno'}"
                             f"\nSHA-256: {info.sha256}")
        self.controls()

    def invalidate(self):
        self.confirm.set(False)
        self.controls()

    def controls(self):
        busy = self.flashing or self.scanning
        if not hasattr(self, "flash_button"):
            return
        for button in (self.import_button, self.folder_button, self.refresh_button, self.compose_button, self.sources_button):
            button.configure(state="disabled" if busy else "normal")
        for box in (self.board_box, self.port_box, self.baud_box):
            box.configure(state="disabled" if busy else "readonly")
        self.port_button.configure(state="disabled" if busy else "normal")
        self.confirm_box.configure(state="disabled" if busy else "normal")
        for button in (self.sd_button, self.sd_clear, self.preserve_box):
            button.configure(state="disabled" if busy else "normal")
        data_ready = not busy and self.board.get() == TDECK and self.port.get() in self.ports
        for button in (self.backup_button, self.restore_button):
            button.configure(state="normal" if data_ready else "disabled")
        entry = self.selected
        ready = (not busy and entry and entry.info and entry.info.kind == "merged" and self.confirm.get()
                 and self.port.get() in self.ports and self.board.get() in BOARDS
                 and (not entry.board or entry.board == self.board.get())
                 and (not self.preserve.get() or self.board.get() == TDECK))
        self.flash_button.configure(state="normal" if ready else "disabled")

    def refresh_ports(self):
        if self.flashing:
            return
        try:
            self.ports = {label: device for device, label in serial_ports()}
            self.port_box.configure(values=list(self.ports))
            if self.port.get() not in self.ports:
                self.port.set(next(iter(self.ports), ""))
            if not self.ports:
                self.status.set("Žádný sériový port. Připoj rádio datovým USB kabelem a obnov porty.")
        except Exception as error:
            messagebox.showerror("Sériové porty", str(error), parent=self.root)
        self.invalidate()

    def import_files(self):
        paths = filedialog.askopenfilenames(title="Přidat lokální firmware", filetypes=[("Firmware", "*.bin")], parent=self.root)
        try:
            for path in paths:
                self.library.add(path)
            if paths:
                self.ready_only.set(False)
                self.scan()
        except OSError as error:
            messagebox.showerror("Knihovna", str(error), parent=self.root)

    def import_folder(self):
        path = filedialog.askdirectory(title="Složka s verzemi firmwaru", parent=self.root)
        if path:
            try:
                self.library.add(path)
                self.scan()
            except OSError as error:
                messagebox.showerror("Knihovna", str(error), parent=self.root)

    def sources(self):
        window = tk.Toplevel(self.root)
        window.title("Složky a soubory knihovny")
        window.geometry("720x340")
        ttk.Label(window, text="Odebrání odstraní pouze odkaz z knihovny, nikoli firmware na disku.", padding=10).pack()
        items = tk.Listbox(window)
        items.pack(fill="both", expand=True, padx=10)
        values = [*self.library.folders, *self.library.files]
        for value in values:
            items.insert("end", value)
        def remove():
            if self.flashing or self.scanning or not items.curselection():
                return
            index = items.curselection()[0]
            try:
                self.library.remove(values[index])
                window.destroy()
                self.scan()
            except OSError as error:
                messagebox.showerror("Knihovna", str(error), parent=window)
        ttk.Button(window, text="Odebrat vybraný odkaz", command=remove).pack(pady=10)

    def compose(self):
        from .bundle import compose
        window = tk.Toplevel(self.root)
        window.title("Složit instalační obraz pro T-Deck / T-Deck Plus")
        window.geometry("770x330")
        ttk.Label(window, text="Vyber čtyři části ze stejného sestavení pro T-Deck / T-Deck Plus.\n"
                  "Nezaměňuj je s jinou deskou nebo rozložením oddílů. Výsledkem je kompletní instalace.", padding=14).pack(anchor="w")
        form = ttk.Frame(window, padding=12)
        form.pack(fill="x")
        variables = []
        for row, name in enumerate(("bootloader.bin", "partitions.bin", "boot_app0.bin", "firmware.bin")):
            var = tk.StringVar()
            if name == "firmware.bin" and self.selected and self.selected.info and self.selected.info.kind == "application":
                var.set(str(self.selected.path))
            variables.append(var)
            ttk.Label(form, text=name).grid(row=row, column=0, sticky="w", pady=5)
            ttk.Entry(form, textvariable=var, width=66).grid(row=row, column=1, padx=8)
            def choose(value=var, label=name):
                chosen = filedialog.askopenfilename(title=label, filetypes=[("Firmware", "*.bin")], parent=window)
                if chosen:
                    value.set(chosen)
            ttk.Button(form, text="Vybrat…", command=choose).grid(row=row, column=2)
        def save():
            if self.flashing or self.scanning:
                return
            target = filedialog.asksaveasfilename(title="Uložit kompletní obraz", defaultextension=".bin",
                                                 initialfile="T-Deck-merged.bin", filetypes=[("Firmware", "*.bin")], parent=window)
            if not target:
                return
            try:
                compose(*(v.get() for v in variables), target)
                self.library.add(target)
                window.destroy()
                self.ready_only.set(True)
                self.scan()
            except (OSError, ValueError) as error:
                messagebox.showerror("Složení obrazu", str(error), parent=window)
        ttk.Button(window, text="Složit a přidat do knihovny", command=save).pack(pady=12)

    def start_flash(self):
        if str(self.flash_button.cget("state")) == "disabled":
            return
        entry = self.selected
        backup = self.new_backup_path() if self.preserve.get() else ""
        request = FlashRequest(str(entry.path), entry.info.sha256, self.ports[self.port.get()], int(self.baud.get()), self.board.get(),
                               self.preserve.get(), backup, self.sd_root.get() if self.preserve.get() else "")
        preservation = (f"Před zápisem se uloží záloha a po instalaci se obnoví data.\nZáloha: {backup}\n"
                        "Při nekompatibilních oddílech nebo chybě zálohy se firmware nezapíše.\n" if backup else
                        "Záloha je vypnutá. Identita a nastavení mohou být přepsány.\n")
        text = (f"{entry.title}\n\nDeska: {request.board}\nPort: {request.port}\n"
                f"Zápis: kompletní obraz od 0x0\nSHA-256: {request.sha256[:16]}…\n\n"
                f"{preservation}Čip ESP32-S3 neurčuje model desky.\n"
                "Během zápisu neodpojuj kabel. Spustit instalaci?")
        if not messagebox.askokcancel("Nahrát vybraný firmware?", text, parent=self.root, default="cancel"):
            return
        self.begin_job(request, entry.title)

    def new_backup_path(self):
        return str(self.library.home / "backups" / (datetime.now().strftime("radio-%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:8] + ".gmbak"))

    def choose_sd(self):
        if self.flashing or self.scanning:
            return
        path = filedialog.askdirectory(title="Kořen SD karty (obsahuje meshcomod; pro obnovu může být prázdný)", parent=self.root)
        if path:
            self.sd_root.set(path)

    def start_backup(self):
        if str(self.backup_button.cget("state")) == "disabled":
            return
        path = filedialog.asksaveasfilename(title="Uložit zálohu nastavení a zpráv", defaultextension=".gmbak",
                  initialfile=Path(self.new_backup_path()).name, filetypes=[("Záloha GUARD-MESH", "*.gmbak")], parent=self.root)
        if not path:
            return
        if Path(path).exists():
            messagebox.showerror("Záloha už existuje", "Vyber nový název; dřívější zálohy se nepřepisují.", parent=self.root)
            return
        request = DataRequest("backup", path, self.ports[self.port.get()], int(self.baud.get()), self.board.get(), sd_root=self.sd_root.get())
        self.begin_job(request, "Záloha nastavení a zpráv")

    def start_restore(self):
        if str(self.restore_button.cget("state")) == "disabled":
            return
        path = filedialog.askopenfilename(title="Obnovit zálohu stejného rádia", filetypes=[("Záloha GUARD-MESH", "*.gmbak")], parent=self.root)
        if not path:
            return
        try:
            digest = file_hash(path)
            meta, _, _ = load_backup(path)
            if meta["sd_included"] and not self.sd_root.get():
                raise ValueError("Záloha obsahuje SD data. Nejdříve připoj kartu k PC a vyber její složku.")
            text = (f"Záloha: {Path(path).name}\nZařízení: {meta['mac']}\nVytvořeno: {meta['created']}\n"
                    f"Port: {self.port.get()}\nSD data: {'ano' if meta['sd_included'] else 'ne'}\n\n"
                    "Obnova nahradí nastavení, identitu a zprávy stavem ze zálohy. Aktuální data se nejdříve zálohují.\n"
                    "Jiné rádio nebo nekompatibilní oddíly budou odmítnuty. Pokračovat?")
            if not messagebox.askokcancel("Obnovit nastavení a zprávy?", text, parent=self.root, default="cancel"):
                return
            request = DataRequest("restore", path, self.ports[self.port.get()], int(self.baud.get()), self.board.get(),
                                  digest, self.sd_root.get(), str(self.library.home / "backups"))
            self.begin_job(request, "Obnova nastavení a zpráv")
        except (OSError, ValueError) as error:
            messagebox.showerror("Obnova zálohy", str(error), parent=self.root)

    def begin_job(self, request, title):
        self.flashing = True
        self.confirm.set(False)
        self.controls()
        self.progress["value"] = 0
        self.progress.configure(mode="indeterminate")
        self.progress.start(12)
        self.status.set("Připravuji ověřené soubory a připojuji rádio…")
        self.append_log(f"\n--- {title} → {request.port} ---\n")
        self.background(lambda: flash(request, self.emit, self.library.home / "logs"), "flashed")

    def append_log(self, text):
        self.log.configure(state="normal")
        self.log.insert("end", text.replace("\r", "\n"))
        if int(self.log.index("end-1c").split(".")[0]) > 1600:
            self.log.delete("1.0", "400.0")
        self.log.see("end")
        self.log.configure(state="disabled")
        matches = re.findall(r"\((\d+)\s*%\)", text)
        if matches and self.flashing:
            self.progress.stop()
            self.progress.configure(mode="determinate")
            self.progress["value"] = min(100, int(matches[-1]))
            self.status.set("Probíhá přenos a ověřování dat; neodpojuj rádio.")

    def poll(self):
        if self._poll_token:
            self.root.after_cancel(self._poll_token)
            self._poll_token = None
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == "catalog":
                    self.scanning = False
                    self.entries = {str(i): entry for i, entry in enumerate(value)}
                    self.render()
                    ready = sum(bool(e.info and e.info.kind == "merged") for e in value)
                    self.status.set(f"Načteno {len(value)} souborů, {ready} instalačních obrazů. Vyber verzi a USB port.")
                    self.controls()
                    if self.smoke_report:
                        self.root.after(300, self.smoke)
                elif kind == "log":
                    self.append_log(value)
                elif kind == "flashed":
                    self.flashing = False
                    self.progress.stop()
                    self.progress.configure(mode="determinate")
                    self.progress["value"] = 100
                    operation = value.get("operation", "flash")
                    done = {"flash": "Firmware byl zapsán a ověřen.", "backup": "Záloha byla uložena a ověřena.",
                            "restore": "Nastavení a zprávy byly obnoveny a ověřeny."}[operation]
                    details = f"{done}\n\nProtokol: {value['log']}"
                    if value.get("backup"):
                        details += f"\nZáloha: {value['backup']}"
                    if self.sd_root.get():
                        details += "\nVrať SD kartu do rádia a restartuj ho."
                    self.status.set("Hotovo. " + done)
                    self.append_log("\n" + details + "\n")
                    self.controls()
                    messagebox.showinfo("Operace dokončena", details, parent=self.root)
                elif kind == "error":
                    self.scanning = self.flashing = False
                    self.progress.stop()
                    self.progress.configure(mode="determinate")
                    self.progress["value"] = 0
                    self.confirm.set(False)
                    self.status.set("Operace se nezdařila. Podrobnosti jsou v protokolu.")
                    self.append_log(f"\nCHYBA: {value}\n")
                    self.controls()
                    messagebox.showerror("Operace se nezdařila", value, parent=self.root)
        except queue.Empty:
            pass
        self._poll_token = self.root.after(80, self.poll)

    def smoke(self):
        # Exercise actual widgets without opening a serial port or invoking esptool.
        report = {"version": __version__, "entries": len(self.entries), "rows": len(self.tree.get_children()),
                  "flash_disabled_without_confirmation": str(self.flash_button.cget("state")) == "disabled"}
        report["backup_default_on"] = self.preserve.get()
        report["data_controls_visible"] = all(x.winfo_manager() for x in (self.backup_button, self.restore_button, self.sd_button))
        self.ready_only.set(False)
        self.render()
        report["all_rows"] = len(self.tree.get_children())
        self.search.set("__no_firmware_matches__")
        report["search_empty"] = len(self.tree.get_children()) == 0
        self.search.set("")
        self.ready_only.set(True)
        self.render()
        self.root.update_idletasks()
        report["log_visible"] = self.log.winfo_rooty() + self.log.winfo_height() <= self.root.winfo_rooty() + self.root.winfo_height()
        report["ok"] = all(report[k] for k in ("flash_disabled_without_confirmation", "search_empty", "log_visible", "backup_default_on", "data_controls_visible"))
        Path(self.smoke_report).write_text(json.dumps(report, indent=2), encoding="utf-8")
        self.root.after(100, self.destroy)

    def help(self):
        messagebox.showinfo("Guard-Mesh-Flasher — nápověda",
            "1. Vyber místní kompletní obraz nebo přidej .bin / složku.\n"
            "2. Zkontroluj desku; T-Deck Plus a T-Deck Pro jsou odlišné.\n"
            "3. Připoj datový USB kabel, obnov porty a vyber port.\n"
            "4. Nech zapnuté zachování dat, potvrď volbu a stiskni Nahrát firmware.\n\n"
            "T-Deck / Plus: nastavení, identita a interní zprávy se uloží do .gmbak a po instalaci obnoví. "
            "Zálohovat data… funguje i bez instalace; Obnovit zálohu… vrátí uložený stav na stejné rádio. "
            "Při obnově se nejprve uloží bezpečnostní kopie aktuálního stavu.\n\n"
            "Zprávy na SD: připoj kartu k PC a vyber její kořen pomocí Připojit SD složku…; "
            "ukládá se složka meshcomod. Bez výběru karty USB záloha SD nezahrnuje. "
            "Záloha není šifrovaná a obsahuje soukromou identitu i přístupové údaje. Nesdílej ji.\n\n"
            "Pokud se rádio nepřipojí: zavři sériový monitor, zkus 115200 baud, jiný kabel/USB port. "
            "Podle návodu desky podrž BOOT při resetu, pak obnov seznam portů.\n\n"
            "Samotný firmware.bin je aplikace, ne kompletní instalace. Složit obraz… vyžaduje čtyři odpovídající části "
            "sestavení T-Decku. Ostatní desky ESP32-S3 přijímají hotový sloučený obraz. Tanmatsu / P4 nejsou podporované.\n\n"
            "Složky se procházejí bez podadresářů. Původní soubory se nekopírují ani nemažou. "
            "Knihovna a protokoly: " + str(self.library.home), parent=self.root)

    def close(self):
        if self.flashing:
            messagebox.showinfo("Probíhá operace", "Počkej na dokončení nebo chybu operace. Neodpojuj USB kabel.", parent=self.root)
            return
        self.destroy()

    def destroy(self):
        self.progress.stop()
        if self._poll_token:
            self.root.after_cancel(self._poll_token)
            self._poll_token = None
        self.root.destroy()


def run(data_dir=None, smoke_report=None):
    root = tk.Tk()
    Application(root, Library(data_dir), smoke_report)
    root.mainloop()
    return 0
