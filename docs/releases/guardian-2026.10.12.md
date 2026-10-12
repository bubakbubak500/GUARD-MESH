# guardian-2026.10.12

Oprava vyčerpání zásobníku při obnovování zpráv v chatu na T-Decku.
Vydání navazuje na `guardian-2026.10.11` a zachovává jeho funkce Guardian v3.

## Diagnostika pádu

Dodaný dump odpovídá T-Deck sestavení `guardian-2026.10.10.3`.
Identifikátor aplikace `62d839aa193c1ffc` souhlasí s SHA-256 archivovaného
`firmware.elf`:
`62d839aa193c1ffce441e528b52450e4295583ca60cc9267e652de6b3a50edee`.
Nejde tedy o poslední vydání, ale dotčené soubory chatu se mezi verzemi
`2026.10.10.3` a `2026.10.11` nezměnily.

Postižená úloha je `loopTask`, její zásobník má 8 KiB. Záznam obsahuje
debug exception `EXCCAUSE=0x41` a kontext přepínání úlohy u dolního okraje
zásobníku. Zachované návratové adresy odpovídají cestě
`UITask::loop` → LVGL async timer → `refreshChatDetail` →
`chatVirtRenderWindow` → `chatVirtSyncBubblePositions` → kontrola objektů LVGL.
Kombinace těchto údajů a velikostí rámců ukazuje na vyčerpání zásobníku
při obnově chatu. Samotný první GDB backtrace začíná v plánovači;
není úplným backtracem původní operace UI.

Význam ochrany konce zásobníku při debug exception popisuje
[dokumentace Espressif](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/fatal-errors.html#unhandled-debug-exception).
Surový dump a obsah zpráv zůstávají pouze lokálně.

## Oprava

`chatVirtSyncBubblePositions` nyní nastavuje polohy přímo při průchodu
dětmi kontejneru. Každá poloha závisí na logickém indexu zprávy a poloze
viewportu, takže nepotřebuje pomocné pole 128 položek ani řazení.
Odstraněné pole zabíralo 1 536 bajtů zásobníku; navíc se v něm ukládala
nepoužitá výška bubliny. Zůstává zachováno levé/pravé zarovnání příchozích
a odchozích zpráv, kompaktní režim, denní oddělovače i hranice nepřečtených zpráv.

Ověření instrukcí `entry` v novém T-Deck ELF potvrzuje pokles z 1 904 na
384 bajtů (352 bajtů vlastní implementace + 32 bajtů vstupní funkce).
Aktivní cesta tak šetří 1 520 bajtů zásobníku bez nové globální pracovní paměti.

## Instalace

| Zařízení | USB, offset `0x0` | OTA přes Wi-Fi |
| --- | --- | --- |
| T-Deck / T-Deck Plus | `Guard-Mesh-TDeck-guardian-2026.10.12-merged.bin` | `Guard-Mesh-TDeck-guardian-2026.10.12-app-ota.bin` |
| Heltec V4 TFT | `Guard-Mesh-Heltec-V4-TFT-guardian-2026.10.12-merged.bin` | `Guard-Mesh-Heltec-V4-TFT-guardian-2026.10.12-app-ota.bin` |

Pro OTA používejte pouze soubor `app-ota.bin` pro odpovídající desku.
Release přikládá kontrolní součty, manifesty, debug ELF a `validation.json`.
Ověření sestavení a obrazů nenahrazuje fyzický test USB/OTA instalace ani
provozu zařízení. **Hardware nebyl testován.**

Výsledky review a ověření jsou v
[záznamu ověření](guardian-2026.10.12-validation.md).
