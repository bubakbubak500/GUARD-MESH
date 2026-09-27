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
