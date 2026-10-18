# guardian-2026.10.18.1

Zrychlení práce se zprávami nad zaplněnou historií a obnovování dlouhých chatů.
Vydání navazuje na `guardian-2026.10.18`; zachovává české UI a funkce Guardian v3.

## Změny

- Při přepsání nejstarší zprávy se počty zpráv vláken upravují průběžně.
  Každý další příjem do zaplněného ringu už nevyžaduje přepočet celé historie.
- Náhled poslední zprávy používá index slotu a sekvenci pro každé vlákno,
  včetně zapamatování prázdného výsledku. ACK a opravy času čtou aktuální záznam;
  cache neuchovává kopii textu. Pořadí zůstává podle příchodu, i když se čas vrátí.
- Kompaktní chat používá omezený přehled změněných zpráv. Nezměněná obnova
  a ACK se stejnou výškou řádku nehashují ani neformátují celou historii.
  Zůstává jednoduchý průchod ringem pro sestavení indexů aktivního chatu.
- Změna zalomení mimo obrazovku přepočítá offsety se zachováním kotvy viewportu.
  Změna času, vzhledu, jména, echo nebo přeplnění přehledu změn používá plnou kontrolu.
  ACK nebo echo přijaté společně s novou zprávou nezanechá staré výšky řádků.
- Přepnutí 12/24 hodin, časového pásma nebo UTC posunu obnoví rozložení;
  reset i vytlačení počtu opakování obnoví jeho zobrazení.
- Kontrola aktivního kontaktu používá uložený index ověřený celým klíčem.
  Běžná obnova tak neprochází celý adresář; změna pořadí kontaktů má bezpečný fallback.
- Formát uložené historie, velikost zprávy, počty zachovaných zpráv a pravidla
  nepřečtených zpráv se nemění. Nové indexy jsou pouze v RAM.

## Výkonnostní a regresní ověření

`python scripts/test_message_performance.py --baseline-ref d0ad0cc` porovnává
stejný scénář s původním kódem. Pro 40 příchodů do ringu 5 000 zpráv a dotazy
na všech 48 vláken:

| Scénář | Operace před změnou | Po změně, zahřáté indexy |
| --- | ---: | ---: |
| Přepočty počtů, ostatní vlákna prázdná | 200 000 návštěv historie | 0 |
| Hledání náhledů, ostatní vlákna prázdná | 9 400 040 návštěv historie | 0 |
| Hledání náhledů při vytlačování starých tichých vláken | 9 383 860 návštěv historie | 0 |

Shodují se kontrolní součty výsledků původního a nového kódu. Čítače jsou
součástí testovacího sestavení; firmware je neobsahuje. Časy desktopového
benchmarku nejsou měřením odezvy ESP32 ani násobkem zrychlení celého UI.

Skutečné LVGL s modelem 4 995 zpráv ověřuje 14 kontrol podpisu při nezměněné
obnově a 15 při ACK, které nemění výšku. ACK měří jeden řádek bez přestavby
layoutu. Test dále pokrývá zalomení mimo obrazovku, kotvu, souběh s příchodem,
echo včetně snížení, změny vzhledu a jména, přeplnění přehledu změn, rollover,
denní oddělovače a nastavení hodin. Samostatný test ověřuje reset a vytlačení
skutečných echo počítadel; test identity kontaktů ověřuje rychlou cestu a přesun kontaktu.
Výsledné výšky se porovnávají s novým nezávislým přeměřením všech řádků.

Modelové regrese ověřují tombstones, prázdný text, prázdné náhledy, přejmenování
a opětovné použití vlákna, import/rotaci, sekvence, ACK/čas i dotazy během
persistenčních callbacků. Callbacky nadále nejsou určeny pro vnořené mutace modelu.

## Instalace

| Zařízení | USB, offset `0x0` | OTA přes Wi-Fi |
| --- | --- | --- |
| T-Deck / T-Deck Plus | `Guard-Mesh-TDeck-guardian-2026.10.18.1-merged.bin` | `Guard-Mesh-TDeck-guardian-2026.10.18.1-app-ota.bin` |
| Heltec V4 TFT | `Guard-Mesh-Heltec-V4-TFT-guardian-2026.10.18.1-merged.bin` | `Guard-Mesh-Heltec-V4-TFT-guardian-2026.10.18.1-app-ota.bin` |

Pro OTA použijte `app-ota.bin` odpovídající desky. Vydání obsahuje SHA-256,
manifesty, debug ELF a `validation.json` s přesným zdrojovým commitem.
**Hardware nebyl testován.** Sestavení a simulátor neověřují fyzickou instalaci,
rádio ani skutečnou odezvu displeje na zařízení.
