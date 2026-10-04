Firmware pro **LilyGo T-Deck / T-Deck Plus** navazující na `guardian-2026.09.29.1`.

### Novinky

- Nastavení aplikace Guardian má volitelné zvukové upozornění na novou příchozí zprávu: dvakrát přehraje nastavený zvuk zprávy. Volba je ve výchozím stavu vypnutá a respektuje celkové ztlumení i režim Nerušit.
- Dvojité klepnutí v historii kanálu nebo soukromé konverzace skočí k nejnovější zprávě.
- Posouvání dlouhé historie méně zatěžuje rozložení obrazovky: viditelný úsek se vyhledává binárně a neměnná scrollovací plocha se nenastavuje opakovaně.

### Soubory

- **app-ota.bin**: aktualizace aplikace. Nezapisovat od adresy `0x0`.
- **merged.bin + merged.json**: úplná instalace od `0x0`, včetně oblasti NVS. Před plnou instalací zálohujte nastavení a zprávy.
- **debug-elf.zip**: odpovídající ELF pro diagnostiku.
- **SHA256SUMS.txt**: kontrolní součty souborů.

Ověřeno regresními testy Guardianu a plným průchodem simulátoru včetně dlouhé historie zpráv. Fyzické ověření tohoto sestavení na T-Decku zatím neproběhlo.
