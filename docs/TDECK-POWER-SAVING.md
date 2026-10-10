# T-Deck display and GPS power saving

Implemented for `guardian-2026.10.10.3`. The changes apply to the main firmware;
the keyboard's separate ESP32-C3 firmware does not need to be flashed.

## Display

`UITask` filters the LVGL display refresh callback using screen policy, screenshot
capture and web-mirror demand. A paused timer alone is insufficient: LVGL resumes
it when an object is invalidated. The callback therefore checks every invocation.
The physical flush has its own guard. Application/input timers keep running.
Visual status, badges, dirty lists, diagnostics and map decoding are deferred when
there is no display consumer. Worker completion and battery/history sampling
continue. A wake prepares active/top/system layers while the backlight is dark.
If wake originates inside LVGL's handler, preparation runs after it returns.

GPIO10 is never switched off by this change. ST7789's existing SLPIN/SLPOUT handling
remains separate from backlight control. These changes do not introduce ESP32-S3
light sleep or modify the keyboard's scan loop.

## GPS

`TDeckGpsStream` is the sole Serial1 consumer and observes incoming binary/NMEA
frames before passing ASCII sentences to the existing MicroNMEA provider. Reads
are bounded to 1,024 bytes per service pass. When the sensor manager disables GPS,
`tdeckGpsPowerService()` continues only the identity/standby handshake and UART
drain; it cannot publish a fix or discipline the RTC. A changed live UART baud
invalidates the previous identity proof.
Re-enabling discards the queued RX snapshot so a buffered pre-enable fix cannot
be presented as a new acquisition. Complete UBX frames, including any embedded
ASCII text, are withheld from the NMEA parser.

The native `GnssPowerControl` state machine uses two bounded identity probes.
Unknown hardware never receives a power command. M10 ROM SPG 5.10 / protocol
34.10 / hardware 000A0000 is accepted; `MOD=MIA-M10Q` adds exact module identity.
Disabling a supported receiver sends:

1. UBX-CFG-CFG `clearMask=0`, `saveMask=0000FFFF`, `loadMask=0`, explicit
   `deviceMask=01` to retain the current configuration in BBR only.
2. After matching checksum-valid ACK for `06/09`, UBX-RXM-PMREQ v0, duration 0,
   flags 6 (backup + force), wake source 8 (UART RX).

A NAK, one-second ACK timeout or incomplete UART write skips standby. Enabling
sends `FF` to wake UART, waits asynchronously before bounded MON-VER retries and
resumes position acquisition. Rapid enable/disable changes cancel a pending
standby while enabled. PMREQ has no promised ACK; the UI reports command sent,
and valid NMEA still arriving after the settling interval reports failure.

Force standby clears runtime RAM settings. The BBR save is therefore necessary
to retain UART speed and output configuration. This path intentionally excludes
SPG 5.20, whose interface documentation does not provide CFG-CFG. It never writes
the receiver's flash or sends factory-reset/recovery commands.

L76K/CASIC is recognized only from checksum-valid identification text and remains
software-disabled. The stock L76K shield exposes UART but leaves its WAKE_UP pin
unconnected. The MIA shield supports UART wake, but its active antenna supply is
not switched by this software standby and can continue drawing current.

Primary references: [LILYGO identification example](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/GPSShield/GPSShield.ino),
[M10 SPG 5.10 interface](https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf),
[MIA-M10Q integration manual](https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_IntegrationManual_UBX-21028173.pdf),
[LILYGO schematics](https://github.com/Xinyuan-LilyGO/T-Deck/tree/master/schematic).

## Repeatable software validation

Run `scripts/test_gnss_power.py`, `scripts/test_tdeck_gps.py`,
`scripts/test_screen_policy.py`, `scripts/test_ui_models.py`,
`scripts/test_firmware_transport.py` and `scripts/test_czech.py` with Python.
The adapter test requires the installed T-Deck PlatformIO dependencies and uses
the actual MicroNMEA/provider sources with UART/RTC hardware replaced by fixtures.
Build `simulator/build.py` and run the resulting executable with `--smoke`,
`--smoke-cs` and `--smoke-nav`. These execute real UI and LVGL code.

Build both `LilyGo_TDeck_companion_radio_touch` and
`heltec_v4_tft_companion_radio_usb_tcp_touch` with PlatformIO's `mergebin` target.
The application must fit 0x3E0000 bytes, begin with E9, contain the release tag and
match the application in the merged image at offset 0x10000. Release assets use
the updater's exact board-specific names and GitHub SHA-256 asset digests.

## Device validation still required

Measure battery-input current under the same voltage, radio conditions and GPS
antenna placement before and after upgrading. Test screen off/on with GPS on/off,
idle LoRa reception, message notification, manual lock/reveal/unlock, touch,
trackball, keyboard, screenshot and web mirror. Check repeated MIA-M10Q GPS
off/on cycles, the diagnostic state, resumed fix/time data and retained baudrate.
An unsupported L76K should receive no power command and continue normal fix
acquisition when enabled. Verify an OTA installation downloads the exact board
image, validates it and boots the new version while retaining preferences.

No physical device was attached during this release's software validation.
Neither native tests nor the simulator measure standby current or validate the
real module's electrical response. No numerical battery-life claim is made.
