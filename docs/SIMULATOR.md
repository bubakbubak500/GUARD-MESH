# T-Deck simulátor pro Windows

Spusť `Start-Simulator.cmd`. Potřebuje Windows x64, Python 3.9+ s pip a Git.
První spuštění stáhne připnuté LVGL, ArduinoJson, hlavičky MeshCore a přenosný Zig compiler;
další spuštění používají lokální cache. Visual Studio ani deska nejsou potřeba.
Spouštěč znovu přeloží změněný kód a otevře samostatné okno.

- Myš: dotyk; tažením posouváš seznamy.
- Klávesnice: psaní do polí, Enter odešle zprávu. Zpět je šipka v UI.
- Šipky: pohyb trackballu. F6: jeho stisk, podržení funguje i pro odemknutí.
- F5: příchozí simulovaná zpráva od `SIM Alpha`.
- F12: PNG a texty se souřadnicemi do `.sim-cache/screenshots/`, použitelné pro připomínky.
- F1: nápověda.

Běží **skutečný `src/ui-touch/UITask.cpp`**, LVGL 8.4, původní fonty, obsluha
dotyku/kláves a logika nastavení. Displej má 320 × 240 bodů jako klasický
LilyGo T-Deck. Vyzkoušet lze chaty, kontakty, profil, nastavení, mapovou obrazovku
a nativní nabídky aplikací. Zprávy se odesílají pouze do lokálního modelu;
potvrzení doručení je simulované. Scénář kontaktů a zpráv se při spuštění obnovuje.
Nastavení/profil jsou odděleně v `.sim-cache/state/preferences.txt`.

Jde o nativní hostitelské sestavení UI části firmwaru. ESP32 instrukce, rádiový
protokol, elektrické periferie, reálná SD/flash, Wi-Fi/BLE, OTA a spotřeba se
neemulují. Mapa běží bez stahování dlaždic. Lua runtime, audio a USB Files jsou
v tomto cíli vypnuté. Je určený pro vývoj a proklik UI; ověření těchto funkcí
a časování zůstává na desce. Podpora jiných desek ani Linuxu zatím není součástí.

## Vývoj

`Start-Simulator.cmd --test` sestaví UI, spustí samostatné C++ testy modelů/služeb
a skrytě provede integrační scénář v angličtině, češtině a s aktivní klávesovou navigací: navigace, chat, psaní,
odeslání/ACK, profil, ukládání nastavení a nabídka aplikací. Ověří také opakované
otevření a zánik dialogů, kontaktů a nastavení, opožděné callbacky a identitu
příjemce v rádiovém adaptéru. Snímky jsou v `.sim-cache/test-artifacts/` a
podadresářích `cs/` a `keyboard-nav/`. `--build-only` pouze sestaví program.
Přímo sestavený simulátor přijímá také `--guardian-test` a `--guardian-test-cs`:
krátký průchod BLE V2 modelem, zprávami, kontakty a opakováním nepotvrzeného
odeslání bez fyzického BLE. Koncepty jsou v tomto testu uložené pouze v paměti;
firmware používá atomický NVS blob. Test ukládá snímky do pracovního adresáře.
Další scénáře používají paměťový filesystem pro skutečný segmentový writer,
migraci historie, kopírování/přesun, krátké čtení a zápis. Asynchronní historie
prochází stejným vstupem executorů jako firmware. Testy mapy dekódují skutečné
PNG a ověřují limity bufferů i externí smazání canvasu a popupů. Testy terminálu
a správce souborů ověřují příkazy, editor, BMP, staré callbacky, pozdní potvrzení
mazání a zrušení čekajícího kopírování při zavření stránky. Paměťový filesystem
neemuluje FAT, fyzickou SD ani výpadky napájení; interaktivní simulátor jej
standardně nezapíná.
Obchod má vlastní test skutečné obrazovky: instalace dokončená během zavření,
opožděná tlačítka, opětovné otevření, externí DELETE a selhání spuštění workeru.
Katalogy, inventář a instalace používají sdílené služby. Testy zápisů ověřují
každý krok přejmenování, rollback a zachování záloh při selhání obnovy. Síťový
backend je v těchto testech lokální; neověřují veřejný server ani skutečnou Wi-Fi.
Vazba klávesnice se testuje v přímém i mirror režimu včetně kurzoru, přepnutí
polí a zániku vypůjčených widgetů či vlastníka.
Chatová časová osa se testuje na dlouhé historii nad rozsahem souřadnic LVGL,
se skoky na začátek/konec, změnou vlákna a přepsáním ring slotu. Testy ruší
naplánovanou práci při zavření a DELETE. Nabídka zprávy ověřuje stará tlačítka,
změnu konverzace a pozdní potvrzení opakovaného odeslání.
Detail zprávy ověřuje dlouhé trasy, opakování, kopírování metadat, replay
ze snímku a zánik dialogu i jeho posuvného těla.
Navigační skupina se testuje s 200 prvky, smazáním vybraného prvku, pořadím
DELETE/refocus callbacků a zánikem vlastníka před vypůjčenými widgety.
Obnova fokusu ověřuje modalitu, přeuspořádání vláken, composer, čekající požadavky,
DELETE mimo aktivní skupinu a nahrazení widgetu. Geometrická navigace ověřuje
čtyři směry, sousední úzký prvek, zarovnání, gear, omezení obrazovky a přechod
ze seznamu do záhlaví. Slider má test zániku během preview callbacku.
Seznam konverzací ověřuje kompaktní i rozšířené řádky, scroll, změněný náhled,
staré callbacky, zánik vlastníka a hardwarový hold přes testovací vzorek.
Bootovací jazyková služba používá paměťový FS pro parsování, migraci, čekání na
úložiště, chybějící soubor, krátké čtení, OOM a životnost zveřejněného překladu.
Akce konverzace se testují proti změně identity, starým nabídkám a potvrzením,
nahrazeným pickerům ikon, smazaným ovládacím prvkům a sdílení kanálu. Scénář
navíc otevírá skutečný picker přes produkční Host v UITask a zkouší opožděné
události starého stromu i externí DELETE; původní ikonu po testu obnoví.
Emoji/symboly a rychlé odpovědi mají testy vložení uprostřed textu, neměnných
voleb, GPS při výběru, navigace, externího DELETE a otevření náhradního pickeru
z callbacku. Návrhy zmínek testují UTF-8, token kolem kurzoru, zachování suffixu,
změnu textu/kurzoru před akcí a odpojené staré řádky. Diakritika testuje skutečný
znak před kurzorem, dlouhá nastavení, arming navigace a zánik pole či popupu.
Long-press test používá skutečný default handler LVGL klávesnice a následný handler
cyklu. Ověřuje i timeout, ALT, změnu pole, zánik během zavření a reentrantní výběr.
Výběr textu a editační menu ověřují sticky selection, dvojklik, UTF-8, cut/copy/paste,
zachování suffixu při limitu délky, povolené znaky, hesla, extrakci hex klíče,
změněnou mirror vazbu a zánik či reentrantní úpravu pole během callbacku.
Composer testuje nezávislé výšky obou panelů, resize, počítání UTF-8, chybu
odeslání, kopii textu, nový draft, změnu konverzace a události ze starého stromu.
Vazba klávesnice má scénáře DELETE/detach/rebind uprostřed změny omezeného pole,
minimální změnu při jednom stisku a zachování délkových/znakových pravidel.
Schránka v testech modelu ověřuje UTF-8 ořez, recolor a překrývající se kopii.
Aktualizační úlohy se testují s fake transportem a skutečnými dvěma vlákny:
publikace výsledků, neměnné požadavky, chyby startu workeru, lifecycle SD,
změna kanálu během kontroly, opakování a přetečení časovače. Parser verzí odmítá
přetečení čísla. HTTP/Arduino Update a skutečný SD zápis se tím nesimulují.
LVGL picker starších vydání ověřuje staré nabídky a potvrzení, zrušení, DELETE
a reentrantní otevření nového dialogu při zavírání potvrzené akce.
Panel aktualizací používá stejné LVGL i na desktopu. Testuje obsazený job,
změnu kanálu během instalace, zaniklé/staré stránky, callbacky po detach,
OOM executorů, přetečení intervalu a převzetí dokončení bez otevřeného About.
Reboot je počítaný host callback; simulátor žádný firmware neflashuje.
Wi-Fi scan job testuje publikaci celého seznamu ze skutečného druhého vlákna,
limity/duplicity SSID, OOM a souběh claim/cancel. Platformní scan/reconnect
skutečného rádia není simulován; desktopový limit SSID odpovídá firmwaru.
Hodiny používají skutečné preference a řízený RTC backend. Testy kontrolují
kalendář, dolní hranici RTC, UTC převod, limity posunu, klávesnicový draft,
staré formuláře/pickery, DELETE a přestavbu během callbacku. Windows CRT není
náhradou ověření všech POSIX DST pravidel ESP32.
GPS používá řízený snímek přijímače a skutečné preference. Model testuje start
v čase 0, přetečení a formátování do malých bufferů; LVGL zkouší růst statusu,
baud/soukromí, zařízení bez GPS, uzavření dropdownu, staré události a reentranci.
Příjem NMEA a fyzické zapínání modulu tyto testy neověřují.
Zvuk ověřuje všechna časová okna DND, priority upozornění, master mute,
limity hlasitosti, schopnosti desky a tři bliknutí s retry i přetečením hodin.
LVGL scénář ověřuje formulář a nabídku souborů včetně náhrady/zániku během
callbacků. Kontroly labelů nejprve dokončí layout, protože LONG_DOT před ním
může dočasně měnit jejich text. WAV dialog používá skutečný parser nad
paměťovým filesystemem a musí uložit do slotu zachyceného při otevření.
Fyzická hlasitost, I2S výstup a RGB klávesnice jsou v těchto testech fake callbacky.
Kalibrace baterie používá skutečné preference a řízený ADC/SOC backend.
Ověřuje zachování EMA a publish cache, průměr nenulových vzorků, hranici
kalibrace, přetečení časovače, zpoždění bez doháněcího burstu, zrušení/reset
při reentrantním čtení a životnost skutečného LVGL formuláře. Neověřuje
fyzickou přesnost ADC ani odběr při úsporném režimu.
Historie baterie testuje skutečnou službu nad paměťovým filesystemem: původní
4/5 sloupců, nejnovější vzorky, limity, krátké I/O, readback, rename/rollback
a odmítnutí přepsat neobnovenou zálohu po restartu. Graf ověřuje alokace/OOM,
staré stromy a potvrzení, zachycený backend a přestavbu v callbacku. Hodiny,
GPS a graf navíc kontrolují doručení DELETE dalším pozorovatelům, aby navigaci
nezůstal ukazatel na uvolněný kořen.
Historie navíc ověřuje modelové přejmenování/mazání, různé kapacity importu,
restart po částečné migraci indexu/markeru a prázdný již migrovaný archiv.
LOS testuje nulovou vzdálenost, Fresnelovu zónu, opravu výšek, limity URL a
poledník 180°. Dvě skutečná vlákna ověřují neměnnost požadavku a publikaci výsledku.
Sdílená LVGL obrazovka zkouší staré výsledky, změny antén, stará tlačítka/backdrop,
DELETE a vlastní body každého grafu. HTTP elevation server je nahrazen fake backendem.
Systémové informace ověřují malé buffery s canaries, vlastnictví stall tagů,
SD snímky mezi vlákny a invalidaci během scanu. Skutečné LVGL testuje obnovování
přes wrap, vynechání nezměněného textu, růst flex layoutu, DELETE a náhradu stránky
během čtení. Skutečný ESP32 heap ani FAT kapacita se v desktopu neměří.
Režim `--smoke-nav` navíc ověří skutečně naplněnou skupinu a posun fokusu
klávesou přes vstupní cestu firmwaru; poté projde běžný integrační scénář.
Samostatné modely a služby: `python scripts/test_ui_models.py`.
Dialog odpovědi Ping lze ověřit také samostatně přepínačem `--ping-test`
nebo `--ping-test-cs` sestaveného simulátoru. Testuje skutečný model a modal:
binární/JSON odpověď, správný offset uptime (20, nikoli TX airtime na 16),
chybějící hodnoty, odhad baterie 3,3–4,2 V, zavírání a životnost nahrazeného
dialogu. Ukládá snímky `ping-reply*.png` do pracovního adresáře. Procenta jsou
označená `~`; Ping přenáší napětí, ne kapacitu ani kalibraci vzdáleného uzlu.
Detail překladače: `.sim-cache/build/errors.txt`; běhový log: `.sim-cache/simulator.log`.
Každý automatický UI scénář ukládá stdout/stderr do svého `smoke.log`
v `.sim-cache/test-artifacts/`, `cs/` nebo `keyboard-nav/`; při chybě se zobrazí
také konec tohoto logu.
Nativní Windows výjimky mají v témže logu kód, adresu a zásobník. Funkce a řádky
se dohledávají v PDB vedle konkrétního exe; tento soubor je potřeba zachovat spolu
s exe při diagnostice pádu. Firmware tento desktopový handler nepoužívá.
Nová sestavení mají vlastní exe, takže se nepřepisuje právě spuštěná verze.

Platformní adaptéry jsou v `simulator/include/`, Windows okno v `simulator/main.cpp`.
Sdílené moduly a platformní rozhraní popisuje [UI-REFACTOR.md](UI-REFACTOR.md).
`GUARD_SIMULATOR` volí tyto adaptéry; firmware pro desku používá dosavadní větev.
Všechny stažené knihovny, binárky, testovací snímky a lokální stav jsou ignorované
v `.sim-cache/`. Verze a commity jsou v `simulator/dependencies.json`.

Základ: [LVGL](https://github.com/lvgl/lvgl/tree/v8.4.0), MIT, a hlavičky
[MeshCore fork](https://github.com/ALLFATHER-BV/meshcomod/tree/core-v1.17.4), MIT.
PNG zapisuje LodePNG z LVGL (zlib licence). Zig je pouze sestavovací nástroj
(MIT, distribuované části mají vlastní licence). Licence zůstávají u stažených
zdrojů. Na LVGL se aplikuje stejná oprava animací jako ve firmwaru.
Prověřený starší [MeshCore desktop simulator](https://github.com/thepacket/meshcore-standalone)
používá jiné obrazovky; jeho UI jsme nepřebírali.
