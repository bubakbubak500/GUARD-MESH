# Lokální instalace GUARD-MESH

Potřeba: Python 3.9+ a desktopový Chrome nebo Edge s Web Serial.
JavaScript instalátoru je přibalený; při instalaci není potřeba internet.
Sestavení firmwaru může potřebovat stažení závislostí přes PlatformIO.

1. Sestav správnou desku, například:

   ```sh
   pio run -e LilyGo_TDeck_companion_radio_touch -t mergebin
   ```

2. Na Windows spusť `Start-Flasher.cmd`. Na Linuxu spusť `sh start-flasher.sh`.
   Otevře se `http://localhost:8766`. Alternativně:

   ```sh
   python scripts/local-flasher.py --port 8766
   ```

3. Vyber přesnou desku a její soubor
   `.pio/build/<environment>/firmware-merged.bin`.
4. Zálohuj nastavení, potvrď správnost obrazu, připoj USB a vyber sériový port.
   Další kroky instalace nabídne ESP Web Tools.
5. Server ukončíš `Ctrl+C` v okně, ve kterém běží.

Instalátor neposkytuje žádné předpřipravené binárky ani automatické stahování.
Python servíruje pouze soubory instalátoru na lokálním rozhraní. Firmware se
načítá z vybraného souboru přímo do prohlížeče, nikam se nenahrává.

## Rozsah první verze

- Pouze kompletní sloučené obrazy **ESP32-S3**, zápis od adresy `0x0`.
- Ověří základní hlavičky bootloaderu a aplikace a tabulku oddílů. Jde o
  kontrolu struktury, nikoliv podpisu, bezchybnosti nebo kompatibility s deskou.
- ESP Web Tools kontroluje rodinu připojeného čipu. Stejný ESP32-S3 ale používá
  více desek; správnou desku a variantu rádia musí určit uživatel.
- **Sloučený obraz přepisuje také mezery mezi oddíly, tedy i NVS.** Může dojít
  ke ztrátě identity, kontaktů a Wi-Fi nastavení, i když nezaškrtneš úplné
  vymazání čipu. Toto není režim aktualizace zachovávající nastavení.
- Samotný `firmware.bin` je odmítnut. Tanmatsu má vlastní AppFS instalační
  postup; T-Display P4 zatím není součástí tohoto instalátoru.

Pro zachování NVS použij odpovídající původní postup s oddělenými obrazy a
správnými adresami pro danou desku. Jednotnou aktualizaci a zálohy doplní
budoucí Guardian utilita.

Na Linuxu musí uživatel mít přístup k sériovému portu. Pokud je obsazený,
zavři sériový monitor. Pro jiný port serveru použij `--port 8767`.

Knihovna: [ESP Web Tools](https://esphome.github.io/esp-web-tools/), verze 10.4.0;
původ a licence jsou v `deploy/flasher/vendor/esp-web-tools/PROVENANCE.md`.
