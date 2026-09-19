# Další fáze: rozdělení UITask.cpp

Výchozí `src/ui-touch/UITask.cpp` má 62 839 řádků. Problémem jsou propojené
odpovědnosti a sdílený stav. V úklidu webu se firmware nemění.

Navržené hranice:

- `UiApplication`: životní cyklus UI a navigace.
- `screens`, `widgets`, `theme`: obrazovky a společné LVGL prvky.
- `models`: stav zpráv, kontaktů, polohy, baterie a nastavení.
- `services`: operace s rádiem, konfigurací a soubory.
- `platform/esp32`: skutečné služby a vstupy zařízení.
- `platform/desktop`: falešná data a vstupy pro stejné UI na Windows/Linuxu.

Postup: nejprve ověřit sestavení cílové desky, oddělit jednu obrazovku a její
data, spustit ji na desktopu, pak přesouvat další části po funkčních celcích.
LVGL zůstane na jednom UI vlákně, pomalé operace budou předávat výsledky přes
události. Vlastnictví objektů, rušení callbacků a životnost dialogů musí být
součástí rozhraní. Samotné rozdělení souboru při zachování globálních vazeb nestačí.

Desktop má vykreslovat skutečné sdílené LVGL obrazovky. Galerie starých snímků
neověří změny C++ a nebude vydávána za simulátor. Refaktor nemá současně měnit
chování firmwaru, protokol ani rozhodovat o odstranění Lua.
