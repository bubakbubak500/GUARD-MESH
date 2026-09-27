# Výkon UI — body 1–3, 27. 9. 2026

Navazující cílené úpravy po uzavřeném kole modularizace. Změny se týkají
synchronizace chatů, navigace a seznamu kontaktů. Mapové značky a načítání
inventáře aplikací zůstávají mimo toto zadání.

## Výsledek a lokální firmware

Body 1–3 jsou implementované. Finální společné testy i sestavení
`LilyGo_TDeck_companion_radio_touch` prošly. Rádio nebylo flashováno.

- [Sloučený BIN pro Guard-Mesh-Flasher](../out/Guard-Mesh-TDeck-20260927_133359-performance-merged.bin)
  a [metadata JSON](../out/Guard-Mesh-TDeck-20260927_133359-performance-merged.json).
- [Samostatná aplikace](../out/LilyGo_TDeck_companion_radio_touch-20260927_133359-9782e12-dirty.bin).
- RAM 120 376 B (36,7 %), flash 3 648 889 B (89,8 %).
  Proti předchozímu sestavení +192 B statické RAM a +3 488 B flash;
  to nezahrnuje dynamické alokace widgetů a cache.
- SHA-256 sloučeného BINu:
  `12cc8811469ddab685a4d90908275e91fa27e57b7a97452e9d814acf514f266a`.

Balíček ověřuje shodu aplikace s výstupem PlatformIO, součásti na offsetech
`0x0`, `0x8000`, `0xE000`, `0x10000` a hash metadat. Kontrolní snímek 682
zdrojových/konfiguračních/testových souborů se mezi testy a sestavením nezměnil.
Doklady jsou v `.sim-cache/performance-20260927-final/`, včetně
`artifact-verification.json`, manifestu, tří smoke logů a protokolu sestavení.

## 1. Synchronizace adresáře chatů

`application/ThreadRefreshPolicy.h` nahrazuje plnou kontrolu každé čtyři sekundy.
Explicitní změny a změna počtu kontaktů ji vyvolají v následujícím průchodu UI;
opakovaná oznámení se sloučí. Změna názvu uloženého kontaktu invaliduje jeho
vazbu, běžný inzerát se změnou stáří nikoliv. Pro starší volající bez oznámení
zůstává pojistný interval 60 sekund.

`refreshThreadsFromMesh()` hlásí skutečnou změnu vazeb/názvů. Nezměněný adresář
neoznačuje seznam chatů k obnovení a odstranění neexistujících prázdných chatů
zbytečně neinvaliduje cache historie. Samostatná minutová kontrola zobrazeného
času zachovává změny popisků při přechodu na další den.

Integrační test skutečného `UITask`: 4,3 s klidu = 0 plných synchronizací;
12 běžných inzerátů = 0; přejmenování = 1; deset oznámení změny = 1;
odstranění kontaktu = 1. Samostatný test pokrývá minutovou pojistku a přetečení
32bitového časovače.

## 2. Navigace

`application/FocusNavigation` uchovává seznam sledovaných objektů. Při
nezměněné struktuře porovnává jejich příznaky bez opakovaného procházení
potomků a výpočtu podpisu stromu. Události vytvoření, smazání a přeuspořádání
vyvolají nový průchod. Změna textu nebo rozměru popisku jej nevyvolává.

LVGL neoznamuje všechny změny příznaků, proto zůstává lineární kontrola
viditelnosti, klikatelnosti a pravidel navigace, včetně dříve skrytých podstromů.
Nejde o kontrolu v konstantním čase. Udržování cache stojí paměť a registrační
práci při změně struktury; při selhání alokace se použije plný průchod.

Regrese ověřuje nezvyšující se čítač plných průchodů při opakovaném volání,
změny příznaků, přidání/smazání/přesun, kontext, focus hint a návraty z modálů.

## 3. Seznam kontaktů

`screens/ContactsScreen` zachovává až 128 lehkých klikacích řádků a stabilní
data callbacků. Ikony, názvy, stáří, vzdálenosti, hvězdy a checkboxy jsou
recyklované podle viditelné části seznamu s rezervou a podporou fokusu.
Obnovení a řazení na stejném seznamu znovu používá existující řádky.

Ve scénáři se 128 kontakty a oknem vysokým 150 px vzniklo **5 sad obsahu**,
tedy 35 widgetů obsahu v režimu výběru; posun na konec zásobník nezvětšil
(4 aktivní sady / 28 widgetů). Dříve bylo možné vytvořit obsah všech 128 řádků.
Tato čísla nezahrnují 128 kotev řádků ani skrytý zásobník. Jeho maximální velikost
se řídí největším navštíveným výřezem, rezervou a fokusovaným řádkem.

Test zahrnuje poslední položku, checkboxy po recyklaci, změnu pořadí a identity
callbacku, živé jméno, změnu velikosti, prázdný seznam, externí vyčištění,
smazání a nahrazení kořene. Načítání aktuálních jmen/stáří se omezuje na obsah
viditelných řádků; plný snímek pro filtrování a řazení se nadále vytváří.
Aktivace předem připravené záložky a změna rozložení znovu synchronizují výřez.
Společný UI scénář kontroluje viditelnost kontaktu po přepnutí záložky; to
doplňuje samostatné testy i vizuální kontrolu anglického a českého seznamu.

## Ověření a omezení

Finální `simulator/run.py --test` prošel: modely/služby, anglické UI, české UI
a klávesová navigace. Protokol: `.sim-cache/performance-20260927-tests.log`.
Provedena také vizuální kontrola anglických a českých kontaktů a `git diff --check`.

Při předchozích opakováních došlo k několika 90s timeoutům integračního scénáře
po testu historie. Kontrolní starší sestavení i přímé opakování stejného původně
selhávajícího nového EXE v samostatném adresáři prošly; příčina timeoutů není
jednoznačně prokázaná. Do testů jsou proto doplněné značky jednotlivých etap.
Ty představují diagnostiku, nikoliv prokázanou opravu timeoutu. Poslední celá
sada prošla bez prodloužení časového limitu. Při případném opakování je potřeba
navázat na poslední vypsanou etapu, ne považovat tento jev za vyřešený.

Čítače dokazují omezení práce a počtu widgetů; nejde o měření FPS, spotřeby ani
latence na skutečném rádiu. Fyzické rádio ani RF komunikace nebyly ověřovány;
produkční sestavení cílí na T-Deck, ostatní desky nebyly v tomto kole sestavené.

## České překlady

Upravovatelný zdroj je `deploy/apps/lang/cs.lang`, formát UTF-8:
anglický klíč, **tabulátor**, český text. Zachovat klíč a formátovací zástupné
symboly (`%s`, `%d`, `%lu` apod.). `src/ui-touch/i18n_builtin.h` je generovaný
výstup a při sestavení se obnovuje ze souborů `.lang`. Tato úprava `cs.lang`
nemění.
