# Guardian BLE v1 na T-Decku

Implementace pro **LilyGo T-Deck / T-Deck Plus**, 28. 9. 2026. Vychází z místního
`main` s posledními opravami Home a z [kontraktu Guardian 1.1.12](GUARD_MESH_BLE_V1_CS.md).
PC je BLE Central; T-Deck inzeruje službu a přijímá počty a stav Guardianu.
Wi-Fi k propojení není potřeba.

## Připojení

1. Na zařízení otevři **Aplikace → Guardian**, případně **Nastavení → Guardian BLE**
   nebo kartu Guardian na hlavní obrazovce.
2. Stiskni **Spárovat PC (2 min)**. Tím se zapne Bluetooth i režim Guardianu
   a na dvě minuty se povolí nové párování. Jméno `GuardMesh-XXXX` je na obrazovce.
3. V Guardianu 1.1.12 na Windows otevři **Provoz → Guard Mesh — BLE…**, zvol
   **Vyhledat BLE**, vyber zařízení podle jména a **Spárovat a připojit**.
4. Při dalším připojení stačí zapnutý Guardian BLE a uložený bond. Nové párování
   ani náhrada existujícího bondu nejsou mimo párovací okno povolené.

**Zapnout** umožňuje návrat již spárovanému PC. **Vypnout** v aplikaci ukončí
PC spojení a vrátí BLE do dosavadního režimu MeshCore pro telefon. Vypnutí
Bluetooth v ovládacím panelu vypne celé BLE; volba Guardianu zůstává uložená.
Párovací okno se po restartu neobnovuje. Režim externí Bluetooth klávesnice
je potřeba nejprve vypnout. Vestavěná klávesnice T-Decku funguje dál.

Telefon a Guardian se v této verzi přes BLE používají střídavě. Mesh rádio,
USB i další existující transporty zůstávají k dispozici. Aplikace zobrazuje TX,
RX, inbox, nepřečtené, outbox, stáří snímku a příznaky CAT / VARA / řídicího
kanálu. TX i RX mohou být aktivní současně. Karta Home ukazuje TX/RX a počty
inboxu. Těla zpráv, přílohy ani ovládání PC nejsou součástí protokolu v1.

## Chování a bezpečnost

- Charakteristiky vyžadují šifrování, 16bajtový klíč a uložený bond.
  Just Works je dostupné pouze po ručním povolení párování. Neověřuje identitu
  protistrany pomocí PIN. Telefonní režim si zachovává svůj původní PIN/MITM.
- Inzeruje se jen UUID právě zvoleného režimu, proto se Guardian UUID vejde
  přímo do standardního advertising paketu. Připojit lze jedno PC.
- Neplatná délka, magic, verze, flags i duplicitní/zpětná sekvence vracejí ATT
  chybu a neposouvají čas snímku. Sekvence se resetuje po novém spojení a
  podporuje přetečení uint32. Po 15 sekundách nebo odpojení jsou hodnoty `—`.
- Callback pouze validuje paket a aktualizuje malý model pod krátkou kritickou
  sekcí; nekreslí, nečeká na rádio a nezapisuje preference. UI model čte odděleně.
- Checked build patch knihovny NimBLE 1.4.3 brání automatickému smazání starého
  bondu při požadavku na nové párování mimo povolené okno. Služba je registrovaná
  přímo přes NimBLE host API, aby vadný zápis skutečně dostal ATT chybu.
- Samostatné mazání bondů na T-Decku není v tomto kroku přidané. Pro nové
  spárování odeber zařízení ve Windows a znovu otevři párovací okno na T-Decku.

## Ověření

`python scripts/test_guardian_status.py` zkouší referenční vektor, nevyrovnaný
buffer, uint32 počty, chybné pakety, sekvence, přetečení času a 15sekundový limit.
Druhá část kompiluje skutečný transport a mailbox s testovací náhradou NimBLE
a NVS: kontroluje vlastnosti služby, odpověď Protocol, ATT chyby, nešifrovaný
přístup, párovací okno, cizí klienty, obnovu bondu a oddělení od MeshCore UART.

`python simulator/run.py --test` zahrnuje tyto testy, Home i skutečnou aplikaci
Guardian v českém a anglickém UI, přechody pairing/live/stale/disconnected
a návrat horní lištou. Snímky `guardian-*.png` a `home-guardian-live.png`
jsou v `.sim-cache/test-artifacts/` a podadresáři `cs/`.

PlatformIO target je `LilyGo_TDeck_companion_radio_touch`. Firmware i úplný
instalační obraz se sestavují pomocí `pio run -e LilyGo_TDeck_companion_radio_touch -t mergebin`.
Výstupní merged BIN se zapisuje na adresu `0x0`; samotný app BIN je pro OTA.

**Dosud neověřeno na fyzickém HW:** Windows/Bleak párování, skutečné bondy po
restartu, odchod z dosahu, současný mesh provoz a živé TX/RX v Guardianu.
Host testy a simulátor nenahrazují tento integrační průchod.

## Předaná sestava 28. 9. 2026

- Zdrojový commit implementace: `ad4e113006caecac31a355b83384e1ddd51e4763`.
- Instalační obraz: `out/Guard-Mesh-TDeck-20260928_075543-Guardian-BLE-v1-merged.bin`
  a stejně pojmenovaný `.json` pro Guard-Mesh-Flasher; offset `0x0`.
- App BIN: 3 667 040 bajtů; merged BIN: 3 732 576 bajtů.
  V OTA oddílu po započtení celého app obrazu zbývá 396 192 bajtů.
  Statická RAM podle linkeru: 122 752 / 327 680 bajtů.
- SHA-256 merged BIN: `38d2ef5f9ccc913f2bc796d16c4b6ac75ae69de2da68be94f37fe41653df32cf`.
- PlatformIO build i `mergebin` prošly. Flasher ověřil ESP obrazy a sidecar;
  bajty aplikace ve sloučeném obrazu přesně odpovídají novému app BIN.
  Objekt NimBLEServer odkazuje na ochranu opakovaného párování a výsledný ELF
  obsahuje její implementaci.
- Prošly protokolové a transportní testy, katalog češtiny, modely/služby
  simulátoru i anglický, český a klávesnicový UI scénář. Český scénář s novými
  přechody překročil původní 90sekundový limit; po jeho navýšení na 120 sekund
  byl úspěšně dokončen. Anglické a české snímky Guardianu byly vizuálně zkontrolovány.
- Zařízení nebylo flashováno. Fyzické BLE spojení zůstává k ověření podle výše
  uvedeného seznamu; sestava není pro jiné modely LilyGo.
