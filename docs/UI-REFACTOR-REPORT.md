# Report modularizace UI — uzavřené kolo 27. 9. 2026

Navazující cílené optimalizace synchronizace chatů, navigace a kontaktů popisuje
[výkonový report](UI-PERFORMANCE-REPORT.md). Níže zůstává záznam uzavřeného kola
modularizace a jeho tehdejšího sestavení.

## Výsledek a firmware — 27. 9. 2026

**Toto kolo je dokončené a další plošný refaktor je zastavený. Celý backlog
rozdělení monolitu dokončený není.** Zastavení odpovídá dohodnutému smysluplnému
mezníku pro další funkční vývoj, nikoliv vyčerpání limitu. Poslední odečet ukazuje
**22 % využitého týdenního limitu**, tedy 78 % zbývá; dohodnutý strop byl 50 %.

Kolo uzavírá společné přijetí práce a obnovu SD, pravidla Back, politiku obrazovky
a zamčení, relaci chatu a příjem zpráv. `UITask.cpp` má **32 369 řádků** proti
32 798 na začátku tohoto kola (−429) a 61 354 v původní rozpracované kopii.
Přínosem je vlastnictví stavu a ověřitelné hranice mezi moduly; počet řádků není
důkaz zrychlení ani procento dokončenosti celého refaktoru.

Poslední část tvoří `application/MessageIngress` a společný `UIMessageEvent`:

- Mesh předává kopii typu, názvu konverzace, autora, úplného klíče, textu,
  sender timestamp, SNR/RSSI, trasy a scope. Sdílené `lastRx*` a jednorázový
  `uiConsumeLastSenderTs` jsou odstraněné. Odmítnutý room/DM příjem už nemůže
  předat svůj čas další kanálové zprávě; kanály zachovávají místní čas doručení.
- Filtry klíče, autora kanálu/room a drobných zpráv běží před upozorněním,
  Lua callbackem, zvukem a probuzením. Vnořený příjem nemění původní snímek.
  Čítač dodaný companion transportem se aktualizuje i u filtrované události;
  nevzniká ale bublina ani upozornění. Toto není změna companion historie.
- `AbstractUITask` zachovává výchozí notify-then-message adaptér pro ostatní UI.
  Staré `newMsg*` metody zůstávají kompatibilní; nový příjem z MyMesh používá
  výhradně explicitní událost. Vše běží synchronně na UI vláknu.
- Limity celé RF/companion zprávy hlídají compile-time kontroly. Text se před
  filtrováním nezkracuje na délku uložené bubliny; stávající limit historie
  zůstává 160 bajtů. Binární formát uložené historie se nemění.

**Závěrečné ověření prošlo:** úplná sada modelů a služeb, souběh SD/executoru,
nativní testy s aktivními asercemi a skutečné UI v EN/CZ/klávesovém režimu.
Nová integrační regrese ověřuje filtrování bez upozornění/probuzení, čítač,
reentrantní změnu původního vstupu, zachování identity/metadat, replay čas,
scoped direct, parsování dlouhého autora a výchozí adaptér ostatních UI.
Simulátor: `guard-mesh-sim-ceb066d207d6.exe`, 398 překládaných zdrojů;
log `.sim-cache/refactor-20260927-ingress-tests.log`.

T-Deck build prošel za **70,16 s**, RAM **120 184 B (36,7 %)**,
flash **3 645 401 B (89,7 % aplikačního slotu)**.
Log `.sim-cache/refactor-20260927-final-firmware.log`.
Český katalog prošel s **1 322 klíči** a standardní `git diff --check` prošel.
Manifest **677 zdrojových, konfiguračních a testových souborů** zůstal beze změny
mezi ověřováním a balením. Doklady a kopie všech tří smoke logů jsou v
`.sim-cache/refactor-20260927-final/`.

| Výstup pro LilyGo T-Deck / T-Deck Plus | Soubor |
| --- | --- |
| Kompletní obraz pro Guard-Mesh-Flasher, offset `0x0` | [Guard-Mesh-TDeck-20260927_013946-refactor-merged.bin](../out/Guard-Mesh-TDeck-20260927_013946-refactor-merged.bin) |
| Metadata, ponechat vedle BIN | [Guard-Mesh-TDeck-20260927_013946-refactor-merged.json](../out/Guard-Mesh-TDeck-20260927_013946-refactor-merged.json) |
| Samostatná aplikace, offset `0x10000` | [LilyGo_TDeck_companion_radio_touch-20260927_013946-9782e12-dirty.bin](../out/LilyGo_TDeck_companion_radio_touch-20260927_013946-9782e12-dirty.bin) |

Merged BIN má **3 711 360 B**, SHA-256
`27e9b27264a53f5754bd0964bbb607c597e745b8ccf409e0f6060717aadbd5b0`.
App BIN má **3 645 824 B**, SHA-256
`46ad50670cefef60500351bbbcf1841869fc7f816c27b3715805ef7c0f4f423e`.
Flasher ověřil strukturu ESP32-S3 obrazu, checksumy a tabulku oddílů; všechny
čtyři části byly porovnány na offsetech `0`, `0x8000`, `0xE000`, `0x10000`.
Shoduje se app z `.pio`, kopie v `out` i hash v JSON. Podrobný doklad:
`.sim-cache/refactor-20260927-final/artifact-verification.json`.
Pole version v ESP descriptoru dál označuje základní SDK; toto sestavení
jednoznačně určuje název a title `2026-09-27 01:39` v JSON.

Jde o lokální vydání. Rádio nebylo flashováno, nebyl proveden commit/push/PR
ani publikace na GitHubu. Nesouvisející rozpracované změny zůstaly zachované.
Kompletní instalační obraz může přepsat identitu a nastavení; zachování dat
neslibuje. Je určen pro T-Deck / T-Deck Plus, nikoliv T-Deck Pro.

### Proč zde končit a co dál

Další aplikace lze přidávat přes stávající moduly. Nejbližší plošné extrakce
Home, senzorů, Control Center, telemetrických grafů a menších dialogů by teď
neodstranily nutnou podmínku jejich vývoje. Doporučení je oddělit je při
konkrétní změně funkce. V `UITask` dlouhodobě patří sestavení modulů, předání
závislostí a start/tick/stop; dosud v něm zůstávají i tyto obrazovky, jejich
stav, hardwarové adaptéry a některé efekty příjmu. Detailní backlog níže
uvádí důvody i podmínky navazující práce.

Samostatný návrh stále vyžaduje historie kontaktů se stejným jménem
(`MessageStore` je nadále indexovaný názvem), korelace admin odpovědí bez
request ID a skutečný opakovaný stop/start workerů. Nejde o hotové vlastnosti.
Příjem neumožní přepsat připnutý cizí klíč, ale neodděluje historie kolizních
jmen. Simulátor ani T-Deck překlad nenahrazují fyzické ověření RF, SD/FAT,
napájení nebo dalších desek. Měření rychlosti a špičkové paměti nebylo provedeno.

Zachované neúspěšné meziběhy: první ingress desktop build narazil na kolizi
konstanty `MAX_PATH` s Windows SDK; opraveno na `PATH_CAPACITY`, následný build
a celá sada prošly. Dřívější jednotlivé 90s timeouty jsou zaznamenané níže;
závěrečný ingress běh prošel všechny tři režimy bez změny časového limitu.

## Historie práce a předchozí checkpointy

Změny jsou v pracovní kopii a používá je skutečné sdílené UI firmwaru i simulátoru.
Po původním předčasném ukončení pokračovalo vyčlenění velkých částí monolitu,
včetně jejich stavu, vlastnictví callbacků, bufferů a workerů.

`UITask.cpp` měl v původní rozpracované kopii **61 354 řádků**, po prvním kroku
59 328, po dokončení akcí kontaktu 36 049 a po kole z 24. 9. **32 798**.
V kole z 24. 9. ubylo **3 251 řádků (9,02 %)**; celkový rozdíl proti původní kopii
je **28 556 řádků (46,54 %)**. V sedmi adresářích vrstev je **266** zdrojových
souborů a hlaviček, v tomto kole přibylo **26** (13 dvojic modulů).
Počty jsou orientační ukazatel rozsahu, nikoliv
důkaz rychlejšího běhu nebo úplně splaceného dluhu.

**Celý původní plán úplného rozdělení monolitu ještě není dokončený.** Níže je
oddělen skutečný výsledek od zbývající práce. Technický blocker se nepotvrdil.
**Předchozí kolo: zastaveno po dokončení dne 21. 9. 2026.**
Kód a regresní testy připravili Luna Max subagenti; hlavní agent zkontroloval
změny, vrátil konkrétní opravy a provedl integrační ověření a sestavení.
Výchozí týdenní využití bylo 81 %. Při dosažení 90 % zůstalo rozpracované menu
akcí kontaktu; podle zadání se dokončil pouze tento celek, report a firmware.
Poslední odečet po dokončení testů/build procesu ukazuje **91 % využito, 9 % zbývá**.
Tehdy se další část nerozpracovala; nové zadání z 24. 9. 2026 pokračování obnovilo.
Toto kolo zmenšilo `UITask.cpp` z 36 427 na 36 049 řádků (o 378).
Podstatným výsledkem je samostatné vlastnictví stavu záloh a cílů akcí kontaktů.


## Průběh uzavřeného kola — 26.–27. 9. 2026

**Níže je průběžná historie checkpointů; finální předání je nahoře.** Uživatel povolil pokračovat
do dosažení smysluplného bodu pro další funkční vývoj, nejvýše přibližně do 50 %
týdenního využití. Nové okno začalo na 0 %, odečet při dotazu na odhad ukázal 7 %.
Limit je strop, nikoliv požadovaná spotřeba. Baseline 485 souborů je zachována
v `.sim-cache/refactor-20260926-baseline/`; původní úplná sada simulátoru prošla
(`.sim-cache/refactor-20260926-baseline-tests.log`).

První ověřený celek tvoří společné řízení přístupu k úložišti (`StorageAccess`),
rezervace životního cyklu přes více průchodů smyčkou (`StorageMaintenance`),
monitor obnovy SD (`SdHealthMonitor`) a samostatná pravidla návratu
(`BackNavigation`). Workery historie, preferencí,
audia, dlaždic, aktualizací a měření SD mají odkládat nové operace během změny
filesystemu. Obnova, formátování, reset a vypnutí musí rezervovat skutečné
vlastnictví otevřených souborů; pouhá kontrola příznaku busy nestačí.

Průběžně prošly testy rezervace, modelů/služeb a devíti scénářů executoru.
Po integraci resetu a obnovy historie prošel T-Deck build
(`.sim-cache/refactor-20260927-storage-firmware.log`, RAM 120 064 B,
flash 3 640 153 B). EN i CZ simulátor prošly; navigační scénář měl jednou
90s timeout, opakování stejným binárním souborem a limitem prošlo
(`.sim-cache/refactor-20260927-integration.log`,
`.sim-cache/refactor-20260927-navigation-recheck.log`). Celý první příkaz tedy
nebyl úspěšný; průchody jsou doloženy hlavním logem a navazujícím ověřením.
Po oddělení monitoru už **celá sada prošla v jediném běhu**: modely/služby,
nativní testy i EN/CZ/navigace (`.sim-cache/refactor-20260927-monitor-tests.log`).
Samostatně prošel i doplněný debounce test mezery delší než 1100 ms.
T-Deck checkpoint monitoru SD je
`out/LilyGo_TDeck_companion_radio_touch-20260927_003703-9782e12-dirty.bin`,
RAM **120 136 B**, flash **3 641 385 B**, 147,79 s
(`.sim-cache/refactor-20260927-monitor-firmware.log`). Manifest 309 zdrojů/hlaviček
UI a SD preferencí je v `.sim-cache/refactor-20260927-monitor-sources.json`;
žádný z těchto zdrojů nebyl novější než dokončený obraz při kontrole checkpointu.
`UITask.cpp` měl po tomto kroku **32 700 řádků** (−98 proti začátku kola). Hlavním výsledkem
je ověřitelné vlastnictví přístupu, časování a přechodů, nikoliv úbytek řádků.
Tento obraz není závěrečné předání celého kola a nebyl flashován do zařízení.
Při integrační kontrole byl doplněn požadavek, aby resync na zformátovanou kartu
obnovil také index a commit marker, a aby zrušil neprovedený snapshot starého
filesystemu. Simulátor ověřuje i nové načtení historie po této operaci.

Další ověřený celek je `application/ScreenPolicy`: vlastní stav zhasnutí a tvrdého
zamčení, poslední aktivitu, idle timeout, ochranu rozsvícené zamykací obrazovky
a okna upozornění/glance včetně jednorázového zahájení fade. `UITask` předává
události a provádí LVGL/backlight/CPU efekty ve stávajícím pořadí. `TouchSleep`
nadále vlastní úsporné čekání procesoru; fyzická gesta a specifika jednotlivých
desek zůstávají v adaptérech. Konzole sdílí idle rozhodování, ale stále pouze
zhasíná bez grafického hard-lock flow. Porovnání časů nově zvládá přetečení
`millis()` a nula už neznamená neaktivní okno. Opakovaný reveal nerozšiřuje
burn-in deadline a příchozí zpráva nesmí odemknout zařízení.

Nativní testy prošly se skutečně zapnutými asercemi i v optimalizovaném buildu
(`.sim-cache/refactor-20260927-screen-native.log`). Celá sada prošla v jednom běhu
(`.sim-cache/refactor-20260927-screen-tests.log`), včetně testu skutečných metod
`UITask` pro soft sleep, touch wake, hard lock, reveal, unlock a aplikaci idle
deadlinu ve všech třech režimech simulátoru. Fyzické řízení panelu jiných desek
tím není ověřeno. Nezávislá kontrola integrace proti uloženému předchozímu stavu
nenašla konkrétní regresi v posuzovaných větvích.

Checkpoint politiky obrazovky:
`out/LilyGo_TDeck_companion_radio_touch-20260927_005310-9782e12-dirty.bin`,
RAM **120 152 B**, flash **3 642 189 B**, build **156,44 s**
(`.sim-cache/refactor-20260927-screen-firmware.log`). SHA-256 manifest **311**
zdrojů/hlaviček je v `.sim-cache/refactor-20260927-screen-sources.json`; při
kontrole nebyl žádný zdroj novější než dokončený obraz. `UITask.cpp` má nyní
**32 588 řádků** (−210 proti začátku kola). Práce pokračuje prověřením hranice
aktivního chatu a identity příjemce; závěrečný merged firmware bude připraven
až po uzavření kola. Poslední odečet týdenního využití je **13 %**.

Ověřený navazující celek `ChatSession` přebírá vybraný chat, zachycený klíč
příjemce a přípravu odeslání. Kontrola původního kódu našla rozdílné priority
full pubkey / legacy key6 / jméno mezi výběrem, lookupem a sendem. Nový kontrakt
nepřipouští návrat ke jménu u nevyřešeného klíče ani automatické přepnutí
příjemce po kolizi jmen. Odeslání nese kopii cíle a textu; UI smí zapsat výsledek
jen do stále odpovídajícího vlákna a smazat draft jen při nezměněné relaci a textu.
Historie předává výběr přes hodnotový callback místo zápisu přes cizí `int&/bool&`.
Modelové testy se zapnutými asercemi prošly; skutečný `UITask` v simulátoru
ověřuje přeuspořádání kontaktů, přepnutí chatu a editaci draftu uvnitř transportního
callbacku, nepřesměrovaný výsledek sendu, nepřepsání existujícího klíče příchozí
kolizí jména a použití úplného klíče první RX zprávy před vyhledáváním podle jména.
Historie ověřuje předání výběru vlastníkovi a odmítnutí neplatného indexu nebo
režimu kanálu nad DM vláknem. Samotný `MessageStore` zůstává indexovaný názvem vlákna;
oddělené historie různých kontaktů se stejným jménem vyžadují další návrh identity
a kompatibility uložených dat. Poslední odečet po zahájení tohoto celku: **17 %**.

Nativní testy: `.sim-cache/refactor-20260927-chat-native.log`. Samostatný EN
integrační běh prošel (`.sim-cache/refactor-20260927-chat-integration.log`). První
úplný běh překročil 90 s v EN scénáři; neúspěšný log a částečný výstup zůstávají
v `.sim-cache/refactor-20260927-chat-tests.log` a
`.sim-cache/refactor-20260927-chat-timeout-smoke.log`. Příčina timeoutu není
prokázaná. Opakování beze změny produkčních zdrojů a limitu, po dokončení FW
překladu, prošlo celé: `.sim-cache/refactor-20260927-chat-tests-confirm.log`.

Aktuální T-Deck checkpoint je
`out/LilyGo_TDeck_companion_radio_touch-20260927_011700-9782e12-dirty.bin`:
RAM **120 192 B**, flash **3 644 357 B**, build **154,39 s**
(`.sim-cache/refactor-20260927-chat-firmware.log`). Manifest **313** zdrojů/hlaviček
`.sim-cache/refactor-20260927-chat-sources.json` byl po testech znovu ověřen beze
změn. `UITask.cpp` má **32 433 řádků** (−365 proti začátku kola). Tehdy ještě následovalo
oddělení přípravy příchozí zprávy, filtrů a snímku metadat před Lua/UI callbacky.
Tento krok i závěrečný merged BIN/JSON jsou nyní dokončené, viz předání nahoře.

### Odhad a doporučený bod ukončení

Hrubý odhad sdělený uživateli 27. 9.: dokončení aktuálního celku SD/vstupů
3–6 hodin, následná politika zamykání/probouzení a podstatná koordinace zpráv
dalších 8–16 hodin; důsledné rozdělení celého zbytku přibližně 40–80 hodin od
tohoto odhadu. Jde o odhad práce agenta včetně integrace a testů, nikoliv
závaznou dobu dodání ani požadavek na uživatelovo ruční testování rádia.

Smysluplný mezník má přednost před dosažením konkrétního počtu řádků:
`UITask.cpp` má vytvářet a spojovat moduly, předávat závislosti a řídit
start/tick/stop. Vlastní obrazovky, stav, souborové operace a pravidla přechodů
mají mít oddělené vlastníky. Home, senzory, Control Center, telemetrické grafy
a menší dialogy lze dále oddělovat při funkčních změnách; úplné vyčištění není
podmínkou přidávání aplikací. Detailní backlog níže nadále platí, dokud nová
integrační kontrola konkrétní řádek neuzavře.

## Obnovené kolo — 24. 9. 2026

### Výsledek kola a lokální firmware

**Dokončeno a zastaveno: 24. 9. 2026, 02:07 CEST.** Poslední odečet ukázal
**49 % týdenního využití, 51 % zbývá** (výchozí stav 11 %). Kolo končí na
uzavřeném a ověřeném celku těsně před dohodnutou hranicí 50 %; další část by
vyžadovala nový pracovní celek, proto se nerozpracovává. Celý refaktor tím
není dokončen. Zbývající práce a důvody jsou v backlogu níže.

Kód připravovali GPT-6 Sol subagenti s effortem high v ohraničených celcích;
hlavní řešitel kontroloval návrh, skutečné změny, integraci a ověření. Přibyly
vlastníci záloh, admin relací, obnovy SD, síťového executoru, fronty a transportu
dlaždic, kontextu fokusu, Wi-Fi/Bluetooth formulářů, plánování telemetrie,
zamykací obrazovky, úvodního průvodce a blokovaných uživatelů. BLE příkazy navíc
zachycují konkrétní zařízení a revizi. Byla odstraněna nevolaná stará Wi-Fi větev.
Podrobnosti, opravené vady a praktická omezení jsou v bodech 56–69 níže.

Závěrečný T-Deck build prošel: **RAM 120 016 B (36,6 %), flash 3 633 961 B
(89,4 % aplikačního slotu)**, 68,29 s. Log:
`.sim-cache/refactor-20260924-final-firmware.log`. Po sestavení nebyl změněn
žádný produkční zdroj; kontrola 270 souborů proti uloženému SHA-256 manifestu
prošla. Počet zahrnuje 266 souborů vrstev, `UITask.cpp/.h` a `BleKeyboard.cpp/.h`.

| Výstup pro LilyGo T-Deck / T-Deck Plus | Soubor |
| --- | --- |
| Kompletní obraz pro Guard-Mesh-Flasher, zápis od `0x0` | [Guard-Mesh-TDeck-20260924_015811-refactor-merged.bin](../out/Guard-Mesh-TDeck-20260924_015811-refactor-merged.bin) |
| Metadata; ponechat vedle BIN | [Guard-Mesh-TDeck-20260924_015811-refactor-merged.json](../out/Guard-Mesh-TDeck-20260924_015811-refactor-merged.json) |
| Samostatná aplikace, offset `0x10000` | [LilyGo_TDeck_companion_radio_touch-20260924_015811-9782e12-dirty.bin](../out/LilyGo_TDeck_companion_radio_touch-20260924_015811-9782e12-dirty.bin) |

Merged BIN má **3 699 920 B**, SHA-256
`ee87a21bb62385e594b1d6b237992e3901bf05b092b7533e599b23f6f96d6fc3`.
App BIN má **3 634 384 B**, SHA-256
`2e04f16c51cf5eb0a25bba0d8ba64b927cb7d7a5b44f84f362208223feabf129`.
Composer flasheru ověřil 16MiB tabulku oddílů i kontrolní součty ESP obrazů.
Byla porovnána totožnost všech čtyř částí na offsetech `0`, `0x8000`, `0xE000`
a `0x10000`, shoda app s posledním buildem i SHA v JSON. Úplný záznam:
`.sim-cache/refactor-20260924-final/artifact-verification.json`.
Pole `version` z ESP descriptoru obsahuje verzi základního ESP-IDF, nikoli datum
tohoto sestavení; nové vydání jednoznačně označuje název souboru a `title` v JSON.

Český katalog prošel s **1 322 klíči**. Při závěrečném ověřování byly zachovány
i neúspěšné běhy: `.sim-cache/refactor-20260924-final-tests.log` zachycuje CZ
timeout 90 s; samostatný běh stejného programu prošel za 32,73 s
(`.sim-cache/refactor-20260924-final-cs-diagnostic.log`). Příčina timeoutu není
prokázaná a nezvyšoval se limit. `simulator/run.py` nově při timeoutu ukládá
i částečný výstup, který dříve zanikl.

Další běh `.sim-cache/refactor-20260924-final-tests-confirm.log` prošel EN/CZ,
ale klávesový režim odhalil vadnou aserci testu admin watchdogu: kontrolovala
jen adresu již zrušeného timeru, kterou mohl alokátor ihned přidělit LVGL timeru
pro odložené smazání dialogu. Produkční `clearPending()` timer skutečně ruší
před Host callbackem. Oprava fixture ověřuje identitu callbacku, vlastníka
a periodu v seznamu živých timerů, zrušení pending stavu i následné smazání
dialogu. Nepřistupuje k uvolněnému timeru. Tyto dvě závěrečné změny se týkají
jen testů a jejich diagnostiky, proto se nemění hotový firmware.

**Závěrečný společný běh prošel celý**:
`.sim-cache/refactor-20260924-final-tests-verified.log`, simulátor
`guard-mesh-sim-18e76b7e7ab9.exe` (388 překládaných zdrojů). Prošly C++11 modely,
runtime harnessy executoru a transportu dlaždic, BLE cíle, telemetrický plánovač
i EN/CZ/klávesové UI regrese, stále s limitem 90 s na režim. UI logy mají časy
**02:06:01 / 02:06:34 / 02:07:06 CEST**; jejich kopie a log sestavení jsou v
`.sim-cache/refactor-20260924-final/`. `git diff --check` prošel.
Nově ukládaná diagnostika timeoutu jeho příčinu sama neopravuje; sporadický
timeout z předchozích běhů zůstává zaznamenaným omezením ověřování.
Fyzické RF, Wi-Fi/BLE, SD a uspávání se v tomto kole na zařízení neověřovaly.

Výstupy jsou lokální, sestavené z pracovní kopie na základu `9782e12-dirty`.
Nevznikl nový commit, push, PR ani veřejné GitHub vydání. Nesouvisející
rozpracované změny zůstaly zachované a rádio nebylo flashováno.

### Průběžné checkpointy

Uživatel výslovně obnovil refaktor podle tohoto backlogu a povolil GPT-6 Sol/Luna
agenty s effortem podle hlavního řešitele. Výchozí týdenní využití je **11 %**;
hranice tohoto kola je **50 % celkového týdenního využití účtu**. Kontroluje se
průběžně. Při dosažení hranice se nerozpracuje další celek; dokončí se běžící
celek, jeho ověření, report a nový lokální firmware app + merged BIN/JSON.
Předchozí zastavení na 91 % výše je historický stav minulého kola.

První celek: oddělení provozních operací záloh (scan/import/export/delete/reset)
za úzké adaptéry. Navazující návrh řeší admin/room relace a identitu cíle po
změně slotu kontaktu. Kód je před převzetím vždy zkontrolován hlavním agentem;
průběžné sestavení a testy jsou nutnou součástí celků. Výchozí kopie má
36 049 řádků `UITask.cpp` a 240 souborů vrstev; nesouvisející změny se zachovávají.
Lokální baseline pro diff: `.sim-cache/refactor-20260924-baseline/`.

Průběžné ověření tohoto kola: samostatný překlad `BackupOperations` a izolované
regrese operací záloh prošly (`.sim-cache/refactor-20260924-backup-isolated.log`).
Testy používají paměťový filesystem s krátkým čtením/zápisem a fake callbacky
resetu/restartu; neprovádějí operace na rádiu. Společný UI a firmware build
tohoto celku již prošel. Zjištěné a opravené vady: nerozpoznaný krátký zápis
exportu, nesprávně navyšovaná délka admin logu a opožděné login akce nad starým
slotem kontaktu. Odpovědi témuž kontaktu bez request ID zůstávají omezením mesh API;
generační kontrola UI sama neumí určit, ke kterému pokusu odpověď patří.

Průběžný checkpoint: modelová sada v C++11 včetně `SdRestoreJob` prošla
(`.sim-cache/refactor-20260924-sd-model-tests.log`). První společný simulator
build odhalil odlišný návratový typ desktop stubu importu; explicitní `bool`
v adaptéru sjednotil hranici se skutečným firmwarem. Následný překlad prošel;
výběr admin picker řádku v testu byl opraven na skutečnou `lv_list_btn_class`.
Poté prošla celá sada modelů, EN/CZ UI a klávesová navigace se stejným limitem
90 s na režim. Log `.sim-cache/refactor-20260924-storage-admin-tests-final.log`,
simulátor `guard-mesh-sim-463c40ae7bfb.exe`, UI logy 00:34:45 / 00:35:17 / 00:35:49.
Český katalog prošel s 1 319 klíči. Monolit má po tomto celku **35 151 řádků**,
oproti výchozím 36 049 ubylo **898**; přibylo šest souborů tří vlastníků/služeb.
T-Deck sestavení prošlo za 155,42 s: RAM 118 248 B, flash 3 606 645 B;
log `.sim-cache/refactor-20260924-storage-admin-firmware.log`, průběžný app obraz
`out/LilyGo_TDeck_companion_radio_touch-20260924_003244-9782e12-dirty.bin`.
Toto není závěrečné vydání kola. Poslední odečet využití: **20 %**.
Navazuje oddělení sdíleného síťového executorového vlákna a fronty dlaždic.

Další průběžné důkazy: `TileRequestLedger` prošel modelovou C++11 sadou
(`.sim-cache/refactor-20260924-tile-ledger-tests-final.log`). Skutečný zdroj
`SharedNetworkExecutor.cpp` prošel osmi scénáři s náhradním runtime
(`.sim-cache/refactor-20260924-executor-harness-final.log`): selhání queue/stack/task,
plná 64položková fronta, dokončení uvnitř publikace požadavku, invalidace za běhu,
priorita/pacing a diagnostika. Tento test není testem skutečného FreeRTOS plánování,
HTTP, rádia nebo SD. Společná integrace executor + navigační kontext prošla celou
sadou modelů, EN/CZ UI a klávesovou navigací; log
`.sim-cache/refactor-20260924-network-focus-tests-final.log`, simulátor
`guard-mesh-sim-185b2b929a77.exe`, UI logy 00:50:45 / 00:51:17 / 00:51:49.
T-Deck build prošel za 151,35 s: RAM 119 872 B, flash 3 609 677 B (88,8 % slotu);
log `.sim-cache/refactor-20260924-network-focus-firmware.log`, průběžný app obraz
`out/LilyGo_TDeck_companion_radio_touch-20260924_005049-9782e12-dirty.bin`.
Přesná identita požadavků a synchronizovaná fronta stojí proti předchozímu
checkpointu 1 624 B statické RAM a dalších 256 B alokované fronty.
Monolit má **34 946 řádků**, v tomto kole ubylo **1 103**. Poslední odečet
po tomto ověření byl **26 %**. Navazují Wi-Fi formuláře a transport dlaždic.

Wi-Fi checkpoint prošel celým simulátorem (modely, EN/CZ UI a navigace):
`.sim-cache/refactor-20260924-wifi-tests.log`, `guard-mesh-sim-92641e56f2dd.exe`,
UI logy 01:06:31 / 01:07:03 / 01:07:35. Samotný nový formulář i test se také
přeložily v C++11. Po integraci formuláře měl monolit 34 587 řádků, po transportu
dlaždic a opravě adaptéru **34 342** (v tomto kole úbytek **1 707**).
Společný T-Deck build prošel za 142,72 s: RAM 119 992 B, flash 3 616 669 B (89,0 %).
Log `.sim-cache/refactor-20260924-wifi-tile-firmware-final.log`, průběžný app obraz
`out/LilyGo_TDeck_companion_radio_touch-20260924_011357-9782e12-dirty.bin`.
První pokus zachytil chybějící forward deklaraci, návratový typ `lv_coord_t`
pro callback očekávající `int` a starý konstruktor LED příkazu po změně payloadu;
všechny tři chyby byly před úspěšným buildem opraveny. Poslední odečet **33 %**.
Samostatný test Bluetooth příkazů používá skutečnou sdílenou politiku kopírování,
identity a revize peeru; prošel v C++11 s warnings-as-errors
(`.sim-cache/refactor-20260924-ble-command-targets.log`). Neověřuje NimBLE provoz.

Bluetooth checkpoint prošel v celém simulátoru (modely, služby, EN/CZ a klávesová
navigace), log `.sim-cache/refactor-20260924-bluetooth-tests.log`, executable
`guard-mesh-sim-4aef44b9aef8.exe`; UI logy 01:26:10 / 01:26:43 / 01:27:16.
Skutečný T-Deck build prošel za 153,27 s, RAM 120 296 B, flash 3 624 749 B (89,2 %).
Log `.sim-cache/refactor-20260924-bluetooth-firmware.log`, průběžný app obraz
`out/LilyGo_TDeck_companion_radio_touch-20260924_012814-9782e12-dirty.bin`.
`UITask.cpp` má **33 750 řádků**, o **2 299 méně** než na začátku tohoto kola.
Samostatná služba telemetrie prošla modelovými testy, její zapojení a vlastník
zamykací obrazovky jsou další běžící celek. Poslední odečet: **37 %**.

Checkpoint telemetrie a zamykání prošel v celém simulátoru i firmware buildu.
Logy `.sim-cache/refactor-20260924-telemetry-lock-tests.log` a
`.sim-cache/refactor-20260924-telemetry-lock-firmware.log`, executable
`guard-mesh-sim-609d40d709b3.exe`; UI logy 01:36:36 / 01:37:09 / 01:37:42.
Regrese zamykání ověřila skutečnou LVGL cache před uvolněním obrázku, externí
DELETE, náhradu obrazovky z Host callbacku a časové přetečení. T-Deck build:
70,77 s, RAM 120 504 B, flash 3 629 213 B (89,3 %), průběžný app obraz
`out/LilyGo_TDeck_companion_radio_touch-20260924_013716-9782e12-dirty.bin`.
Monolit má **33 483 řádků**, o **2 566 méně** než výchozí stav. Český katalog
prošel s **1 321 klíči**. Poslední využití **41 %**; další celek je průvodce
prvním spuštěním a správce blokovaných uživatelů.

Průvodce prošel EN/CZ scénáři a modely v
`.sim-cache/refactor-20260924-setup-tests-final.log`. Klávesový režim v tomto
společném běhu překročil 90 s; samostatný diagnostický běh stejného executable
`guard-mesh-sim-57f94cebd1c4.exe` se stejným limitem pak prošel, log
`.sim-cache/refactor-20260924-setup-nav-diagnostic.log`. Příčinu jednorázového
timeoutu tento výsledek nedokazuje; finální společná regrese se znovu ověří po
navazující změně. Skutečný T-Deck build prošel za 156,66 s, RAM 120 704 B,
flash 3 632 629 B (89,4 %), log `.sim-cache/refactor-20260924-setup-firmware.log`,
app `out/LilyGo_TDeck_companion_radio_touch-20260924_014437-9782e12-dirty.bin`.
Monolit má **33 211 řádků**, v tomto kole ubylo **2 838**. Poslední odečet **43 %**.

## Implementace

1. **Zprávy a kontakty:** `MessageStore` vlastní ring, vlákna, unread/mention,
   ACK a mazání; `ContactModel` filtruje a třídí snímky, vzdálenost počítá jednou
   na kontakt. Modely nepotřebují LVGL. `ContactsScreen` vlastní řádkové kontexty.
2. **Historie:** `HistoryService` vlastní loadery, migraci, segmentový writer,
   snapshoty, plánování, diagnostiku a worker. `HistoryWorkerState` synchronizuje
   Idle/Queued/Running jedním atomickým stavem. Worker zapisuje snapshot a
   publikuje výsledek; tabulku segmentů mění UI. Opravené jsou ztracené informace
   o opravě segmentu, souběh queued úlohy se synchronním zápisem a neúspěšný zápis
   migračního markeru. Diskové formáty zůstaly zachované.
3. **Soubory a úložiště:** `FileOperations` odděluje kopírování, přesun a mazání,
   vlastní DMA scratch a kontroluje krátké čtení/zápis, délky cest, rekurzi a
   kopii do sebe. Při neúplné kopii nemaže zdroj. `DataFilesystem` vlastní výběr
   backendu dat a bootovací politiku; platformní mount volá přes Host.
4. **Mapa:** `MapScreen` vlastní stránku, canvas, posun/zoom, značky, trasy,
   časovače a popupy. Mesh a cache čte přes Host. `MapTileLayer` vlastní pixely,
   popisovače a cache LVGL, dodržuje limit bufferů a sdílí se s Lua mapou.
   `MapProjection` a `ImageCodec` jsou samostatné. Externí smazání stránky,
   canvasu nebo popupu zneplatní odpovídající odkazy.
5. **Lua a zvuk:** `LuaIntegration`, `LuaMapView`, `AppPermissionsScreen` a
   `AudioService` oddělují bridge, oprávnění, mapu, audio fronty, I2S a kodeky.
   Lua ABI se nemění. SD a UI akce procházejí explicitním rozhraním Host.
6. **Nastavení:** vlastní obrazovky mají profil, rádio, auto-add, experimentální
   nastavení, MQTT, rychlé odpovědi a přehled kategorií. `SettingsForm` odpojuje
   callbacky a odmítá události ze starého stromu. Parsování polí a rádiové presety
   jsou samostatné. Konfigurace používá existující NodePrefs.
7. **Terminál:** `TerminalSession` vlastní příkazy a vybraného příjemce,
   `TerminalScreen` log, vstup a picker. Stejnou relaci používá web i zařízení.
   Změna obsazení kanálového slotu už neodešle text do jinak pojmenovaného kanálu.
   Zavření odpojí klávesnici i sink; starý picker neovládá novou obrazovku.
8. **Správce souborů:** `FileManagerScreen` vlastní seznam, filtr/třídění, editor,
   obrázky a dialogy. Dostává snímky řádků úložišť a příkazy platformy; přímo
   neřídí mesh ani SD sběrnici. `FullscreenToolView` vlastní společný obal a
   kopii názvu nástroje. Zavření ruší čekající paste i nepotvrzené mazání.
   Editor odmítá krátké čtení a obrázek odpojí pixely před async smazáním widgetu.
   Chyba mazání už nezobrazuje úspěšné dokončení.
9. **Rádio, baterie a poloha:** služba odesílání a platformní transport oddělují
   UI draft od timestampů, pokusů, scope a ACK. Samostatné modely řeší baterii,
   polohu, publikační intervaly a časové přetečení.
10. **Společné vrstvy:** palety, fonty, styly, potvrzovací dialog, navigační stav
    a atomové invalidace jsou oddělené. PlatformIO i simulátor překládají nové
    adresáře. Platformní rozhraní poskytuje alokaci, čas, konfiguraci a transport.

11. **Inventář aplikací:** `AppInventory` vlastní dva snímky instalovaných aplikací.
    Worker pouze sestaví pracovní snímek; UI publikuje výsledek přes atomický stav
    Idle/Queued/Running/Ready. Úspěšná instalace ani invalidace se nepřepíše starým
    skenem. Žádosti se slučují a offline UI může atomicky převzít čekající úlohu,
    nikdy však běžící práci. Ošetřena je částečně neúspěšná alokace, krátké čtení
    manifestu a identifikátory, které by se jinak zkrátily na jiné jméno souboru.
12. **Obchod:** `AppStoreJobs` vlastní kopie požadavků a výsledků, používá atomické
    stavy a ponechává dokončenou úlohu až do převzetí UI. Zavření obchodu ani
    opakované klepnutí nepřepíše stahování. `AppStoreData` vlastní katalogy a seznam
    jazyků; parsování odmítá zkrácené identity, zohledňuje uvozovky/závorky v textu
    a zkracuje popisy na hranicích UTF-8. `AppStoreScreen` vlastní LVGL strom a
    časovač, ruší navigační odkazy při DELETE a odmítá staré callbacky.
13. **Síť a instalace obchodu:** HTTP/Lua bridge se přesunul do ESP32 adaptéru,
    který nesahá do tabulek UI. `StagedFileInstall` odděluje zápis od sítě: nejdříve
    zapíše a znovu přečte všechny části, pak zálohuje a nahradí původní soubory.
    Běžné chyby vedou k rollbacku; neúspěšný rollback zachová zálohy. Odstraněn je
    pokus napravovat chybu jazyka smazáním celého adresáře `/lang`. Změna nezaručuje
    transakčnost při výpadku napájení. Přetrvávající `.tmp`/`.bak` vyžadují obnovu.

14. **Vazba klávesnice:** `KeyboardBinding` vlastní mirror widget a sleduje životnost
    vypůjčené klávesnice a cílového pole. Přímé psaní zachovává kurzor; přepnutí
    pole nepřenese starý obsah. Externí smazání a reentrantní callback zneplatní
    vazbu před dalším zápisem. Politika zařízení, rotace, hardwarové klávesy,
    composer a další pickery zůstávají v koordinátoru.

15. **Chatová časová osa:** `ChatTimeline` vlastní virtualizované rozložení, indexy,
    posun, odložené renderování a URL dialogy; `ChatText` sdílené formátování.
    Čtení zpráv a příkazy dostává přes Host, bez přímého `UITask` nebo mesh.
    Externí smazání seznamu ruší práci a uvolní rozložení. Opakované požadavky
    na detail se sloučí. Řádkové callbacky ověřují sekvenci zprávy, takže po
    přepsání slotu ring bufferu neotevřou nabídku jiné zprávy.
16. **Akce zprávy:** `MessageActionMenu` vlastní snímek zprávy, nabídku a potvrzení
    opakovaného odeslání. Před akcí ověří sekvenci záznamu a aktivní konverzaci.
    Zavření a nahrazení menu zruší staré akce; odložené potvrzení neposílá do nově
    otevřeného vlákna.
17. **Detail zprávy a trasování:** `MessageInfoScreen` vlastní oba dialogy a životnost
    posuvného těla metadat. Callbacky ověřují aktuální strom. Trace kontroluje
    aktivní konverzaci; replay sestaví trasu z vlastního snímku zprávy. Skládání
    dlouhého výpisu omezuje délku po každém appendu. Rádiový požadavek a jeho
    asynchronní výsledek zůstávají koordinované přes Host.

18. **Vlastnictví navigační skupiny:** `FocusTargets` vlastní LVGL group a dynamický
    seznam jejích prvků. DELETE vyřadí záznam před automatickým refokusem; koordinátor
    včas zahodí odkazy na zvýrazněný a předchozí prvek. Už neexistuje paralelní pevné
    pole 160 objektů, které mohlo vynechat konec většího seznamu. Smazání vlastníka
    odpojí pozorovatele a zachová vypůjčené widgety. Pravidla navigace mezi obrazovkami,
    opakování kláves a výběr aktivního stromu ještě zůstávají v koordinátoru.

19. **Obnova a směrování fokusu:** `FocusNavigation` vlastní kolekci, přestavby,
    žádosti o fokus a obnovu po zavření dialogu, přeuspořádání vláken či náhradě
    widgetu. `ObjectRef` sleduje DELETE i mimo aktivní skupinu. `SpatialNavigation`
    vybírá souseda podle geometrie a explicitní politiky obrazovky; nepřeskočí
    úzký prvek v bližším řádku kvůli lepšímu zarovnání vzdáleného prvku. Preview
    slideru může smazat widget; následný commit se pak neposílá. Výběr aktivního
    kořene, hardwarový vstup a zvláštní přechody Home/obchod/M9 zůstávají v UITask.

20. **Jazykový soubor:** `LanguageFile` vlastní bootovací stav, buffer a seřazenou
    tabulku. Převádí starší volbu jazyka, čeká na dostupné úložiště a jednou žádá
    o stažení chybějícího souboru. Nezveřejní krátké čtení ani tabulku s vloženým NUL.
    Opakovaný klíč má deterministicky poslední hodnotu. Zánik nejprve odpojí překlad.
21. **Seznam konverzací:** `ThreadListScreen` vlastní oba styly řádků, jejich callbacky
    a podpis dat. Zachovává scroll a přeskočí nezměněný seznam; změnu náhledu pozná
    i při stejném timestampu. Před akcí ověří index, název a druh konverzace.
    Odpojené řádky neovládají náhradní seznam ani zaniklého vlastníka. Dlouhý stisk
    používá LVGL nebo explicitně dodaný hardwarový vzorek; přidružený click neotevře chat.
22. **Akce konverzací:** `ThreadActionMenu` vlastní nabídku, snímek identity,
    samostatné potvrzení, obsah sdílení kanálu a odloženou volbu ikony. Změna jména,
    klíče kontaktu nebo secretu kanálu zneplatní zachycenou akci. Změněný slot stejného
    kanálu se při otevření regionu načte znovu. Staré menu/confirm/share callbacky
    neovládají novou instanci. Picker předává generaci požadavku, odmítá události
    ze starého stromu a čistí odkazy při externím DELETE. Rádiové/modelové
    příkazy zůstávají v koordinátoru.
23. **Emoji a symboly:** `GlyphPicker` vlastní sady, popup, grid, cílový field,
    výběr a akumulátory pohybu. Má příkazy pro hardware a samostatný režim volby
    s request ID. Zánik cíle/gridu ruší popup. Staré gridy neovládají novou instanci;
    smazání buňky neposune identitu vkládaného znaku. Callback vložení může otevřít
    náhradní picker bez jeho následného zavření.
24. **Rychlé odpovědi:** `QuickReplyPicker` vlastní kopie zobrazených odpovědí,
    popup a pozorování cíle. Změna konfigurace po otevření nezmění význam starého
    tlačítka. GPS se ověřuje znovu při výběru. Host respektuje mirror softwarové
    klávesnice; popup se ruší při přepnutí/zavření konverzace i DELETE pole.
    Desktop nyní vykresluje skutečné řádky odpovědí přes stejnou komponentu.
25. **Návrhy zmínek:** `MentionPicker` vlastní nabídku, kopie jmen a snímek textu
    i kurzoru. Nahrazuje celý aktivní token, zachová text za ním a odmítne akci
    po změně cíle či textu. Směrování kláves používá veřejné příkazy komponenty.
26. **Nabídka diakritiky:** `AccentPicker` vlastní popup, snímek a výběr;
    `AccentCharacters` sdílí původní neměnné sady s ostatními režimy zadávání.
    Opravena záměna posledního znaku textu za znak před kurzorem. Náhrada zachová
    UTF-8, suffix a dlouhá nastavení.
27. **Cykly diakritiky:** `AccentCyclePicker` vlastní long-press i ALT režim,
    cílové pole, snímek, popup a timeout. Odložený přepis textu je odstraněný;
    skutečná LVGL klávesnice testuje náhradu až po výchozím vložení znaku.
    Změna pole, textu či kurzoru zneplatní původní akci. Zánik pole/rootu zruší timeout.
28. **Výběr textu a editace:** `TextSelection` vlastní sticky selection a časování
    dvojkliku s DELETE pozorováním. `TextEditMenu` vlastní nabídku, oba fieldy
    (původní/mirror) a snímek akce. Copy/Cut/Paste/All/Sym odmítají zastaralý kontext.
    Úpravy respektují UTF-8, max-length, accepted-chars, one-line a password režim.
    Omezená pole používají nativní kroky LVGL s kontrolou životnosti a reentrantní
    editace; neomezená používají jednu změnu. Clipboard a vazby kláves zůstávají v Host.

29. **Composer:** `ChatComposer` vlastní prvky, callbacky, počítadlo znaků a růst
    pole; každý panel má vlastní výšku. Přestavba odpojí staré události před
    odloženým smazáním. Tlačítko a hardwarový Enter sdílejí odeslání z kopie textu,
    se zachováním rozdílné politiky klávesnice. Přepnutí konverzace zneplatní
    rozpracovaný stisk i dokončení starého odeslání. Host ověřuje aktivní vlákno
    před rádiovým příkazem. `FkeyShape` sdílí kreslení a uvolňuje canvas buffer.
30. **Synchronizace klávesnice:** `KeyboardBinding` upravuje jen změněný UTF-8
    úsek a kontroluje generaci i zánik objektů po každém nativním kroku.
    Nepoužívá vícekrokový `lv_textarea_set_text` na omezeném poli, které může být
    smazáno uvnitř VALUE_CHANGED. Mirror přebírá povolené znaky, délkový limit
    a režim hesla; nová hodnota se filtruje před výpočtem rozdílu, aby starý suffix
    nespotřeboval limit nového textu. Jeden vložený znak vyvolá jednu změnu cíle.
31. **Schránka:** `TextClipboard` vlastní původní kapacitu 640 B, bez mezilehlého
    600B bufferu pro kopírovaný label. Ořez neoddělí část UTF-8 znaku. Recolor se
    odstraňuje jen u labelů s tímto režimem; běžné a escapované mřížky se zachovají.
    Testy modelu ověřují hranice bufferu, null/prázdný vstup a kopírování do sebe.

32. **Aktualizační úlohy:** `FirmwareUpdateJobs` vlastní požadavky na kontrolu
    a instalaci, atomický stav a výsledky. Kanál, verze a cíl jsou snímkem okamžiku
    zadání. OTA a SD nemohou současně přepsat rozpracovaný požadavek. Selhání
    startu executorového vlákna publikuje chybu; běžící úlohu už nezruší.
    `ReleaseMonitor` vlastní opakování/backoff a generační kontrolu kanálu.
    `ReleaseListing` parsuje inkrementálně a odmítá přetečení čísla vydání.
33. **Transport aktualizací:** `FirmwareUpdateTransport` odděluje HTTP,
    mapování cílové desky, Arduino Update a SD zápis od LVGL. Zachovává adresy
    serverů, timeouty a DMA buffer. SD čtení nepřesáhne deklarovanou délku;
    OOM po otevření souboru odstraní prázdný neúspěšný download. Lifecycle SD
    čeká na celý queued/running zápis, nikoliv jen na dosud nepřevzatý požadavek.
34. **Výběr staršího vydání:** `ReleasePicker` vlastní picker i potvrzení a je
    součástí registru popupů. Zavření nastavení nebo změna kanálu jej zruší.
    `ConfirmDialog::showCaptured` uchová hodnotovou kopii akce přes odpojení
    stromu; callback zavření může vytvořit nový dialog bez přepsání původního
    potvrzeného požadavku nebo ztráty nové akce.

35. **Rádiová viditelnost (LOS):** `Sightline` odděluje vzorkování, CSV, opravu
    mezer, zakřivení a Fresnelovu zónu. Nulová vzdálenost nezpůsobuje dělení nulou;
    nekonečné výšky jsou chybějící data. URL musí obsahovat všech 24 bodů a při
    překročení kapacity se odmítne. Vzorkování respektuje přechod přes poledník 180°.
    `SightlineJob` uchová souřadnice i server a publikuje výsledky atomicky;
    starý 48s watchdog už nemůže přepsat právě používané buffery workeru.
    `SightlineTransport` používá existující worker a jeho HTTP/socket, omezuje
    odpověď na 1 023 B a zpracuje HTTP chunk framing přes nativní HTTPClient.
    `SightlineScreen` vlastní modal, výšky antén a výsledek; graf má vlastní
    body až do DELETE. Staré výsledky, tlačítka a backdrop neovládají nový dialog.

36. **Systémová diagnostika:** `SystemDiagnostics` formátuje hodnotové snímky
    s omezenou délkou a vlastní historii stallů včetně kopií tagů. Krátký buffer
    nepřeteče při dalším appendu; neexistující hardwarová data vracejí prázdný text.
    `DeviceDiagnostics` sbírá ESP32 heap/PSRAM, čip, flash, NVS a důvod resetu bez
    LVGL/`UiDevice.h`; drahá kontrola obrazu zůstává jednorázově cachovaná.
    `SystemInfoScreen` vlastní odkazy a obnovování obou textových částí, zachová
    flex layout a vynechá nezměněný text. DELETE, přestavba či reentrantní náhrada
    stránky zabrání použití starého snímku. Memory detail je jednorázový snímek.
37. **Kapacita SD:** `StorageUsage` nahrazuje několik sdílených `volatile` polí
    jedním atomickým stavem a odděleným výsledkem workeru. UI publikuje celou
    64bitovou kapacitu a řídí 30s interval. Mount notifikace z libovolného vlákna
    pouze zvýší atomickou generaci; starý výsledek ani queued scan se nepoužijí
    pro náhradní mount. Pouhé převzetí stejného živého VFS scan neruší. Lifecycle
    brána zahrnuje queued i running práci; samotný mount/recovery zůstává v koordinátoru.

38. **Vlastnictví zpráv:** `MessageStore` už nemá `friend UITask` ani
    `friend HistoryService`. Sám provádí přejmenování včetně souvisejících zpráv,
    mazání, vazby kontaktů/kanálů a kontrolu identity ring slotu. Koordinátor
    pouze plánuje persistenci a aktualizaci obrazovky. Konflikt celého pubkey
    nelze obejít shodným prefixem. Pouhé obnovení cached indexu nepíše metadata.
    Sekvence po vyčerpání nepřeteče na znovupoužitou identitu.
39. **Obnova historie:** oba starší loadery používají stejný převod fyzického
    diskového ringu do aktuální RAM kapacity, zachovávají nejnovější záznamy
    ve správném pořadí. Model publikuje počet/head až po naplnění bufferu.
    Migrace zapisuje index vláken před markerem; částečný zápis indexu/markeru
    se obnoví ze starého souboru. Platné prázdné segmentové úložiště už nemůže
    při rebootu vzkřísit dříve smazaný kombinovaný archiv.
40. **Aktualizační panel:** `FirmwareUpdatePanel` vlastní widgety a callbacky
    aktualizací About. Běžící job zůstává obsazený i po změně kanálu nebo zavření
    stránky; dokončení přebírá UI smyčka bez globálních LVGL časovačů. Starý strom
    nemůže spustit instalaci či měnit nový kanál. OOM workeru uvolní job po
    převzetí chyby. Úspěšná OTA vyvolá reboot i po DELETE stránky, právě jednou.
    `OtaCapability` přesouvá původní kontroly slotu a fyzické adresy do platformy.
41. **Předání Wi-Fi scanu:** `WifiScanJob` nahrazuje sdílená volatile pole
    atomickým stavem a dvěma vlastněnými snímky. SSID se publikují společně
    s diagnostikou, mají omezenou délku/počet a deduplikují se podle uložené hodnoty.
    Claim a zrušení čekající práce soutěží o jeden přechod; timeout nemůže
    odpojit již běžící scan. OOM spuštění executorů ukončí čekání výsledkem.
    `WifiScanTransport` přebírá watchdog scan/retry a kontrolu dostupnosti rádia
    bez LVGL/UiDevice. Síťový handshake a formuláře zatím zůstávají v koordinátoru.
    SSID limit desktopového adaptéru byl sjednocen s produkčním limitem 32 B.
42. **Nastavení hodin:** `ClockSettingsScreen` vlastní pole, přepínače a picker
    zón, `ClockSettings` preference a vazbu RTC, `ClockTime` validaci kalendáře
    a převod lokálního času. Neplatný den už `mktime` tiše neposune do dalšího
    měsíce; zůstává zachována dolní hranice RTC. Staré formuláře/pickery nemění
    novou stránku, ani když se přestaví během synchronizace klávesnice či zavírání.
    Aplikování TZ patří platformě; desktopový CRT není důkaz shodných DST pravidel
    se všemi zónami ESP32. Deterministický převod je ověřen pro UTC.
43. **Nastavení GPS:** `GpsSettingsScreen` vlastní živé odkazy a callbacky,
    `GpsSettings` volby baud/soukromí a hodnotový snímek přijímače. `GpsStatus`
    vlastní začátek akvizice a omezené formátování pro stránku i Control Center.
    Start v čase 0 i přetečení millis jsou platné; otevření stránky čas nerestartuje.
    Flex rozložení posune ovládání pod rostoucí status. Detach zavře dropdown,
    staré stromy nezapisují preference a opakovaná volba nepřepisuje celý blob.
    Stránka bez GPS ponechá volby soukromí; žádné skutečné souřadnice nemění.
44. **Historie baterie:** model `battery::Sample`/`estimate` odděluje parser
    původních 4/5 sloupců a výpočet výdrže. `BatteryHistory` vlastní interval,
    výběr nejnovějšího konce historie a zápis na jeden zachycený backend.
    Log je omezen na 24 hodin a 288 vzorků, včetně doby bez platného RTC.
    Staging kontroluje krátké čtení/zápis i readback před přejmenováním; původní
    soubor má zálohu až do úspěšné publikace. Selhání rollbacku ponechá zálohu
    a temp pro obnovu a další zápis je nepřepíše. Není to power-loss transakce.
    `BatteryHistoryScreen` vlastní popup a potvrzení s kopií cílového backendu.
    Buffer vzorků se uvolní po naplnění vlastních řad LVGL, nikoliv až při rebootu.
    `ChartTicks` sdílí převod mV na V také se senzory a vzdálenou telemetrií.
45. **Pořadí DELETE a diagnostika simulátoru:** nový test grafu odhalil pád
    navigace na starém kořeni. Příčinou bylo odstranění callbacků během DELETE,
    které v LVGL 8 posunulo a přeskočilo následující pozorovatele. Hodiny, GPS
    a graf nechají nejprve proběhnout `ObjectRef`; následný úklid tak nemění
    seznam callbacků mazaného kořene. Testy kontrolují doručení třem dalším
    pozorovatelům i životnost potvrzení. Windows simulátor nově zapisuje nativní
    výjimku a zásobník s funkcemi/řádky z PDB; diagnostika byla ověřena řízenou
    výjimkou. Tato diagnostika není součástí firmwaru.

46. **Nastavení a kalibrace baterie:** `BatterySettings` vlastní `BatteryModel`,
    kalibrační preference, filtrované/publikované napětí, volbu hardwarového
    fuel gauge a rozpracovaný kalibrační požadavek. Osm vzorků nyní pořizuje
    postupně přes `tick`, nejvýše jeden po 20 ms; handler už neobsahuje
    osm blokujících `delay(20)`. Zůstává průměr nenulových hodnot a hranice
    3,5 V. Nový požadavek, reset či zavření formuláře zneplatní staré měření.
    `BatterySettingsScreen` vlastní formulář, callbacky a token dokončení;
    změna stránky během ADC/sleep callbacku nemůže dokončit starou akci.
    Zachovány jsou board-specific EMA a úsporný režim; neplatný SOC z hardware
    přechází na napěťový model. Chyba uložení už nehlásí úspěch, ale stávající
    preference store může držet změnu v RAM (nejde o transakční NVS rollback).
    Odstraněn byl i dávno vypnutý panel živých informací, které patří do About.

47. **Zvuk a upozornění:** `NotificationPolicy` odděluje půlhodinová časová
    okna, prioritu typů upozornění a paletu indikátoru. `SoundSettings` vlastní
    preference, kontrolu master mute/DND pro náhledy i příchody, cílový slot
    souboru a časovaný stav blikání včetně retry I/O. Opožděný tick nedohání
    přechody v burstu; vypnutí zneplatní starou sekvenci i při reentranci.
    `SoundSettingsScreen` vlastní formulář, swatche, nabídku souborů a callbacky.
    Staré menu nepřepíše novou volbu; zánik formuláře zavře jeho nabídku.
    WAV dialog správce souborů navíc zachytí slot jednou při otevření a při
    potvrzení ukládá do právě zobrazeného cíle. Náhled při zapnutí master Sound
    nyní respektuje DND stejně jako ostatní náhledy. Hardwarové chime/volume/RGB
    operace zůstávají v úzkém adaptéru, bez LVGL v pravidlech a službě.

48. **Klávesnice a podsvit:** `KeyBindings` vlastní tabulku a validaci kláves,
    výchozí mapy desek a převod jasu. `KeyboardSettings` vlastní preference,
    aktivní cache mapování, režim/jas a odložený zápis; zavření formuláře zápis
    nezruší. `KeyboardSettingsScreen` vlastní ovládací prvky a právě probíhající
    zachytávání klávesy. DELETE/close zachytávání ruší a odpojuje staré callbacky.
    Nastavení jazyků používá skutečný sdílený registr; vypnutí právě aktivního
    rozložení přejde na EN. Posuvník plánuje uložení i bez události RELEASE.
    Adaptér zachovává okamžitou reakci periferií a navigace; pravidla nemají LVGL.
    V ovládacím centru byl zároveň odstraněn zbylý odkaz na starý zvukový preview
    helper, nahrazený stejnou službou zvuku jako ostatní náhledy.

49. **Nastavení displeje:** `DisplaySettings` odděluje validaci časového limitu,
    preference vzhledu a požadavky na restart. `DisplaySettingsScreen` vlastní
    textarea, dropdown, přepínače, tlačítka a callbacky; reentrantní čtení,
    synchronizace klávesnice či uložení nepokračují do nahrazeného formuláře.
    Zavření zavře také seznam velikostí. Vypnutí hlavního náhledu zpráv uchová
    preferenci náhledu na zamčené obrazovce a pouze zakáže její přepínač.
    Prázdný/nečíselný časový limit se neukládá, platný se omezí na původních
    0 nebo 10–3600 sekund a pole ukáže uloženou hodnotu. Prázdné pole tak už
    nechtěně nevypne zhasínání. Změna tématu se ukládá před požadavkem restartu;
    stejné téma restart nevyvolá. Runtime vykreslování displeje zůstává další
    částí monolitu; barvový picker a stylování jsou vyčleněné níže.

50. **Picker barvy vzhledu:** `AccentColorPicker` vlastní rozepsanou hodnotu,
    strom, swatche a callbacky. Před uložením synchronizuje mirror a validuje
    aktuálních šest hex číslic. Chyba zápisu či neúplný text neaplikuje starou
    barvu ani nerestartuje zařízení. Výměna pickeru během read/sync/save/close/apply
    nepokračuje do nového stromu. Zánik vlastníka odmítá opětovné otevření z close.
    Čistý model `ColorChoice` vlastní palety a validaci bez LVGL.
51. **Nastavení zamčení:** `LockSettings` odděluje preference a schopnosti desek;
    `LockSettingsScreen` vlastní caption tapety, přepínač a barevné swatche.
    Souborový manager oznamuje změnu přes veřejné rozhraní; zavřený formulář
    už nezanechá globální LVGL ukazatel. Deska bez podporovaného odemčení
    nedostane auto-lock. Nepoužívaný starý rekurzivní SD scanner/picker byl
    odstraněn; skutečný výběr nadále používá správce souborů.
52. **Společné stylování:** `TouchTheme` vlastní wrapper tématu a pravidla
    vzhledu widgetů pro denní/noční a e-paper variantu. Opakované připojení
    stejného wrapperu nevytvoří cyklus v řetězci témat. `ColorSwatch` zajišťuje
    stejnou barvu při stisku, fokusu i checked stavu pro oba pickery.
53. **Obecné nastavení:** `GeneralSettings` odděluje mapování limitů historie,
    fallback zápisu, preference SD/konzole, vyhodnocení snímku úložiště a akce
    zařízení. `GeneralSettingsScreen` vlastní formulář i potvrzovací dialog.
    Pozdní potvrzení neovlivní nahrazený formulář ani novější výběr ve stejném
    formuláři; DELETE nevynechá další pozorovatele. Konzole nejprve uloží volbu
    a až po úspěchu požádá o restart. Obnova SD se stále pouze naplánuje do
    původní recovery pipeline, kterou tato iterace nepřepisovala. Z monolitu
    ubylo dalších 223 řádků; celá migrace a mount SD zůstávají níže v backlogu.
54. **Katalog, importní picker a nastavení záloh:** `BackupCatalog` vlastní
    omezený seznam 24 cest a popisků; `BackupPickerScreen` a `BackupSettingsScreen`
    mají každý svůj katalog, LVGL odkazy a potvrzení. Cíle importu a mazání jsou
    kopie celé cesty, nikoliv odkazy do sdíleného přepisovaného bufferu. Příliš
    dlouhá cesta se odmítne, popisek se může zkrátit. Generace brání pokračování
    staré akce po náhradě stránky. Nastavení vlastní a ruší odložené obnovení,
    včetně uvolnění paměti při zrušení či selhání naplánování. Zachované jsou
    RTC názvy exportů a dosavadní board/storage/reset policy. Z monolitu ubylo
    134 řádků; vlastní I/O a reset dosud zůstávají za adaptéry v `UITask.cpp`.


55. **Menu akcí kontaktu:** `ContactActionSheet` vlastní strom, odkazy, kontexty
    tlačítek a hodnotový snímek 32bajtového veřejného klíče. Před zavíracím Host
    callbackem odpojí staré události a vyprázdní stav; generace zabrání předání
    původní akce, pokud Host mezitím otevřel náhradní menu. Externí DELETE
    zachovává další pozorovatele, destruktor odmítá reentrantní otevření.
    `UITask` při akci znovu vyhledá kontakt podle klíče a teprve poté získá jeho
    aktuální index; `MapScreen::showContactOnMap` dostává kontakt explicitně.
    Schopnosti a preference dodává adaptér, stavový pruh se čte při otevření.
    Rozložení, pořadí akcí a mesh policy zůstávají zachované. Z monolitu ubylo
    244 řádků. Následné admin/room relace a opožděné odpovědi nejsou tímto
    vyřešené: mimo jiné `s_room_join_idx` stále patří do dalšího celku.

56. **Backend záloh:** `BackupOperations` vlastní katalogový scan, názvy,
    výběr backendu podle zachycené cesty, import/export/mazání a reset policy.
    Kontroluje i poslední neúplný buffer exportu; po krátkém zápisu odstraní
    neúplný soubor a nehlásí úspěch. Import drží watchdog přes navazující
    persistenci a restart. Pager při neověřené SD nedokončí destruktivní reset.
    Core import/parser a trvanlivost fyzického úložiště se nemění.

57. **Admin a room relace:** `AdminSessionScreen` vlastní prompt, konzoli,
    nabídku příkazů, klíč cíle, heslový pokus, watchdog a odložené skrytí
    klávesnice. Zavření/náhrada ruší staré callbacky; room Join dohledává
    aktuální slot podle klíče. Bounded log přičítá skutečně zapsané bajty
    a bezpečně zvládne dlouhou odpověď i selhání alokace. Tichý relogin
    nezasahuje do čekajícího interaktivního pokusu. Chybějící request ID
    v core API nadále omezuje rozlišení starých odpovědí témuž kontaktu.

58. **Ruční obnova SD:** `SdRestoreJob` vlastní odložený požadavek a pořadí
    drain → kontrola profilu → příprava → durable latch → kopie → restart.
    Zachovává rozdílnou Pager/legacy policy a při neúplné kopii ponechá latch
    aktivní. Adaptér používá původní watchdog guardy a spouští úlohu až mimo
    LVGL event stack. Chybový alert nyní přichází po uvolnění guardů; samotné
    I/O a restart zůstávají pod nimi. Mount, obnova sběrnice a atomické přijetí
    práce všech SD workerů zatím nejsou přesunuté ani tímto vyřešené.

59. **Síťový executor a fronta dlaždic:** `SharedNetworkExecutor` vlastní
    permanentní core-0 task, frontu 64, časně rezervovaný interní stack a
    synchronizovanou evidenci požadavků. `TileRequestLedger` drží přesné
    souřadnice a tokeny: rezervuje před publikací, vrací neúspěšné vložení
    a odmítá dvojité dokončení. Aktivní požadavky se neztratí vyčištěním recent
    historie; chybná stará odpověď neuvolní nový token. Zachované pořadí jobů,
    250ms probouzení, zavření socketu/lease před 500ms pacingem. Přesné záznamy
    stojí přibližně 1,9 KiB RAM navíc oproti původnímu hash ringu/frontě.
    Transport/cache, backend lease a mapové stavové proměnné zatím zůstávají
    za adaptérem; skutečný stop/join workeru není implementován ani předstírán.

60. **Aktivní navigační kontext:** `FocusContextSelector` sdílí pořadí popup →
    Wi-Fi → nastavení → chat → aktivní tab mezi focusem, hlavními zkratkami
    a e-paper scroll rootem. Neuchovává widgety ani stav relace. Zachovává
    signatury a zvláštní prioritu platných base-layer sheetů; Wi-Fi-only sheet
    nově blokuje hlavní zkratky. Samotné HW/BLE klávesové směrování, repeat
    a lock/edit přechody zůstávají k dalšímu rozdělení.

61. **Wi-Fi formuláře:** `WifiFormsScreen` vlastní seznam sítí a Join/Hidden/Details
    sheet, pozorované odkazy, zachycené SSID řádků a životnost callbacků. Adaptér
    dohledává uloženou síť podle SSID před každou akcí. Join nejprve synchronizuje
    klávesnici; neúspěšné uložení zachová draft a nehlásí připojování. Radio policy,
    přechody Wi-Fi/BLE a scan/reconnect zůstávají v platformní koordinaci.
    Staré aktivní sheet/list funkce jsou odstraněné; dosud nepoužívané starší
    slotové editory a scan popup nejsou tímto prohlášené za refaktorované.

62. **Transport mapových dlaždic:** nový `TileFetchTransport` vlastní cache/HTTP,
    kontrolované zápisy, odstranění neúplného souboru a výsledný stav/pacing.
    Scoped Host zajišťuje původní lease; socket a File se zavírají před jeho koncem,
    viditelná mapa se invaliduje až potom. Přímý test produkčního transportu
    prošel v C++11 s `-Wall -Wextra -Werror`
    (`.sim-cache/refactor-20260924-tile-transport-harness.log`). Adaptér je zapojený
    a T-Deck checkpoint prošel. Zachovaný dluh: zápis přijme dvoubajtové SOI, cache read vyžaduje
    tři bajty; `FF D8 00` se proto může zbytečně stáhnout znovu. Test to výslovně
    zaznamenává. Lease sám neřeší veškerou souběžnou práci s SD ani mapovým stavem.

63. **Cíle Bluetooth příkazů:** fronta `BleKeyboard` nyní kopíruje Device místo
    indexu proměnlivého scan seznamu. Podmíněný Forget zachytí adresu, její typ
    a revizi spárovaného peeru; worker ověří cíl před operací a před vymazáním
    aktuálního peeru. Bez běžícího workeru zůstává přímé zapomenutí podporované.
    Návratové hodnoty rozlišují odmítnutí fronty od přijatého příkazu, nikoliv
    dokončení fyzického párování. Zachované omezení: přerušení starého connectu
    probíhá před vložením; při plné frontě tedy může být starý connect přerušen,
    přestože nový příkaz není přijatý. Obrácené pořadí by mohlo rušit nový connect.

64. **Bluetooth formuláře a párování:** `BluetoothSettingsScreen` vlastní PIN,
    přepínače, výběr klávesnice, potvrzovací token a životnost stránky. Akce mají
    kopii adresy i jejího typu; změna pořadí scanu nemění cíl. Nezměněný snímek
    nastavení nepřepisuje widgety každou UI smyčku. Politika rádia a směrování
    kláves zůstávají v adaptéru. Starý stav Connected sám nezavře nový pokus;
    automatické zavření vyžaduje pozorovaný průběh a shodnou identitu. Velmi
    rychlé dokončení mezi dvěma UI odečty může nechat stránku otevřenou, protože
    BLE core dosud neposkytuje identifikátor požadavku. Plné EN/CZ/klávesové
    ověření i skutečný T-Deck build prošly.

65. **Plánovač telemetrie:** `TelemetryPolling` vlastní osm záznamů, parser,
    migraci, konečný počet požadavků, round-robin výběr a wrap-safe časování.
    Pozdě vložená SD se znovu načte; změny provedené před načtením mají při
    sloučení přednost. Přetečení kapacity nepublikuje částečný plán. Chyby čtení
    a zápisu mají pětisekundový odstup opakování. Adaptér ukládá konfiguraci
    přes `StagedFileInstall`, prázdný plán jako jediný newline, a rozlišuje
    přijetí změny od úspěšného uložení. Spotřebovaný požadavek a mezera se
    zapíší do stavu před voláním transportu; regresní test zkouší synchronní
    opakované volání a výměnu záznamu. Trvalá chyba zápisu stále znamená, že
    počet zbývajících požadavků nepřežije restart spolehlivě. Host zachovává
    dosavadní šestibajtový klíč kontaktu a platformní životní cyklus SD.

66. **Zamykací obrazovka:** `LockScreen` vlastní overlay, odpočet, stabilní
    obrazový deskriptor a vypůjčenou/vlastněnou tapetu. Před uvolněním či novým
    použitím deskriptoru invaliduje skutečnou LVGL image cache. Dočasné sledované
    odkazy chrání zavírání před callbackem, který smaže starý strom nebo otevře
    novou obrazovku. Hodiny zachovávají rozlišení kvality času a minutový posun;
    počet nepřečtených zpráv se čte nejvýše jednou za sekundu. Hardware lock/wake,
    dekódování JPEG a výběr board assetu zůstávají v adaptéru. Plná EN/CZ/klávesová
    regrese i skutečný T-Deck build prošly.

67. **Průvodce prvním spuštěním:** `SetupWizardScreen` vlastní čtyři kroky,
    jméno, výběr regionu a LVGL strom. Každý přechod má novou generaci, takže
    návrat Back zevnitř ukládacího callbacku nepřebije staré pokračování.
    Selhání uložení jména/regionu nebo příznaku dokončení ponechá krok otevřený.
    Back znovu načítá uložené jméno jako dříve; dosavadní výběr regionu trvá
    do zavření. Draft unese 30 UTF-8 znaků bez záměny znaků za bajty; původní
    32bajtová hranice core preference se nemění. Všechny klávesové a main-loop
    kontroly už používají vlastníka; prvotní vypnutí BLE zůstává boot politikou.
    Mrtvá větev dřívějšího Wi-Fi kroku je odstraněná. První test odhalil chybný
    selektor popisku s LV_LABEL_LONG_DOT; závěrečný test vybírá řádek přímo
    ve strukturálně určeném kontejneru katalogu regionů.

68. **Blokovaní uživatelé:** `BlockedUsersScreen` vlastní stránku a kopie cílových
    šestibajtových klíčů nebo 32bajtových jmen. Kontejner řádků vzniká až při
    otevření, s PSRAM a interním fallbackem; staré callbacky se odpojí před jeho
    uvolněním i při odloženém smazání stromu. Prázdná stránka i chyba načtení
    obnoví stav horní lišty při externím DELETE. Adaptér zachovává párové odblokování
    klíče a jména dohledáním aktuálního kontaktu. Staré preference vracejí stav
    členství, nikoli výsledek zápisu; dvoučástkové odblokování proto není nově
    deklarováno jako atomické. Layout používá skutečně naměřenou výšku lišty.
    Závěrečná kontrola odhalila čtení Y souřadnice před prvním LVGL layoutem;
    výška se nyní předává přímo do tvorby seznamu. Regrese kontroluje rozměry
    stránky i vnitřního seznamu při liště vysoké 22 a 44 pixelů.
69. **Odstraněná stará Wi-Fi větev:** po ověření všech vstupních bodů zmizely
    nevolané formuláře, credential globály a scan popup. Aktivní `WifiFormsScreen`,
    scan job/fronta, příprava rádia, publikace výsledků i migrace tří starých
    profilů zůstávají. Výsledkem je jediná aktivní implementace těchto formulářů.

## Ověření

Následující souhrnná tabulka zachycuje dokončené kolo z 21. 9. 2026;
průběžný stav obnoveného kola je uveden výše.

| Kontrola | Výsledek a rozsah |
| --- | --- |
| `python simulator/run.py --test` | Po dokončení obou celků prošel překlad sdíleného UI, C++ modely/služby i celý EN/CZ/klávesový průchod. Původní limit 90 s se nezvyšoval. Log `.sim-cache/luna-contact-final-tests.log`, binární soubor `guard-mesh-sim-d159c97afa11.exe`; UI logy 19:01:05 / 19:01:36 / 19:02:08. |
| Zálohy | Filtr JSON/hidden/AppleDouble, duplicity a limity katalogu, dlouhé cesty/popisky, zachycené cíle, Cancel/náhrada, pozdější DELETE observers, staré události po zániku vlastníka, reentrantní potvrzení, RTC název a zrušení odloženého refresh. Mazání/reset jsou řízené fake Host akce. |
| Akce kontaktu | Kopie celého klíče, staré události po zavření/náhradě/zániku vlastníka, externí DELETE s dalším pozorovatelem, odmítnuté otevření během destrukce, reentrantní náhradní menu, close badge/backdrop, dynamická výška statusbaru, chat/repeater/room/map/GPS/location varianty, mapování všech 15 akcí a změny popisků. Síťové/mazací operace jsou fake Host dispatch; skutečný adaptér dohledání kontaktu a mapy byl zkontrolován v diffu a přeložen pro T-Deck. |
| Historie | Přechod přes hranici 256 záznamů, reload, přejmenování a mazání/kompakce; import 550→128 a diskového ringu 6→3/10; restart při neúplném indexu/markeru i po úplném vymazání migrace; krátký zápis, retry a asynchronní snapshoty. |
| Soubory | Kopie přes více bufferů, strom adresářů, selhání čtení/zápisu/rename, zachování zdroje při neúspěšném přesunu. |
| Mapa | Skutečné dekódování PNG, opakovaný snímek bez načítání, noční režim, kapacita bufferů, oprava vadné dlaždice, externí DELETE a staré popupy. |
| Terminál a správce souborů | Směrování příkazů, změněný kanál, staré callbacky, otevření/uložení editoru, BMP, zánik stromu, odložená kopie a pozdní potvrzení mazání. |
| Samostatné modely | C++11, aktivní aserce, `-Wall -Wextra -Werror`, mimo jiné závod worker claim/cancel. |
| Hodiny a GPS | Kalendář/leap year, UTC převod a RTC floor; skutečné preference, limity posunu, mirror draft, staré pickery, DELETE a reentrantní náhrada. GPS: start v čase 0, wrap, sdílený čas akvizice, bounded text, rostoucí layout, capability a životnost dropdownu. |
| Zvuk a upozornění | Všech 48×48 DND oken přes každý denní minutový slot, nulové/neplatné hodiny, priority/mute, hlasitost a schopnosti desek; blikání přes wrap, neúspěšný I/O a reentrantní vypnutí. LVGL: dynamický formulář, stará menu, náhrada při čtení/změně/alertu/zavření, DELETE a zánik vlastníka. WAV parser a potvrzení zachyceného cíle po změně aktuálního slotu. |
| Vzhled a zamčení | Hex parser a palety, neúplný draft, selhání save, skutečné preference, reentrantní read/sync/save/close/apply/attach, DELETE a zánik vlastníka. Změny tapety po zavření, lock capabilities a swatche ve fokusu; opakované install tématu a výsledná barva přepínače po standardní animaci LVGL. |
| Obecné nastavení | Všechna mapování historie/fallback, neplatné indexy, priority stavů SD, skutečné preference a odmítnuté capabilities. LVGL: Cancel/potvrzení, starý formulář, nový výběr během zavírání potvrzení, reentrantní akce/čtení, DELETE a zánik vlastníka. Recovery/reboot jsou zachycené Host akce. Desktop neumí bootovací zápis SD (`SimPrefs::writeFileBool` vrací false): test výslovně ověřuje předání chyby při již změněné běžné preferenci; neprokazuje úspěšný bootovací zápis. |
| Nastavení displeje | Limity/neplatné a přetékající hodnoty timeoutu, skutečné preference, zachování sekundární volby při vypnutí master přepínače, zamítnutí nepodporovaných akcí, směrování restartu. LVGL: náhrada během čtení, připojení/synchronizace/uložení pole a změny přepínače, dropdown close, DELETE a zánik vlastníka. Preference notifikační LED specifická pro Tanmatsu má v desktop testu řízený backend. |
| Nastavení klávesnice | Výchozí mapy, duplicity napříč taby/směry, cancel/neplatné vstupy, gamma; skutečné preference a registr jazyků, debounce a přetečení času, uložení po zavření bez RELEASE. LVGL: přemapování, legacy režim, reentrantní výměny formuláře, odpojení starých událostí, DELETE a schopnosti desek. |
| Nastavení baterie | Skutečné preference, raw/EMA a fuel-gauge fallback, publish cache, nulové/nízké/maximální vzorky, přetečení času, opožděný tick bez burstu, zrušení/restart/reset a reentrantní ADC. LVGL: staré události, opětovná stavba, náhrada během čtení/změny spánku, DELETE s dalšími pozorovateli a zánik vlastníka. |
| Historie baterie | Starší řádky, nejnovější konec historie, limity 24 h/288, krátký zápis/čtení/readback, každý rename a neúspěšný rollback po restartu; interval přes wrap. LVGL: vlastnictví vzorků, OOM, staré události/potvrzení, DELETE, zachycený backend a reentrantní zavření. |
| `python scripts/test_czech.py` | 1 319 klíčů; pokrytí katalogu, placeholdery a technické termíny v pořádku. |
| Obchod | Závod claim/cancel, OOM, neměnné požadavky, chyba executorů, katalogy a manifesty; skutečné LVGL zavření/znovuotevření, staré callbacky, DELETE a pozdní dokončení instalace. |
| Instalace aplikací a jazyků | Krátké čtení/zápis, všechny kroky rename, rollback, selhání rollbacku se zachováním záloh, nedokončená nová instalace, zachování ostatních jazyků. |
| Klávesnice | Přímé psaní s kurzorem uprostřed, živá synchronizace a programatická změna, password/max-length, změna pole, DELETE klávesnice/pole/mirroru, reentrantní odpojení a zánik vlastníka. |
| Chat a akce zpráv | Dlouhá virtualizovaná historie, první/poslední zpráva, přepnutí vláken se stejným počtem zpráv, přepsání ring slotu, stará URL tlačítka, sloučení refreshů, zavření/DELETE/shutdown; akce menu a pozdní potvrzení retry. |
| Detail zprávy a trasování | Dlouhé příchozí trasy a seznamy opakování, kopírování metadat, staré dialogy, změna konverzace, replay ze snímku, externí DELETE těla i kořene. |
| Navigační skupina | 200 prvků, opakovaná inicializace, duplicity, smazání vybraného i prostředního prvku, pořadí callbacků, clear/reuse a zánik vlastníka před widgety; skutečná skupina a směrová klávesa přes UITask v `--smoke-nav`. |
| Obnova a směrování fokusu | Obnova podle identity přeuspořádaného vlákna, composer, modalita, čekající fokus, DELETE mimo skupinu, náhrada prvku na stejné pozici; čtyři směry, sousední úzký prvek, zarovnání, gear, preferovaný seznam/záhlaví a smazání slideru během preview. |
| Jazykový soubor | Escapy, CRLF, recolor klíče, duplicity a řazení, migrace, nedostupné úložiště, chybějící soubor, krátké čtení, OOM obou alokací, prázdná tabulka, NUL, poslední řádek bez newline a životnost zveřejněných řetězců. |
| Seznam konverzací | Oba vzhledy, badges, no-op refresh, zachování scrollu, změna náhledu bez změny času, znovupoužitý slot, staré řádky po rebindu/zániku vlastníka, DELETE, gear/long-press a simulovaný hardwarový hold/swipe. |
| Akce konverzace | Staré nabídky a potvrzení, změněný pubkey/secret, validní mazání a historie, mute, zánik ovládacího prvku, generace ikon, share/Copy, změněný slot regionu, room login/join a reset path. Skutečný picker přes UITask navíc ověřuje starý glyph/backdrop, náhradní výběr a externí DELETE. |
| Emoji a symboly | Kurzor, diskrétní pohyb i akumulace, staré gridy/tlačítka, DELETE cíle/gridu/rootu, neměnné request ID, smazaná buňka a reentrantní vložení. |
| Rychlé odpovědi | Kopie zobrazeného textu, staré události, změna GPS při výběru, ztráta fixu, zánik cíle/rootu, zrušení a reentrantní náhradní picker. |
| Návrhy zmínek a diakritiky | UTF-8 a kurzor uprostřed textu, zachování suffixu, neměnná volba, změna tokenu/textu/kurzoru, limit zprávy a dlouhá nastavení, navigace, staré řádky a DELETE pole/rootu. |
| Long-press a ALT | Skutečné pořadí vložení z LVGL klávesnice, cyklování a wrap, UTF-8 prefix/suffix, dlouhé pole, změna kurzoru/textu, timeout, DELETE cíle/rootu a reentrantní náhrada pickeru. |
| Výběr a editace textu | Sticky selection a změna stejně dlouhého textu, dvojklik a přetečení hodin, zánik pole během editace, UTF-8, limity a povolené znaky, hesla, zachování suffixu, copy/cut/paste, extrakce klíče, mirror rebind a stará menu. |
| Composer a vazba klávesnice | Nezávislé výšky panelů, změna šířky a klávesnice, UTF-8 počítadlo, kopie odesílaného textu, chyby odeslání, nový draft, změna konverzace, staré stromy, DELETE, reentrantní přestavba a délkové/znakové limity mirroru. |
| Schránka | Plain/recolor text, escapované mřížky, UTF-8 na hranici kapacity, null/prázdný vstup a překrývající se zdroj. |
| Aktualizační úlohy | Skutečné souběžné worker/UI volání, neměnný kanál/verze, odmítnutí duplicit a předčasného čtení, chyby executorů, progress, lifecycle SD, backoff, wrap času a přepnutí stable → beta → stable před dokončením odpovědi. |
| Výběr vydání | Rozsah verzí, staré řádky i potvrzení, Cancel, externí DELETE, nový picker po instalaci a nové potvrzení vytvořené uvnitř zavíracího callbacku. |
| LOS | Geometrie, nulová vzdálenost, chybějící/neplatné výšky, limity URL, skutečný souběh worker/UI, retries; LVGL staré výsledky, tlačítka, backdrop, DELETE a oddělená paměť bodů grafu. |
| Aktualizační panel | Skutečné LVGL: dostupnost slotu/sítě, verze/kanál zachycené za běhu, staré stromy, DELETE, detach, zánik vlastníka, reentrantní přestavba, OOM, wrap časovače a dokončení SD/OTA po zavření About. Reboot a zápis obrazu jsou fake host operace. |
| Wi-Fi scan job | Limity a duplicity SSID, zachování starého snímku během částečného zápisu workeru, publikace výsledku, OOM a 200 skutečných souběhů worker claim/cancel. Skutečné RF vyhledávání se na desktopu neprovádí. |
| Systémová diagnostika | Canaries u nulových/malých/plných bufferů, historie stallů a tagů, všechny stavy kapacity; LVGL refresh/wrap, nezměněný text, růst flex layoutu, DELETE a reentrantní náhrada stránky. |
| SD usage | Dvě vlákna, souvislá busy fáze, nezávislý publikovaný snímek, invalidace z jiného vlákna, odmítnutí zastaralého výsledku/scanu a 30s interval přes wrap. |
| PlatformIO `LilyGo_TDeck_companion_radio_touch` | Úspěšný překlad a link; konečný log a velikosti sestavení jsou uvedeny níže. |
| `git diff --check` | Bez chyb whitespace ve sledovaných změnách; nové moduly zkontrolované zvlášť. |

RF, Wi-Fi/BLE, skutečné I2S, SD/FAT, OTA, spotřeba a výpadky napájení se
simulátorem neověřovaly. Lua runtime/audio a MQTT mají v desktop cíli omezené nebo
vypnuté větve; u nich tento běh dokládá především překlad firmwaru. Samotná
obrazovka obchodu a její služby už mají desktop test s lokálním síťovým backendem.
LOS používá v testech řízený backend; skutečný HTTP elevation server nebyl volán. Ostatní desky
nebyly překládány. Existující warningy v hlavičkách desky, LVGL a RadioLib zůstávají.
Srovnávací benchmark rychlosti ani špičkové spotřeby heapu/PSRAM nebyl proveden.

Mezilehlé ověřené sestavení pouze se zálohami hlásí **117 376 B statické RAM** z 327 680 B (35,8 %)
a **3 597 581 B flash** z 4 063 232 B (88,5 %): proti obecnému nastavení +128 B RAM
a +5 968 B flash. Nejde o měření běhového heapu ani zrychlení. Dva nezávislé
katalogy cachují dohromady nejvýše 9 984 B cest/popisků místo původních 4 992 B
sdílených dat; alokace je lazy v PSRAM s interním fallbackem a uvolňuje se se
zánikem vlastníka. To je vědomá cena za nezávislé seznamy.
Verzovaný artefakt: `out/LilyGo_TDeck_companion_radio_touch-20260921_182110-9782e12-dirty.bin`.
Kompletní obraz pro desktopový flasher: `out/Guard-Mesh-TDeck-20260921_182110-merged.bin`
a stejně pojmenovaný `.json`. Aplikace v obrazu je bajtově shodná se sestavením;
kontrola formátu/oddílů a SHA-256 je v `.sim-cache/luna-backup-package.json`.


**Konečný firmware tohoto kola** (zálohy + akce kontaktu) úspěšně sestaven pro
`LilyGo_TDeck_companion_radio_touch` za 152,56 s. Statická RAM **117 656 B / 327 680 B
(35,9 %)**, flash **3 597 685 B / 4 063 232 B (88,5 %)**. Proti mezilehlým zálohám
jde o +280 B RAM a +104 B flash; proti předchozímu obecnému nastavení +408 B RAM
a +6 072 B flash. Tato čísla nejsou měřením rychlosti ani dynamické paměti.

- Kompletní obraz do Guard-Mesh-Flasheru:
  [`Guard-Mesh-TDeck-20260921_190230-merged.bin`](../out/Guard-Mesh-TDeck-20260921_190230-merged.bin)
  a [metadata JSON](../out/Guard-Mesh-TDeck-20260921_190230-merged.json).
  Celý obraz se zapisuje od **0x0**, aplikace je uvnitř od 0x10000.
- Samostatná aplikace:
  [`LilyGo_TDeck_companion_radio_touch-20260921_190230-9782e12-dirty.bin`](../out/LilyGo_TDeck_companion_radio_touch-20260921_190230-9782e12-dirty.bin).
- Merged velikost **3 663 632 B**, SHA-256
  `74a808395e07b980e90ed75912f7c9857ba774a22d0f61af84450a13e9686643`.
- Aplikace **3 598 096 B**, SHA-256
  `78dc7bd64332ec28415ca19198181185f9834e72df9a655407a08d97077d64d7`.

Validátor desktopového flasheru ověřil formát, 16MiB T-Deck rozložení oddílů a
metadata. Aplikační část merged obrazu je bajtově shodná s novým výstupem buildu.
Jde o lokální vydání z rozpracované kopie (`9782e12-dirty`), nikoli o publikovaný
GitHub release. Existující instalátor flasheru se neměnil; tento nový `.bin` lze
přidat do jeho knihovny, `.json` ponechat vedle něj. Fyzické flashování neproběhlo.

Dva meziběhy (synchronizace a první integrace aktualizačního panelu) překročily
90s limit scénáře. Cílené samostatné průchody a konečné EN/CZ/klávesové scénáře
prošly bez navýšení limitu; příčina občasného timeoutu není potvrzena.

V předchozí iteraci obecného nastavení první test nesprávně očekával úspěšný
bootovací zápis SD v desktop backendu, který tuto operaci nepodporuje. Opravený
test ověřuje předání chyby a skutečnou změnu běžné preference; produkční kód
kvůli tomu měněn nebyl. Další společný běh přeložil UI a dokončil modely i EN,
ale překročil 90s limit CZ scénáře. Samostatné opakování CZ prošlo se stejným
binárním souborem a stejným limitem; následně prošla také klávesová navigace.
Timeout tedy zůstává zaznamenaným omezením
spolehlivosti testovacího běhu, nikoliv důkazem odstraněné příčiny.

V celku záloh první firmware překlad odhalil GNU++11 nekompatibilní inicializaci
Host struktur a chybějící capture callbacku; obě chyby byly opraveny. První UI
test hledal celý dlouhý popisek, který LVGL LONG_DOT při layoutu zkracuje.
Testové popisky byly zkráceny a selektory nejprve vyhodnocují layout; aserce
celých předaných cest zůstaly. Následná úplná sada prošla bez navýšení timeoutu.

Lokální důkazy (ignorovaná cache):

- `.sim-cache/luna-contact-final-tests.log` — konečný úplný průchod modely/službami a EN/CZ/navigací
- `.sim-cache/luna-contact-firmware.log` — úspěšné konečné sestavení T-Decku
- `.sim-cache/luna-contact-package.json` — ověřené velikosti, offsety a SHA-256 výsledného obrazu
- `.sim-cache/luna-contact-review.diff` — kontrolovaný diff tohoto celku proti dokončeným zálohám
- `.sim-cache/luna-backup-final-tests.log` — kompletní úspěšné modely/služby a EN/CZ/navigace
- `.sim-cache/luna-backup-firmware-final.log` — T-Deck firmware po opravě překladových chyb
- `.sim-cache/luna-backup-test-debug.log` — cílený úspěšný EN běh po diagnostice LONG_DOT
- `.sim-cache/luna-backup-tests.log` a `luna-backup-firmware.log` — první neúspěšné integrační pokusy, ponechané pro dohledatelnost
- `.sim-cache/luna-backup-package.json` — velikosti a kontrolní součty app/merged obrazu
- `.sim-cache/general-settings-firmware.log` — produkční sestavení odděleného obecného nastavení
- `.sim-cache/general-settings-tests.log` — první běh odhalil chybný předpoklad testu o podpoře bootovacího zápisu SD v desktop backendu
- `.sim-cache/general-settings-final-tests.log` — úspěšný překlad/modely/EN, následný timeout CZ; není to úspěšný průchod celé sady
- `.sim-cache/general-settings-ui-checks.log` — samostatné navazující UI průchody stejným binárním souborem a 90s limitem
- `.sim-cache/appearance-verified-tests.log` — konečný úspěšný průchod modely a EN/CZ/navigačním UI včetně vzhledu a zamčení
- `.sim-cache/appearance-firmware.log` — produkční sestavení pickerů, nastavení zamčení a tématu
- `.sim-cache/display-settings-final-firmware.log` — konečné produkční sestavení klávesnice a displeje pro T-Deck
- `.sim-cache/display-settings-final-tests.log` — konečné modely a EN/CZ/navigační scénáře po oddělení displeje
- `.sim-cache/keyboard-settings-tests.log` — modely a všechny EN/CZ/navigační scénáře s oddělenou klávesnicí
- `.sim-cache/keyboard-settings-firmware.log` — úspěšné produkční sestavení oddělené klávesnice
- `.sim-cache/sound-settings-final-tests.log` — konečné modely/služby a EN/CZ/klávesové scénáře s nastavením zvuku
- `.sim-cache/sound-settings-layout.log` — diagnostika kontrol před dokončením LVGL layoutu
- `.sim-cache/sound-settings-firmware.log` — oddělené nastavení a pravidla zvuku v T-Decku
- `.sim-cache/battery-settings-firmware.log` — sestavení neblokující kalibrace a odděleného formuláře pro T-Deck
- `.sim-cache/battery-settings-tests.log` — úplný průchod modely/službami a EN/CZ/klávesovým UI včetně neblokující kalibrace
- `.sim-cache/battery-nav-final-checks.log` — tři další úspěšné klávesové průchody po opravě DELETE
- `.sim-cache/battery-history-lifetime-tests.log` — předchozí modely/služby a EN/CZ/klávesové scénáře s historií baterie a opravou DELETE
- `.sim-cache/battery-history-final-firmware.log` — konečné sestavení historie baterie a opravy životnosti
- `.sim-cache/battery-nav-checks.log` — zachycený zásobník původního pádu v navigaci před opravou
- `.sim-cache/crash-probe.log` — ověření nativní diagnostiky řízenou výjimkou
- `.sim-cache/battery-history-diagnostic-tests.log` — meziběh před opravou DELETE; sám neprokazuje odstranění občasného pádu
- `.sim-cache/battery-history-firmware.log` — první překlad oddělené historie
- `.sim-cache/gps-settings-final-tests.log` — předchozí modely/služby a EN/CZ/klávesové scénáře včetně hodin a GPS
- `.sim-cache/gps-settings-firmware.log` — sestavení odděleného GPS modelu, služby a formuláře
- `.sim-cache/gps-settings-tests.log` — první úplné ověření GPS
- `.sim-cache/clock-settings-final-tests.log` — celé UI a modely po oddělení hodin
- `.sim-cache/clock-settings-firmware.log` — sestavení s odděleným nastavením hodin
- `.sim-cache/wifi-scan-tests.log` — předchozí modely/služby a EN/CZ/klávesové scénáře včetně Wi-Fi jobu
- `.sim-cache/wifi-scan-firmware.log` — sestavení odděleného Wi-Fi scanu
- `.sim-cache/update-panel-final-tests.log` — EN/CZ/klávesové scénáře včetně panelu a restartů historie
- `.sim-cache/update-panel-firmware.log` — sestavení panelu, platformní kontroly OTA a prázdné historie
- `.sim-cache/message-boundary-final-tests.log` — první úplné ověření modelového rozhraní a migrace indexu
- `.sim-cache/message-boundary-final-firmware.log` — sestavení bez friend vazeb
- `.sim-cache/update-panel-debug.log` — samostatný úspěšný běh panelu po prvním timeoutu
- `.sim-cache/diagnostics-final-tests.log` — aktuální modely/služby a EN/CZ/klávesové scénáře včetně systémových informací
- `.sim-cache/diagnostics-final-firmware.log` — konečné sestavení diagnostiky a SD usage
- `.sim-cache/diagnostics-tests.log` — první integrace diagnostiky
- `.sim-cache/diagnostics-firmware.log` — první překlad diagnostiky
- `.sim-cache/sightline-tests.log` — předchozí modely/služby a EN/CZ/klávesové scénáře včetně LOS
- `.sim-cache/sightline-firmware.log` — sestavení s ESP32 transportem LOS
- `.sim-cache/releases-captured-tests.log` — předchozí EN/CZ/klávesový simulátor, aktualizační úlohy, picker a hodnotově zachycené potvrzení
- `.sim-cache/releases-captured-firmware.log` — předchozí sestavení T-Decku
- `.sim-cache/firmware-jobs-tests.log` — první integrace aktualizačních úloh a monitoru
- `.sim-cache/firmware-jobs-firmware.log` — první překlad odděleného transportu HTTP/OTA/SD
- `.sim-cache/composer-final-tests.log` — composer, schránka a transakce vazby klávesnice
- `.sim-cache/composer-final-firmware.log` — předchozí integrace composeru v T-Decku
- `.sim-cache/chat-composer-tests.log` — první integrace composeru
- `.sim-cache/text-editing-final-tests.log` — cykly diakritiky, výběr a editační menu včetně omezených polí
- `.sim-cache/text-editing-final-firmware.log` — předchozí integrace editace v T-Decku
- `.sim-cache/accent-cycle-tests.log` — první integrace cyklů diakritiky
- `.sim-cache/text-selection-tests.log` — první integrace vlastníka výběru textu
- `.sim-cache/suggestion-pickers-tests.log` — integrace zmínek a diakritiky
- `.sim-cache/text-pickers-tests.log` — integrace emoji/symbolů a rychlých odpovědí
- `.sim-cache/thread-menu-tests.log` — akce konverzací a produkční picker
- `.sim-cache/suggestion-pickers-firmware.log` — integrace zmínek a diakritiky v T-Decku
- `.sim-cache/text-pickers-firmware.log` — integrace emoji/symbolů a rychlých odpovědí
- `.sim-cache/thread-menu-firmware.log` — integrace akcí konverzací
- `.sim-cache/test-artifacts/smoke.log`, `cs/smoke.log`, `keyboard-nav/smoke.log` — výstupy posledních úspěšných UI scénářů
- `.sim-cache/thread-list-tests.log` — integrace jazykové služby a seznamu konverzací
- `.sim-cache/thread-list-firmware.log` — integrace seznamu konverzací v T-Decku
- `.sim-cache/language-file-tests.log` — integrace jazykové služby a chyby čtení/alokace
- `.sim-cache/spatial-navigation-tests.log` — EN/CZ/klávesový simulátor, modely/služby a navigační regrese
- `.sim-cache/navigation-core-firmware.log` — integrace vyčleněného jádra navigace v T-Decku
- `.sim-cache/focus-navigation-tests.log` — obnova fokusu a životnost uložených odkazů
- `.sim-cache/focus-targets-tests-final.log` — EN/CZ simulátor, modely/služby a životnost navigační skupiny
- `.sim-cache/focus-nav-integration.log` — samostatný běh s aktivní klávesovou navigací
- `.sim-cache/focus-targets-firmware.log` — integrace vlastníka navigační skupiny v T-Decku
- `.sim-cache/message-info-tests.log` — celý simulátor, včetně detailu zprávy a trasování
- `.sim-cache/message-info-firmware.log` — integrace detailu zprávy
- `.sim-cache/chat-modules-tests-final.log` — celý simulátor včetně časové osy, menu a retry
- `.sim-cache/chat-modules-firmware-final.log` — integrace časové osy, menu a retry
- `.sim-cache/keyboard-binding-tests-final.log` — celý simulátor a životnost vazby klávesnice
- `.sim-cache/keyboard-binding-firmware.log` — integrace vazby klávesnice
- `.sim-cache/store-final-tests.log` — celý simulátor včetně obchodu a instalací
- `.sim-cache/store-final-firmware.log` — integrace obchodu v T-Decku
- `.sim-cache/inventory-tests-final.log` — nové testy inventáře a celý simulátor
- `.sim-cache/inventory-firmware-final.log` — integrace inventáře v T-Decku
- `.sim-cache/final-refactor-tests.log`
- `.sim-cache/final-refactor-firmware.log`
- `.sim-cache/test-artifacts/` a `cs/`
- `.sim-cache/refactor-baseline/` — původní rozpracovaná kopie
- `.sim-cache/refactor-full-baseline/` — stav před pokračováním
- `out/LilyGo_TDeck_companion_radio_touch.bin` — sestavený firmware

## Zbývající refaktor a proč

Níže jsou konkrétní dosud neoddělené odpovědnosti podle aktuálního zdrojového
kódu. Názvy funkcí slouží jako dohledatelné vstupní body v `src/ui-touch/UITask.cpp`.
Priority určují doporučené pořadí při případném obnovení práce, nikoliv seznam
potvrzených provozních chyb. P1 znamená silnou vazbu na životnost nebo souběh,
P2 další oddělení funkcí a P3 závěrečné omezení závislostí a úklid.

| Priorita / oblast | Co konkrétně zbývá | Proč a jak poznat dokončení |
| --- | --- | --- |
| P1 — vstupy, fokus a přechody | `navPump`, `handleHwKey`, směrování fyzické/BLE klávesnice a trackballu. `FocusNavigation`, výběr aktivního kontextu i politika návratu `BackNavigation` už mají vlastníky. | Zbývají board adaptéry a širší dispatch událostí; další rozdělení má převzít konkrétní stav, ne znovu zavádět kopii pravidel Back. Ověřovat zavření/přestavbu modalu během vstupu a specifika desek. |
| SD — společné přijetí a obnova odděleny, zbývá hardware | `StorageAccess`, `StorageMaintenance`, `SdHealthMonitor` a `SdRestoreJob` vlastní rezervaci, monitor a pořadí obnovy. Zůstávají `fmSdTryMount`, `fmSdUnmount`, fyzický SPI/rail restart a adaptéry filesystemů. | Souběžný vstup workerů už chrání společná rezervace, nikoliv pouhé busy. Přesun zbývajících hardwarových adaptérů má smysl při úpravě podpory desek. Simulátor neprokazuje odolnost FAT proti výpadku napájení ani reálné chování sběrnice. |
| P1 — síť a životní cyklus workerů | Executor, fronta, deduplikace, HTTP/cache transport a odložení nové práce při rezervaci SD jsou oddělené. Zbývají všechny sdílené mapové proměnné a společné ukončení práce. | Executor má životnost procesu, bez stop/join. Před skutečným opakovaným start/stop aplikace je nutné definovat rušení a dokončení všech jobů; samotné přidání další aplikace takovou změnu nevyžaduje. |
| Zálohy — dokončeno v tomto kole | `BackupOperations` odděluje provozní backend; v koordinátoru zůstávají adaptéry filesystemů konkrétních desek a UI feedback. | Ověřeno včetně krátkého zápisu/čtení a reset guardů. Core parser a fyzická odolnost importu/resetu nejsou přepsané; další UI extrakce tohoto backendu není nutná. |
| P1 — admin: korelace odpovědí | `AdminSessionScreen` vlastní dialogy i relaci; `s_room_join_idx` je odstraněn. Zůstává omezení core callbacků bez request ID a oddělené formuláře kanálů. | Stará a nová odpověď témuž kontaktu nejdou spolehlivě rozlišit čistě generační kontrolou UI. To vyžaduje samostatný návrh mesh/core korelace. Globální UI stav admin dialogů už není zbývající dluh. |
| Wi-Fi/Bluetooth formuláře — dokončeno v tomto kole | `WifiFormsScreen` a `BluetoothSettingsScreen` vlastní aktivní formuláře, kopie cílů, callbacky a teardown. Nevolaná Wi-Fi větev je odstraněná; zůstává platformní politika přepínání rádií. | Plné regresní ověření prošlo. Rozhraní rádií stále nemá jednotné request ID ani atomický snímek všech BLE getterů. |
| P2 — Home, senzory a hlavní ovládací prvky | Home globály/grafy a jejich historie, `makeSensorsTab`, `openControlCenter`, app drawer, `buildGlobalStatusBar` a refresh. | Sběr dat, layout, časovače a hardware akce zůstávají propojené. Vyčlenit snímky dat a obrazovky s vlastním stavem/časovači; ověřit opakované otevření, resize a změny dat bez přístupu do zaniklého stromu. |
| Zamčení — politika oddělena, zbývají adaptéry a glance view | `LockScreen` vlastní overlay, tapetu/cache a odpočet; `ScreenPolicy` stav a časování obrazovky. `TouchSleep` vlastní úsporné čekání CPU. V `UITask` zůstávají hardwarová gesta, pořadí efektů a sestavení glance overlay. | Další přesun není podmínkou bezpečného přidávání funkcí. Samostatné vlastnictví glance widgetů a platformní dekódování tapety lze dokončit při jejich úpravě; odlišné fyzické panely vyžadují ověření na příslušné desce. |
| P2 — telemetrie a zpracování odpovědí | Plánovač `TelemetryPolling` už je oddělený. Zůstávají detail/grafy telemetrie, ruční požadavek, `onTelemetryReply` a korelace admin odpovědí. | Dekódování a stav ručního požadavku se stále prolínají s aktuálními widgety. Služba má publikovat hodnotový výsledek pro zachycenou relaci/příjemce; testovat opožděné odpovědi, timeout a novou relaci. Opětovné načtení konfigurace této služby při výměně karty je navazující integrace s novým monitorem SD. |
| Chat a příjem — hranice odděleny, zbývají efekty a datový návrh | `ChatSession` vlastní výběr, identitu a odeslání; `MessageIngress` přípravu, filtry a metadata. `UITask` aplikuje upozornění/Lua/glance/console a připojuje výsledek k UI. Zůstávají callbacky objevených kontaktů a historie indexovaná jménem. | Další přesun efektů má smysl při jejich změně. Samostatné historie různých klíčů se stejným jménem vyžadují návrh persistentní identity a migrace dat; tento problém není vyřešen pouhým odmítnutím přepsání připnutého klíče. |
| P2 — setup a zbývající menší obrazovky | Objevené kontakty, regiony a `openChannelScopeModal`. Průvodce a správce blokovaných uživatelů už mají vlastníky. | Globální rozepsané hodnoty a callbacky stále přežívají změny stránek. Každý flow má vlastnit draft, navigaci a odkazy; ověřit Back/Cancel/reopen a potvrzení po změně cíle. |
| P2 — About, crash export a screenshoty | Zbývající About shell, `crashDumpExport`, `crashReportMaybePrompt`, screenshot a diagnostický export. `SystemInfoScreen`, diagnostika a firmware panel už jsou oddělené. | Sběr dat, souborový výstup a dialogy jsou stále v koordinátoru. Oddělit exportní službu a vlastnictví výběru/cesty, ověřit neúplný zápis a zavření během dokončení. |
| P2 — sestavení a životní cyklus aplikace | `buildUiTree`, `UITask::begin`, `UITask::loop`, `UITask::shutdown`. | Koordinátor má nadále vytvářet a propojovat moduly, ale zbývající implementace funkcí a implicitní pořadí globálů mu nepatří. Po předchozích extrakcích zavést explicitní start/tick/stop a pořadí závislostí; ověřit opakovaný start/stop a zastavení workerů před zánikem jejich dat. |
| P3 — závislosti a zbylá kostra | `platform/UiDevice.h` stále zahrnuje 10 `.cpp`: LuaIntegration, LuaMapView, AudioService, HistoryService, ImageCodec a AutoAdd/Experimental/MQTT/Profile/Radio settings. Zůstávají i osiřelé komentáře a prázdné podmíněné bloky po extrakcích. | Široký include skrývá vazby a zvětšuje rozsah překladů/testů. Nahradit přesnými includes a úzkými Host rozhraními, odstranit prokazatelně mrtvou kostru. `MapScreen` a `FileManagerScreen` zatím vědomě zůstávají jednou instancí na zařízení; víceinstančnost není podmínkou dokončení. |

Napříč zbylými dialogy ještě chybí jednotný audit vlastnictví LVGL odkazů,
časovačů, bufferů a zachycených cílů potvrzení. Testy nových modulů tento audit
nenahrazují pro staré části. Extrakce má vždy přesunout i stav a životní cyklus;
samotné přesunutí funkcí do jiného `.cpp` nebude považováno za splacení dluhu.

### Zbývající ověření a omezení

- Doplnit sestavení odlišných desek (zejména e-paper, Tanmatsu, Pager a M9),
  až se bude pokračovat v jejich podmíněných větvích. Nyní je doložen T-Deck
  a desktop, nikoliv celá produktová matice.
- Pokud má být cílem i měřitelná optimalizace, pořídit srovnávací baseline
  odezvy UI a špičkové spotřeby heapu/PSRAM. Úbytek řádků a statické velikosti
  sestavení nejsou dokladem zrychlení.
- Arduino remove/rename a kopie adresářového stromu dosud nejsou transakce
  odolné proti výpadku napájení; neúplná kopie může nechat část cíle.
  Případné doplnění journalu/obnovy je samostatné rozhodnutí nad požadovanou
  odolností úložiště, nelze je vydávat za hotové díky modularizaci.
- RF, reálné Wi-Fi/BLE, SD/FAT, bootovací zápis SD, OTA, I2S a napájecí přechody
  vyžadují pozdější integrační ověření na zařízení. Tato iterace po uživateli
  nevyžaduje flashování ani ruční přenášení testů na rádio.

### Bod pro rozhodnutí o pokračování

Předchozí předání skončilo oddělením obecného nastavení. Zbývající dluh je rozsáhlý,
ale nebyl nalezen technický blocker další modularizace. Rozpracování dalšího
řádku backlogu tehdy nebylo součástí dokončení: uživatel požádal práci pozastavit.
Následné výslovné rozhodnutí obnovilo ohraničené kolo s Luna Max a limitem využití,
uvedené výše, a poté kolo s GPT-6 Sol. To je nyní uzavřené po dokončení
příjmu zpráv, úplném ověření a vydání lokálního firmwaru při 22 % týdenního
využití. Další plošné pokračování není potřebné pro navazující vývoj aplikací.
Samotný backlog není pokynem pokračovat po zastavení.

### Dohoda o dalším vývoji — 21. 9. 2026

Uživatel schválil pokračovat v přidávání aplikací a úpravách současného stavu
ještě před dokončením celého refaktoru. Zbývající technický dluh budeme splácet
postupně; plošný refaktor vyžaduje samostatné rozhodnutí o dalším kole (aktuální
kolo bylo následně schváleno výše). Dokončení celého backlogu není podmínkou vývoje nových funkcí.

- Nové aplikace a funkce implementovat v samostatných modulech přes existující
  rozhraní. Do `UITask.cpp` přidávat jen nezbytné propojení, nikoliv další
  implementace obrazovek, služeb a jejich stavu.
- Při změně starší části splatit dluh potřebný pro danou změnu. Rozsah držet
  u zadané funkce; automaticky tím neobnovovat celý refaktor.
- U zásahů do SD, workerů, směrování vstupů a uspávání posoudit souběh,
  životnost a pořadí operací. Pokud změna vyžaduje nejprve oddělení související
  části, zahrnout toto oddělení do práce a přiměřeně je ověřit.
- Změny ověřovat relevantními testy sdíleného UI v simulátoru a překladem
  dotčeného firmwaru. Specifické chování hardwaru, které simulátor nepokrývá,
  výslovně zaznamenat k pozdějšímu ověření na zařízení.
- Po dokončení změny aktualizovat tento backlog: uvést splacený dluh i nově
  zjištěné zbývající vazby a důvod jejich odložení.

### Navazující vývoj: Guard-Mesh-Flasher

Nový desktopový flasher je samostatná Python aplikace v `desktop-flasher/`.
Kontrola obrazů, knihovna, skládání instalačních obrazů, flashovací proces a Tk
okno mají oddělené moduly. Nepřidává kód do `UITask.cpp` a neobnovuje pozastavený
plošný refaktor. Původní webový flasher zůstává samostatně použitelný.
Přenositelné zdroje umožňují navázat na Linuxu/macOS; jejich nativní balíčky
a skutečný USB průchod jsou další ověřovací práce, nikoli splacený dluh firmwaru.

Nesouvisející rozpracované změny zůstaly zachované. Nevznikl commit, push ani PR;
rádio nebylo flashováno. Rozhraní a pravidla popisuje [UI-REFACTOR.md](UI-REFACTOR.md).
