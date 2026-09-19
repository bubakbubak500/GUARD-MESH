> Imported upstream reference (2026-09-19), text only. Describes WadaMesh; features and upstream service URLs have not yet been adapted for Guardian.

<a id="overview"></a>

# WADAMESH user guide

WADAMESH turns a LilyGo T-Deck — its landscape screen, BlackBerry-style keyboard and trackball — into a complete front-end for your MeshCore radio: chat, channels, contacts, a live map and deep radio settings, all on the device. (It also runs on the Heltec V4 and Tanmatsu.) Every screen below is a real screenshot from the device.

 No phone required
 LoRa + Wi-Fi + Bluetooth
 Keyboard + touch**Got a microSD slot? Keep a card inserted.**

On devices with a microSD slot (the LilyGo T-Deck and Tanmatsu), wadamesh is best used with a card in place. It keeps map tiles, your contacts and your chat history on the card, which is roomier and far more reliable than the small internal flash, so a card prevents memory issues and stops that data being lost on reboot. The Heltec V4 has no card slot and manages fine on its internal storage.

<a id="home"></a>

## Home

The home tab is your **app launcher** — a grid of everything on the device:

- **Cmdr** — the command centre overview.
- **Chats**, **Contacts**, **Map**, **Mentions** — jump straight to those.
- **Advertise** — announce yourself to the mesh (manual or on a timer).
- **Signal** and **Monitor** — live RX signal and a repeater-style RF activity view.
- **Spectrum** — sweep the LoRa band for interference.
- **Settings**, **Terminal** (MeshCore CLI), **Files** (microSD).

The bar along the bottom is always there — it switches the five main tabs (Chat, Contacts, Home, Map, Settings) from anywhere. The top bar shows unread count, time, signal and battery.

### Building it yourself Dev

Firmware for the ESP32-S3 boards builds with PlatformIO from the repo. The **Tanmatsu** is different: it runs under badge.team's launcher as an app rather than as standalone firmware, and flashing it the usual way replaces the launcher OS. There is a separate guide for that: [Sideloading on the Tanmatsu](UPSTREAM-TANMATSU.md).

If you only want to write software for the device, you do not need a firmware build at all: see the [Lua app SDK](UPSTREAM-LUA-SDK.md).

### Is my history actually saving? Cmdr

**Settings → About → Chat store** is the answer: it names the storage backend in use, when history last saved successfully, and the exact stage and error a failing save is hitting. Worth a look if messages seem to vanish across a reboot.

Home — app launcher

<a id="chat"></a>

## Chat

Every conversation in one list, newest first — both **channels** (group chats like `#wadamesh`, `Public`) and **direct messages**. An unread badge shows the count.

### On each row

- **Tap** a row to open the conversation, then type on the keyboard to reply.
- The **gear** on the right opens that chat's settings — mute, region & scope, share secret, blocked users, remove — the same screen as the cog inside a chat.

The top-right buttons add a contact, mark everything read, and show your QR code.

Chat — channels & DMs

<a id="contacts"></a>

## Contacts

Everyone your node knows — name, type and how recently you heard them. Tap a contact to message it or open its details: signal history, a route trace, telemetry, and share/favourite options.

The **overflow menu** (top right) holds Search, the **Discovered** list (nodes heard but not yet added), Add contact, Auto-add settings, and the Blocked list. That list is passive, built from what the device overhears; to go looking for nodes actively, see [Discover](#discover). Auto-add can pull new nodes in from their adverts, or you can add them by hand.

Contacts

<a id="map"></a>

## Map

An OpenStreetMap / OpenTopoMap view with your position and your contacts plotted as markers, plus link lines from you to each node. Tiles are fetched over Wi-Fi through the wadamesh proxy and cached to flash (or microSD), so areas you've viewed work offline. Coloured dots along a route you have walked or driven come from [wardriving](#discover).

It also has a line-of-sight analyzer: pick a contact and it uses SRTM elevation to show whether terrain blocks the path.

Map

### Bring your own tiles (offline maps)

You don't need Wi-Fi to see the map. Copy a standard OpenStreetMap tile pack onto the T-Deck's microSD card and the map reads straight off the card, fully offline.

**Where they go.** Use the usual slippy-map `z/x/y` folder layout, in either of these locations on the card:

- `/maps/osm/<z>/<x>/<y>.png` — the Meshtastic / MeshCore standard layout (recommended, so packs are interchangeable between apps).
- `/tiles/<z>/<x>/<y>.png` — also read. This is the same folder the Wi-Fi download cache uses, so your own tiles and downloaded ones live side by side.

**Format.** 256×256 `.png` tiles (`.jpg` also works). `z` is the zoom level and `x`/`y` the tile column/row at that zoom — the same scheme OSM's own tile server and every XYZ tile downloader use, so most ready-made packs drop in unchanged.

**Turn it on.** Open the map's options (the gear button on the map) and switch on **Tiles from SD card**; the status line confirms `Map tiles: microSD`. Turn it back off to fetch live tiles over Wi-Fi again.

**Making a pack.** Any XYZ / slippy-map tile downloader works — point it at OpenStreetMap for your area and a sensible zoom range, then copy the resulting `z/x/y` folders onto the card. Each extra zoom level roughly quadruples the tile count, so grab only the levels you'll actually use.

**Board differences**

The "Tiles from SD card" toggle is a T-Deck feature. The Heltec V4 has no microSD slot, so it caches downloaded tiles to a small internal partition (no offline packs there). On the Tanmatsu, tiles — your own or downloaded — live at `/tiles/<z>/<x>/<y>.jpg` on the microSD card; drop JPEG tiles there and they show alongside the downloaded ones.

<a id="discover"></a>

## Discover & wardriving

**Discover** is an app in the home launcher. It does something the rest of the firmware never does: instead of waiting to overhear other nodes, it **asks**. It broadcasts a request that neighbouring repeaters answer directly, so anything that replies is provably reachable from exactly where you are standing.

**Two similar names, two different things.** The **Discovered** list in the Contacts overflow menu is passive: nodes you happened to overhear, waiting to be added. The **Discover app** is active: it transmits a request and lists who answers. This section is about the app.

### What you see

A live list of everything answering right now, strongest signal first. Each row shows the node's name (or `Node ·A1B2` from its key if it is not a contact yet), its signal, how many hops away it is, and how long ago it replied. **Tap a row to add that node as a contact.**

The list is live rather than historical: a node that stops answering for more than about eight seconds drops off it. That is the point, because the question being answered is "what can I reach from here, now".

### Wardriving: mapping your coverage

If the device has a **GPS fix**, Discover starts recording while it scans. There is nothing to switch on. Every time you move roughly 15 metres, or every 20 seconds if you stay put, it takes a sample: your position, plus every node answering at that moment.

Because a reply proves reachability from that spot, the trail of samples is a map of **your own radio coverage**. Open the **Map** tab and the samples are drawn as coloured dots along the route you walked or drove: **green** where the best signal was strong, **red** where it was weak.

The footer of the Discover app tells you what is happening: `Wardrive: 42 coverage pts · 137 logged to SD`, or `waiting for GPS fix` if there is no position yet.

### How to do a wardrive

1. Put a **microSD card** in, if your board takes one. Without it the map still works, but nothing survives a reboot.
2. Wait for a **GPS fix**. The footer says so plainly when it is still waiting.
3. Open **Discover** from the home launcher and leave it open.
4. Go for a walk or a drive. Samples are taken automatically as you move.
5. Open the **Map** tab to see the coverage dots along your route.

Discover keeps transmitting while it is open, so it uses more power and more airtime than sitting idle. It is a tool to use deliberately for a while, not something to leave running all day.

### The log file

Every sighting is appended to `/meshcomod/discover/wardrive.csv` on the card, one row per node per sample:

```
epoch,lat,lon,type,pubkey,rssi,snr,hops
```

That is a plain CSV, so it opens in any spreadsheet or mapping tool. It is the only part of a wardrive that survives a reboot: the coloured dots on the map are held in memory and the newest 160 samples are kept, so a long drive keeps the most recent stretch rather than the whole thing.

There is **no on-device viewer** for the log. You can see the file in the Files app, but reading the sightings back means taking the card to a computer.

<a id="settings"></a>

## Settings

A tile for each category. Tap one to open its page; the title bar carries a back chevron. The full set is in [Settings pages](#settings-pages) below.

- **Profile** — your name and node identity.
- **Radio & Mesh** — frequency, spreading factor, region scope, high-gain receiver, signal probe, auto-advert.
- **Auto-add, Wi-Fi, Bluetooth, GPS** — discovery and connectivity.
- **Clock, Battery, Sensors, Display, Keyboard, Sound, Quick replies, Lock screen** — the device experience.
- **Language** (twelve UI languages), **Backups**, and **About** (firmware version, update check, diagnostics).
Settings categories

<a id="companion"></a>

## Companion mode

WADAMESH runs everything on its own, but it can also be the radio for the **official MeshCore app** on your phone (iOS and Android). Your phone brings the big screen and full keyboard; the device does the LoRa. Best of all it works **alongside** the on-device UI — you can keep using the touch screen while your phone is connected, and your contacts, channels and messages stay in sync between them.

### Three ways to connect

- **USB** — plug the device into an Android phone or a computer; the app connects over the cable. Nothing to set up on the device.
- **Bluetooth** — turn on Bluetooth in **Settings → Bluetooth**. The device appears in the app as `MeshCore-<your node name>`; pair with the 6-digit code (default `123456`, changeable on the same page).
- **Wi-Fi** — put the device on your network in **Settings → Wi-Fi**. On the same Wi-Fi, the app connects to the device's IP address on port `5000` (the Wi-Fi page shows the IP).

**Run Bluetooth and Wi-Fi together only when you need to**

Bluetooth and Wi-Fi can be on at the same time, but each one reserves a good chunk of RAM. On the Heltec V4 especially — it has the least free memory of the three boards — keeping both on at once leaves little headroom and can lead to instability or odd behaviour. If you only want the phone app, pick one transport (Bluetooth or Wi-Fi) and leave the other off; if you mostly use the device on its own, keep both off.

Bluetooth — pairing code

<a id="remote"></a>

## Remote & web app

Over Wi-Fi, a WADAMESH device can put its screen — or a whole browser app — on your phone or laptop. There is nothing to install: the device serves a web page you open in any browser on the same network. Turn it on from **Home → Remote** (or the Remote screen in Settings), pick a mode, and open the address it shows.

### Three ways to connect

- **Remote app** (recommended) — a full control panel, native to the browser: **Chats**, **Contacts**, a **Terminal** and **Settings**, with live message notifications. Run the device entirely from your phone.
- **Screen mirror (VNC)** — streams the device's live screen to the browser and sends your taps and key presses back, so you see and drive exactly what is on the device, in real time.
- **Remote UI** — renders the interface off-screen at desktop size (a wide landscape layout) and streams that instead of the small panel, for a roomier view on a big screen.

Screen mirror and Remote UI both show the same interface you already know from the device — mirrored, or drawn larger. The **Remote app** is different: a browser-native panel built for a phone, shown below.

### Turning it on

Connect the device to Wi-Fi first, then open **Home → Remote**. The screen shows the address to type into your browser (like `http://192.168.1.42`) and a switch for each mode. Only one streaming mode runs at a time, and the phone and the device must be on the same Wi-Fi network.

Remote — turn it on, then open the addressThe remote app in a browser

The remote app is phone-first. It carries the same status icons as the device (signal, Wi-Fi, Bluetooth, battery), pops up a notification when a message arrives, and refreshes an open chat on its own as replies come in — so you rarely need to touch the device itself.

Chats — DMs, channels, unread badges

A conversation, with delivery ticks

Contacts — search, sort, filter

Tap a contact for actions

Discovered nodes — add with a tap

Settings — including live radio

Terminal — the MeshCore CLI

<a id="web"></a>

## Web browser

WADAMESH can read the web on its own screen. Open the **Web** app from the home launcher, type an address, and the device fetches the page and shows it as plain, readable text — articles, wikis, docs, simple sites. There is no proxy in the middle and nothing is tracked: it pulls the page straight from the site and strips it down to the words.

- **Text only** — no JavaScript, no images, no ads. Pages load small and read clean.
- **Tappable links** — follow a link to the next page, with **back**, **forward** and **refresh** in the toolbar.
- **Direct & private** — the device connects to the site itself over Wi-Fi (HTTPS); no server of ours sees what you read.

Reading the web on-device takes more memory than the smallest board carries, so the Web app is on the **T-Deck** and **Tanmatsu**. On the 2 MB Heltec V4 it is hidden — that board can't complete the secure handshake — but you can still bounce any link to your phone (below).

Web — open an address, read it as textLinks in chat

A link that arrives in a message is now tappable. Tap it for a small menu: **Open in web** loads the page in the on-device browser (T-Deck and Tanmatsu), and **Create QR** puts a scannable QR code on screen so you can open the link on your phone. Create QR works on *every* board, including the Heltec V4 — so even where the browser isn't available, a link is one tap from your phone.

Tap a link — Open in web, or Create QRCreate QR — scan to open on your phone

<a id="updates"></a>

## Updates & test builds

On the boards that update over Wi-Fi (the Heltec V4 and the standalone T-Deck), WADAMESH checks for new firmware on its own. When one is available, **Settings → About** shows it and you can flash it straight from the device with **Install update**, no computer needed. (Under Launcher and on the Tanmatsu you update through Launcher or the app store instead.)

### Two channels: Stable and Test

- **Stable** is the default. It is the build that has been tested and is recommended for everyday use. Leave your device on this unless you want to help test.
- **Test (beta)** gets new features and fixes earlier, before they are proven. It can be rough, so it is meant for trying things early and reporting bugs, not for a device you rely on.

### Turn on test builds

Flip **Settings → About → "Get test builds (beta)"** on. From then on, both the update check and **Install update** follow the test channel, so the device offers the newest test build over Wi-Fi. Turn the switch back off to return to stable. You can switch either way at any time.

Prefer to start on a test build from scratch? Pick **Beta · testing** when you flash from [flasher.wadamesh.com](https://flasher.wadamesh.com).

The About page

<a id="settings-pages"></a>

## Settings pages

WADAMESH splits its settings into nineteen pages. Here is what each one does and the controls you will find on it. Open any page from the Settings tab; a back chevron in the title bar returns you to the grid.

Identity & radio

### Profile

Who you are on the mesh.

- **Node name** — your callsign on the network
- **Advert location** plus a share-in-advert toggle
- **Identity key** and **Share QR** to hand out your contact
- **Export / Import** your whole setup as a JSON backup

### Radio & Mesh

Every LoRa and mesh parameter, split into RADIO / MESH / SIGNAL.

- **Radio** — preset, frequency, bandwidth, spreading factor, coding rate, TX power, with a live duty-cycle and airtime-per-packet readout
- **Mesh** — answer-telemetry, path-hash size, region scope tag, scope direct messages
- **Signal** — high-gain receiver (Heltec V4.3), signal probe, and a shortcut to the Advertise app
- **Experimental** — multi-ACKs, client repeat, RX boost, duty meter

### Auto-add

Which nodes join Contacts automatically from their adverts.

- Per-type toggles: **chat**, **repeater**, **room**, **sensor**
- **Overwrite oldest** when the contact list is full
- **Notify** when a new contact is added
- **Max hops** filter for how far away a node can be
Connectivity

### Wi-Fi

For map tiles, clock sync and update checks.

- On / off with a live status line (IP, signal)
- **Scan** and tap an SSID, or type one in by hand
- **Save & reconnect** applies live — no reboot
- Saved network slots for one-tap quick-connect

### Bluetooth

Pair with the MeshCore phone app over BLE.

- **Enable Bluetooth** with a live mode status line
- Set a 6-digit **pairing code** (applies after a reboot)

### GPS

The on-board GPS receiver.

- On / off with a live **fix status** (acquiring, 2D/3D, age)
- **Serial baud** rate (applies after a reboot)
- Reads "no GPS module" on devices without one

### MQTT bridge experimental

Forward received messages to your own MQTT broker.

- Publishes the **text, sender and time** of messages your node hears to an MQTT broker
- Broker **host, port and topic**, plus an **encryption key**
- Stays off until you **accept the privacy note** — anyone who can read the broker sees the messages, so use one you control, never a public broker
Time & power

### Clock & time

Keep the clock right.

- **Sync clock** from the system / NTP
- **Time zone** picker, or a custom UTC offset with steppers
- **12-hour clock** toggle

### Battery

Power readouts and tuning.

- **Battery & power history** chart
- **Calibrate** — tap to set 100% at the current voltage
- **Battery saver** throttles the CPU when idle (T-Deck)

### Sensors Heltec V4

For the V4 Expansion Kit.

- **Expansion Kit** configuration
- **Show Sensors tab** in the bottom bar (applies after restart)
Experience

### Display

Look and feel.

- **Screen timeout** before it sleeps
- **UI size** — Normal / Large / Huge
- **Colourful chat bubbles**, distance in miles, hide device name
- **Theme colour** picker

### Keyboard

Typing and on-device input.

- **Secondary layouts** — Cyrillic, Greek, Arabic, French, German and more
- **Accent popups** — long-press for accented letters
- **Enter sends** the message (T-Deck)
- **Flash** the screen and keys on a new message (T-Deck)

### Sound

Notification audio.

- Master sound plus per-type toggles: **message**, **DM**, **@mention**
- Custom **sound files** from microSD where supported
- **Volume** control (T-Deck / Tanmatsu)

### Quick replies

One-tap canned messages.

- Edit **six** short replies
- Pick them from the composer to send instantly

### Lock screen T-Deck

A simple lock when idle.

- **Wallpaper** picker and **lock text colour**
- **Lock when screen off**, then hold to unlock
SystemScreenshot pending

### General

Device housekeeping.

- **Send advert now**
- **Store data on SD** (reboot)
- **Run setup again** from scratch
- **Reboot device**

### Backups

Save and restore your whole config.

- **Export** a new JSON backup
- Restore from the **saved list**
- **Factory reset** in the danger zone

### Language

Twelve UI languages.

- Tap a language; the device reboots to apply

### About

Version, updates and diagnostics.

- **Firmware version** plus update check / install
- **Get test builds (beta)** switch: opt into the [test channel](#updates)
- Full **system diagnostics** — uptime, chip, memory, flash, storage
- **Device ID** and crash-report export

<a id="apps"></a>

## Apps

Tools from the home launcher. **RF Monitor** is a repeater-style view of recent packets and radio stats; **Spectrum** sweeps the band so you can spot interference or find a clear frequency.

### Writing your own

The Apps drawer is open. Snake, RF Monitor and Airtime are ordinary Lua apps downloaded from the store over Wi-Fi, and anyone can submit one — they are not built into the firmware and they get no privileges yours would not have.

An app is a single Lua file plus a one-line manifest, talking to the firmware through the `wada.*` API. Submit it as a pull request to [the repository](https://github.com/ALLFATHER-BV/wadamesh), or open an issue with the file attached if you would rather not use git. The full procedure — file layout, versioning, and what the review looks for — is in [deploy/apps/README.md](https://github.com/ALLFATHER-BV/wadamesh/blob/main/deploy/apps/README.md).

**Every submission is reviewed and safety-checked before it is added.** An app runs on other people's radios, so each one is read for anything touching the node identity, keys or channel secrets, for flash-write patterns that can stall the device, and for anything that would block the UI or the mesh. If something needs changing we say what and why, and we would rather help you land it than turn it away.

RF MonitorSpectrum analyzer

<a id="translations"></a>

## Translating WADAMESH

The interface ships in thirteen languages besides English, and every one of them was written by somebody who uses the device. Corrections and new languages are welcome. This section is the whole procedure, because getting it wrong is easy and the failure is quiet.

### Edit one file, and only that file

The canonical source for a language is:

```
deploy/apps/lang/<code>.lang
```

for example `hu.lang`, `de.lang`, `nl.lang`. It is a plain UTF-8 text file, one string per line, with the English original and your translation separated by a **single TAB**:

```
Contacts	Névjegyek
No SD card	Nincs SD-kártya
```

A short header at the top carries the language code, its name as it appears in the picker, and a version number.

**Do not edit `src/ui-touch/i18n_builtin.h`.** It is a *generated* file and says so on its first line. It is rebuilt from the `.lang` files by a script, so anything typed into it directly is thrown away the next time anyone regenerates it — and it never reaches the language Store either, so devices that download their language would keep the old text while devices using the built-in copy showed the new. Two sources of truth, silently disagreeing. Put the work in the `.lang` file and it lands in both.

### Rules that keep a language file safe

- **The English side is a key, not a label.** It has to match the firmware's string exactly — every character, including the ellipsis `…` where the original uses one rather than three dots. A key that does not match is simply never used, and nothing warns you.
- **Placeholders must survive.** If the English contains `%s`, `%d` or `%u`, your line needs the same ones, the same number of times, in an order that still makes sense. The firmware checks this and *ignores* a line whose placeholders do not match, because a mismatch there can crash the device.
- **A newline is written `\n`**, two characters, exactly as in the English. A real line break would end the entry.
- **No tab characters inside a translation** — the tab is the separator.
- **Leave the technical strings alone.** Units and protocol terms (`SF`, `CR`, `MHz`, `dBm`, `GPS`, `Wi-Fi`, `Bluetooth`) and the product name are meant to stay as they are.
- **Watch the length.** Buttons and list rows are sized for the English. A translation twice as long will be cut off on a small screen — prefer the shorter phrasing where there is a choice.

### How to send it

A pull request that changes only `deploy/apps/lang/<code>.lang` is the easiest thing to review and merge. If you would rather not use git, open an issue and attach the file, or paste the lines you have changed — that is just as welcome, and we will do the rest.

What happens on our side: the version number is bumped, the file is republished to the language Store so existing devices are offered the update, and the compiled-in table is regenerated so a fresh install has it too. You do not need to do any of that.

### Starting a new language

Copy an existing file, change the header's code and name, and translate as much as you have patience for. **Partial is fine.** Any line you leave out falls back to English, so a half-finished language is genuinely useful on day one and can be filled in over time.

### Finding what still needs doing

Strings are added and reworded as the firmware grows, so a language drifts. Open an issue asking for the current gaps for your language and we will generate the list: exactly which strings are missing, and which are still sitting in English. It is usually a couple of dozen lines rather than a thousand.

One thing worth knowing when a translation you wrote seems to have disappeared: if the English wording itself was changed — say a setting was renamed to be more specific — the old key no longer matches anything, and the translation attached to it goes unused. It has not been deleted or rejected. It just needs re-pointing at the new English, which is a good moment to check the meaning still fits.
