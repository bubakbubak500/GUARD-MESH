# Čeština

Čeština je v Nastavení → Jazyk → Čeština. Je přibalená přímo do firmwaru,
takže funguje i bez internetu a SD karty. Existující volba jazyka se nemění.

Zdroj: `deploy/apps/lang/cs.lang`. Zachováváme názvy a pojmy Advert, ACK,
Flood, Zero-hop, repeater, room, Scope, Ping, Trace a technické zkratky.
Běžné ovládání, popisy a chybová hlášení jsou česky. Texty Lua aplikací
mimo společný katalog mají vlastní lokalizace; tento překlad pokrývá katalog UI.

Po úpravě spusť `python scripts/build/gen-lang-builtin.py` a
`python scripts/test_czech.py`. Test ověřuje pokrytí existujícího katalogu,
formátovací parametry, barevné značky a zachování technických výrazů.
Čeština má poslední index (14), takže nerozbije dříve uložené jazyky.
