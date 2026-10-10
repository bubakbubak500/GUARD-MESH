Firmware for **LilyGo T-Deck / T-Deck Plus** and **Heltec V4 TFT**, following `guardian-2026.10.10.1`.

## Wi-Fi updates from GitHub

- Settings has a dedicated **Update** page (**Aktualizace** in Czech), one level
  above About. Enable Wi-Fi, configure a network, select **Check for updates**,
  then confirm **Install update** when a newer release is available.
- The source is the latest published stable release in `bubakbubak500/GUARD-MESH`.
  The updater requires the exact board-specific `app-ota.bin` asset, its SHA-256,
  and its release URL. It does not select merged USB images or another board.
- Downloads use the ESP-IDF certificate bundle, verified hostnames and a bounded
  chain of HTTPS redirects restricted to GitHub download hosts. Metadata and JSON
  allocations are bounded; network and flash work run outside the UI task.
- The image streams directly to the inactive internal flash slot. Size, ESP32-S3
  app headers, SHA-256 and ESP-IDF image validation must all pass before changing
  the boot partition. Interrupted or rejected downloads retain the existing boot
  partition. Successful installs flush history and restart the device.
- Checks happen on request; there is no background polling or SD requirement.
  Devices with incompatible partition layouts use USB installation.

## System Information

- Removes whole-image verification from the nested UI event path used to open
  System Information. Diagnostics read only the application and segment headers.
- Moves the snapshot and 2 KB formatting buffer into LVGL's heap, preserving
  deleted-widget and reentrant-page protections. The compiled screen render frame
  uses 48 bytes of stack on T-Deck.
- About now contains firmware identification and diagnostics without the old
  update instructions.

## Installation and validation

Use the matching **merged.bin** for USB, or **app-ota.bin** for a manual OTA upload.
An earlier release without this updater requires that initial installation.

Native tests exercise the production OTA transport with controlled TLS, redirect,
short-read, header, digest, memory and flash failures. UI scenarios cover English,
Czech, keyboard navigation, version selection, confirmation, errors, partition
capability, detached completion and System Information lifetimes. T-Deck and
Heltec V4 TFT firmware builds are checked before publication.

Direct GitHub downloading and rebooting after OTA have not yet been tested on a
physical device. Automatic rollback after a newly installed app fails to boot is
not enabled by this change.
