> Imported upstream reference (2026-09-19), text only. Describes WadaMesh; features and upstream service URLs have not yet been adapted for Guardian.

# Sideloading on the Tanmatsu

For developers building wadamesh from source and running it on a Tanmatsu. If you just want to use it, install **WadaMesh** from the launcher's own app store instead.

ESP32-P4 ESP-IDF 5.5.1 AppFS

<a id="model"></a>

## How it works, and the one way to break it

wadamesh is **not** standalone firmware on this board. The Tanmatsu boots badge.team's launcher, and wadamesh runs as an **AppFS app** underneath it, exactly like the launcher's own apps.

So you never flash the whole device. You write **one app image and one metadata sector** into the AppFS partition, and leave the bootloader, the partition table and the OTA slots alone.

**Never run `idf.py flash` on a Tanmatsu.** It writes the bootloader, partition table and OTA data, which replaces the launcher OS itself. Recovering means reflashing the launcher from badge.team. Everything on this page touches only the AppFS partition.

The board has three processors and only one of them is yours: the **ESP32-P4** runs the launcher and your app, an **ESP32-C6** handles Wi-Fi, Bluetooth and LoRa over esp-hosted, and a **CH32** runs the keyboard and power. You flash the P4.

<a id="setup"></a>

## One-time setup

**The tools live on `main`.** `tanmatsu/tools/` was added on 21 August 2026, shortly *after* `beta_67` was tagged, so a release tarball or a `stable` checkout does not contain it and neither does a clone made before that date. Get it with `git fetch origin && git checkout main && git pull`, or browse the four scripts directly at [github.com/ALLFATHER-BV/wadamesh/tree/main/tanmatsu/tools](https://github.com/ALLFATHER-BV/wadamesh/tree/main/tanmatsu/tools). They will be in the next beta as a matter of course.

You need ESP-IDF 5.5.1. The build uses a project-local copy in `tanmatsu/esp-idf` (gitignored, around 3 GB); `make prepare` from `tanmatsu/` fetches it. Then two one-off steps:

```
tanmatsu/tools/fetch-appfs.sh
```

Clones badge.team's `esp32-component-appfs`, whose `appfs.py` reads and rewrites the AppFS image. It is deliberately not vendored into the wadamesh repo: it is their code, and the copy in circulation carries no licence header.

```
tanmatsu/tools/dump-pristine.sh /dev/cu.usbmodemXXXX
```

Dumps **your** device's AppFS partition as the baseline that later deploys diff against. Do it while the launcher's own apps are installed and wadamesh is not, so the baseline is the device as it ships.

Use your own dump, never someone else's. The deploy writes only the sectors that *differ* from this file, so a baseline describing a different device can overwrite apps you actually have.

<a id="build"></a>

## Build

```
cd tanmatsu && ./build.sh build
```

Use `build.sh` rather than `idf.py` directly: it applies two build-time patches to `managed_components` (esp-hosted Wi-Fi-init tolerance, and an Arduino BLE stub) that the build needs.

**The build ends with an error, and that is expected.**

```
Generated .../application.bin
Error: All app partitions are too small
```

The ~2.9 MB app is being size-checked against the 2 MB `ota_0` slot, but it does not live there: it lives in the 8 MB `appfs` partition. Judge success by `Generated ... application.bin` appearing with no `error:` or `undefined reference` above it.

<a id="install"></a>

## Install

```
tanmatsu/tools/tan_flash.sh
```

With no port argument it probes each USB serial device and picks the one answering as an ESP32-P4. The Tanmatsu exposes **two** ports, the P4 and the C6, and which name each gets changes between replugs, so detecting by chip beats guessing. Pass a port explicitly if you prefer.

| Step | Why in this order |
| --- | --- |
| Build the image, diff against the baseline | Only the 64 KB sectors that actually changed get written. |
| Write the **app data**, no reset | The bytes land before anything points at them. |
| Write the **metadata sector**, then reset | That sector is what makes the app visible to the launcher. Writing it last means an interrupted flash leaves the previous app intact rather than a half-written one marked complete. |
| Read the metadata back and compare | The commit write is the one that can silently drop. Without this check the launcher would quietly keep showing the old build. |

Expect `metadata verified — installed`. Anything else means run it again.

The version number increments on every deploy, and it has to: the launcher keys updates on name plus version, so republishing the same number leaves you on the old binary with no error anywhere.

<a id="verify"></a>

## Verify

Launch **WadaMesh** from the launcher menu. An empty serial log straight after flashing is normal: the launcher is a silent GUI OS on USB-CDC, and the boot log only appears once the app itself starts.

```
python3 tanmatsu/tools/sermon.py /dev/cu.usbmodemXXXX /tmp/wada.log
```

**Do not attach a monitor that asserts DTR/RTS.** Resetting the P4 also desynchronises the C6, which has no reset line of its own, and the launcher then hangs on "Initializing radio" until the board is fully power-cycled. `sermon.py` does not reset, and survives the USB re-enumeration when an app launches.

<a id="trouble"></a>

## When it goes wrong

| Symptom | Cause and fix |
| --- | --- |
| No serial ports at all | The C6 has wedged. **Fully power-cycle** the board, off then on. A reset is not enough. |
| "No ESP32-P4 found", ports present | Something else is holding the port, usually a monitor still running. Close it and retry. |
| "metadata DIFFERS" | The commit write did not land. Re-run. Nothing is broken; the device still has the previous app. |
| Launcher stuck on "Initializing radio" | esp-hosted desync, almost always from a resetting monitor. Full power cycle. |
| A crash to decode | `riscv32-esp-elf-addr2line -e tanmatsu/build/tanmatsu/application.elf -fpC <MEPC> <RA> <stack RAs>` |

**Before you ship anything:** the Tanmatsu shares one `src/` with the ESP32-S3 boards, so a change here can break them without you noticing. Build at least the T-Deck and Heltec V4 environments as well.

Full text of this guide, kept with the code: `TANMATSU_SIDELOAD.md` in the repo. Port status and outstanding work: `TANMATSU_PORT.md`.
