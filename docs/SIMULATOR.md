# T-Deck simulátor pro Windows

Spusť `Start-Simulator.cmd`. Potřebuje Windows x64, Python 3.9+ s pip a Git.
První spuštění stáhne připnuté LVGL, hlavičky MeshCore a přenosný Zig compiler;
další spuštění používají lokální cache. Visual Studio ani deska nejsou potřeba.
Spouštěč znovu přeloží změněný kód a otevře samostatné okno.

- Myš: dotyk; tažením posouváš seznamy.
- Klávesnice: psaní do polí, Enter odešle zprávu. Zpět je šipka v UI.
- Šipky: pohyb trackballu. F6: jeho stisk, podržení funguje i pro odemknutí.
- F5: příchozí simulovaná zpráva od `SIM Alpha`.
- F12: PNG a texty se souřadnicemi do `.sim-cache/screenshots/`, použitelné pro připomínky.
- F1: nápověda.

Běží **skutečný `src/ui-touch/UITask.cpp`**, LVGL 8.4, původní fonty, obsluha
dotyku/kláves a logika nastavení. Displej má 320 × 240 bodů jako klasický
LilyGo T-Deck. Vyzkoušet lze chaty, kontakty, profil, nastavení, mapovou obrazovku
a nativní nabídky aplikací. Zprávy se odesílají pouze do lokálního modelu;
potvrzení doručení je simulované. Scénář kontaktů a zpráv se při spuštění obnovuje.
Nastavení/profil jsou odděleně v `.sim-cache/state/preferences.txt`.

Jde o nativní hostitelské sestavení UI části firmwaru. ESP32 instrukce, rádiový
protokol, elektrické periferie, reálná SD/flash, Wi-Fi/BLE, OTA a spotřeba se
neemulují. Mapa běží bez stahování dlaždic. Lua runtime, audio a USB Files jsou
v tomto cíli vypnuté. Je určený pro vývoj a proklik UI; ověření těchto funkcí
a časování zůstává na desce. Podpora jiných desek ani Linuxu zatím není součástí.

## Vývoj

`Start-Simulator.cmd --test` sestaví a skrytě spustí integrační test: navigace,
chat, psaní, odeslání, profil a nabídka aplikací. Snímky jsou
v `.sim-cache/test-artifacts/`. `--build-only` pouze sestaví program.
Detail překladače: `.sim-cache/build/errors.txt`; běhový log: `.sim-cache/simulator.log`.
Nová sestavení mají vlastní exe, takže se nepřepisuje právě spuštěná verze.

Platformní adaptéry jsou v `simulator/include/`, Windows okno v `simulator/main.cpp`.
`GUARD_SIMULATOR` volí tyto adaptéry; firmware pro desku používá dosavadní větev.
Všechny stažené knihovny, binárky, testovací snímky a lokální stav jsou ignorované
v `.sim-cache/`. Verze a commity jsou v `simulator/dependencies.json`.

Základ: [LVGL](https://github.com/lvgl/lvgl/tree/v8.4.0), MIT, a hlavičky
[MeshCore fork](https://github.com/ALLFATHER-BV/meshcomod/tree/core-v1.17.4), MIT.
PNG zapisuje LodePNG z LVGL (zlib licence). Zig je pouze sestavovací nástroj
(MIT, distribuované části mají vlastní licence). Licence zůstávají u stažených
zdrojů. Na LVGL se aplikuje stejná oprava animací jako ve firmwaru.
Prověřený starší [MeshCore desktop simulator](https://github.com/thepacket/meshcore-standalone)
používá jiné obrazovky; jeho UI jsme nepřebírali.
