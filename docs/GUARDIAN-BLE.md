# Guardian BLE v2 na T-Decku

Implementace pro **LilyGo T-Deck / T-Deck Plus** podle
[kontraktu Guardian 1.1.13](GUARD_MESH_BLE_V2_CS.md). PC je BLE Central,
zařízení Peripheral. Tento firmware vyžaduje Guardian **1.1.13 nebo novější
s podporou API v2**. Guardian 1.1.12 podporuje pouze starší firmware v1.

## Připojení

1. T-Deck: **Nastavení → Guardian BLE → Spárovat PC (2 min)**.
2. PC: **Guardian → Nastavení → Nastavení stanice → Guard Mesh**.
   Vyhledej `GuardMesh-XXXX`, spáruj a připoj zařízení.
3. Další připojení používá uložený bond. Nové párování ani výměna bondu nejsou
   mimo fyzicky otevřené dvouminutové okno povolené.

Zapnout/Vypnout v tomto nastavení přepíná mezi Guardianem a BLE režimem
pro telefon. Bluetooth klávesnice musí být vypnutá. Vestavěná funguje dál.
Párování už není součástí aplikace Guardian.

## Home a aplikace

- Blok Home má vlastní modrý štít, střídá Inbox a Outbox po třech sekundách,
  přednostně zobrazuje RX/TX s procenty. Souběh má dva bargrafy. Přírůstek
  inboxu zobrazí upozornění na pět sekund; aktivní přenos má přednost.
- Neznámá procenta jsou `—`, bez falešně vyplněného bargrafu. Nový Status zruší
  procenta staré sekvence. Po 15 sekundách bez Status nebo ihned po odpojení
  zmizí živé počty, procenta i směry přenosu.
- Modrý štít v horní liště nahrazuje BLE symbol při autentizovaném připojení PC.
  Znamená spojení; aktuálnost Guardianu ukazuje blok a aplikace.
- **Aplikace → Guardian** používá rozložení A3: vlevo jeden aktivní přenos,
  počty zpráv a CAT/VARA/CTRL; vpravo **Zprávy, Síť, Napsat** a pod nimi vedle
  sebe ikony **nastavení a Home**. Home vrací na domácí obrazovku Guard Meshe.
  Při souběhu se bargraf drží jednoho aktivního směru. Systémová lišta zůstává
  společná s ostatními obrazovkami zařízení.
- Nastavení aplikace nabízí modrý a zelený motiv, uložený do NVS. Motiv mění
  pouze Guardian. Oba mají tmavé výplně s 70% krytím, jemný obrys a oranžové
  hlavní/vybrané akce s černým textem i ikonami. Párování zůstává v hlavním
  nastavení zařízení. Neznámé počty a procenta v aplikaci používají `-`, který
  mají i velké číselné fonty. Klepnutí na Přijaté otevře Inbox, na K odeslání
  Outbox (frontu na PC; odeslané jsou samostatnou složkou).
- Zprávy mají složky Inbox, Outbox, Odeslané, Koncepty a Tranzit. Seznam se čte
  po třech položkách, text po 512 Unicode znacích; používá přesné
  `revision`/`next_offset`. Změna seznamu/textu obnoví první stránku.
  Prohlížení nemění příznak přečteno na PC. Přílohy se nenačítají.
- Síť začíná uloženými trasami, přepíná mezi živými a uloženými, po dvou
  položkách. Uložený `next_hop` se nezaměňuje s `live_next_hop`.
  Výběr vyplní příjemce nové zprávy.
  Přítomnost kontaktu není zárukou okamžitého doručení.
- Grafika drží návrh A3: obrysové ikony obálky, sítě, tužky a odesílání,
  záložky uložených tras, kruhové kontrolky a oranžové hlavní akce. Ikony se
  kreslí přímo v LVGL. Zpět a název stránky jsou ve společné systémové liště.
- Otevřený seznam zpráv nebo sítě se obnovuje automaticky každých pět sekund.
  Stejný obsah nezpůsobí překreslení; pozdní odpověď nemění jinou stránku ani
  rozepsaný text. Stránkování má šipky zpět/vpřed. Stavová zpráva zabírá jeden
  řádek a klepnutím otevře plné vysvětlení.
- Navštívené stránky zpráv, textu a kontaktů se drží v RAM: nejvýše 12 stránek
  s rozpočtem 24 KiB pro započítaný obsah a objekty (bez režie alokátoru).
  Návrat zobrazí obsah ihned a ověří jej na pozadí. Cache rozlišuje složku,
  zprávu, zdroj kontaktů a stránku; při odpojení zůstává, po restartu zmizí.
- Přehled kontroluje stav každých 250 ms. PC podle kontraktu vytváří snímek
  po 500 ms a posílá heartbeat nejpozději po 5 s; po 15 s bez něj je stav
  neaktuální. Obrazovka textu se automaticky nepřekresluje během čtení.
- Podržení **R na 2 s** otevře zrušitelný načítací modal a vyžádá `status.get`
  i aktuální seznam/text. Jeden stisk spustí jedinou obnovu. V editoru R normálně
  píše. Starý firmware klávesnice bez matice a událostí uvolnění neumí délku
  stisku: tam stejné obnovení spustí krátké R. Zrušení zavře modal; rozběhnutá
  odpověď RPC se bezpečně dočte. Obnovení neposílá zprávy ani znovu nepáruje BLE.
- Editor podporuje příjemce, předmět, text do 4096 Unicode znaků a prioritu
  0–3. Koncept se ukládá při odchodu. Před prvním odesláním se text a UUID v4
  atomicky uloží jako jeden NVS blob. Pokud uložení selže, nic se neposílá.
- Potvrzení znamená **zařazení do fronty na PC**, nikoli rádiové doručení.
  Při výpadku zůstane obsah a token uložený i přes restart T-Decku. Opakování je
  ruční, vždy se stejným tokenem a obsahem; nepotvrzený obsah nelze upravovat.
  Novou zprávu lze začít po výslovném potvrzení kontroly předchozí zprávy na PC.
  Po změně databáze či BLE identity starou operaci automaticky neopakujeme.

API v2 poskytuje **stav** CAT, VARA a řídicího kanálu. Neobsahuje příkazy
k jejich zapnutí/vypnutí ani ke spuštění Guardianu. Aplikace proto nenabízí
nefunkční ovládání rádia, kanálu nebo PC procesu.

## Transport

Všechny charakteristiky vyžadují šifrování, bond a 16bajtový klíč. Just Works
je povolené pouze v ručně otevřeném párovacím okně. Telefonní režim si ponechává
PIN/MITM. Guardian nikdy nepoužívá MeshCore UART pro RPC.

Služba používá původní Status a nové Protocol v2, Progress, Request Notify
s CCCD a Response Write. Požadavky čekají na odběr CCCD. Současně běží jeden
požadavek, fragmenty mají maximálně 20 bajtů včetně čtyřbajtové hlavičky.
Zpracování JSON probíhá až po END; velikost je omezená na 32768 bajtů.
Pořadí fragmentů, ID a časové limity 30/60 sekund se kontrolují. Chyba rámcování
ukončí spojení, odpojení zahodí neúplná data. RPC ani Progress neomlazují Status.
BLE callbacky nekreslí UI a neukládají koncepty; sdílený RPC model chrání mutex.

Po ztrátě spojení T-Deck obnovuje advertising (kontrola každou sekundu).
PC jako Central musí spojení znovu zahájit, obnovit odběr Request CCCD a posílat
Status/Progress. Aplikace po obnovení transportu a čerstvého stavu sama obnoví
otevřený seznam. Podrobnosti a požadavky pro PC jsou v
[reportu úprav 2026-09-29](GUARDIAN-POLISH-2026-09-29.md).

Ověřený build patch NimBLE 1.4.3 chrání výměnu bondů a předává událost CCCD
surově registrované charakteristiky Request. Při změně kontextu knihovny build
selže místo tichého vypuštění ochrany.

## Ověření a sestavení

`python scripts/test_guardian_status.py` ověřuje Status/Progress, sekvence,
stárnutí, registraci, ATT chyby, párování, oddělení UART, CCCD,
fragmentaci, UTF-8, meze, timeout a obnovu stejného tokenu.

`python simulator/run.py --test` ověřuje skutečné obrazovky v EN/CS i
klávesnicovou navigaci. Průchod Guardianem kontroluje seznam,
změnu revision, 32bitová ID, Unicode text a ztracené potvrzení při odeslání,
motivy, uloženou trasu, automatické načítání a ochranu rozpracovaného editoru.
Knihovny simulátoru včetně ArduinoJson jsou připnuté na konkrétní commity.

`pio run -e LilyGo_TDeck_companion_radio_touch -t mergebin` vytvoří instalační
merged BIN pro adresu **0x0**; samostatný app BIN je pro OTA.

Uživatel potvrdil funkční připojení předchozího sestavení v2 po odebrání starého
bondu ve Windows a novém spárování přes Guardian. Kompletní merged BIN zapisuje
i oblast NVS; po jeho instalaci může být nutné staré párování ve Windows odebrat
a spárovat znovu. Aktualizace aplikačním BIN přes OTA NVS nepřepisuje.
Rozložení A3 je ověřováno simulátorem a sestavením; fyzické ověření této úpravy
a výpadku po uložení zprávy na PC před potvrzením zůstává na zařízení.
