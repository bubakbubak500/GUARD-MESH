Firmware for **LilyGo T-Deck / T-Deck Plus**, following `guardian-2026.10.04`.

### Changes

- Discover displays names learned from valid node adverts, including nodes
  outside Contacts. Contacts take precedence over cached names.
- A bounded hash index stores full public keys and names: up to 1,024 entries
  with PSRAM, 128 without it, with a smaller allocation fallback and LRU eviction.
  Prefix-only replies are translated only when their identity is unambiguous.
- Learned names are independent of Found. Deleting, filtering or pruning Found
  keeps the cache; clearing the cache does not alter Contacts or Found.
  Settings → General shows usage and has a separate confirmed clear action.
- Cache I/O runs on a worker with batched saves, a maximum save delay and two
  versioned, CRC-checked snapshots. A failed or interrupted write falls back to
  the previous valid snapshot. Clearing invalidates queued stale writes.
- Chat keeps overlapping bubbles alive and reuses retired rows. Materialisation
  is coalesced every 16 ms during movement, including active scrolling.
- ACK and echo metadata updates preserve bubble roots and the viewport.
  Compact rows remeasure changed delivery text when wrapping can change.
- Row heights are cached by message sequence and display configuration.
  Appends reserve offset capacity and measure only new rows; rollover and
  reopening reuse unchanged measurements. A surviving message anchors the
  viewport when history shifts.

### Files

- **app-ota.bin**: application update; do not flash at address `0x0`.
- **merged.bin + merged.json**: complete USB installation from `0x0`, including
  the NVS area. Back up settings and messages before a complete installation.
- **debug-elf.zip**: matching ELF for crash diagnostics.
- **SHA256SUMS.txt**: artifact checksums.

### Validation

Native cache tests cover 10,000 identities, LRU eviction, rename, ambiguous
prefixes, restart, interrupted writes, queued clear and continuous adverts.
Simulator regressions cover ACK stability, animated materialisation, append,
rollover, reopening, compact rows, English/Czech UI and keyboard navigation.
Both T-Deck and Heltec V4 TFT firmware builds are checked.

This release has not yet been tested on physical T-Deck hardware.
