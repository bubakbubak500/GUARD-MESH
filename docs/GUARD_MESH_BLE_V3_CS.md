# Guard Mesh ↔ Guardian: BLE API v3

Implementováno na straně PC v **Guardianu 1.1.25 pro Windows a Linux**.
Toto je integrační smlouva pro samostatnou úpravu firmwaru Guard Mesh.
Firmware nebyl změněn. Zařízení musí implementovat níže uvedenou službu,
dekódování a vlastní obrazovky. Ukázkové pakety jsou v
[guard_mesh_ble_v3_vectors.json](guard_mesh_ble_v3_vectors.json).

## Dosavadní a nové API

v1 poskytuje krátký stav stanice. v2 přidává procenta přenosu a pět operací:

| Operace v2, dostupná také ve v3 | Co vystavuje |
|---|---|
| `status.get` | TX/RX, počty zpráv, stav připojení, průběh jednotlivých rádií |
| `contacts.list` | Živé a uložené kontakty, další hop a dostupnost |
| `messages.list` | Metadata zpráv v inbox/outbox/sent/draft/transit |
| `message.get` | Předmět a text zprávy po stránkách |
| `message.queue` | Trvalé vložení nové textové zprávy do Outboxu Guardianu |

Přesné parametry těchto operací zůstávají podle
[referenčního dokumentu v2](GUARD_MESH_BLE_V2_CS.md).
`message.queue` nadále používá běžnou rádiovou cestu Guardianu.

| Nová operace v3 | Účel |
|---|---|
| `capabilities.get` | Verze, limity, povolení ovládání, podporované a aktuálně dostupné povely pro každé rádio |
| `station.get` | Podrobný provozní stav obou rádií, modemů, řízení, přenosů, scanneru, kalibrace, schránky a serveru |
| `control.execute` | Jednoznačně vybraný provozní povel pro rádio 1 nebo 2 |
| `command.get` | Výsledek dříve přijatého povelu |
| `alert.types` | Stabilní kódy upozornění, lokalizované názvy a priority |
| `alert.publish` | Upozornění ze zařízení do zobrazení v Guardianu |
| `alerts.list` | Trvalá historie upozornění, nejnovější první |
| `alert.seen` | Potvrzení přečtení konkrétního upozornění |
| `events.list` | Obnova historie událostí po výpadku |
| `events.ack` | Potvrzení příjmu událostí pro dané zařízení |

Upozornění přijatá z rádiové sítě Guardianu i lokálně odeslaná z PC se
zpřístupňují zařízení. Upozornění přijatá přes `alert.publish` se uloží a
zobrazí na PC. **V této verzi se automaticky neposílají do RF ani do
MeshCore/LoRa.** `forwarded` a `mesh_forwarding` jsou `false`.

## BLE role, verze a charakteristiky

Guardian na PC je **Central / GATT Client**. Guard Mesh je **Peripheral /
GATT Server**. PC vyhledá inzerovanou službu, spáruje se, připojí a přečte
Protocol. FW musí chránit všechny charakteristiky šifrováním a bondem,
povolovat párování fyzicky na zařízení a nepřijímat zápisy od cizího klienta.
Windows používá backend WinRT, Linux BlueZ/D-Bus; párovací dialog spravuje OS.

| Protocol, přesně 4 bajty | Vyjednané API |
|---|---|
| `47 4d 01 00` | v1, původní stav |
| `47 4d 02 00` | v2, stav/progress/RPC |
| `47 4d 03 00` | v3, navíc stav stanice/ovládání/upozornění/události |

Guardian 1.1.25 podporuje všechny tři verze. Zařízení s v1/v2 nedostává nové
události ani provozní povely. API v3 vyžaduje aktualizovaný PC i FW; starší PC
v3 odmítne. Neúplná služba a neznámá verze se odmítnou.

Všechny UUID mají suffix `-b000-4042-ba50-05ca45bf8abc`:

| Prvek | Plné UUID | Vlastnosti | Směr |
|---|---|---|---|
| Služba | `f3641400-b000-4042-ba50-05ca45bf8abc` | Primary + advertising | — |
| Status | `f3641401-b000-4042-ba50-05ca45bf8abc` | Write with response | PC → FW |
| Protocol | `f3641402-b000-4042-ba50-05ca45bf8abc` | Read | FW → PC |
| Progress | `f3641403-b000-4042-ba50-05ca45bf8abc` | Write with response | PC → FW |
| Request | `f3641404-b000-4042-ba50-05ca45bf8abc` | Notify + CCCD | FW → PC |
| Response | `f3641405-b000-4042-ba50-05ca45bf8abc` | Write with response | PC → FW |
| **Event, nová ve v3** | `f3641406-b000-4042-ba50-05ca45bf8abc` | **Write with response** | **PC → FW** |

Event je callback zápisu na serveru zařízení. Pro odesílání událostí z PC
se nepoužívá Notify. Ve v3 jsou všechny charakteristiky povinné. Zařízení
zahájí RPC až po zapnutí CCCD Request. Status je stále původní 20bajtový
paket verze 1, Progress původní 9bajtový paket verze 2; obě struktury a
významy bitů zůstávají podle v2. Heartbeat je 5 s; po 15 s bez čerstvého
Status musí UI označit spojení/stav jako zastaralý. Nesmí dál ukazovat
ověřenou připravenost podle starých dat.

## Fragmentace JSON

Request, Response a Event používají stejnou fragmentaci jako v2:

| Offset | Délka | Význam |
|---|---:|---|
| 0 | 1 | Flags: bit 0 START, bit 1 END; ostatní bity 0 |
| 1 | 1 | Transfer ID, uint8 |
| 2 | 2 | Index fragmentu, uint16 little-endian, začíná 0 |
| 4 | 1–16 | UTF-8 bajty JSON |

Celý paket má 5–20 bajtů, funguje s MTU 23. Limit celého JSON je 32768
bajtů, objekt nejvyšší úrovně musí být JSON objekt; NaN/Infinity nejsou
povolené. UTF-8 dekódovat až po spojení celého přenosu: znak může přesahovat
hranici fragmentu. Závěrečný jediný fragment má flags `03`.
Chybějící, přeházený, překrývající se nebo déle než 30 s skládaný Request
Guardian odmítne odpojením. Po odpojení zahodit všechny neúplné sestavy.

FW používá **dva nezávislé assemblery pro Response a Event**. Transfer ID
události se může shodovat s ID odpovědi a po 255 se přetočí na 0; nejedná
se o trvalé ID události. Na jedné charakteristice se dvě JSON sestavy
neprokládají. Response a Event se mohou prokládat vzájemně se Status/Progress.
FW může mít nejvýše čtyři čekající Request; jejich ID nesmí znovu použít
před posledním fragmentem příslušné odpovědi. RPC odpověď má timeout 60 s;
dlouhá hardwarová operace vrací včas stav `pending` a dokončení samostatně.

## Obálky a chyby

Každý Request obsahuje `id` (1–64 ASCII znaků `[A-Za-z0-9._-]`) a `op`.
Response vrací stejné `id`:

```json
{"id":"s1","op":"station.get"}
{"id":"s1","ok":true,"result":{"version":"1.1.25","callsign":"OK7PS","ready":false,"radios":[]}}
{"id":"bad1","ok":false,"error":{"code":"control_denied","message":"Enable station control for this paired device on the PC"}}
```

Výše uvedený výsledek stanice je zkrácená ilustrace. Nepodporovaná operace
vrací `unknown_operation`, neplatný parametr `invalid_request`, nenalezený
záznam `not_found`, změna stránkovaného seznamu `list_changed` a nedostupné
úložiště `storage_error`. Text chyby je pro diagnostiku, FW rozhoduje podle
`code`. Neznámá pole odpovědí ignorovat pro další kompatibilní rozšíření.

## Stav stanice a capabilities

Po připojení načíst `capabilities.get`, `station.get` a `alert.types`.
Pro živou obrazovku opakovat `station.get` přibližně jednou za 1–2 s,
bez dalšího souběžného pollu téže operace. Event přenáší upozornění a
výsledky povelů; nenahrazuje poll podrobného stavu.

`capabilities.get` vrací `api_version:3`, `control_allowed`, seznam
`operations`, `actions`, `limits` a `radios`. Každé rádio uvádí profily,
`total_profiles` (seznam nejvýše prvních 20), `tune_mode`, `tune_modes`
a mapu `actions[action]` s `supported`, `available`, `reason`.
Tato dostupnost je orientační snímek; před provedením se ověří znovu.

`station.get` obsahuje:

| Část | Pole / význam |
|---|---|
| Stanice | `version`, `callsign`, `grid`, `updated_at` (Unix sekundy), `revision`, `ready`, `readiness_verified` |
| `mailbox` | `inbox`, `unread`, `outbox`, `outbox_failed`, `transit` |
| `server` | `enabled`, `state`, `last_verified` pro ARDOS-CZ; bez přihlašovacích údajů |
| `alerts` | `total`, `unseen` v trvalé historii |
| `radios[]` | `radio` (1/2), `revision`, `backend`, `name`, `connected`, `frequency_hz`, `mode`, `ptt`, `signal`, `error`, `ready`, `readiness_verified`, `busy`, `busy_reason`, `pending_operations` |
| `radios[].modem` | `selected`, `name`, `configured`, `started`, `state`, `ready`, `readiness_verified`, `lifecycle`, `active_transport`, `control_modem`, `vara_mode`, stav příkazového/datového portu VARA |
| `radios[].control` | `active`, `suspended_for_payload`, `active_sessions`, `handoff_pending`, `handoff_error` |
| `radios[].scanner` | `scanner_active`, `scanner_holding`, `scanner_paused`, `scanner_channel`, `scanner_frequency_hz`, `scanner_channels` |
| `radios[].calibration` | `state`, `peer`, `session_id`, `mode`, `progress`, `total`, `current`, `message`, `pending_offer`, `error` |
| `radios[].transfers[]` | `msg_id`, `state`, `direction` (send/receive), `transport`, `source`, `destination`, `via`, `progress_bytes`, `total_bytes`, `total_bytes_exact`, `percent`, `bitrate_bps` |

Každé rádio vrací nejvýše 20 aktivních sessions a jejich skutečný počet
v `total_sessions`. Číselné neznámé hodnoty a neznámý průběh jsou `null`.
Údaj `signal` zachovává význam driveru rádia, není univerzální dBm.
`percent:100` nepotvrzuje doručení adresátovi.

`ready` má tři hodnoty: `true` = dostupný provozní řetězec, `false` =
nepřipravený, `null` = připravenost dosud neověřena. `readiness_verified`
je `false` právě při `ready:null`. VARA vyžaduje příkazové i datové spojení
bez transportní chyby. SC-FTN a ARDOP se spouštějí pro konkrétní přenos
(`lifecycle:"per_transfer"`); samotný běžící řídicí kanál neověřuje payload
modem a jeho `ready` zůstává `null`. `configured` neznamená `started`.
Po začátku skutečného přenosu se připravenost může ověřit jeho stavem.
Stanice je připravena, pokud je připravené alespoň jedno rádio; jinak vrací
`null`, má-li alespoň jedno rádio neověřenou připravenost, nebo `false`.
Snímek popisuje současný stav, nezaručuje budoucí úspěch RF přenosu.

## Provozní ovládání

Na PC v **Nastavení → Nastavení stanice → Guard Mesh** musí uživatel pro
připojené v3 zařízení zapnout **Povolit ovládání stanice z tohoto
spárovaného zařízení**. Výchozí hodnota je vypnutá. Povolení se ukládá pro
BLE identitu zařízení, přežije dočasný výpadek a restart; **Odpojit / zrušit**
zapomenuté spojení i jeho povolení odstraní. Čtení, zprávy v2 a display
upozornění toto dodatečné povolení nevyžadují.

```json
{"id":"c1","op":"control.execute","token":"11111111-1111-4111-8111-111111111111","radio":1,"action":"radio.connect"}
```

`radio` musí být vždy explicitně 1 nebo 2; druhé rádio musí být
nakonfigurované. Žádný povel se automaticky nerozesílá na obě rádia.

| `action` | Další pole | Význam / omezení |
|---|---|---|
| `radio.connect`, `radio.disconnect` | žádná | Existující připojení/odpojení driveru, asynchronní výsledek |
| `modem.connect`, `modem.disconnect` | žádná | VARA; u SC-FTN/ARDOP používat lifecycle řídicího kanálu |
| `control.start`, `control.stop` | žádná | Běžný start/stop řízení; stop ruší také aktivní payload a kalibraci |
| `beacon.send` | žádná | Zařazení majáku pro vybrané rádio, výsledek `queued`; ne potvrzení odvysílání |
| `scanner.start`, `scanner.stop` | žádná | Scanner existujících vhodných frekvencí/routes; start vyžaduje řízení, CAT a alespoň dva kanály |
| `calibration.start` | `peer`, `mode` | SC-FTN AutoTune; peer 1–16 znaků callsign, mode quick/full (výchozí quick), výsledek pending |
| `calibration.cancel` | žádná | Zrušení aktuální AutoTune |
| `radio.tune` | `frequency_hz`, `mode` | Celé Hz 1–10000000000, povolený mód z capabilities; řízení vypnuté, rádio připojené a nečinné |
| `profile.apply` | `profile` | Název již uloženého profilu (1–24 znaků); rádio/VARA odpojené, řízení vypnuté, žádný přenos |

K5/K61 podporují `tune_mode:"frequency_only"`, pouze `mode:"FM"` a
násobky 10 Hz; profil modulace se tím nemění. CAT backendy uvádějí dostupné
módy v capabilities. Ladění se provádí pod zámkem driveru, ověřuje aktuální
PTT a zpětné čtení skutečné frekvence/módu. Chybný zpětný stav vrací failed.
Vzdálené nastavení není libovolný zápis konfigurace: lze zvolit pouze
existující uložený profil. Přenos, PTT, scanner, kalibrace, handoff nebo
jiná probíhající změna mohou povel odmítnout jako `busy`. Stop/cancel
zachovávají chování běžných místních ovládacích prvků.

Každý nový povel má **UUID token vytvořený a uložený ve FW před odesláním**.
Opakování se stejným tokenem a stejnými poli vrátí původní `command_id` a
`duplicate:true`; jiné údaje se stejným tokenem vrátí `token_conflict`.
Pole `id` lze při opakování změnit. Token je oddělený pro každé zařízení.

```json
{"id":"c1","ok":true,"result":{"command_id":"22222222-2222-4222-8222-222222222222","action":"radio.connect","radio":1,"accepted":true,"state":"pending","worker":"radio-control","duplicate":false,"created":1791712800.0}}
{"id":"c2","op":"command.get","command_id":"22222222-2222-4222-8222-222222222222"}
```

`ok:true` potvrzuje zpracování API. **Úspěch povelu hodnotit podle jeho
`state` a `error`**, ne pouze podle `ok`. `accepted:false,state:"failed"`
znamená odmítnutí před provedením. `accepted:true,state:"failed"` znamená,
že přijatá operace následně selhala. Stavy jsou `executing`, `pending`,
`completed`, `queued`, `failed`, `unknown`. Změna výsledku přichází také
událostí `command.changed`. `completed` potvrzuje dokončení této místní
operace; u stop/cancel není potvrzením vzdáleného doručení. `queued` je
koncový výsledek zařazení majáku.

Intent povelu se trvale zapíše před vedlejším účinkem. Pokud Guardian
restartuje během executing/pending, výsledek je `unknown` s chybou
`interrupted`; povel se automaticky znovu neprovede. FW pak obnoví stav
stanice a vyžádá rozhodnutí obsluhy před novým povelem. `command.get`
zpřístupňuje výsledek jen zařízení, které jej odeslalo.

## Upozornění a budoucí přenos dále

Nejprve načíst `alert.types`. Aktuální kódy: 1 MAYDAY, 2 zdravotní nouze,
3 evakuace, 16 QRT, 17 QSY, 18 QRV, 32 test sítě, 48 výpadek energie,
49 bateriový provoz. Čísla jsou stabilní; popisky lokalizuje Guardian.

```json
{"id":"a1","op":"alert.publish","token":"33333333-3333-4333-8333-333333333333","code":2,"note":"Cvičení: potřeba zdravotní pomoci, bod A."}
{"id":"a1","ok":true,"result":{"accepted":true,"alert_id":"44444444-4444-4444-8444-444444444444","duplicate":false,"delivery":"display","forwarded":false}}
```

Povolená pole jsou pouze id/op/token/code/note. Poznámka má nejvýše **512
Unicode znaků**, bez NUL. UTF-8 diakritika se zachová v PC historii a
zobrazení; nejde o limit starého krátkého RF rámce. UUID token zajišťuje
stejné opakování jako u povelů. `accepted:true` znamená trvalé uložení pro
zobrazení. Neznamená přečtení člověkem ani odeslání přes rádio.

Upozornění má `alert_id` UUID, `code`, `note`, `source`, `created`,
`origin`, `radio`, `rf_id`, `forwarded`, `seen`. Pro BLE je origin
`{"network":"guard_mesh_ble","device":"<BLE identita>"}`, radio/rf_id
jsou null. Pro rádiové upozornění je origin
`{"network":"guardian_rf","local":false}`, `radio` označuje první
zachycené rádio a `rf_id` původní identifikátor rámce. Lokální RF upozornění
má `local:true`. Stejné RF upozornění zachycené oběma rádii se deduplikuje
podle původu, rámce a obsahu v okně 15 minut.

```json
{"id":"a2","op":"alerts.list","offset":0,"limit":10}
{"id":"a3","op":"alert.seen","alert_id":"44444444-4444-4444-8444-444444444444"}
```

`alerts.list` má `items`, `total`, `revision`, `next_offset`; další stránku
vyžádat se stejnou revision. `list_changed` znamená začít od offset 0.
Limit 1–20, výchozí 10; Guardian jej může snížit podle limitu JSON.
`alert.seen` je idempotentní potvrzení přečtení, generuje `alert.seen`.
`seen:true` znamená přečtení alespoň jedním BLE zařízením. Potvrzení příjmu
Event neoznačuje upozornění přečteným a nemaže je z historie.

Trvalé `alert_id`, `origin` a `forwarded` jsou základ pro budoucí gateway.
Budoucí forwarding bude vyžadovat samostatnou explicitní operaci/politiku,
deduplikaci, omezení hopů a potvrzení doručení. FW nyní uchovává ID a původ,
ale nepovažuje BLE událost za pokyn k odvysílání. Krátký RF alert má menší
textový limit; převod 512znakové poznámky do RF je samostatné budoucí
rozhodnutí, zde se nic nezkracuje ani nepřevádí.

## Události, obnova a potvrzení příjmu

Event obsahuje obálku bez RPC id/ok:

```json
{"stream":"55555555-5555-4555-8555-555555555555","history_truncated":false,"seq":1,"event_id":"66666666-6666-4666-8666-666666666666","type":"alert.created","created":1791712800.0,"data":{"alert_id":"44444444-4444-4444-8444-444444444444","code":2,"note":"Cvičení","source":"OK7PS","origin":{"network":"guardian_rf","local":false},"created":1791712800.0,"radio":1,"rf_id":123,"forwarded":false,"seen":false}}
```

Typy jsou `alert.created`, `alert.seen`, `command.changed`. `seq` je
rostoucí číslo ve streamu, `event_id` je trvalé UUID. `stream` je UUID
úložiště Guardianu a přežívá běžný restart. Po změně/obnově úložiště se může
změnit. FW uchovává vlastní stream+cursor a deduplikuje podle event_id.

Po zpracování a trvalém uložení souvislého úseku událostí FW odešle:

```json
{"id":"e1","op":"events.ack","stream":"55555555-5555-4555-8555-555555555555","cursor":1}
{"id":"e2","op":"events.list","stream":"55555555-5555-4555-8555-555555555555","after":1,"limit":10}
```

Ack potvrzuje všechny seq nejvýše cursor pro dané BLE zařízení, je
monotónní a ukládá se na PC. Opakovaný starší ack vrací již potvrzený
cursor. Ack s nesprávným streamem nebo cursor nad poslední událostí se
odmítne. `events.list` vrací `stream`, `items`, `cursor`, `latest`,
`truncated`, `has_more`. Limit 1–20, výchozí 10; může být snížen kvůli
velikosti odpovědi. `after` a `cursor` jsou celá čísla 0–9007199254740991.
Při changed stream/future cursor se začne dostupnou historií a truncated
je true. Pokud je after starší než zachovaná historie, vrátí se její
dostupný začátek a truncated je true.

PC při spojení posílá Event od posledního uloženého ack. Odesílá okno až
20 událostí; bez ack se další historie neposune. Fronta je omezená, při
zahlcení zůstane událost v historii pro další pokus. V jedné relaci se
stejné event_id neposílá opakovaně; po reconnectu lze neackované události
dostat znovu. Jedná se o doručení s možným opakováním. FW musí odebrat
duplicitní Event i událost získanou přes events.list podle stejných ID.

Pokud `history_truncated` nebo `truncated` je true, obnovit snímek
`station.get`, `alerts.list` a známé neukončené `command.get`, zobrazit
mezeru v historii a potvrdit až skutečně uložené dostupné události.
Při novém streamu resetovat starý cursor. Tlačítko přečtení upozornění
odesílá samostatné `alert.seen`.

Uchovává se nejvýše **256 událostí, 128 upozornění a 128 povelů**, ack a
seznamy čtenářů pro nejvýše 32 zařízení. Token deduplikace alert/povel platí
jen dokud příslušný záznam zůstává v historii. FW nesmí po delší odmlce
automaticky opakovat staré neověřené operace: nejprve obnovit stav a nechat
obsluhu rozhodnout. Pro zprávy message.queue platí původní pravidla v2.

## Doporučený postup implementace FW

1. Přidat Event Write, Protocol nastavit na `47 4d 03 00`; zachovat v1
   Status a v2 Progress i Request/Response.
2. Přidat dva samostatné assemblery, ověřit bajty podle přiložených vectors.
3. Po CCCD načíst capabilities/station/alert.types a stránkovanou historii.
4. Trvale ukládat vlastní token před požadavkem a stream/cursor po přijetí;
   zpracovat opakování, timeout, truncated a unknown bez automatického RF.
5. UI rádia rozlišuje 1/2 a připravenost ano/ne/neověřeno, dostupnost povelů
   bere z capabilities. U ovládání vyžaduje vědomý výběr akce obsluhou.
6. Zařazení běžné zprávy používá message.queue; samostatná display nouzová
   upozornění používají alert.publish. Události pouze zobrazovat.

## Běh na PC a ověření

Windows balíček používá WinRT; Linux balíček BlueZ/D-Bus a vyžaduje
funkční systémovou Bluetooth službu a oprávnění k párování. Přístup k
sériovému portu se řídí místním nastavením OS. Linux obsahuje SC-FTN a
nativní ARDOP; VARA se instaluje a provozuje samostatně.
Lokální Linux x64 balíček byl sestaven na Debianu 12, minimum glibc 2.36.
Podrobné pokyny jsou v README-Linux.txt balíčku a build_linux.sh.

Automatické testy pokrývají kompatibilitu v1/v2, simulované GATT v3,
fragmentaci UTF-8, nezávislé kanály, heartbeat, reconnect, stránkování,
trvalost a konflikty tokenů, oprávnění, oba rádia, provozní gates a chyby
operací. Používají simulované BLE/radio drivery. Skutečný v3 firmware,
živé Bluetooth a provedení nových povelů na fyzickém rádiu je potřeba
ověřit po jeho samostatné implementaci.
