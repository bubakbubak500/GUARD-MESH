# T-Deck status bar and Home navigation — 2026-09-27

This follow-up addresses the hardware photos from the first Home build.

## Changes

- All T-Deck screens share the existing 20 px status bar and 12 px text. The separate opaque Home overlay is removed. Tool titles and chat/overview controls fit the same single row.
- The clock is centered; the original mesh signal bars sit before the battery. Battery percentage and icon have a visible gap, including at 100%.
- Home alternates the left label between the node name and compact RX/TX counts every three seconds. The hide-node-name preference is retained.
- The Czech Home shortcut says **Hledat** using the existing Search translation. Other Discover labels and the user's Czech catalog remain unchanged.
- Discover and Advert were classified as popups, so the status-bar modal scrim swallowed their own Back taps. Shared-header pages now have a registry flag that excludes the page itself from scrim calculation, while real modal dialogs still block the header. The same classification covers Spectrum, Known regions, File Transfer and Wi-Fi forms.

## Verification

- Simulator regression uses actual pointer input to launch Advert/Discover and return through both the back-chevron area and the title area. It also checks the Terminal Home button, the Apps/Cmdr tile, and opening/closing Control Center.
- Layout assertions cover Home and all five tabs, a 100% battery, a 12-hour clock, the three-second label boundaries, and modal shielding above a tool page.
- Existing model/service tests and Czech catalog validation passed (1,322 keys). The user's Czech file hash is unchanged.
- Final EN/CZ/keyboard UI scenarios passed in 53.5 / 53.4 / 53.3 seconds with a 180-second allowance. One earlier Czech run reached the previous 90-second timeout; this intermittent simulator issue is not claimed fixed by the UI change.
- Final screenshots were inspected at 320 × 240, including both Home label phases, the wide clock/100% case, Contacts, Advert and Discover.

## Build and local delivery

- Firmware source commit: `a81c967`.
- PlatformIO environment: `LilyGo_TDeck_companion_radio_touch`; successful build at 17:33 local time, 2026-09-27.
- Linker usage: static RAM 122,472 / 327,680 bytes (37.4%); application flash 3,655,433 / 4,063,232 bytes (90.0%).
- Application: `out/LilyGo_TDeck_companion_radio_touch-20260927_173307-a81c967.bin` (3,655,856 bytes).
- Application SHA-256: `6ebd0432bf37b6657d2359c5a302251053cf47ae38aa1e42f4a5514ad74c7741`.
- Flasher image: `out/Guard-Mesh-TDeck-20260927_173307-home-chrome-merged.bin` (3,721,392 bytes), with matching JSON metadata titled **GUARD-MESH T-Deck — Home header fix 2026-09-27 17:33**.
- Merged SHA-256: `b821b7b37bce60e11334a3495a9d55dd77c67b15f483e1dbd24ba89c7037f1cd`.
- Image checksums, partition layout and component offsets (`0`, `0x8000`, `0xE000`, `0x10000`) verified. All 920 captured source/configuration/test/catalog hashes remained unchanged through packaging.
- Local evidence and screenshots: `.sim-cache/home-chrome-20260927-final/`.

No radio flash operation or Git push is part of this follow-up. The merged installer can overwrite NVS identity/settings when written from address zero; it is not a data-preserving update.
