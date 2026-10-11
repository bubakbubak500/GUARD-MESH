# guardian-2026.10.11

Verze pro **LilyGo T-Deck / T-Deck Plus** a **Heltec V4 TFT**,
navazující na `guardian-2026.10.10.3`. Buildy obou cílových desek a softwarové
regrese prošly. Zdrojový commit a kontrolní součty finálních balíčků uvádí
`validation.json` u artefaktů. Fyzická zařízení nebyla při přípravě připojena
ani vyzkoušena.

## Guardian API v3 na T-Decku

Rozsah vychází ze [schváleného výběru](../design/guardian-v3/review-data/selection.json).
Guardian BLE používá **pouze API v3**; přepínač ani zpětná podpora API v2 nejsou
součástí vydání. Pro spojení je potřeba Guardian na PC s podporou API v3.
**Guardian BLE na Heltecu V4 TFT je odložené**; jeho firmware získává společné
úpravy obrazovky OTA, ale nepřidává tím připojení ke Guardianu.

Vybrané funkce:

- **F01 — Stanice:** volací znak, stav obou rádií, frekvence a mód,
  připravenost, schránka a ARDOS-CZ. Připravenost rozlišuje ano, ne a neověřeno;
  starší data jsou označená jako neaktuální.
- **F02 — Detail rádia:** modem, řízení, kalibrace a důvod nepřipravenosti.
  Prohlížení stavu nevyžaduje povolení provozního ovládání.
- **F03 — Přenosy:** samostatné zobrazení rádia 1 a 2, směru, protistran,
  modemu, bajtů, procent a rychlosti, pokud je PC poskytuje. Neznámé údaje se
  nevydávají za nulu a samotných 100 % nepotvrzuje doručení.
- **F04a/b/c — Ovládání:** připojení či odpojení driveru rádia a modemu VARA,
  spuštění či zastavení řízení. Zastavení řízení má potvrzení, protože může
  přerušit přenos nebo kalibraci.
- **F05 — Maják:** zařazení majáku pro výslovně vybrané rádio. Výsledek
  **„Maják zařazen“ znamená frontu, nikoli potvrzené odvysílání do RF**.
  Požadované samostatné vynucení advertu není ve smlouvě API dostupné;
  dostupná operace je `beacon.send`.
- **F07 — Ladění:** frekvence a aktuálně povolené módy vybraného rádia;
  zohlednění omezení rádia, včetně FM a kroku 10 Hz pro K5/K61.
- **F08 — Profily:** výběr a použití profilů uložených na PC. T-Deck profily
  nevytváří ani neupravuje.
- **F09 — SC-FTN AutoTune:** výběr partnera, rychlého nebo plného režimu,
  spuštění, průběh a zrušení kalibrace.
- **F10 — Upozornění:** stránkovaná historie, detail s původem a poznámkou,
  vědomé označení přečtené. Potvrzení příjmu události neznamená přečtení.

Ovládání F04–F09 respektuje oprávnění udělená na PC, capabilities a vybrané
rádio. Povel je před odesláním trvale uložený; chyba uložení odeslání zastaví.
Neznámý výsledek není automaticky opakovaný provozní povel. Obnova spojení
a událostí patří do společného základu v3.

**F06 scanner je vyřazený. F11 odesílání upozornění je odložené a jeho návrh
zůstává zachovaný. F12 samostatná obrazovka obnovy a F13 přepínání v2/v3 jsou
odmítnuté.** Stávající zprávy, koncepty, kontakty a trasy zůstávají součástí
Guardianu.

## Nouzový příjem

Nepřečtené nouzové upozornění s kódem 1–3 na T-Decku aktivuje displej, zobrazí
celou zprávu a opakuje zvukový alarm. Zpráva zůstává viditelná až do vědomého
**trvalého označení jako přečtené**. Více nouzových zpráv se postupně zobrazí
od nejstarší nepřečtené.

Zobrazení zprávy nezruší ruční PIN zámek. Při selhání uložení stavu přečtené
zůstane upozornění i alarm aktivní a obrazovka ukáže chybu. Místní označení
přečtené se následně synchronizuje s PC; nepotvrzuje zásah ani poskytnutí pomoci.

## Skutečné obrazovky podle návrhu

Guardian a aktualizace přebírají tmavé karty s cyan obrysy, výrazné vektorové
ikony a oranžové hlavní akce ze [schválených vizuálních návrhů](../design/guardian-v3/README.md).
Obrazovky používají skutečná data a stavy aplikace. Návrhové ilustrace
a snímky simulátoru jsou odlišné podklady.
[Galerie 17 skutečných českých obrazovek](../design/guardian-v3/screens/README.md)
ukazuje implementaci s testovacími daty.

OTA přehled ukazuje cílovou desku, současnou a dostupnou verzi, velikost obrazu,
očekávaný SHA-256 a skutečný stav Wi-Fi. Instalace stále vyžaduje potvrzení.
Průběh používá velké procento, lištu a fáze stažení, kontroly a restartu.
**Očekávaný hash není ověřený hash**; štít a 100 % se objeví až po skutečném
úspěchu kontrol SHA-256 a obrazu. Průběh stahování sám nepotvrzuje dokončenou
instalaci. Ověřují se české i anglické rozložení na 320 × 240.

## Lokální USB a OTA

USB a OTA obrazy mají jeden zdrojový commit a stejný tag
`guardian-2026.10.11`, doložené v `validation.json`. Aplikace v merged obrazu na offsetu `0x10000` musí
být po bajtech shodná s odpovídajícím `app-ota.bin`.

| Deska | Lokální USB obraz, adresa 0 | OTA aplikace |
|---|---|---|
| T-Deck / T-Deck Plus | `Guard-Mesh-TDeck-guardian-2026.10.11-merged.bin` | `Guard-Mesh-TDeck-guardian-2026.10.11-app-ota.bin` |
| Heltec V4 TFT | `Guard-Mesh-Heltec-V4-TFT-guardian-2026.10.11-merged.bin` | `Guard-Mesh-Heltec-V4-TFT-guardian-2026.10.11-app-ota.bin` |

Každá deska má také `debug-elf.zip` a `merged.json`; balíček doprovází
`SHA256SUMS.txt` a `validation.json` s hashi, velikostmi, kontrolami obrazu
a zdrojovým commitem.

Před USB instalací je potřeba záloha; kompletní merged obraz může přepsat
uložené nastavení. OTA používá kompatibilní neaktivní OTA oddíl a vyžaduje
připojenou Wi-Fi. Starší zařízení bez kompatibilního oddílu potřebuje USB
instalaci. Stabilní GitHub OTA navíc potřebuje správný `sha256` digest přímo
v metadatech assetu; samostatný soubor `SHA256SUMS` ho nenahrazuje.

## Stav ověření

**PASS:** sedm nativních Guardian sad, modely/služby, anglické a české UI,
klávesnicová navigace, poslední regrese zachování formulářů, nouzové zobrazení
a skutečný touch při ručním zámku. **PASS:** embedded buildy obou cílových desek.
[Záznam review a testů](guardian-2026.10.11-validation.md) uvádí opravené nálezy
i omezení opakovaného externího review. Balíčkování kontroluje ESP32-S3 chip ID 9,
tag v aplikaci, velikost nejvýše `0x3e0000`, shodu merged/OTA a SHA-256 artefaktů.

**Hardware nezkoušeno.** Reálné BLE spojení s Guardianem, zvuk a probuzení
fyzického T-Decku ani stažení, instalace a restart přes OTA na obou deskách
zatím nejsou potvrzené fyzickým testem. Dosavadní omezení automatického OTA
rollbacku z předchozího vydání zůstává relevantní.
