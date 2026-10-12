# Guardian 2026.10.12 — záznam ověření

## Diagnostika a review

- CRC32 dodaného ESP coredump souhlasí s uloženou kontrolní hodnotou.
- Identifikátor aplikace odpovídá SHA-256 archivovaného T-Deck ELF vydání
  `guardian-2026.10.10.3`, takže adresy byly porovnány s odpovídajícími symboly.
- Dotčený kód chatu je shodný i v posledním vydání `guardian-2026.10.11`.
- Nezávislé interní code review skutečného diffu `ChatTimeline.cpp`, regresního
  scénáře a release notes skončilo bez nálezů vyžadujících opravu. Kontrola
  zahrnula meze indexů, filtrování widgetů, souběh synchronních LVGL callbacků,
  životnost kontejneru, zarovnání, oddělovače a zachování rozsahu scrollu.
  Nejde o externí GitHub schválení ani o test fyzického zařízení.

## Regrese

`python simulator/run.py --test` prošel: nativní modely a služby, všech sedm
Guardian sad, kompletní anglické a české UI i klávesová navigace.
Nový scénář chatu prošel v obou jazycích. Ověřuje opravu chybného levého/pravého
zarovnání při ACK, polohy po obrácení pořadí LVGL potomků, denní oddělovač
a zachování widgetů, rozsahu historie a polohy scrollu. Existující scénáře
nadále ověřují dlouhou historii, průběžné scrollování, rollover, kompaktní
režim, zavření obrazovky a zánik kontejneru.

Nové kontroly v simulátoru ověřují chování UI; samotnou úsporu zásobníku
ověřuje cílový ESP32-S3 strojový kód. Instrukce `entry` archivovaného ELF
ukazuje 1 904 B pro `chatVirtSyncBubblePositions`. Nový T-Deck ELF ukazuje
352 B vlastní implementace a 32 B vstupní funkce, dohromady 384 B.
Kontrola potvrzuje úsporu 1 520 B a limit nejvýše 512 B pro obě části dohromady.

Místní podklady jsou v `.sim-cache/crash-20261012`: `simulator-tests.log`,
`stack-validation.json`, `tdeck-build.log` a `heltec-build.log`.
Surový dump ani zprávy uživatele nejsou veřejnou součástí vydání.

## Obrazy a instalace

Balíčkování přes `scripts/package_guardian_release.py` ověřuje přesný vestavěný
tag, ESP32-S3 chip ID 9, limit OTA oddílu `0x3e0000`, shodu celé aplikace
v merged obrazu na offsetu `0x10000`, debug ELF a SHA-256 všech podkladů.
`validation.json` zaznamenává přesný zdrojový commit a výsledné velikosti
a kontrolní součty obou desek. Veřejná OTA používá odpovídající `app-ota.bin`
a digest z metadat GitHub assetu.

**Hardware nebyl testován.** USB/OTA instalace, restart a následný provoz na
fyzickém T-Decku nebo Heltecu tímto softwarovým ověřením nejsou potvrzeny.
