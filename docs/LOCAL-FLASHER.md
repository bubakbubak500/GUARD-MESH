# Lokální instalace GUARD-MESH

Pro provoz bez prohlížeče je k dispozici [Guard-Mesh-Flasher](../desktop-flasher/README.md)
s nativním oknem, knihovnou místních verzí a instalátorem Windows. Níže je původní webová varianta.

Potřeba: Python 3.9+, Git a desktopový Chrome nebo Edge s Web Serial.

1. Spusť `Start-Flasher.cmd` na Windows nebo `sh start-flasher.sh` na Linuxu.
   Otevře se `http://localhost:8766`.
2. Klikni **Sestavit aktuální firmware pro T-Deck**. První běh si do
   `.flasher-cache` nainstaluje PlatformIO a stáhne build nástroje a knihovny;
   potřebuje internet a může trvat několik minut. Průběh je přímo na stránce.
3. Hotový obraz se načte automaticky. Ověř **T-Deck / T-Deck Plus**,
   zálohuj nastavení a potvrď přepsání. Build není pro T-Deck Pro.
4. Připoj datový USB kabel, klikni **Připojit rádio a instalovat**, vyber port
   a dokonči postup ESP Web Tools. Zavři případný sériový monitor.

Sestavují se aktuální **lokální zdroje**, včetně necommitnutých změn; instalátor
neprovádí automatický Git pull. Další buildy používají cache. Úspěšné obrazy se uchovávají i po restartu instalátoru. V nabídce
**Vyber uložené sestavení** lze vybrat novou i starší verzi podle data,
zdrojového commitu a identifikátoru buildu. Jde o místní sestavení, nikoli
seznam upstream vydání. Při výběru se ověří SHA-256 a načte konkrétní obraz.
Změna verze vždy zruší předchozí potvrzení instalace.
Po změně zdrojů stiskni sestavení znovu. Chybný build nenabídne starý obraz jako
nový; starší úspěšný obraz lze dál výslovně vybrat z historie. Při přerušení serveru během buildu před restartem počkej na ukončení jeho
build procesu. Nespouštěj více instancí instalátoru nad stejným repozitářem.

Pro jinou podporovanou desku zůstává ruční výběr `firmware-merged.bin`.
Samotný `firmware.bin` instalátor odmítá. Tanmatsu a T-Display P4 nejsou podporované.

## Co se zapisuje

**Kompletní obraz se zapisuje od adresy 0 a může přepsat identitu, kontakty
či Wi-Fi nastavení i bez volby „Erase device“. Nejde o aktualizaci zachovávající
nastavení.** Zálohuj data před instalací. Pro zachování NVS je potřeba postup
s oddělenými obrazy a správnými adresami, který tato verze nenabízí.

Kontrola ověřuje strukturu obrazu ESP32-S3 a build kontroluje velikost aplikace
vůči OTA oddílu T-Decku. Není to ověření funkčnosti firmwaru na skutečné desce.
ESP Web Tools ověřuje rodinu čipu, nikoli konkrétní desku se stejným ESP32-S3.

## Lokální provoz

Server poslouchá pouze na `127.0.0.1`. API spouští jen pevně určený T-Deck build;
nepřijímá příkazy ani cesty od prohlížeče. Ochrana původu požadavku a token brání
spuštění buildu z cizí webové stránky. Soubory se neposílají na externí server.
Samotná instalace má JavaScript přibalený a nevyžaduje internet.

Alternativní spuštění: `python scripts/local-flasher.py --port 8766`.
Server v terminálu ukončíš `Ctrl+C`. Na Linuxu musí mít uživatel přístup k portu.
PlatformIO se instaluje do lokálního Python prostředí, nikoli globálně.

Knihovna: [ESP Web Tools](https://esphome.github.io/esp-web-tools/), verze 10.4.0;
původ a licence jsou v `deploy/flasher/vendor/esp-web-tools/PROVENANCE.md`.
