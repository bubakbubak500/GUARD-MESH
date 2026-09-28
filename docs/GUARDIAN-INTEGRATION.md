# Guardian přes Bluetooth – rozvojový plán

Historický návrh z 19. 9. 2026. První implementace z 28. 9. 2026 používá
[Guardian BLE v1](GUARDIAN-BLE.md): PC předává stav a počty do T-Decku.
Ovládání, obsahy zpráv a most do MeshCore popsané níže zůstávají budoucím plánem.

## Co chceme

- Na T-Decku ovládání Guardianu: odeslat text, schránka zpráv a přehled sítě.
- Přes Bluetooth propojit T-Deck a Guardian, bez příloh a přenosu souborů.
- Do Guardianu zveřejnit jen uživatelem vybrané kontakty MeshCore.
- Strukturované zprávy z Guardianu předávat určeným kontaktům MeshCore.

Potvrzené zadání: jde o náš **Guardian 1.1.11 běžící na Windows**.
T-Deck se přes Bluetooth páruje s tímto Windows počítačem; Guardian na něm
zajišťuje aplikační protistranu. T-Deck je jeho ovladač a most do MeshCore.
Konkrétní API Guardianu 1.1.11 a dostupnost Bluetooth adaptéru v jeho kódu
ještě nebyly prověřeny. Samotné spárování nenahrazuje aplikační protokol.
„Síť“ zatím znamená stav připojení Guardianu a odděleně stav MeshCore, nikoli
automaticky úplnou mapu všech dosažitelných uzlů.

## Jsme omezeni místem?

Ano, ale pro textové propojení není v tuto chvíli doložená překážka, která by
vyžadovala jinou desku. Lokální build z 19. 9. 2026 (základ `369802b`
s pracovními změnami) má aplikaci 3 400 128 bajtů, tedy přibližně 3,24 MiB.
V OTA oddílu zbývá 663 104 bajtů, přibližně 648 KiB. Nejde zatím slíbit,
že se celá integrace vejde: její velikost ani paměť při provozu nejsou změřené.
Build hlásí statickou RAM 113 588 / 327 680 bajtů; to nezahrnuje všechny
pozdější dynamické alokace a neříká, kolik RAM zbude při zapnutém BLE a UI.

| Oblast | Stav v tomto repozitáři | Dopad na návrh |
| --- | --- | --- |
| Flash | T-Deck konfigurace počítá s 16 MiB | Není celá k dispozici pro program. |
| Firmware | Dva OTA oddíly, každý `0x3E0000` = 3,875 MiB | Nový firmware se musí vejít do jednoho; oddíly nelze sčítat. |
| Mapové dlaždice | `0x4C0000` = 4,75 MiB | Samostatný oddíl, nikoli volná rezerva aplikace. |
| Datový oddíl SPIFFS | `0x360000` = 3,375 MiB | Sdílený s existujícími daty; skutečně volné místo se musí zjistit. |
| PSRAM | Definice desky uvádí 8 MiB, build ji zapíná | Vhodná pro omezené cache a větší buffery, ne náhrada veškeré interní RAM. |
| Interní RAM | Aktuální volná kapacita nezměřena | Hlídáme BLE, zásobníky úloh, DMA a největší volný blok, nejen součet volné paměti. |
| Kontakty / kanály | T-Deck build: 2 000 kontaktů, 40 kanálů | Jde o nakonfigurované stropy, nikoli volná místa ani požadavek vše zrcadlit. |
| Companion rámec | Připnuté jádro: `MAX_FRAME_SIZE = 176` bajtů | Nejde o limit čistého textu ani garantovaný BLE payload. |
| MeshCore text | `MAX_TEXT_LEN = 160` bajtů | Počítat UTF-8 bajty, nikoli znaky; u kanálu se odečítá i prefix odesílatele. |
| Companion historie | Výchozí ring 128 položek, offline fronta 16 | Synchronizační okno není neomezená schránka. |

Zdroj: [definice desky](../boards/t-deck.json),
[T-Deck build](../platformio.ini),
[partition table](../variants/lilygo_tdeck/partitions_tdeck_touch.csv),
[MyMesh.h](../src/MyMesh.h), [MyMesh.cpp](../src/MyMesh.cpp),
[main.cpp](../src/main.cpp). Limity jádra byly ověřeny v připnutém `meshcomod`
`core-v1.17.4`: `BaseSerialInterface.h`, `BaseChatMesh.h/.cpp`, `MeshCore.h`.
Historický odhad velikosti firmwaru v komentáři partition table není měření
aktuálního buildu. Limity desktopového simulátoru nejsou limity desky.

Velký objekt `MyMesh` i tabulka kontaktů už využívají PSRAM. Proto nové služby
nemají bez rozmyslu přidávat další velká statická pole do interní RAM.
Odstranění veřejné webovky z repozitáře samo o sobě nezvětšilo OTA oddíl ani
neuvolnilo RAM běžícího firmwaru. Změnu partition table teď nenavrhujeme.

Dalším limitem bude propustnost rádia: rychlý příjem přes BLE nesmí vytvořit
neomezenou frontu pro pomalejší MeshCore. Přílohy vynecháme i v datovém kontraktu.

## Rozdělení funkcí

| Funkce | První provedení |
| --- | --- |
| Odeslání do Guardianu | Výběr příjemce, text, stav přijetí/odeslání/chyby podle možností jeho API. |
| Schránka Guardianu | Stránkované přehledy a detail na vyžádání; plná historie zůstává v Guardianu. |
| Síť | Připojeno/odpojeno, stáří posledního stavu, dostupné uzly a fronta, pokud je Guardian poskytuje. MeshCore zobrazovat zvlášť. |
| Sdílení kontaktů | Výslovný výběr, náhled sdílených údajů, změny a odvolání sdílení. |
| Guardian → MeshCore | Validovaný požadavek, povolený příjemce, omezená fronta, odeslání textu a návrat stavu. |

BLE odpojení nesmí blokovat místní MeshCore UI. Poslední načtená data označit
jako neaktuální. Obecné automatické přeposílání všech MeshCore zpráv do Guardianu
není součástí první verze; pravidla opačného směru musíme určit samostatně.

## Bluetooth a opětovné použití současného kódu

Firmware již používá NimBLE a companion rozhraní pro odesílání textu,
načítání kontaktů a synchronizaci přijatých zpráv (`CMD_SEND_TXT_MSG`,
`CMD_GET_CONTACTS`, `CMD_SYNC_NEXT_MESSAGE`). Znovu použijeme služby MeshCore
a existující BLE infrastrukturu tam, kde odpovídají požadovanému směru komunikace.
Tato rozhraní sama o sobě nejsou API pro vzdálené ovládání Guardianu.

Výchozí návrh: aplikace Guardian 1.1.11 na Windows bude BLE central / GATT
klient a T-Deck peripheral / GATT server, čímž navážeme na dosavadní companion
model T-Decku. Jde o navržené role, nikoli ověřenou současnou schopnost Guardianu.
Před implementací prověřit jeho kód a Windows Bluetooth adaptér; podle výsledku
doplnit transport do Guardianu. Souběžné připojení telefonu neslibujeme
bez ověření limitů a směrování odpovědí.

Nevymýšlet druhý Bluetooth stack. Rozšíření musí mít vyjednání verze a schopností,
jasné oddělení od původních companion příkazů a omezené rámce. Ověřit skutečně
vyjednané MTU a režii ATT; 176bajtový aplikační rámec se nesmí automaticky
považovat za jednu BLE notifikaci. Pokud bude nutná fragmentace, omezit velikost
sestavené zprávy, počet rozpracovaných zpráv a timeout.

## Kontakty a směrování

MeshCore je zdroj identity MeshCore kontaktů. Guardian dostává pouze jejich
vybraný projekční seznam: stabilní ID odvozené z plného veřejného klíče,
zobrazované jméno a povolené schopnosti. Soukromé klíče, klíče kanálů ani polohu
neexportovat. Aktualizovat po stránkách a změnách; odebrání výběru musí vyvolat
odvolání kontaktu v Guardianu a zrušit jeho dosud neodeslané požadavky ve frontě.

Nesměrovat podle měnitelného jména. Současný companion příkaz používá šestibajtový
prefix veřejného klíče; adaptér musí plnou identitu ověřit a nejednoznačný prefix
odmítnout. Stejný příkaz dnes podporuje také CLI a zvláštní místní příjemce:
Guardian most povolí pouze běžný text a vybrané skutečné kontakty.

„Speciálně tvarovaná zpráva“ bude verzovaný datový požadavek, ne příkaz skrytý
v libovolné chatové zprávě. Minimální logický kontrakt:

```json
{
  "version": 1,
  "type": "mesh.send_text",
  "id": "unikatni-id-pozadavku",
  "origin": "guardian",
  "recipient_id": "sdilena-identita-meshcore-kontaktu",
  "text": "Text pro prijemce",
  "ttl_seconds": 300
}
```

Jde o názorný model, nikoli návrh jednoho BLE paketu. Konkrétní kódování zvolíme
podle Guardian API; celý JSON se neposílá přes LoRa. Příjemce dostane pouze text.
Text překračující limit odmítnout s vysvětlením, ne tiše zkracovat či automaticky
rozdělovat na mnoho rádiových zpráv. První verze směruje soukromé textové zprávy;
kanály lze doplnit po určení jejich oprávnění a odlišných potvrzení.

Před zařazením ověřit spárovanou identitu Guardianu, typ/verzi, délky, oprávnění,
příjemce a TTL. Omezení musí platit na straně T-Decku, nejen ve formuláři
Guardianu. Samotný existující příkaz „vrať všechny kontakty“ výběr nevynucuje;
ověřit i dostupnost ostatních companion operací pro stejného BLE klienta.
Výchozí PIN z konfigurace není dostačující identita důvěryhodného Guardianu.

Stavy rozlišit: přijato, ve frontě, odesláno do mesh, potvrzeno příjemcem,
selhalo, vypršelo. Přijetí přes BLE není doručení přes rádio. Opakování stejného
ID vrátí známý stav místo nového odeslání; uložit omezenou evidenci i pro reconnect
a restart. Po pádu mezi vysíláním a zápisem potvrzení může zůstat stav neurčitý:
neslibovat přesně jedno doručení a automaticky takový požadavek nevysílat znovu.
Původ zprávy a zákaz opětovného mostování brání smyčkám.

## Paměťový rozpočet první verze

Výchozí návrhové stropy, ne naměřená spotřeba:

- Nejvýše 100 sdílených kontaktů, stránka 20; referencovat existující kontakty.
- Nejvýše 16 čekajících odchozích zpráv; při zaplnění vracet „fronta plná“.
- Cache nejvýše 50 přehledů zpráv a právě otevřený detail, další načítat z Guardianu.
- Cíl do 64 KiB přidaných datových bufferů/cache v PSRAM a do 16 KiB
  přidaných aplikačních dat v interní RAM. Náklady BLE, UI a zásobníků měřit zvlášť.
- Pro vlastní perzistentní metadata/frontu navrhnout kvótu do 128 KiB, až po
  ověření volného datového oddílu. Bez neomezených logů; mazání historie nesmí
  tiše zahodit neodeslanou zprávu. SD karta není podmínkou základní funkčnosti.

Před přijetím implementace změřit velikost `.bin` a `.map`, volnou interní RAM,
její minimum a největší blok, PSRAM a úložiště: po startu, s BLE, při otevřené
schránce, naplněných kontaktech/frontě a při opakovaném odpojení. Ověřit souběh
s běžnými funkcemi T-Decku. Desktopový simulátor tyto hardwarové limity nepotvrdí.

## Jak to připravit bez dalšího růstu UITask.cpp

1. Začít v kódu našeho Guardianu 1.1.11 pro Windows: doložit API, BLE role, identity příjemců a význam
   schránky/sítě. Sepsat kontrakt a příklady požadavků, odpovědí a chyb.
2. Připravit falešný Guardian pro simulátor a ověřit tok zpráv bez desky.
3. Oddělit `GuardianService`, transport, codec, výběr kontaktů, router a úložiště
   fronty. Žádný parser, směrování ani BLE callbacky uvnitř `UITask.cpp`.
4. Obrazovky Guardianu vést v samostatných souborech. `UITask.cpp` pouze připojí
   vstup do navigace. Nevyžaduje to předem přepsat celé stávající UI.
5. Přidat BLE adaptér, změřit skutečnou desku a teprve potom rozšiřovat limity.

Simulátor bude používat stejné služby a obrazovky s falešným transportem.
Scénáře: odeslání a potvrzení, nedostupný Guardian, reconnect, duplicitní ID,
neznámý či odvolaný kontakt, plná fronta, vypršení, restart a příliš dlouhý UTF-8
text. Pro BLE, souběh s rádiem a paměť zůstane samostatná zkouška na T-Decku.

Otevřené před implementací: API a Bluetooth implementace Guardianu 1.1.11
na Windows; přesný význam „síť“; pravidla příchozích odpovědí
MeshCore → Guardian; souběh telefonu; požadovaná retence a počet kontaktů.
Tento dokument zachycuje směr rozvoje, nepředstavuje hotovou integraci.
