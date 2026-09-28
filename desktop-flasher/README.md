# Guard-Mesh-Flasher

Nativní desktopový flasher pro Windows, napsaný v Pythonu/Tk. Provoz nevyžaduje
prohlížeč, server ani připojení k internetu. Instalátor a portable balíček obsahují
Python, Tk, pySerial a esptool. Firmware se vybírá z místních souborů.

## Windows

Spusť `Guard-Mesh-Flasher-1.1.0-Windows-x64-Setup.exe`. Instalace je pro aktuálního
uživatele, bez požadavku na administrátora, se zástupcem v nabídce Start a
odinstalátorem. Alternativně rozbal portable ZIP a spusť `Guard-Mesh-Flasher.exe`;
ponech u něj celou složku `_internal`. Balíček není digitálně podepsaný.

1. Vyber instalační obraz v seznamu. Poslední T-Deck build je přibalený, pokud
   byl balíček sestaven s `--firmware-dir`. Verze jsou označené názvem souboru či
   metadaty sestavení; staré údaje `arduino-lib-builder` jsou označené jako SDK.
2. **Přidat firmware…** přijme soubory odkudkoliv. **Přidat složku…** si zapamatuje
   adresář s verzemi. **Obnovit** najde nové soubory; podadresáře se neprocházejí.
   **Zdroje…** umožní odebrat odkaz bez smazání původních souborů.
3. Ověř desku. T-Deck / T-Deck Plus není T-Deck Pro. Připoj datový USB kabel,
   obnov porty a vyber port. Při potížích zkus 115200 baud nebo bootloader režim
   podle návodu desky; zavři ostatní programy používající daný port.
4. Nech zapnuté **Před instalací zálohovat a automaticky obnovit nastavení a interní zprávy**,
   potvrď volbu a stiskni **Nahrát firmware**. Finální dialog
   ukáže konkrétní verzi, desku a port. Během zápisu neodpojuj kabel.
5. Aplikace zkontroluje čip i kapacitu flash, zapíše obraz, esptool ověří data
   a resetuje zařízení. Chyba se nezobrazuje jako úspěch. Protokol se ukládá.

**Bez zapnutého zachování dat může kompletní obraz od adresy 0 přepsat identitu
a nastavení i bez celkového mazání flash.** Nepoužívá
`erase_flash`, `--force`, zápis eFuse ani obcházení ochran zabezpečeného zařízení.
ESP32-S3 samo neurčuje model desky; výběr správného obrazu/desky je nutný.

Podporované jsou raw merged obrazy ESP32-S3 s bootloaderem na 0, tabulkou oddílů
na 0x8000 a úplnou factory/OTA0 aplikací. Kontroluje se rozsah a překryv oddílů,
MD5 tabulky, segmentové checksumy, SHA-256 obrazu a případné SHA-256 v metadatech.
Nejde o ověření autora ani digitální podpis. ESP32 (bez S3), ESP8266, P4 a
Tanmatsu nejsou podporované. Nejde o stahovač internetových vydání ani IDE.

## Záloha a obnova (od 1.1.0)

Pro **T-Deck / T-Deck Plus, 16 MiB, GUARD-MESH nebo WadaMesh**:

- Zapnutá volba zachování dat před instalací načte a ověří NVS a SPIFFS,
  uloží samostatný `.gmbak`, zapíše firmware, obnoví datové oddíly a ověří je.
  Rádio se spustí až po dokončení. Bez platné a uložené zálohy se zápis nespustí.
- **Zálohovat data…** uloží stav rádia bez instalace firmwaru.
- **Obnovit zálohu…** importuje `.gmbak` do stejného rádia. Nejdříve uloží aktuální
  stav do nové bezpečnostní zálohy. Obnoví se identita, nastavení, kontakty,
  kanály a interně uložené zprávy; nezapisuje se firmware, výběr OTA ani mapový oddíl.
- Při chybě po zahájení zápisu zůstává záloha uložená a její cesta je v protokolu.
  Po opětovném připojení lze použít **Obnovit zálohu…**. Pokud je poškozený i firmware,
  nejdříve nahraj správný kompletní obraz s vypnutým zachováním dat, pak obnov původní zálohu.

Kontroluje se MAC stejného rádia, velikost flash, typ, umístění a velikost NVS/SPIFFS,
MD5 tabulky oddílů a SHA-256 souborů. Rozdílné datové oddíly, šifrovaná flash,
Secure Boot nebo poškozená záloha se odmítnou před zápisem. Rozložení dat bylo
porovnáno se současným firmwarem a starším WadaMesh `beta_83`; libovolné jiné
rozložení nebo přenos identity na jiné rádio nejsou podporované. Obnova vrací
celý uložený stav, neslučuje nové zprávy se starými. Jiné desky mohou nadále
použít instalaci bez zachování dat. Přenos přes skutečné USB rádio je třeba
ověřit na zařízení; automatické testy používají simulovanou flash a skutečné soubory.

**Zprávy na SD:** USB flasher neumí kartu v rádiu přečíst. Chceš-li ji zahrnout,
připoj ji čtečkou k PC a přes **Připojit SD složku…** vyber její kořen.
Zálohuje se složka `meshcomod`, nikoli celá karta či mapy. Bez této volby
zůstanou SD data na kartě, ale nejsou v záloze. Archiv se SD daty při importu
vyžaduje připojenou cílovou kartu. Původní složka se zachová vedle obnovené jako
`meshcomod-before-restore-…`. Po operaci kartu vrať do rádia a restartuj jej.
Limit zálohy je 544 MiB a 10 000 datových souborů. Během zálohy neměň SD soubory.

Automatické a bezpečnostní zálohy jsou v `backups/` v adresáři dat aplikace
(na Windows `%LOCALAPPDATA%\Guard-Mesh-Flasher\backups`). **Záloha není šifrovaná,
obsahuje soukromou identitu rádia, zprávy a případná hesla. Nesdílej ji veřejně.**

## Samotné aplikace a lokální sestavení

`out/<environment>-<datum>-<commit>.bin` je zpravidla **jen aplikace**.
Po vypnutí filtru **Jen instalační obrazy** je viditelná, ale nelze ji omylem
zapsat na adresu bootloaderu. **Složit obraz…** umí vytvořit plnou instalaci
pro současné 16MiB rozložení GUARD-MESH T-Deck / T-Deck Plus. Vyžaduje odpovídající
`bootloader.bin`, `partitions.bin`, `boot_app0.bin` a aplikaci ze stejného
sestavení. Nepoužívej části jiné desky ani neověřenou kombinaci starých buildů.
Složení neflashuje rádio; vytvoří `.bin` a `.json` s kontrolním součtem.

Bootloader a tabulka jsou při sestavení v `.pio/build/LilyGo_TDeck_companion_radio_touch/`.
`boot_app0.bin` dodává Arduino framework v `tools/partitions/`.
Hotový merged obraz lze také sestavit stávajícím PlatformIO targetem `mergebin`.
Při spuštění ze zdrojů se automaticky načtou `out/` a historie webového flasheru
v `.flasher-cache/`; zabalená aplikace navíc načítá přibalenou složku `firmware/`
a složku `firmware/` vedle EXE. Další adresáře přidej tlačítkem.

Volitelná metadata mají stejný název jako obraz s příponou `.json`:

```json
{"title": "T-Deck · moje sestavení", "board": "LilyGo T-Deck / T-Deck Plus", "sha256": "64 hex číslic"}
```

## Data aplikace

- Windows: `%LOCALAPPDATA%\Guard-Mesh-Flasher`
- Linux: `$XDG_DATA_HOME/Guard-Mesh-Flasher` nebo `~/.local/share/Guard-Mesh-Flasher`
- macOS: `~/Library/Application Support/Guard-Mesh-Flasher`

`library.json` obsahuje jen odkazy na soubory/složky, `logs/` protokoly instalací,
`backups/` automatické zálohy. Odinstalátor knihovnu, protokoly, zálohy ani vlastní firmware nemaže. Před zápisem
se obraz znovu ověří a zkopíruje do dočasného pracovního adresáře, takže změna
původního souboru během zápisu nezmění běžící instalaci.

## Spuštění ze zdrojů a další systémy

Python 3.10+ s Tk. Na Debian/Ubuntu je Tk samostatný balíček `python3-tk`;
uživatel musí mít oprávnění k sériovému portu. Na macOS lze použít Python s Tk
z python.org. Z adresáře `desktop-flasher`:

```text
python -m venv .venv
# Aktivuj .venv podle systému.
python -m pip install -r requirements.txt
python main.py
```

Python zdroje používají přenositelné Tk a pySerial. Windows balíček je sestaven
a ověřován na Windows; Linux a macOS je třeba ještě ověřit na cílovém systému.
Binární distribuci sestavuj samostatně na každém OS.

## Sestavení distribuce a testy

```text
python -m pip install -r requirements-build.txt
python -m unittest discover -s tests -v
python main.py --smoke-test smoke.json --data-dir test-data
python build.py --firmware-dir cesta/k/overenym/merged-obrazum --iscc cesta/k/ISCC.exe
```

Inno Setup 6.7.3 je potřeba jen pro Windows instalátor. Bez `--iscc` vznikne
portable ZIP; `--firmware-dir` je volitelný. Výstupy jsou v `out/flasher/`, cache
v `.flasher-cache/desktop-package/`. Build zachová zdroje a nevykonává zápis na rádio.
`--smoke-test` ověřuje skutečné Tk widgety bez přístupu k sériovému zařízení.

Licence projektu je GPL-3.0-or-later; licence přibalených závislostí jsou v
`_internal/licenses/`. Zdrojové kódy flasheru jsou v `desktop-flasher/` tohoto
repozitáře, zdroje firmwaru v témže repozitáři. Při další distribuci přilož
odpovídající zdrojové kódy včetně lokálních změn a licenčních podmínek.

Podklady: [esptool 4 – zápis firmwaru](https://docs.espressif.com/projects/esptool/en/release-v4/esp32s3/esptool/basic-commands.html),
[PyInstaller – sestavení pro cílový OS](https://pyinstaller.org/en/stable/operating-mode.html),
[Inno Setup](https://jrsoftware.org/isinfo.php).
