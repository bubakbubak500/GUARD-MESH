Firmware for **LilyGo T-Deck / T-Deck Plus**, following `guardian-2026.10.10`.

### Changes

- About displays the actual Guardian release tag, the GUARD-MESH GitHub Releases
  address and instructions for manual Wi-Fi OTA.
- The upstream release listing, beta-channel controls, OTA downloader and SD
  firmware downloader have been removed from the updater.
- Automatic checking and installation remain unavailable. Retained install
  callbacks and executor entry points reject old requests without using a
  network socket or writing firmware; legacy completions cannot trigger reboot.
- The existing manual `ota start` upload remains available. Download the correct
  board's application image from GitHub, connect to MeshCore-OTA, open the address
  shown by Terminal and upload it as **Firmware**.
- Czech translations include the new release source and installation guidance.

A verified GitHub metadata/download transport, exact-board asset selection and
digest validation are still required before enabling automatic installation.

### Files

- **app-ota.bin**: manual application update using the device's `/update` page.
- **merged.bin + merged.json**: complete USB installation from `0x0`, including
  the NVS area. Back up settings and messages before a complete installation.
- **debug-elf.zip**: matching ELF for crash diagnostics.
- **SHA256SUMS.txt**: artifact checksums.

### Validation

Native regressions exercise blocked legacy OTA/SD calls with null socket handles,
job completion and storage admission. English/Czech/keyboard simulator scenarios
cover GitHub guidance, missing OTA slots and rejection of retained install calls.
T-Deck and Heltec V4 TFT firmware builds are checked before publication.

Manual OTA has not yet been tested on physical T-Deck hardware.
