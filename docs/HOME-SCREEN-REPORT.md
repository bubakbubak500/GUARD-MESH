# T-Deck Home and fullscreen preview — 2026-09-27

## Implemented

- Landscape Home uses a separate `screens/HomeScreen` module with three unread conversation previews. Each row opens its conversation; the smaller heading opens the inbox. The preview shows the latest message in each unread conversation, not three messages from one conversation.
- The lower left card is explicitly marked `Guardian [MOCK]`, with `Link: --` and `Sync: --`. These are visual placeholders; no Guardian service is implemented or reported as connected.
- Right-side actions are Advert, Terminal, Discover, Apps and Control. Apps has an orange accent. The graph is removed from this layout, and Discover replaces the Files shortcut.
- The T-Deck status bar is 20 px at normal scale. Home shows compact receive/transmit counts in the center, node/connectivity on the left and time/battery on the right. Other screens retain their existing status content.
- Fullscreen incoming previews check the actual font and fallback chain before displaying text. Czech accents remain intact; unsupported codepoints and malformed UTF-8 use `*` instead of a missing-glyph rectangle. Stored messages are not rewritten. Newly created preview geometry is resolved before assigning text to avoid premature ellipsis truncation.
- The user's five Czech wording edits were preserved verbatim and saved separately with the regenerated built-in catalog.

## Verification

- Simulator tests cover the three visible preview rows, their height, action dispatch, stale conversation rejection, row reuse/focus, external deletion/rebinding and owned-tree cleanup.
- Fullscreen regression uses the real preview path and checks Czech accents, punctuation, an unsupported character, malformed UTF-8 and a font that returns a placeholder glyph.
- English/Czech UI and keyboard-navigation integration suites passed; Home and fullscreen screenshots were inspected at 320 × 240.
- Czech catalog validation passed: 1,322 keys, placeholder and technical-term checks.
- Two runs reached the 90-second UI limit in the shared confirmations stage (one general run and one keyboard run). Model/service checks passed. After the C++11 compatibility correction, final English/Czech UI scenarios passed with the normal limit; the final keyboard retry passed in 39.2 seconds with a 180-second allowance and no overlapping build. The intermittent simulator timeout remains undiagnosed and is not claimed fixed.

## Device and delivery

The user will flash the firmware manually. No flash writes or erases were performed. Read-only backup attempts were interrupted by USB transfer errors; partial data remains in the ignored local cache and is not a usable full backup. The radio was reset to its existing firmware. Changes are committed locally only; no push or release publication is requested.

## Final build

- Source commit: `25bbf9f` (Home feature `e5e3a87`, preserved Czech edits `47ce090`).
- Environment: `LilyGo_TDeck_companion_radio_touch`; PlatformIO build succeeded on 2026-09-27 at 16:40 local time.
- Static RAM: 122,504 / 327,680 bytes (37.4%). Application flash usage: 3,655,857 / 4,063,232 bytes (90.0%). These are linker figures, not runtime heap measurements.
- Application: `out/LilyGo_TDeck_companion_radio_touch-20260927_164046-25bbf9f.bin`, 3,656,272 bytes.
- Application SHA-256: `206f33be9c603c3c9528ae8b6455659ed13e5aac3f7b4583540fefb3cdba8128`.
- Desktop flasher image: `out/Guard-Mesh-TDeck-20260927_164046-home-merged.bin`, 3,721,808 bytes, with matching `.json` metadata titled **GUARD-MESH T-Deck — Home 2026-09-27 16:40**.
- Merged SHA-256: `be989398040115f977927d8e1bd78c7f661d5367c3537c0be295a5c9a6c72760`.
- Verified the bootloader, partition table, OTA initializer and application at offsets `0`, `0x8000`, `0xE000`, `0x10000`; image parsing/checksums passed. All 920 captured source/configuration/test/catalog hashes were unchanged between capture and packaging.
- Local evidence: `.sim-cache/home-20260927-final2/`, including the source manifest, artifact verification, build log and final EN/CZ/navigation logs. Screenshots are in `.sim-cache/test-artifacts/`.

The merged image is a full installation, not a data-preserving update: writing it from address zero can overwrite NVS identity/settings even without a full-chip erase. No new firmware was tested on physical hardware in this task; the user chose to flash manually.
