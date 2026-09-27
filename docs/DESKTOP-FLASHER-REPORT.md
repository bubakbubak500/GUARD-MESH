# Guard-Mesh-Flasher 1.0.0 — předání 21. 9. 2026

Samostatná desktopová aplikace v Pythonu/Tk. Windows distribuce obsahuje vlastní
runtime, pySerial 3.5 a esptool 4.8.1; nevyžaduje web ani instalaci Pythonu.
Návod a postup sestavení jsou v [desktop-flasher/README.md](../desktop-flasher/README.md).

## Hotové části

- Knihovna místních `.bin`, přidání jednotlivých souborů nebo složek, hledání,
  filtr instalačních obrazů a obnova seznamu. Odkazy se uchovávají mezi spuštěními.
  Odebrání zdroje nemaže původní soubory. Starší archiv webového flasheru je
  podporovaný, pokud se přidá jeho `.flasher-cache` adresář.
- Kontrola ESP32-S3 obrazů, segmentů, checksumů/SHA-256, MD5 a rozsahů tabulky
  oddílů. Samotná aplikace je viditelná, ale není zaměňována s úplnou instalací.
- Složení kompletního obrazu ze čtyř explicitně vybraných odpovídajících částí
  T-Deck sestavení; kontrola podporovaného 16MiB rozložení oddílů.
- Sériové porty, rychlosti 115200/460800/921600, výběr desky, potvrzení konkrétní
  instalace, průběh, chyby a trvalé protokoly. Kontrola změny souboru po výběru;
  zápis pracuje se samostatnou ověřenou kopií.
- Oddělený proces esptool, kontrola čipu a flash kapacity před zápisem.
  Mezi kontrolou a zápisem zůstává stub aktivní, aby firmware předčasně
  nenabootoval a nepřepnul nativní USB port. Úspěch se hlásí až po úspěšném
  dokončení esptool včetně ověření zápisu.
- Instalátor Windows x64 pro aktuálního uživatele, Start menu, volitelný
  zástupce na ploše a odinstalátor; také portable ZIP a archiv zdrojů aplikace.

## Přibalený firmware

T-Deck / T-Deck Plus, sestavení `20260921_073903-9782e12-dirty` z dokončené
iterace refaktoru. Aplikační část se bajtově shoduje s dříve ověřeným výstupem.
Byla doplněna o bootloader, tabulku a `boot_app0` z odpovídajícího build prostředí.

- Aplikace: `out/LilyGo_TDeck_companion_radio_touch-20260921_073903-9782e12-dirty.bin`
- Kompletní obraz: `out/Guard-Mesh-TDeck-20260921_073903-merged.bin` (3 657 568 B)
- SHA-256 kompletního obrazu: `6733d26766d00dc0ebc77f167e23445d6da230b4b43671dfd53f247bf5e0e004`
- SHA-256 aplikace: `d2f7a881685904cd48ff5492a31c47ba700b56377d2eba797319f553352b05eb`

Obraz je součástí instalátoru i portable distribuce. **Není pro T-Deck Pro.**
Kompletní instalace zapisuje od 0 a může přepsat identitu/nastavení; před nahráním
je potřeba záloha. Zachování uživatelských dat tato verze neslibuje.

## Ověření

- 15 automatických testů modelů, knihovny, flashovacího procesu a skutečných Tk
  widgetů: poškození a zkrácení obrazů, překryv oddílů, kapacita flash, nesoulad
  sidecaru, obnova knihovny, vadná konfigurace, změna zdroje, neměnná pracovní
  kopie, odmítnutí samotné aplikace, zrušení potvrzení a chyba USB.
- Tk smoke test zdrojové i zabalené aplikace: načtení knihovny, hledání,
  potvrzovací podmínka a viditelnost protokolu; bez otevření skutečného portu.
- Skutečné Windows okno bylo zobrazeno a vizuálně zkontrolováno. Protokol je
  celý viditelný, dlouhý název a cesta přibaleného obrazu zobrazené.
- Instalátor byl spuštěn do izolovaného testovacího adresáře v cache; ověřeno
  spuštění instalované aplikace a odinstalování. Testovací instalace odstraněna.
- Zabalený esptool spuštěn s výslovně neexistujícím portem: správný návratový
  kód 1, neúspěšný výsledek a protokol místo falešného úspěchu.

Lokální důkazy: `.flasher-cache/desktop-tests-final.log`,
`desktop-build-final.log`, `desktop-installed-final-smoke.json`,
`desktop-installer-final.log`, `desktop-uninstall-final.log`
a `.flasher-cache/desktop-worker-test/{output.log,result.json}`.
Kontrolní součty distribučních balíčků jsou v `out/flasher/SHA256SUMS.json`.

## Co ještě není ověřené nebo podporované

Skutečné rádio nebylo flashováno. Fyzický USB zápis, ovladače jednotlivých desek,
bootloader režimy a start firmwaru jsou následný integrační krok uživatele.
Linux/macOS mají přenositelné zdroje, ale v této relaci nemají ověřené nativní
balíčky ani instalátory. Windows balíček není podepsaný certifikátem vydavatele.
Není implementováno stahování vzdálených vydání, kompilace firmwaru přímo z GUI
ani aktualizace slibující zachování nastavení.

Flasher je oddělený od firmwarového UI a jeho pozastavený refaktor se neobnovil.
Nevznikl commit, push ani publikované vydání.
