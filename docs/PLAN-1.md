# Plán 1

Rozvojový plán GUARD-MESH. Jednotlivé části popisují dohodnuté chování a požadavky
pro implementaci a její ověření.

## Cache názvů slyšených nodů pro Discover

Zapsáno 2026-10-09. Implementováno pro vydání `guardian-2026.10.10`.
Výsledné limity: 1 024 záznamů s PSRAM, 128 bez PSRAM, při nedostatku
paměti menší kapacita. Načítání a zápis běží na pracovní úloze; index je
v RAM. Dva snapshoty s verzí a CRC chrání před přerušeným zápisem.

Discover má zobrazovat známý název nodu i tehdy, když node není v kontaktech.
Název se zařízení naučí z přijatého platného advertu a uchová jej přes restart,
dokud záznam nevytlačí kapacitní limit nebo uživatel cache nevymaže.

### Chování

- Cache uchovává celý veřejný klíč, poslední známý název a údaj potřebný pro
  pořadí posledního slyšení. Nový advert stejného nodu aktualizuje existující
  záznam; změna názvu se promítne do dalšího zobrazení.
- Překlad v Discover má pořadí: název z kontaktů, název z cache advertů,
  současné náhradní označení s částí klíče.
- Cache se aktualizuje z platných advertů nezávisle na přidání do kontaktů
  a před uplatněním filtrů seznamu Found. Samotná odpověď na discover,
  která název neobsahuje, název do cache nepřidá.
- Cache slouží k překladu identity. Aktuální slyšitelnost a signál nadále
  pocházejí z odpovědí na discover.

### Found a správa v Settings

Found a cache názvů mají samostatný životní cyklus. Ruční mazání Found,
jeho filtry i automatické promazávání zachovají současné chování a nesmažou
cache názvů. Cache nebude obnovovat smazané položky do Found ani přidávat
nody do kontaktů.

V Settings bude samostatná akce „Vymazat cache názvů“ a údaj o zaplnění,
například „327 / 1 024 nodů“. Vymazání odstraní cache z paměti i úložiště;
rozpracovaný zápis nesmí později obnovit staré záznamy. Kontakty a Found
tato akce neovlivní. Názvy mimo kontakty se zařízení znovu naučí až z dalších
přijatých advertů.

### Index a limity prostředků

- Vyhledávání použije hashový index v RAM s ověřením skutečného veřejného
  klíče. Krátký hash ani zobrazená část klíče nejsou samostatnou identitou.
  Pokud odpověď obsahuje pouze prefix klíče, překlad je přípustný jen při
  jednoznačné shodě; nejednoznačný výsledek zůstane bez překladu.
- Kapacita záznamů, indexu a pracovních front bude pevně omezená.
  Výchozí návrh je 1 024 nodů na deskách s PSRAM a menší limit na deskách
  bez PSRAM. Konkrétní limity se určí podle celkové paměti včetně zapisovače.
- Při zaplnění cache vytlačuje nejdéle neslyšené záznamy. Opakované adverty
  stejného nodu nezvyšují počet záznamů. Pořadí musí fungovat i po restartu
  nebo změně hodin.
- Příjem advertů ani vykreslování Discover nesmí procházet celý archiv na
  úložišti, čekat na SD nebo spouštět neomezené zvětšování či přestavbu indexu.
  Běžné hledání má mít přibližně konstantní čas; kolize a zahlcení mají
  ohraničenou práci a předvídatelné chování.

### Ukládání

Cache bude mít vlastní verzované úložiště oddělené od Found a nastavení.
Současný backend nastavení má limit 64 kB na snapshot a zrcadlí data v RAM;
větší cache do něj nelze pouze přidat bez vyřešení těchto omezení.

Změny se ukládají dávkově na pozadí. Stejný název při každém advertu
nevyvolá zápis a údaj o posledním slyšení se trvale aktualizuje řidčeji.
Ukládání musí mít maximální dobu odkladu i při nepřetržitém příjmu.
Index se po restartu sestaví jednou z validovaných záznamů, bez blokování
příjmu na dlouhém načítání. Formát omezí velikost a počet záznamů a umožní
obnovu po přerušeném zápisu.

Chybějící, poškozená nebo nedostupná cache nesmí zastavit mesh ani Discover.
Při nedostatku paměti se použije menší kapacita nebo současné zobrazení klíčů.

### Ověření při implementaci

- Node mimo kontakty se po přijetí advertu zobrazí názvem v Discover
  a překlad zůstane dostupný po restartu.
- Smazání a filtrování Found nezruší překlad a cache Found znovu nenaplní.
- Vymazání cache v Settings odstraní i uložené záznamy a případné čekající
  zápisy; názvy z kontaktů zůstanou dostupné.
- Přejmenování nodu, duplicitní adverty, kolize indexu, shodné prefixy
  a názvy s UTF-8 nevedou k záměně identity nebo poškození zobrazení.
- Při příjmu výrazně více unikátních nodů než je kapacita zůstávají paměť,
  fronty a úložiště omezené, vytlačování funguje a UI i příjem zůstávají
  použitelné. Ověřit také souběh ukládání s discover a trvalý proud advertů.
- Ověřit přerušený zápis, poškozený soubor, nedostupnou SD, nedostatek RAM
  a chování desek bez PSRAM.

## Plynulost historie zpráv

Implementováno pro stejné vydání:

1. Zachování překrývajících se bublin a recyklace řádků při změně okna.
2. Průběžná materializace během scrollování, sloučená do intervalů 16 ms.
3. Aktualizace ACK/echo metadat bez resetu vstupu nebo posunu pohledu.
4. Cache výšek podle identity zprávy a konfigurace zobrazení; opakované
   použití kapacity offsetů, měření nových řádků při appendu, zachování
   kotvy při přetočení historie a využití výšek při opětovném otevření.

Nativní regresní testy a simulátor ověřují tyto scénáře. Plynulost na
fyzickém T-Decku se ještě musí potvrdit při používání nového firmware.
