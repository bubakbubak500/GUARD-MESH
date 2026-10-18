# guardian-2026.10.18

Kratší české ovládací texty a menší režie obnovování seznamů. Vydání navazuje
na `guardian-2026.10.12` a zachovává funkce Guardian v3 i opravu zásobníku chatu.

## Čeština a rozměry UI

- Zkrácené akce, stavy a názvy v nastavení, nabídkách kontaktů, mapě a nástrojích.
  Například `Unfav`: „Odebrat“ místo „Odebrat z oblíbených“, `Show on map`:
  „Na mapu“ a `Sightline`: „Viditelnost“. Vysvětlující odstavce se nezkracují
  podle pravidel pro tlačítka.
- Oblíbené se nyní překládají i se znakem hvězdičky před názvem akce.
  Orientace displeje a editační nabídka nově používají překladový katalog.
- Editační tlačítka mají omezenou šířku a podle potřeby použijí menší dostupný
  font. Česká nabídka zobrazuje „Vyjm.“, „Kopír.“, „Vložit“, „Vše“, „Znaky“.
- Regrese hlídá 69 krátkých překladů proti anglické délce, všech 15 variant
  akcí kontaktu měří fontem LVGL včetně ikon proti 99 px a editační nabídku
  ověřuje přímo ve skutečných 48px tlačítkách.

## Bezpečné optimalizace

- Kontakty znovu nealokují nezměněný text ikony při každé obnově řádku.
- Seznam chatů převede aktuální místní datum jednou za obnovu, společně pro
  podpisy řádků i vykreslení. Formát dnešních, starších a loňských zpráv zůstává.
- Výběr zmínek používá stávající společné převody UTF-8; duplicitní funkce jsou pryč.
- Profil a rádio nepřekládají již přeložené placeholdery podruhé. Profil používá
  jednu společnou lokální funkci pro popisky místo její duplicitní definice.

## Instalace

| Zařízení | USB, offset `0x0` | OTA přes Wi-Fi |
| --- | --- | --- |
| T-Deck / T-Deck Plus | `Guard-Mesh-TDeck-guardian-2026.10.18-merged.bin` | `Guard-Mesh-TDeck-guardian-2026.10.18-app-ota.bin` |
| Heltec V4 TFT | `Guard-Mesh-Heltec-V4-TFT-guardian-2026.10.18-merged.bin` | `Guard-Mesh-Heltec-V4-TFT-guardian-2026.10.18-app-ota.bin` |

Pro OTA použijte `app-ota.bin` odpovídající desky. Vydání obsahuje také SHA-256,
manifesty, debug ELF a `validation.json` s přesným zdrojovým commitem.
**Hardware nebyl testován.** Sestavení a simulátor neověřují fyzickou instalaci,
rádio ani vykreslení na skutečném displeji.
