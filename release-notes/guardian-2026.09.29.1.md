Firmware pro **LilyGo T-Deck / T-Deck Plus** navazující na `guardian-2026.09.29`.

### Oprava

- Opraveno riziko pádu při posouvání a zoomování mapy: načítání a překreslení
  dlaždic probíhá po dokončení obsluhy LVGL, s menší spotřebou zásobníku.
- Rychlé pohyby se sloučí do aktuálního pohledu; čekající překreslení se při
  zavření mapy zruší. Výběr kontaktů na mapě spotřebuje méně zásobníku.
- Zachovány úpravy Guardianu z předchozího vydání.

### Soubory

- **app-ota.bin**: aplikace pro aktualizaci/OTA; nezapisovat od adresy `0x0`.
- **merged.bin + merged.json**: úplná instalace od `0x0`, včetně oblasti NVS.
  Před plnou instalací použijte zálohu nastavení a zpráv ve flasheru.
- **debug-elf.zip**: odpovídající ELF se symboly pro diagnostiku případného pádu.
- **SHA256SUMS.txt**: kontrolní součty souborů.

Ověřeno sestavením PlatformIO, kompletními průchody simulátorem EN/CS,
24 taženími/změnami zoomu v každém průchodu a testy PNG dlaždic, životnosti
objektů i odloženého vykreslení. Ověřeny také formáty a části instalačních
obrazů. **Fyzické ověření tohoto nového sestavení na T-Decku zatím neproběhlo.**

[Podrobnosti diagnostiky a opravy](https://github.com/bubakbubak500/GUARD-MESH/blob/guardian-2026.09.29.1/docs/MAP-SCROLL-FIX-2026-09-29.md).
Flasher 1.1.0 zůstává beze změny; načtěte do něj nový firmware tohoto vydání.
