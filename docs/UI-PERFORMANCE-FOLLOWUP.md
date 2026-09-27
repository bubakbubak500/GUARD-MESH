# Navazující optimalizace UI — 2026-09-27

Rozsah tohoto kola: body 1 a 3, tedy aktualizace seznamu chatů a načítání
instalovaných aplikací. Mapa, český katalog a hlavní README zůstávají beze změny
oproti začátku tohoto kola. Commit zahrnuje také předchozí dosud necommitnutý
refaktor a desktopový flasher; jejich podrobnosti jsou v samostatných reportech.

## Seznam chatů

`ThreadListScreen` porovnává podpis jednotlivých řádků. Při změně obsahu
překreslí pouze obsah změněného řádku a zachová jeho kořen i tlačítko nastavení.
Nezměněné řádky zůstávají nedotčené, při změně pořadí se přesunou existující
objekty a při odstranění konverzace zmizí jen příslušný řádek. Zachovává se
scroll i fokus na řádku nebo tlačítku nastavení, včetně přepnutí kompaktního
zobrazení. Výpočet podpisů nad seznamem nadále probíhá; nejde o úplnou
virtualizaci chatů ani odstranění veškerých alokací uvnitř změněného řádku.

Regrese kontroluje identitu objektů, nepřečtené zprávy, změnu názvu a náhledu,
přesuny, přidání/odebrání, změnu režimu a odmítnutí zastaralých callbacků.

## Instalované aplikace

Otevření nabídky a Store používá cache `AppInventory`; inventuru souborů
provádí existující worker. Před prvním výsledkem jsou dostupné vestavěné
aplikace. Výsledek se publikuje i při zavřeném Store a revize obsahu brání
zbytečnému překreslení nezměněné nabídky. Nedostupný worker nezanechá požadavek
trvale ve frontě. UI snímky a potvrzení odstranění uchovávají ID aplikace,
takže změna pořadí inventáře nepřesměruje staré tlačítko na jinou aplikaci.

Test studeného i opakovaného otevření naměřil **0 volání úložiště z `prepare`**.
Ověřeno je také zveřejnění výsledku, nezměněná revize při stejné inventuře,
opakování po selhání workeru a ochrana odstraněné aplikace před starým výsledkem.
Tato úprava přesouvá inventuru; samotné spuštění, instalace a mazání aplikace
mohou nadále přistupovat k souborům.

## Ověření

- `simulator/run.py --test`: modely/služby, anglické a české UI i klávesová
  navigace prošly; log `.sim-cache/performance2-tests.log`.
- Desktopový flasher: 15 testů; lokální webový flasher: 6 testů.
- Český katalog: kontrola 1322 klíčů, beze změny obsahu.

Simulátor testuje inventář a Store samostatnými regresními scénáři; produkční
zapojení Lua aplikací v `UITask` vyžaduje navíc sestavení T-Deck firmwaru.
Fyzické rádio ani RF komunikace nejsou tímto ověřením pokryté. Dříve pozorované
občasné timeouty simulátoru popisuje `UI-PERFORMANCE-REPORT.md`; aktuální celá
sada prošla, ale příčina předchozích timeoutů není prokázaně odstraněná.

## Firmware a předání

Produkční sestavení `LilyGo_TDeck_companion_radio_touch` prošlo za 65 s ze
zdrojového commitu `6351f96c51ccadfbce3064c97cd292ed5d03122e`.
Statická RAM: 121 872 B / 37,2 %; flash: 3 650 733 B / 89,8 %.
Oproti předchozímu sestavení jde o +1 496 B statické RAM a +1 844 B flash;
nejde o měření celkové spotřeby haldy za běhu.

Lokální soubory v `out/`:

- Kompletní obraz pro flasher, offset `0x0`:
  `Guard-Mesh-TDeck-20260927_141318-performance2-merged.bin` a stejnojmenný JSON.
- Samotná aplikace:
  `LilyGo_TDeck_companion_radio_touch-20260927_141318-6351f96.bin`.
- SHA-256 kompletního obrazu:
  `981f08f0569c7da3b0c33b6ff20f930d9f5fd3e938a966e8b028ed7faaf4966c`.

Ověřena shoda aplikace s výstupem PlatformIO, hash metadat a jednotlivé
součásti obrazu na offsetech `0x0`, `0x8000`, `0xE000`, `0x10000`. Manifest
682 zdrojových/konfiguračních/testových souborů se před sestavením a po něm
nezměnil. Doklady jsou lokálně v `.sim-cache/performance2-20260927-final/`.
Vizuálně byly zkontrolovány také anglické a české snímky seznamu chatů.
Kontrola whitespace prošla s výjimkou čtyř stávajících koncových mezer
v českých překladech textových prefixů; katalog zůstal záměrně nedotčený.

BIN soubory a lokální provozní data nejsou součástí Git commitu. Následný
dokumentační commit přidává tento záznam; firmware odpovídá výše uvedenému
zdrojovému commitu.
