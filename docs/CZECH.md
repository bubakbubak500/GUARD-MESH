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

Krátké ovládací texty mají v `scripts/test_czech.py` limit délky oproti
angličtině: nejvýše 3 znaky nebo 25 % navíc (větší z obou hodnot).
Limit platí pro vybrané kompaktní prvky, nikoli pro vysvětlující odstavce.
Například `Unfav` má překlad „Odebrat“, `Sightline` „Viditelnost“
a `Show on map` „Na mapu“. Význam akce dál upřesňuje její ikona a kontext.

`python simulator/run.py --test` navíc měří české akce kontaktu skutečným
fontem LVGL včetně ikon proti prostoru 99 px a ověřuje překlad i rozměry
48px tlačítek editační nabídky. Cílený přepínač sestaveného simulátoru
`--compact-label-test-cs` ukládá snímky `compact-contact-actions.png`
a `compact-text-actions.png`; měření nenahrazuje kontrolu na fyzickém displeji.
