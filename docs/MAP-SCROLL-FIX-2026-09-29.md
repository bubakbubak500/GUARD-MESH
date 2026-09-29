# Oprava pádu při posouvání mapy

Vydání: `guardian-2026.09.29.1`, LilyGo T-Deck / T-Deck Plus.
Základ: poslední GitHub release `guardian-2026.09.29`, větev `main`
na commitu `0c2707f` (binární firmware předchozího vydání vznikl z `1d95dee`).

## Diagnostika

Nahlášený soubor `wadamesh-crash.elf` obsahuje hlavičku ESP coredump a vlastní
ELF od offsetu 20. Identifikátor aplikace ve výpisu je `8977dafba52fff16`;
odpovídá archivovanému sestavení z 28. 9. 2026 12:38:57 (`4798eb9`), nikoli
novějšímu firmwaru z 29. 9. Oprava je přesto založena na posledním vydání.

Postižená úloha je `loopTask`. Zachycený ukazatel zásobníku `0x3fcea1c0`
leží těsně nad jeho začátkem `0x3fcea1b8`; záznam nese debug exception
`EXCCAUSE=0x41`. Ve výpisu zásobníku jsou návratové adresy obsluhy uvolnění
dotyku, mapy a překreslování LVGL. Spolu s kontrolou zdrojů to ukazuje na
vyčerpání zásobníku při synchronním vykreslování mapy uvnitř události dotyku.
Souvislost debug exception s ochranou konce zásobníku popisuje
[dokumentace Espressif](https://docs.espressif.com/projects/esp-idf/en/v4.4-beta1/esp32s3/api-guides/fatal-errors.html).

Původní ELF tohoto staršího sestavení není k dispozici. Adresy společného
kódu byly porovnány pomocí shodných bajtů v archivovaných BIN obrazech a
symbolů novějšího ELF; nejde o úplný symbolizovaný backtrace původního buildu.
Místní GDB při pokusu o rozvinutí všech úloh havaroval, jeho nesouhlasící
symbolické výpisy proto nejsou použity jako přesná diagnostika.
Disassembly posledního vydání potvrzuje rámec `mapCanvasEventCb` o velikosti
`0x570` (1 392 bajtů), včetně zbytečného pole 256 indexů.

## Změna

- `renderMapTiles()` požadavek pouze zaznamená. `UITask` ho zpracuje až po
  návratu z `lv_timer_handler()`, mimo vnořenou obsluhu dotyku a časovačů.
- Rychlé změny polohy či zoomu se sloučí a použijí poslední stav mapy.
  Značky a informační popisek se aktualizují spolu s dlaždicemi.
- Zavření, skrytí nebo přestavba mapy ruší čekající požadavek.
- Výběr překrývajících se kontaktů drží jen prvních šest indexů, které umí
  zobrazit existující dialog; uvolní tak přibližně 1 KiB zásobníku obsluhy.

## Ověření

Regrese používají skutečný LVGL pointer: 24 tažení a změn zoomu, zpracování
požadavku přes `UITask`, sloučení rychlých požadavků a zrušení při odstranění
mapy. Test vrstvy dlaždic dekóduje PNG, průběžně vykresluje, kontroluje noční
režim, rozpočet paměti, poškozené dlaždice a zánik rodičovských objektů.
Součástí ověření jsou kompletní anglický a český průchod simulátorem a
PlatformIO build `LilyGo_TDeck_companion_radio_touch`.
Oba průchody i build prošly. Rámec obsluhy tažení v novém strojovém kódu
klesl z 1 392 na 400 bajtů; samotné vykreslení už v tomto rámci neběží.
Statická RAM: 123 768 / 327 680 bajtů (37,8 %), flash: 3 573 585 /
4 063 232 bajtů (87,9 %).

Nové sestavení vyžaduje následné ověření na fyzickém T-Decku s mapami
uživatele; simulátor nenahrazuje měření zásobníku a odezvy na ESP32-S3.
Binární coredump zůstává pouze lokálně a není součástí veřejného release.
