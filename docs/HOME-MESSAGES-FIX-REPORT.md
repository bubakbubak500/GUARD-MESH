# Home unread messages and chat controls — 2026-09-27

## Fixes

- Home previously selected one preview per unread conversation. Three unread messages in the same channel therefore produced only one row. It now selects the three newest incoming unread records across the resident message ring, including multiple records from one channel. Arrival order is used rather than sender timestamps. Opening a preview still opens its conversation and marks that conversation read.
- A preview carries its message slot and sequence identity. Deleted, read or replaced records cannot activate a stale preview.
- Deleting an unread record now decrements its conversation's unread count. Applying a per-conversation history cap clamps the count to the retained incoming records, preventing older read messages from resurfacing as unread. Persistence format remains unchanged; ordinary ring eviction is not treated as a history deletion.
- T-Deck chat headers use separate 28 × 18 px Back and settings buttons, with a gap between their hit areas and the conversation name after them. They use the Inbox button style and the same 20 px status row / 12 px title typography. Both channel and direct conversations use this layout.
- The Inbox QR pictogram is reduced from 18 to approximately 12 px; its button retains the full hit area. Other boards retain their existing header controls.

## Verification

- Native model tests exercise one/three/four unread records in one channel, mixed DM/channel names, outgoing records, equal/backwards timestamps, mark-read, deletion, history caps, ring rollover, restore and invalid arguments.
- Simulator integration injects real receive events and checks the text and visibility of all three Home rows. It opens a preview through pointer input, taps both edges of the channel settings button, closes the settings menu, tests incoming messages while the chat is open, returns through the Back button edge, and rejects a deleted preview callback. Direct-message Back/settings buttons are also exercised.
- Existing Home header/clock/battery geometry, Advert/Discover Back routes, modal shielding, fullscreen glyphs and the wider model/service/screen regressions remain part of the run.
- The Czech catalog is unchanged: 1,322 keys and placeholder validation pass; SHA-256 `C7A06871BF001A5A6AD0DCAEC1075931F6AB409AAB53A7949A95EB1C79CFB4FA`.

- Final complete EN/CZ/keyboard-navigation scenarios passed in 73.9 / 73.7 / 73.9 seconds. Screenshots were inspected for three messages in one channel, the compact Inbox QR, channel settings, and both chat headers. The earlier keyboard run reached the 180-second timeout; the previously observed intermittent simulator hang is not claimed fixed here.
- The wider native suites passed, including storage access/maintenance, SD health, Back navigation, screen policy, chat session, message ingress, thread refresh, network executor, tile transport, BLE targets and telemetry polling. The final model executable includes the added history-cap-on-append case.

## Build and delivery

- Firmware source commit: `765bf93` (local only).
- Target: `LilyGo_TDeck_companion_radio_touch`.
- PlatformIO build succeeded at 18:20 local time. Static RAM: 122,496 / 327,680 bytes (37.4%); application flash: 3,656,601 / 4,063,232 bytes (90.0%).
- Application: `out/LilyGo_TDeck_companion_radio_touch-20260927_182040-765bf93.bin` (3,657,024 bytes), SHA-256 `52dbde71ea5aa0ffb63a6c5f0d5aa49a2f50922cbf2127439c9ab43ba1be46f4`.
- Full flasher image: `out/Guard-Mesh-TDeck-20260927_182040-home-messages-merged.bin` (3,722,560 bytes), SHA-256 `c190b8740eac70de277b76335f51f086b3e80c6f8f31c9a98bafa192534a81ba`. Its adjacent JSON names this release **Home messages and chat controls fix 2026-09-27 18:20**.
- Verified image checksums, equality of timestamped/application/build outputs, and component offsets `0`, `0x8000`, `0xE000`, `0x10000`. All 920 captured source/configuration/test/catalog hashes remained unchanged through packaging.
- Local evidence: `.sim-cache/home-messages-20260927-final/`, including model/UI/build logs, source manifest, image receipt and EN/CZ screenshots. The earlier timeout log is retained separately there.
- This is simulator and build verification; no hardware flash or on-radio validation was performed.
- No Git push. A full merged image written from address zero can overwrite NVS identity/settings; use the application image at `0x10000` for an application-only update with the existing partition layout and without full-chip erase.
