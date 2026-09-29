# Guardian: vzhled, obnova a cache — 2026-09-29

## Změny na T-Decku

- Modrý i zelený motiv mají tmavší výplně s 70% krytím a slabší obrysy.
  Oranžové tlačítko znamená černý text i všechny části ikon, včetně tužky,
  šipky a symbolu odeslání. Platí i pro vybraný přepínač sítě.
- Živé a uložené trasy odděluje mezera 8 px. Stavové patičky používají krátké
  popisky na jednom řádku; klepnutí otevře původní úplnou zprávu. Na seznamu
  patička nepřekrývá šipky stránkování.
- Neznámé počty/procenta zobrazují ASCII `-`. Dřívější dlouhá pomlčka chyběla
  ve velkém číselném fontu a vykreslila obdélník.
- Celé bloky Přijaté a K odeslání jsou klikatelné, včetně ikon. Otevírají Inbox
  a Outbox. Outbox je fronta čekající na doručení, ne historie Odeslané.
- RAM cache uchovává navštívené stránky seznamů i textů. Při návratu se zobrazí
  okamžitě a aktualizují na pozadí. Složky pošty a živé/uložené kontakty se
  nemíchají. Shodný seznam se zbytečně nepřestavuje, pozdní odpověď nesmaže
  rozepsaný text. Cache přežije odpojení a zavření aplikace, nikoli restart.
  Limit je 12 stránek a 24 KiB započítaných dat/objektů; skutečná alokace má
  navíc režii řetězců, vektorů a alokátoru. Není to kopie schránky ve flash.

## Jak často probíhá obnova

| Co | Interval / spouštěč |
| --- | --- |
| Kontrola stavu přehledu aplikace | 250 ms |
| Otevřený seznam zpráv nebo kontaktů | 5 s od poslední odpovědi, pokud je spojení připravené |
| Vstup na seznam nebo text | okamžité zobrazení cache + požadavek na PC |
| Otevřený text | při otevření/stránkování, návratu spojení nebo ruční obnově |
| PC snapshot podle kontraktu API v2 | 500 ms; změny průběžně, heartbeat nejpozději 5 s |
| Zneplatnění živého stavu | po 15 s bez Status, nebo ihned při odpojení |
| Kontrola obnovení BLE advertising | 1 s |

Podržení **R na 2 s** otevře modal s animací. Nejdřív se načte `status.get`,
pak aktuální seznam/text. Stisk se neopakuje, dokud se R neuvolní. V editoru
zprávy se R dál píše jako běžný znak. Čtení matice probíhá v existující úloze
klávesnice; UI si jen přečte sdílený stav, neprovádí další I2C transakce.

Starší firmware řadiče klávesnice vrací jen znak, nikoli držení/uvolnění.
U něj je náhradní ovládání **krátké R**. Bez změny řadiče nelze spolehlivě
rozlišit dvousekundové držení od krátkého stisku. Zastaralý stav matice se
ignoruje po 250 ms; zamknutí a zhasnutí ruší rozeznávání držení.

Modal lze zrušit tlačítkem nebo horním Zpět. Zrušení zavře překryv, probíhající
RPC se bezpečně dočte. Offline pokus skončí přibližně za 1,2 s, čekání na
odběr CCCD za 8 s. Již běžící RPC používá stávající transportní limity
30 s nečinnosti / 60 s celkem. Ruční obnova nikdy automaticky neposílá zprávu.

## Ztráta dosahu a automatické připojení: zjištění pro PC

Kontrola zdrojů a regresní test skutečných callbacků `GuardianBLEInterface`
potvrdily následující chování rádia:

1. Po odpojení se vynuluje relace a neúplné RPC. NimBLE obnovuje advertising;
   kontrola firmware jej navíc znovu spustí, pokud neběží a není připojen klient.
2. Známý bond lze znovu připojit bez otevření párovacího okna. Po autentizaci
   se obnoví transport; RPC čeká na nový odběr Request CCCD od PC.
3. Po obnovení transportu a čerstvého Status aplikace sama vyžádá aktuální data
   otevřené stránky. Cache zůstává viditelná i během výpadku.

**Připojení zahajuje PC, nikoli T-Deck.** Z toho plyne, že samotná úprava UI
nemůže opravit PC klienta, který po výpadku přestane zkoušet připojení. Kód PC
Guardianu v této práci nebyl kontrolován ani měněn. Následující body jsou
požadavky k ověření/implementaci na PC, nikoli tvrzení o jeho aktuálním kódu:

- Rozlišit ruční Odpojit/Vypnout od neočekávané ztráty linky. Jen při druhé
  automaticky opakovat připojení, např. po 1, 2, 5 a dále 10 s.
- Použít uloženou identitu/bond, při potřebě obnovit scan pro Guardian službu.
  Nevyžadovat nové párování, nemazat bond a nespouštět paralelní connect pokusy.
- Po každém připojení znovu objevit služby a přihlásit Request Notify/CCCD;
  obnovit obsluhu požadavků, Status a Progress. Pouhá existence BLE linky nestačí.
- Staré čtecí/zapisovací úlohy a neúplné fragmenty ukončit s minulou relací.
  Restart sekvencí a transfer ID musí odpovídat kontraktu API v2.
- Zaznamenat důvod odpojení, retry, výsledek autentizace, odběr CCCD a první
  úspěšný heartbeat. To rozliší ztrátu RF dosahu od zaseknutí PC úlohy.
- Neposílat znovu `message.queue` s novým tokenem jen kvůli reconnectu;
  dodržet stávající idempotenci a potvrzování operace.

K ověření na fyzické sestavě zbývá opakovaný odchod z dosahu a návrat,
uspání/probuzení Windows a restart PC Guardianu. Úspěch simulovaných callbacků
nepotvrzuje chování Windows Bluetooth adaptéru ani skutečného PC klienta.

## Ověření

- Native Guardian testy: Status/Progress, RPC, callbacky BLE, restart advertising,
  opětovné autentizované připojení a nový odběr CCCD.
- Native klávesnice: matice R, držení, uvolnění a reset stavu při změně režimu.
- Simulátor EN/CS: oba motivy, černé ikony na oranžové, počty jako navigace,
  cache zpráv/textů/kontaktů, aktualizace na pozadí, offline cache, reconnect,
  hranice 2 s, pouze jedna obnova na stisk, modal a jeho zrušení během RPC.
- Skutečná vstupní cesta UITask: WM_KEYDOWN/WM_CHAR/WM_KEYUP, držení R a dotyk
  tlačítka Zrušit. Ochrana rozepsaného editoru zůstává pokrytá regresí.
- Vizuální kontrola screenshotů českého i anglického prostředí.

Výsledky širšího průchodu a sestavení firmware budou doplněny po dokončení.
