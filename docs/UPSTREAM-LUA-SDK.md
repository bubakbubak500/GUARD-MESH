> Imported upstream reference (2026-09-19), text only. Describes WadaMesh; features and upstream service URLs have not yet been adapted for Guardian.

# Lua app SDK

WADAMESH runs small Lua apps. They live on the device as a single `.lua` file, appear in the app drawer next to the built-ins, and can be installed and updated from the Store over the air.

API v1 Lua 5.4

<a id="intro"></a>

## What this is

An app is one Lua chunk. The firmware gives it a screen, an input stream, read access to the mesh, a little key/value storage and an HTTP fetch. That is deliberately the whole surface — roughly thirty functions, all under the `wada.` table.

LVGL is **not** exposed. Binding a UI toolkit wholesale would freeze us out of ever changing it, and would hand every app enough rope to wedge the device. Instead there is a small set of widgets we own and can keep stable across firmware versions.

Two of the apps that ship in the Store — **RF Monitor** and **Airtime** — are written against exactly this API and nothing else. They are the reference implementations; read them if a doc paragraph is ambiguous.

**An app cannot put packets on the air behind your back.** Reading the mesh — contacts, the packet log, radio statistics — is free. Transmitting is not: `wada.mesh.send` exists, but the first call from a given app stops and asks you, by name, and a refusal is remembered. Same for reading your incoming messages. See [Permissions](#perms).

<a id="hello"></a>

## Your first app

Save this as `hello.lua`:

```
local app = {}
local status
local n = 0

function app.on_open(w, h)
  wada.ui.label("Hello from Lua", 6, 4, 16, wada.ui.colors.text)
  status = wada.ui.label("tick 0", 6, 26, 12, wada.ui.colors.sub)
  wada.timer.every(1000)
end

function app.on_tick()
  n = n + 1
  status:set("tick " .. n)
end

function app.on_input(ev)
  if ev.type == "key" then
    wada.sys.toast("you pressed " .. tostring(ev.key))
  end
end

return app
```

**The last line matters.** An app is a table you build and `return`; the firmware calls the `on_*` fields on that table. Callbacks defined as plain globals are never found, so the app opens to a blank page with no error. If nothing happens, check for `return app` first.

Copy it to `/apps/hello.lua` on the device's SD card (or internal storage on boards without a card), open the app drawer, and it is there. No manifest is required for a file you side-load yourself — see [Install yours](#install).

<a id="lifecycle"></a>

## App lifecycle

Build a table, put the callbacks you need on it, and `return` it from the file. All of them are optional.

| Callback | When it runs |
| --- | --- |
| app.on\_open(w, h) | Once, when the app opens. Build your UI here. `w` / `h` are the usable body size in pixels — use them instead of assuming a screen size, since the boards differ. |
| app.on\_tick() | On the cadence set by `wada.timer.every(ms)`. Never faster than 33 ms. |
| app.on\_input(ev) | For every touch, key or trackball event. See [wada.input](#input). |
| app.on\_message(m) | An incoming message. `m.kind` is `"channel"`, `"dm"` or `"room"`, alongside `m.channel`, `m.sender` and `m.text`. Channel traffic needs the channel-read permission; DMs and room posts need the private-read one. See [Permissions](#perms). |
| app.on\_close() | Once, when the app closes. Persist anything you care about here. |

Every callback runs inside a guarded call on the UI thread. If your code raises an error the app is closed with a toast rather than taking the firmware down with it — but it does mean a slow callback stalls the whole UI, so keep them short. There is an [instruction budget](#budget) on every callback, so an app that spins forever is stopped rather than taking the device with it.

Widgets you create in `on_open` are destroyed for you when the app closes. You do not free anything.

<a id="ui"></a>

## wada.ui

**Colours.** `wada.ui.colors` carries the firmware's palette: `bg` (the page ground — black, the same as the rest of the interface), `panel` (a raised surface to lift a card or an instrument off that ground), `text`, `sub`, `accent`, `good`, `bad`. Use them rather than your own hex so an app looks like part of the device.

Widgets are created in order, top to bottom, on the app's page. Every constructor returns a handle with methods you can call later.

| Call | Returns / does |
| --- | --- |
| wada.ui.label(text, x, y, size, color) | A text line placed at `x, y`. `size` is a class, not a pixel height: 12, 14 or 16. `color` takes a `wada.ui.colors` value. |
| wada.ui.button(text, x, y, w, h, fn) | A tappable button at `x, y` sized `w × h`; `fn` is called with no arguments. |
| wada.ui.text\_w(text [, size]) | Rendered width of `text` in pixels, for the given size class. |
| wada.ui.text\_lines(text, width [, size]) | How many lines `text` wraps to inside `width`. Counts explicit newlines too. **Use this when you lay out your own rows.** `label:width()` turns wrapping on, so a line longer than the screen becomes two while a fixed `y` step still advances by one, and the next row is drawn on top of it. This is the single most common way an app's layout goes wrong; it happened to the SDK Test app's channel list. |
| wada.ui.clear() | Remove every widget from the app's page, leaving the page itself. This is how you build an app with more than one screen: clear, then create the next screen's widgets in the same place you built the first. **Any handle you still hold from before the clear is dead** — calling a method on it does nothing rather than crashing, but it will not come back, so re-create and re-assign. Older firmware has no `ui.clear`: write `local clear = ui.clear or function() end` if you need to run on both. |
| wada.ui.list(x, y, w, h) | A scrollable list of selectable rows — the "pick one of N" widget. Methods: `add(text [, fn])` returning the row index, `set(i, text)`, `color(i, c)`, `select(i)` (highlights *and* scrolls it into view), `selected()`, `count()`, `clear()`, `pos(x, y)`. Rows are real buttons, so keyboard and trackball navigation walks them on touchless boards without you doing anything. |
| wada.ui.input(title, initial, cb) | A modal text field. `cb(text)` on OK, `cb(nil)` on cancel or on the dialog being closed any other way — exactly one call, always, so an app that disabled itself while waiting always gets to re-enable. Uses the firmware's own dialog and keyboard, so it behaves identically on a touchscreen, on the Tanmatsu's physical keyboard and on a trackball board. One prompt at a time. |
| wada.ui.canvas(w, h) | A drawing surface. See the canvas methods below. |
| wada.ui.chart(points) | A line chart sized to the page. The Airtime and RF Monitor primitive. |
| wada.ui.scroll(on) | Allow the page to scroll when content is taller than the screen. |
| wada.ui.text\_h(size) | Line height in pixels for a size class — use it to lay out a canvas. |
| wada.ui.colors | Theme table: `text`, `sub`, `bg`, `accent`, `good`, `bad`. |

### Label handle

|  |  |
| --- | --- |
| lbl:set(text) | Replace the text. |
| lbl:color(c) | Set the colour, e.g. `wada.ui.colors.accent`. |
| lbl:pos(x, y) | Place it explicitly instead of in flow. |
| lbl:width(px, [align]) | Fix the width; longer text wraps instead of running off the panel. Optional `"center"` / `"right"` aligns the text inside that width (default left). |

### Canvas handle

|  |  |
| --- | --- |
| cv:fill(c) | Flood the whole canvas. |
| cv:rect(x, y, w, h, c) | Filled rectangle. |
| cv:line(x1, y1, x2, y2, c) | Line. |
| cv:circle(x, y, r, c) | Filled circle. |
| cv:text(x, y, s, c, size) | Draw a string. |
| cv:pos(x, y) | Move the canvas itself. |

### Chart handle

|  |  |
| --- | --- |
| ch:push(v) | Append a point, scrolling the series. |
| ch:fill(t) | Replace every point from a table. |
| ch:range(min, max) | Fix the Y range instead of autoscaling. |
| ch:axis(ticks, gutter) | Draw Y-axis labels; `gutter` reserves space for them. |
| ch:pos(x, y) | Move the chart. |

Screens differ a lot — 240 px portrait on a Heltec V4, 320 px landscape on a T-Deck, and a tall high-DPI panel on a T-Display P4. Ask `wada.sys.board()` for the real width and height rather than hardcoding a layout, and prefer `wada.ui.text_h()` over assuming a font is so many pixels tall.

<a id="input"></a>

## wada.input

Input arrives through `on_input(ev)`. The event is a table; `ev.type` tells you which kind it is.

| ev.type | Fields |
| --- | --- |
| "touch" | `ev.x`, `ev.y` in page coordinates. |
| "key" | `ev.key` — a one-character string for printable keys (`"w"`), or a name for the rest: `up`, `down`, `left`, `right`, `enter`, `esc`, `backspace`. `ev.code` carries the raw value. Boards with a keyboard only; the key that closes the app is never delivered, so an app cannot trap you inside it. |
| "dir" | `ev.dir` — one of `up`, `down`, `left`, `right`, `select`. Trackball, D-pad and swipes all arrive here. |

Handle `"dir"` if you want your app to work on every board. A touch-only board sends swipes as directions, and a keyboard board sends its navigation keys the same way, so one branch covers both.

<a id="mesh"></a>

## wada.mesh

|  |  |
| --- | --- |
| wada.mesh.contacts([offset], [limit]) | Array of `{name, pubkey, type, ago_s, lat, lon, lat_e6, lon_e6}`. `pubkey` is the first 4 bytes as 8 hex characters — the same short form the rest of the interface uses, and what lines a contact up with a discovery hit. Returns at most `limit` entries (default 100, maximum 250) starting at `offset`, because every entry is an eight-field table and building thousands in one call would exhaust the app heap. On a device with more contacts than that, page through them: ask for 100, then 100 from offset 100, and so on. |
| wada.mesh.contact\_count() | How many contacts the device holds, so you can page through `contacts()` without calling it repeatedly to find where the list ends. |
| wada.mesh.rx\_log() | Recent packets, newest first: `{ago_ms, type, rssi, snr, hops, route, len}`, plus whatever identity the frame actually carried — see below. The RF Monitor feed. |
| wada.mesh.stats() | `{rssi, noise, rx_air_s, tx_air_s, rx_pkts, rx_err, tx_budget_ms, rx_events, rx_dropped, tx_pkts, freq, bw, sf, duty_pct}`. |
| wada.mesh.self() | This node: `{name, pubkey, lat, lon, lat_e6, lon_e6}`. |
| wada.mesh.discovered() | Everything that has answered a probe this session: `{pubkey, name, type, rssi, snr, their_snr, hops, direct, first_ms_ago, last_ms_ago, heard}`. `name` is present only if the responder is already one of your contacts. See [discovery](#discover). |
| wada.mesh.discover\_clear() | Forget every hit so far. A survey calls this between locations, or the next sample inherits nodes you heard a street back. |
| wada.mesh.discover([types]) ext | Broadcast a discovery probe. Returns the scan tag, or `false, err`. Needs the **discovery-probe** permission and has a 15 second floor. See [discovery](#discover). |
| wada.mesh.send(channel, text) ext | Post to a channel by name, public or private. Returns `ok, err`. Needs the channel-send permission; the first call prompts the user. `err` is `"permission denied"` if they refused, `"too fast"` inside the 5 second floor, `"bad length"` past 180 characters. On success the second return is instead a **fingerprint** for the message just sent, to pass to `wada.mesh.repeats()`. |
| wada.mesh.repeats(fp) | How many repeaters have been heard rebroadcasting the flood identified by `fp`, the second value a successful `wada.mesh.send()` returns. The same count the sent bubble shows beside its refresh glyph. It grows for a while after the send as repeats arrive, so poll it rather than reading it once; a direct message is not flooded and has no repeats to hear. Returns 0 for an unknown or expired fingerprint. |
| wada.mesh.send\_dm(to, text) ext | Send a direct message, or post to a room server. `to` is a contact **name** as it appears in `wada.mesh.contacts()`. A room is a contact too, so one call covers both and the second return is `"sent"` or `"room"`. Needs the **private** send permission, which is separate from channels. |
| wada.mesh.channels() | Array of the channel names configured on this device, so you can find the private ones rather than being told their names. Names only: the channel secret is never exposed to Lua. |

These are snapshots taken when you call them, not live views — call again on each tick to refresh.

### Who sent a packet

MeshCore only puts a real identity on the wire in one place: an **advert**, whose payload opens with the sender's public key. Those entries in `rx_log()` carry `pubkey`, 8 hex characters, and they are the ones worth logging — an advert both names a node and proves it was audible from where you are.

Addressed frames (text, requests, responses, path replies) carry one-byte destination and source hashes instead, exposed as `dst` and `src`, two hex characters each. Two characters collide easily, so treat them as a hint and never as an identity. Everything else is anonymous on the wire and gets neither field. Nothing here is inferred: if a field is absent, the frame did not contain it.

<a id="node-types"></a>

### Node types

The `type` field everywhere, and the filter `discover()` takes, use these. They are on the `wada.mesh` table so you never have to hardcode the wire numbers.

|  |  |
| --- | --- |
| wada.mesh.NODE\_CHAT | `1` — a companion / chat node. |
| wada.mesh.NODE\_REPEATER | `2` |
| wada.mesh.NODE\_ROOM | `3` — a room server. |
| wada.mesh.NODE\_SENSOR | `4` |

<a id="discover"></a>

## Discovery, and writing a coverage survey

`wada.mesh.discover()` broadcasts a zero-hop request that every node in earshot answers. That is a different thing from listening, and the difference is the whole reason it exists: a reply **proves** the link works from exactly where you are standing. Listening only tells you what happened to transmit while you were there, so a node that stays quiet is indistinguishable from a node you cannot reach.

The call returns immediately. Replies arrive over the next few seconds and accumulate in `wada.mesh.discovered()`, so the shape of a survey loop is: probe, wait a few seconds, read, `discover_clear()`, move.

### Both directions of the link

Each hit carries `snr` (how well **you** heard **them**) and `their_snr` (how well **they** heard **you**, reported back inside the reply). These are rarely equal, because their antenna, height and transmit power are not yours. The asymmetry is the most useful thing a survey can record: *"I can hear the repeater but it cannot hear me"* is a different fact from *"no coverage"*, and no amount of passive listening will tell you which one you are looking at.

`hops` is `0` when the reply came straight back — `direct` is that same test as a boolean. A hit with `hops > 0` reached you through a repeater, so it says something about the mesh but nothing about your radio horizon.

### Cost, and the rate limit

One probe costs one transmission from you and **a reply from every node that hears it**. That is airtime spent across the whole neighbourhood, not just yours, which is why it sits behind its own permission rather than reusing the message-send one: making other people's radios transmit and speaking as the user are different impositions, and a survey app has no business acquiring the second. The firmware enforces a **15 second** minimum between probes per app; a faster call returns `false, "too fast"`. In practice sweep every 20 seconds, which is quicker than anyone drives out of a cell.

### Logging it

Lua here is built with 32-bit numbers, so `lat` and `lon` are **single-precision floats** — about a metre, fine to display and lossy to store. `sys.gps()`, `mesh.self()` and `mesh.contacts()` therefore also hand you `lat_e6` / `lon_e6`, the same reading as exact integers in micro-degrees. Write those. `sys.gps()` also gives `alt_m`, which matters more than people expect: height dominates LoRa range, and two samples a metre apart on the map can be a hilltop and a hollow.

`wada.fs` allows one write a second, so buffer a sweep's rows and append them as one block rather than a line at a time. `wada.fs.read(name, offset, len)` reads a window, so an append-only log may grow past the 32 KB single-read limit and still be readable in chunks.

There is a working example of all of this: the **Wardrive** app in the Store. It probes on a cadence, discards samples taken without a GPS fix rather than logging them at 0,0, and writes a CSV with both link directions per node.

<a id="timers"></a>

<a id="timer"></a>

## wada.timer

|  |  |
| --- | --- |
| wada.timer.every(ms) | Sets the period of `on_tick`. This is the app heartbeat and there is one of it. |
| wada.timer.every(ms, fn) | A **named** repeating timer calling `fn`. Returns a handle. |
| wada.timer.after(ms, fn) | Fires `fn` once. Returns a handle you can cancel before it fires. |
| wada.timer.stop() | Stops the `on_tick` heartbeat. |
| wada.timer.stop(handle) / handle:stop() | Stops that one timer. |

**Repeating timers pause while the display sleeps.** Nothing you draw is visible then, and an app polling a sensor would otherwise hold that hardware awake for a dark screen — so `on_tick` and repeating named timers stop being called and resume on wake. A one-shot from `timer.after` still fires: it is scheduled app logic, and skipping it would drop the work rather than defer it. Derive elapsed time from `wada.sys.millis()` rather than counting ticks and a sleep looks like one long frame.

An app used to get exactly one clock. Anything needing a second cadence — a slow poll beside a fast animation, a delayed retry, a timeout on a fetch — had to be a counter inside `on_tick`, which is both tedious and wrong when the tick period changes. Up to **eight** named timers per app; a ninth raises an error rather than quietly filling LVGL's timer list. Everything is stopped for you when the app closes.

## app.on\_packet

Declare `on_packet` and the firmware calls it once per frame the radio received, in arrival order, with the same table [`rx_log()`](#mesh) rows use.

```
function app.on_packet(p)
  if p.pubkey then adverts = adverts + 1 end   -- an advert: a real identity
  total = total + 1
end
```

Polling `rx_log()` on a tick samples a 16-deep ring, so in any real traffic an app sees a subset and cannot *count* anything. This delivers each frame exactly once. An app that does not declare `on_packet` costs nothing: the drain is only started for apps that asked for it.

Ungated, exactly like `rx_log()`: this is radio metadata about frames the device already received, not message content. Anything carrying content still arrives through `on_message` and its permissions.

<a id="map"></a>

## wada.map ext

The firmware's own slippy map, inside your app page: the same OpenStreetMap tiles, the same Web Mercator projection and the same on-disk cache the Map tab uses. Before this an app could draw geography only onto a bare canvas, with no basemap.

|  |  |
| --- | --- |
| wada.map.view(x, y, w, h) | Creates the view. **One per app** — it owns a pool of up to four decoded tiles, which is 512 KB of PSRAM, so opening them in a loop would be a straightforward way to exhaust the board. A second call errors. |
| map:center(lat, lon [, zoom]) | Moves the view and redraws. |
| map:zoom() / map:zoom(z) | Reads, or sets and redraws. 1–19. |
| map:marker(lat, lon [, color [, size]]) | A dot. Returns `false` if the point is outside the view, in which case nothing was drawn. |
| map:line(lat1, lon1, lat2, lon2 [, color [, width]]) | A segment between two coordinates. |
| map:clear() | Removes every marker and line. The basemap stays. |
| map:to\_screen(lat, lon) | `x, y` within the view. Deliberately not clamped, so you can tell that a point is off-view instead of finding everything piled on the edge. |
| map:to\_latlon(x, y) | The inverse — turn a tap into a coordinate. |
| map:tiles() | How many tiles the last redraw actually placed. **Check this.** Zero means nothing is cached for this area at this zoom, and an empty rectangle looks exactly like open water unless you say otherwise. |
| map:redraw() / map:close() | Force a redraw; dispose of the view early rather than waiting for the collector. |

Tiles come from whatever the device already has — the SD pack or the online cache — and a missing one is queued for download if Wi-Fi is up, exactly as on the Map tab. The view follows the user's night-mode setting rather than introducing a second one. The **Nearby** app in the Store is a worked example.

<a id="geo"></a>

## wada.geo

|  |  |
| --- | --- |
| wada.geo.distance(lat1, lon1, lat2, lon2) | Great-circle distance in **metres**. |
| wada.geo.bearing(lat1, lon1, lat2, lon2) | Initial true bearing, 0–360 degrees. |
| wada.geo.cardinal(degrees) | `"N"`, `"NE"`, … Eight points, not sixteen: more would be false precision on a bearing derived from consumer GPS. |

In C because it is not a convenience wrapper: haversine and the bearing formula are a dozen trig calls each, and an app doing them per contact per tick spends a real slice of its [instruction budget](#budget) on arithmetic the chip does in microseconds.

**This is not a compass.** `bearing()` is a true bearing from one coordinate to another — the direction to *steer*, not the direction the user is *facing*. Only the ThinkNode M9 carries a magnetometer, and even there the heading is the app's to derive — see [wada.sys.compass()](#sys). On every other board a magnetic heading is not available at any level of the stack: to point somebody at something you need their course over ground from successive GPS fixes (`sys.gps().course`), or a physical compass.

<a id="net"></a>

## wada.net

|  |  |
| --- | --- |
| wada.net.http\_get(url, cb [, max\_bytes]) | Fetch a URL. `cb(status, body)` runs when it lands, or with a negative status and `nil` on failure. |
| wada.net.http\_post(url, body [, content\_type], cb [, max\_reply]) | Upload. Any 2xx counts as success, so an endpoint answering 201 or 204 works. `content_type` defaults to `application/octet-stream`. This is how an app gets data **off** the device — a survey log, a sensor series, a webhook. |

`http://` only, both of them. On-device TLS is not workable at the heap these boards have left once Wi-Fi has associated, so there is no `https` to offer; put a proxy in front if you need it. One request in flight at a time per app.

The fetch is asynchronous and runs on the firmware's existing network worker, so it does not block the UI. Your callback runs on the UI thread once the body is in memory.

**Plain HTTP only, and that is not an oversight.** After Wi-Fi associates there is not enough free internal memory on the smaller boards for a TLS handshake — mbedTLS wants around 30 KB and roughly 5 KB is free. If you need an HTTPS source, put a small proxy in front of it, the way the map tiles do. Responses are capped (192 KB by default).

<a id="store"></a>

## wada.store

|  |  |
| --- | --- |
| wada.store.get(key) | Read a string, or `nil`. |
| wada.store.set(key, value) | Write a string. |

Keys are namespaced per app, so two apps cannot collide. Keep it small — the budget is about 2 KB per app, and the underlying store silently drops oversized values. High scores and settings, not logs.

Writes hit flash. Do them in `app.on_close()` or on a real user action, never on every tick.

<a id="fs"></a>

## wada.fs ext

A private folder per app, for things too big or too structured for [wada.store](#store) — a log, a track, a cache. Paths are plain names inside your own folder: an app cannot name a file outside it, and cannot see another app's files. `..` and absolute paths are rejected rather than sanitised.

| Call | Returns / does |
| --- | --- |
| wada.fs.read(name [, offset [, len]]) | Returns `data, total_size`, or `nil` if there is no such file. One read hands back at most 32 KB, but `offset` makes that a window rather than a ceiling: an append-only log may grow past it and be walked in chunks using the returned total. |
| wada.fs.write(name, data) | Replace a file. Returns `ok, err`. |
| wada.fs.append(name, data) | Append. Returns `ok, err`. |
| wada.fs.list() | Array of `{name, size}`. |
| wada.fs.remove(name) | Delete a file. |

Writes are rate-limited to roughly one per second and files are capped at 32 KB. A write inside the window returns `false, "too fast"` — that is the normal answer, not a failure, so check it. The limit is not arbitrary: on a board with no card this lands on the same internal flash the mesh uses, and an app looping on writes would stall the whole device.

<a id="audio"></a>

## wada.audio audio

Asynchronous audio playback from the current app's private storage. A plain name such as `track.wav` resolves inside the same app folder as [wada.fs](#fs), whether that folder lives on internal flash, SD, or SD\_MMC. Check `wada.sys.caps().audio` and `audio_wav` before using it.

| Call | Returns / does |
| --- | --- |
| wada.audio.play(name) | Starts or replaces playback and returns `true`, or `nil, error`. Plain names use app-private storage. On boards with `caps().audio_sd`, `sd:/Music/track.wav` reads directly from the physical card. |
| wada.audio.pause() | Pauses an active track. Returns whether the command was accepted. |
| wada.audio.resume() | Resumes a paused track. Returns whether the command was accepted. |
| wada.audio.stop() | Stops the active track. Returns whether the command was accepted. |
| wada.audio.status() | `{state, path, source, format, error}`. State is `stopped`, `playing`, `paused`, `ended`, or `error`; `error` is present only after a playback failure. |

Supported formats are PCM WAV (16-bit mono or stereo at 8–48 kHz) and MPEG Layer III MP3, including CBR, VBR, and ID3-tagged files. Stereo is mixed to mono on the device. Check `audio_wav` or `audio_mp3` rather than assuming a format exists on older firmware.

Playback follows the user's Sound switch and volume, never blocks the Lua callback, and stops automatically when the app closes. A second `play()` replaces the current track. Playlist ordering belongs to the app, so there are no ambiguous host-side `next()` or `previous()` calls.

Plain names follow the same sandbox rules as `wada.fs`. Explicit `sd:` paths follow [wada.sd](#sd) path validation. Expected errors include `bad path`, `no storage`, `no sd`, `not found`, `busy`, `muted`, and `unsupported format`.

## wada.sd sd\_list

Directory access to the physical SD card. This is separate from [wada.fs](#fs): it can inspect the card's directory tree, but cannot read file contents, write or rename anything. The one thing it can delete is Windows malware, and the firmware, not the app, decides what that is. Check `wada.sys.caps().sd_list` before using it, and `caps().sd_clean` before using `check`, `remove` or the paging arguments.

| Call | Returns / does |
| --- | --- |
| wada.sd.list([path [, start [, max]]]) | Returns an array of `{name, type, size, mtime}`, or `nil, error`. `path` defaults to `/`. `type` is `"file"` or `"dir"`; directories report size 0, and `mtime` is 0 when the filesystem has no timestamp. `start` (default 1) is the index of the first entry to return and `max` (1 to 192, default 192) the page size. sd\_clean |
| wada.sd.check(path) | Whether a file on the card is Windows malware: a reason string (`"autorun file"`, `"Windows program"`, `"Windows script"`, `"Windows shortcut"` or `"renamed Windows program"`), `false` when it is not one, or `nil, error`. sd\_clean |
| wada.sd.remove(path) | Deletes the file only if `check` would name it, and returns that reason. Anything else returns `nil, "not a threat"`, and directories are never removed. Ask the user before calling it. sd\_clean |

Paths are absolute from the card root and at most 191 bytes. Empty segments, trailing slashes, backslashes, `.` and `..` are rejected with `nil, "bad path"`. A missing or unreadable card returns `nil, "no sd"`; `"busy"` means another storage operation is changing the card lifecycle, so retry later. Missing paths and files used as directories return `"not found"` and `"not a directory"`; `check` and `remove` on a directory return `"not a file"`.

One call returns at most `max` entries. When more exist, the returned array also has `entries.truncated == true` and `entries.next`, the `start` of the next page. Every entry costs the card a file open, so a small page (the SD Scan app uses 24) keeps the screen responsive in a folder of map tiles. Listing may mount an inserted card through the firmware's existing SD lifecycle, but never formats or modifies it.

What counts as malware is fixed in firmware (`src/ui-touch/SdThreat.h`): a file named `autorun.inf`; a Windows program, script or shortcut by extension (`.exe .scr .com .pif .cpl .msi .msp .dll .jar`, `.bat .cmd .vbs .vbe .js .jse .wsf .wsh .hta .ps1 .reg .msc`, `.lnk .url .scf`); or any file with a real Windows program header, whatever its name. A mesh radio uses none of these, so an app can offer to remove them, but it can never use `remove` to delete map tiles, backups or anything else on the card. It exists for the SD-card worm some ThinkNode M9 cards shipped with.

<a id="sys"></a>

## wada.sys

|  |  |
| --- | --- |
| wada.sys.millis() | Milliseconds since boot. |
| wada.sys.board() | `{w, h, touch, keyboard, trackball, gps}` — size and capabilities. |
| wada.sys.toast(msg) | Brief on-screen message. |
| wada.sys.random(n) | Integer in 1..n. |
| wada.sys.epoch() | Unix time in seconds, or `nil` when the clock has not been set yet (no GPS fix and no NTP). Always handle the `nil`. |
| wada.sys.datetime() | `{year, month, day, hour, min, sec, wday}` in local time. `wday` is 0 for Sunday. |
| wada.sys.tr(s) | Translate `s` through the device's active language, using the same table the firmware's own interface uses. Returns `s` unchanged when there is no translation, so it is always safe to wrap a string. Add your keys to `deploy/apps/lang/<code>.lang` alongside the firmware's. Older firmware has no `sys.tr`: write `local tr = sys.tr or function(x) return x end` and call `tr()`. |
| wada.sys.beep() | Short beep on boards with a buzzer; silent elsewhere, and silent when the user has sound off. |
| wada.sys.caps() | `{sdk_ext, keyboard, touch, sd, sd_list, sd_clean, audio, audio_wav, audio_mp3, audio_sd, compass, accel, discover, input, rx_identity, list, packets, sensors, map, measure}`. Check `sdk_ext` before using anything marked ext, `sd_list` before using `wada.sd`, and `audio` plus the format flag before using `wada.audio`. `audio_sd` means direct `sd:` paths are available; plain audio names use app storage and do not require it. |
| wada.sys.battery() ext | `{mv, pct, charging}`. |
| wada.sys.env() sensors | `{temp_c, humidity, pressure_hpa, alt_m}`, or `nil`. A field is present only when the hardware actually reported it, so you can tell "no humidity sensor" from "0% humidity". Gated on `caps().sensors` — does this board *have* the sensor rail — and **not** on `sdk_ext`, which is a memory gate. Those are different questions: the plain Heltec V4 has the Expansion Kit but not the extended SDK, and most boards with the extended SDK have no sensors at all. |
| wada.sys.gps() ext | `{lat, lon, lat_e6, lon_e6, sats, alt_m, time, speed_kmh, course}`, or `nil` with no fix — which is the normal indoor case, so handle it. `lat_e6`/`lon_e6` are exact micro-degrees; `lat`/`lon` are single-precision floats, so [log the integers](#discover). `alt_m` is metres. `time` is satellite time, absent until the receiver has decoded the date. `speed_kmh` and `course` (degrees clockwise from north) appear only on boards whose GPS provider reports them, and `course` only while actually moving — a stationary receiver has no course, so it is absent rather than 0. Treat those last three as optional. |
| wada.sys.accel() accel | Only where `caps().accel` is true (ThinkNode M9 today). `{x, y, z}` acceleration in g, sensor frame, or `nil` when nothing fresh. Held still the magnitude is 1 and the axis pointing at the sky carries it — which is how an app works out which axis is which. Its real job is tilt: a magnetic heading taken from two axes is wrong by roughly 1.5° per degree of tilt at mid latitudes, because the field dips ~60° and tipping the device leaks that vertical field into the horizontal pair. |
| wada.sys.keep\_awake(on) | For an app that is *measuring* rather than showing: holds the screen on and keeps `on_tick` running. Released automatically when the app closes. Use it around a calibration or a capture, not for the whole app — ticks otherwise pause with the display. |
| wada.sys.compass() | Only where `caps().compass` is true (ThinkNode M9 today), and note that this one is **not** ext-gated: it rides the hardware flag, not the memory flag. `{x, y, z, ovfl}` magnetic field in Gauss in the sensor's own frame, **uncalibrated**, or `nil` when nothing fresh; `ovfl` is true when the chip flagged the sample as saturated (a magnet nearby), in which case show that rather than a heading. There is deliberately no `heading`: the board carries a hard-iron bias the user has to calibrate away (track per-axis min/max while they turn the device, subtract the midpoints), and the sensor-to-screen axis mapping is the app's to set. Then `math.atan(-fy, fx)` with `fx` along the screen's top edge and `fy` along its right edge gives the heading. The GPS Compass app in the Store is the worked example. |

## wada.timer

|  |  |
| --- | --- |
| wada.timer.every(ms) | Call `on_tick` every `ms`. Clamped to 33 ms minimum. |
| wada.timer.stop() | Stop ticking. |

One timer per app. Calling `every` again changes the interval rather than adding a second timer.

<a id="budget"></a>

## The instruction budget

Every callback runs under a cap of **100,000 Lua VM instructions**. Reaching it raises `instruction budget exceeded (app tick too long)`, which closes the app with a toast instead of letting it hold the UI thread forever.

| Question | Answer |
| --- | --- |
| What is counted? | **VM opcodes**, by Lua's own counter (`lua_sethook` with `LUA_MASKCOUNT`). Not native calls, not wall-clock time. |
| Per what? | **Per callback.** The counter is armed immediately before each call and cleared after, so `on_open`, every `on_tick`, every `on_input` and every `on_message` each get a fresh 100,000. It is not a session allowance you can exhaust. |
| Does `on_open` get more? | Yes, **5×** (500,000). Setting a UI up legitimately does more work than a tick. |
| Do `wada.*` calls count? | **No.** Time inside a native call costs wall-clock but not budget, because the VM is not executing opcodes. That cuts both ways: the budget will not save you from a slow host call, which is why `wada.fs` and the mesh sends carry their own rate limits instead. |

For scale, 100,000 instructions is well under 2 ms of straight-line Lua on an ESP32-S3, and a runaway loop is contained in about 30 ms. Ordinary per-tick app code is nowhere near it.

**If you are hitting it, you are probably doing cryptography.** A HMAC-SHA1 in pure Lua is 80 bit operations per 64-byte block with interpreter overhead on every one, and that genuinely exceeds 100,000 instructions. That is not a runaway and the answer is not a bigger budget: use [wada.crypto](#crypto), where the same work costs microseconds and a handful of opcodes. This section exists because a one-time-password app hit exactly this.

The other way to stay inside it is to do less per callback. State kept in your app table persists between ticks, so a long job can be split across several: do a slice of the work, return, and continue on the next `on_tick`.

<a id="crypto"></a>

## wada.crypto

Hashing in C, because hashing in Lua does not fit the instruction budget. HMAC-SHA1 is 80 bit operations per 64-byte block and the interpreter overhead multiplies every one, so a one-time-password app doing it by hand runs out of budget mid-callback. These call the mbedTLS the firmware already links: microseconds, and a handful of VM instructions.

| Call | Returns |
| --- | --- |
| wada.crypto.sha256(data) | 32-byte digest, as a binary string. |
| wada.crypto.sha1(data) | 20-byte digest. |
| wada.crypto.hmac\_sha256(key, msg) | 32-byte HMAC. |
| wada.crypto.hmac\_sha1(key, msg) | 20-byte HMAC, the RFC 4226 / 6238 one. |
| wada.crypto.hex(binary) | Lower-case hex of a binary string. |

Digests come back **raw**, not hex, so RFC 4226 dynamic truncation works on them directly. Lua strings are 8-bit clean, so binary keys and messages are fine as they are.

Available on **every** board, unlike the ext calls. This is pure computation over data your app already holds: it reads nothing of the user's and transmits nothing, so there is no reason for the small boards to go without it.

<a id="perms"></a>

## Permissions

Two things an app can ask for reach past its own window, so they are not granted by installing it:

| Permission | Unlocks |
| --- | --- |
| Post to channels as me | `wada.mesh.send`, posting to a channel in your name. |
| Read channel messages | `app.on_message` for `kind == "channel"`. |
| Send private messages as me | `wada.mesh.send_dm`, writing to one contact or posting to a room as you. |
| Read private messages | `app.on_message` for `kind == "dm"` and `"room"`. |
| Send discovery probes | `wada.mesh.discover`. Separate from the send permissions because it transmits nothing under your name but does make every node in range transmit a reply. |

Four rather than one because the risks genuinely differ: posting to a channel you are already in is not the same act as writing to one person as you, and channel traffic is not somebody's private conversations. They are separate: granting one does not grant another. The prompt names the app and appears on the first attempt, not at install time, so you are asked at the moment it is obvious what the app wants it for. A refusal is remembered — the app does not get to ask again on a loop — and the call simply returns `false, "denied"`, which a well-written app should handle rather than break on.

**Settings → App permissions** lists every installed app and what it holds, and revokes with one switch. Granting happens where an app asks; that page is for review and for taking it back.

**Write for the denial.** The user can say no, or revoke later. Treat a permitted send as the lucky path, not the assumption.

<a id="format"></a>

## App format

On the device an app is one or two files on the active storage root:

```
/apps/<id>.lua     the code
/apps/<id>.json    the manifest (optional for side-loaded apps)
```

The manifest is what the Store and the drawer read — one line, all values quoted strings:

```
{"id":"airtime","name":"Airtime","ver":"1.3","desc":"Duty cycle and airtime budget.","icon":"chart"}
```

|  |  |
| --- | --- |
| id | Lowercase, no spaces. Must match the filename. |
| name | Shown on the drawer tile and the Store card. |
| ver | `major.minor`, matching the version directory. Compared against the catalog to offer an update. |
| desc | One sentence, read in the Store before installing. |
| icon | Optional. Picks the drawer tile's glyph by name — an app cannot ship artwork, and an unknown name falls back to the generic app symbol. One of: `gps` / `compass` / `map`, `radio`, `signal`, `chart`, `list`, `message`, `person`, `group`, `bell`, `star`, `search`, `settings`, `battery`, `game`. |

Earlier versions of this page showed `version`, `min_api`, `description` and `boards`. The device has never parsed those — its manifest reader takes quoted strings only, and the keys above are the ones it looks for. Use `ver` and `desc`.

<a id="install"></a>

## Install your own

Drop a bare `.lua` file into `/apps/` on the SD card and it shows up in the drawer — no manifest needed. The filename becomes the name. That is the fast loop while you are writing something.

Apps you side-load appear in the Store under **Your own apps**, where you can remove them again. A long press on the drawer tile also offers to remove.

On boards without an SD card the same path lives on internal storage.

No card slot you can reach (the ThinkNode M9's card is soldered on)? Push the files over the USB serial console instead: `scripts/sideload_app.py --port /dev/cu.wchusbserial10 --reboot deploy/apps/<id>/<ver>` sends the `.lua` + `.json` through the firmware's `fput`/`fadd`/`fend` commands into the same `/apps/` the Store uses (`--dest /lang` for a language file). Needs pyserial.

<a id="publish"></a>

## Publish to the Store

The Store is served as static files, so publishing is a pull request against the firmware repository:

- Add `deploy/apps/<id>/<version>/<id>.lua` and the matching `.json`.
- Add an entry to `deploy/apps/apps.json`.

Version paths are immutable — publishing 1.1 never rewrites 1.0. Devices compare the catalog version against what they have installed and offer **Update** when they differ, so bumping the version in both places is the whole release process.

Apps in the catalog are reviewed before they are merged. Keep them small and keep them readable.

<a id="sandbox"></a>

## Sandbox limits

Apps run in a restricted environment. These are removed: `io`, `os`, `require`, `dofile`, and loading new chunks at runtime. Available: `math`, `string`, `table`, and the usual `pairs`, `ipairs`, `select`, `pcall`, `tostring`, `tonumber`.

Memory comes from a capped pool in PSRAM, so an app that allocates without bound fails its own allocation rather than starving the radio or the UI. There is an [instruction budget](#budget) on every callback for the same reason.

**The extended calls are not on every board.** Anything marked ext above — `wada.fs`, `wada.mesh.send`, `wada.mesh.send_dm`, `wada.mesh.discover`, `sys.battery`, `sys.gps` — needs a board with the memory to carry it, so the Heltec V4 keeps its RAM for the mesh instead. Call `wada.sys.caps().sdk_ext` and degrade gracefully rather than assuming. `wada.sd` additionally needs a physical card interface, reported by `caps().sd_list`. `wada.audio` instead follows `caps().audio`: storage may be internal, but the device still needs a stream-capable speaker path.

Rate limits are part of the contract, not a rainy-day guard: `wada.fs` writes are about one per second with a 32 KB file cap, and `wada.mesh.send` has a 5 second floor and a 180-character limit. They return `false, "too fast"` rather than throwing, and hitting them is expected — handle it.

None of this makes a hostile app safe, which is why the catalog is curated. It makes an *honest* app that has a bug survivable: it gets closed, and the device keeps carrying traffic.
