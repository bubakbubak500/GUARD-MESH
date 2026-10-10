Firmware for **LilyGo T-Deck / T-Deck Plus** and **Heltec V4 TFT**, following `guardian-2026.10.10.2`.

## Display work while the screen is off

- T-Deck's LVGL display refresh skips rendering while the local screen is dark.
  A separate flush guard prevents physical pixel transfers, including forced
  redraws. Input, application timers, message reception and background jobs
  continue. Purely visual periodic updates and pending map decoding are deferred.
- An active web mirror or explicit screenshot can render while the panel stays
  dark. Screenshots include the active, top and system layers.
- Wake, lock reveal and unlock repaint the complete composited frame before the
  backlight turns on. Wakes originating in an LVGL callback defer that repaint
  until the handler returns. Receiving a message never bypasses the manual lock.
  Chat timeline reconciliation keeps its asynchronous scheduling and completes
  on subsequent UI turns.
- The existing ST7789 sleep commands and the shared peripheral power rail remain
  in use. No power cut affecting LoRa is introduced.

## GPS standby on supported hardware

- A bounded UART identification handshake runs at the selected GPS baudrate.
  Only checksum-valid u-blox MON-VER identifying hardware `000A0000`, ROM
  **SPG 5.10** and protocol **34.10** enables standby. `MOD=MIA-M10Q`, when
  reported, identifies the specific module in GPS settings.
- Disabling GPS first saves the current RAM configuration to **BBR only**. The
  receiver must acknowledge that save before it receives RXM-PMREQ for indefinite
  standby with UART wake enabled. No flash save or receiver reset is used.
- Enabling GPS sends a UART wake byte and performs bounded asynchronous identity
  retries. All existing position/date parsing continues through MicroNMEA. The
  off-mode handshake cannot update the reported position or RTC.
- GPS settings distinguishes identification, active, preparation, command sent,
  unsupported standby and failed confirmation. “Command sent” does not claim a
  measured current reduction. The antenna may continue consuming power.
- L76K/CASIC, SPG 5.20 and unknown versions are left at software off. L76K's
  hardware wake pin is not wired on the standard shield. No keyboard ESP32-C3
  firmware or separate keyboard flash is included or required.

## Installation and validation

Use the matching **merged.bin** for USB installation or **app-ota.bin** for OTA.
The updater introduced in `.2` discovers this stable GitHub release. Devices
without that updater need an initial USB installation or manual OTA upload.

Native tests cover the production GPS protocol/state machine, the T-Deck adapter
with the real MicroNMEA provider, GPS status formatting and the production OTA
transport. Shared UI simulator scenarios cover rendering suppression, live
timers/message reception, wake ordering, overlays, screenshot, web mirror and
lock behavior, as well as the existing English/Czech and keyboard flows. Both
board builds and packaged image sizes/headers/hashes are checked.

No physical T-Deck or Heltec is attached during validation. Actual current draw,
GNSS standby/wake and downloading/rebooting after OTA remain untested on hardware.
The existing OTA rollback limitation described in `.2` is unchanged.

Implementation and device measurement procedure: [T-Deck power saving](../docs/TDECK-POWER-SAVING.md).
