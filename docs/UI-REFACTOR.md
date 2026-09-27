# Modularizace dotykového UI

Stav ověřených modulů k 27. 9. 2026. Firmware a Windows simulátor používají stejné samostatně
překládané moduly. Jejich hranice odpovídají původnímu návrhu `application`,
`screens`, `widgets`, `theme`, `models`, `services` a `platform`.
Rozsah implementace a ověření uvádí [report](UI-REFACTOR-REPORT.md).

## Odpovědnosti a vlastnictví

| Oblast | Modul | Rozhraní a vlastnictví |
| --- | --- | --- |
| Navigace | `application/UiApplication` | Vlastní kopii názvu stránky, uzavírací akci a atomové invalidace; registr popupů je vypůjčený. |
| Navigační prvky | `application/FocusTargets` | Vlastní skupinu a dynamický seznam vypůjčených widgetů. DELETE vyřadí záznam před refokusem; zánik vlastníka odpojí callbacky. Pravidla pohybu mezi obrazovkami zůstávají v koordinátoru. |
| Obnova fokusu | `application/FocusNavigation`, `widgets/ObjectRef` | Vlastní kolekci, invalidaci stromu, požadavky na fokus a obnovu podle identity vlákna či polohy. Uložené widgety sleduje přes DELETE i mimo aktivní skupinu. Koordinátor předává kořen a kontext obrazovky. |
| Směrový výběr | `application/SpatialNavigation` | Geometrický výběr v nejbližším řádku/sloupci, omezení a preferovaná oblast přes explicitní politiku. Úprava slideru neposílá commit po jeho smazání v preview callbacku. |
| Aktivní kontext | `application/FocusContextSelector` | Sdílený výběr popup/Wi-Fi/nastavení/chat/tab a jeho podpis z vypůjčeného snímku. Vlastnictví LVGL stromů a hardwarové směrování vstupu zůstávají u jejich vlastníků a koordinátoru. |
| Zprávy | `models/MessageStore`, `MessageTypes` | Vlastní ring a vlákna, unread/mention, ACK a tombstones. Alokátor a oznámení pro persistenci dostává zvenčí. |
| Relace chatu | `application/ChatSession` | Vlastní aktivní výběr, připnutý úplný klíč a sekvenci odeslání přes `RadioService`. Jediný resolver rozlišuje full pubkey, legacy key6 a jednoznačné jméno. Výsledek nese kopii cíle/textu; UI ověřuje identitu vlákna a nezměněný draft před dokončením. Historie ukládá/obnovuje hodnotový výběr přes Host callback. |
| Příjem zpráv | `application/MessageIngress`, `src/UIMessageEvent.h` | Mesh předává hodnotovou událost s kopií druhu zprávy, jmen, textu, klíče, času a RF metadat. Modul parsuje kanálového autora, filtruje a připraví neměnný výsledek před notify/Lua/UI efekty. `AbstractUITask` má výchozí adaptér pro ostatní UI; dotykové UI přebírá celé přijetí události. |
| Obrazovka a zámek | `application/ScreenPolicy` | Vlastní stav obrazovky, hard lock, aktivitu a idle/burn-in/notify/glance časování. Vrací rozhodnutí; `UITask` vykonává efekty podle desky. Stejná idle pravidla používá konzole. `TouchSleep` samostatně řídí úsporné čekání CPU. |
| Přístup k SD | `platform/StorageAccess`, `services/StorageMaintenance` | Reader lease a explicitní výhradní vstup po uzavření přijetí nové práce. Údržba drží rezervaci přes UI ticky s omezeným čekáním; neblokuje UI čekáním na vlastní odložený krok. |
| Stav SD | `services/SdHealthMonitor` | Vlastní intervaly kontrol, debounce detekce vyjmutí, příznak removal a rezervaci obnovy. Adaptéry provádějí fyzický mount/probe/unmount a následnou inicializaci pod rezervací. |
| Historie | `services/HistoryService`, `HistoryWorkerState` | Vlastní segmentovou tabulku, plánování, migraci, snapshoty, worker a diagnostiku. FS získává přes Host. UI sestavuje a potvrzuje úlohy, worker zapisuje neměnný snapshot. |
| Formát historie | `HistoryFormat`, `HistoryCodec`, `HistoryFileStore` | Zachovaný binární formát, převod záznamů, starší délky, kontrola krátkého čtení a bezpečné skládání cest. |
| Soubory | `services/FileOperations`, `platform/DataFilesystem` | Kopírování/přesun/mazání oddělené od formulářů; vlastní DMA scratch. Datový filesystem vlastní výběr backendu a bootovací politiku, mount volá přes Host. |
| Instalované aplikace | `services/AppInventory` | Vlastní aktuální a pracovní snímek inventáře. Worker nemění tabulku čtenou UI; `poll()` publikuje hotový výsledek a odmítne starší snímek po instalaci nebo invalidaci. |
| Obchod | `AppStoreJobs`, `AppStoreData`, `AppStoreScreen`, `esp32/AppStoreTransport` | Atomové úlohy vlastní kopie požadavků a výsledků. UI vlastní parsované katalogy a LVGL strom/časovač. Síťová implementace nepracuje s LVGL ani UI tabulkami. |
| Instalace | `services/StagedFileInstall` | Ověří zápis všech částí před nahrazením, zálohuje původní soubory a při běžné chybě je obnoví. Při selhání obnovy zachová zálohy; nemaže jiné jazyky. |
| Jazyk při bootu | `services/LanguageFile` | Vlastní bytes a seřazenou tabulku překladu, migraci volby a stav jednorázového načtení. Úložiště, alokace a opravu stažením dostává přes Host; neúplný soubor nezveřejní. |
| Kontakty | `models/ContactModel`, `screens/ContactsScreen` | Třídí snímky, vzdálenost počítá jednou na řádek. Obrazovka vlastní kontexty řádkových callbacků. |
| Baterie a poloha | `models/BatteryModel`, `LocationModel` | Výpočty, vyhlazování, kalibrace a časové politiky bez LVGL. |
| Rádio | `services/RadioService`, `platform/MeshRadioTransport` | Posloupnost odesílání, kontrola identity příjemce/kanálu, scope, flood a ACK. UI vlastní draft. |
| Konfigurace | `ConfigurationService`, `models/RadioPresets` | Používají stávající NodePrefs, společné presety a výpočty vysílací doby. |
| Formuláře | `SettingsForm`, `ProfileSettingsScreen`, `RadioSettingsScreen`, `AutoAddSettingsScreen`, `ExperimentalSettingsScreen`, `MqttSettingsScreen` | Vlastní odkazy na pole a callbacky, odpojují předchozí strom a reagují na DELETE. Sdílený Host poskytuje klávesnici a akce zařízení. |
| Další obrazovky | `ConfirmDialog`, `QuickRepliesScreen`, `SettingsScreen`, `AppPermissionsScreen` | Dialogy, rychlé odpovědi, kategorie a Lua oprávnění s odděleným stavem a životností. |
| Terminál | `services/TerminalSession`, `screens/TerminalScreen` | Příkazy a příjemce oddělené od LVGL; obrazovka vlastní log, vstup a nabídku příkazů. Staré callbacky neovládají novou instanci. |
| Nástroje a soubory | `FullscreenToolView`, `FileManagerScreen` | Společný obal vlastní stránku a kopii názvu. Správce vlastní seznam, editor, obrázky a dialogy; platformní adaptér poskytuje řádky úložišť a operace SD. Zavření ruší čekající paste a potvrzení mazání. |
| Mapa | `screens/MapScreen` | Jeden vlastník mapy na zařízení; soukromý stav zoomu, posunu, značek, trasy a popupů. Veřejné operace a hodnotové snímky; čtení mesh/cache přes Host. |
| Dlaždice a obrázky | `widgets/MapTileLayer`, `services/ImageCodec`, `models/MapProjection` | Vrstva vlastní buffery, LVGL popisovače a obrázky. Sdílí ji mapa i Lua, každá instance má vlastní paměťový limit. Projekce je čistý model. |
| Lua a zvuk | `application/LuaIntegration`, `LuaMapView`, `services/AudioService` | Zachované Lua ABI. Bridge dostává Host; zvuková služba vlastní fronty, workery, přehrávání a kodeky. |
| Vzhled a zamčení | `screens/AccentColorPicker`, `LockSettingsScreen`, `services/LockSettings`, `models/ColorChoice` | Picker vlastní draft/callbacky a před zápisem kontroluje aktuální hex; formulář zamčení vlastní caption tapety a swatche, služby respektují schopnosti desek. |
| Stylování widgetů | `theme/TouchTheme`, `widgets/ColorSwatch` | Wrapper vlastní `lv_theme_t`; opakované install odmítne cyklus. Swatch zachovává vlastní barvu ve všech navigačních stavech. |
| Obecné nastavení | `services/GeneralSettings`, `screens/GeneralSettingsScreen` | Služba vlastní pravidla historie, preference a vyhodnocení snímku SD; obrazovka vlastní ovládací prvky a potvrzení vázané na generaci formuláře i pořadí požadavku. Platforma provádí advert/setup/reboot a plánuje recovery. |
| Seznam záloh a import | `services/BackupCatalog`, `screens/BackupPickerScreen`, `screens/BackupSettingsScreen` | Každá obrazovka vlastní katalog, odkazy a potvrzení s kopií celé cesty. Katalog má nejvýše 24 položek, lazy PSRAM s interním fallbackem a odmítá příliš dlouhé cesty. Nastavení vlastní také odložené obnovení seznamu. |
| Operace záloh | `services/BackupOperations` | Vlastní scan/import/export/delete/reset, kontrolu krátkého zápisu a pořadí watchdogu, flush a restartu. Host vybírá filesystem konkrétní desky a provádí core operace; parser a fyzická odolnost zápisu zůstávají jejich odpovědností. |
| Ruční obnova SD | `services/SdRestoreJob` | Vlastní odložený požadavek, pořadí drain/profile/prepare/latch/copy/reboot a výsledný stav. Host obaluje kopírování původním LoopWdtGuard; mount a společná koordinace workerů zůstávají v platformním adaptéru. |
| Admin a room login | `screens/AdminSessionScreen` | Vlastní plný klíč cíle, heslový pokus, prompt/konzoli/picker, watchdog, odložené skrytí klávesnice a omezený log. Join dohledá aktuální slot podle klíče. Odpovědi dvou pokusů témuž kontaktu bez request ID mesh API nerozliší. |
| Akce kontaktu | `screens/ContactActionSheet` | Vlastní LVGL strom, řádkové kontexty a kopii veřejného klíče. Adaptér rozliší akci, dohledá kontakt a aktuální slot podle klíče; mapa dostává kontakt explicitně. Zavření nejprve odpojí události, generace odmítne původní akci po reentrantní náhradě. Admin/room relace a pozdní odpovědi zůstávají samostatným dluhem. |
| Nastavení displeje | `services/DisplaySettings`, `screens/DisplaySettingsScreen` | Služba validuje hodnoty a ukládá preference; Host zprostředkuje timeout, restart a LED konkrétní desky. Formulář vlastní pole/přepínače/dropdown; generační kontrola odmítá pokračování po náhradě v callbacku. |
| Nastavení klávesnice | `models/KeyBindings`, `services/KeyboardSettings`, `screens/KeyboardSettingsScreen` | Služba vlastní mapování, cache podsvitu a debounced zápis. Formulář vlastní capture a callbacky; zavření ruší capture, zachová čekající zápis. |
| Vazba klávesnice | `widgets/KeyboardBinding` | Vlastní kopii editovaného pole nad klávesnicí; sleduje zánik vypůjčené klávesnice a cílového pole. Synchronizuje změněný UTF-8 úsek s pravidly cílového pole; generační kontrola zruší zbývající kroky po DELETE či změně vazby v callbacku. |
| Chat | `screens/ChatTimeline`, `widgets/ChatText`, `ChatPanel` | Časová osa vlastní rozložení, indexy, naplánované překreslení a URL dialogy. Vypůjčený seznam sleduje přes DELETE; zprávy čte přes Host a navigace dostává hodnotové snímky. |
| Seznam konverzací | `screens/ThreadListScreen` | Vlastní podpis vykreslených dat a kontexty řádkových událostí. Tab půjčuje kořen seznamu. Přestavba zachová scroll, změna seznamu či zánik vlastníka zneplatní staré akce; příkaz ověří identitu konverzace. |
| Akce konverzace | `screens/ThreadActionMenu` | Vlastní nabídku, potvrzení mazání/historie, sdílený secret a generaci požadavku na ikonu. Před příkazem znovu ověří název/druh i zachycený klíč kontaktu nebo secret kanálu. Rádiové a modelové operace volá přes Host. |
| Emoji a symboly | `screens/GlyphPicker` | Vlastní sady znaků, popup, výběr a akumulaci pohybu. Sleduje vypůjčené pole a grid; vrací token volby ikony, odmítá staré stromy a neuzavře popup vytvořený callbackem vložení. |
| Rychlé odpovědi | `screens/QuickReplyPicker` | Vlastní popup a kopie právě zobrazených odpovědí. Cílové pole sleduje přes DELETE; GPS čte znovu při akci. Úložiště odpovědí a vložení přes přímé/mirror pole poskytuje Host. |
| Návrhy zmínek | `screens/MentionPicker` | Vlastní nabídku, kopie jmen, snímek textu/kurzoru a privátní navigaci. Ověří původní token před vložením; příjemce načítá přes Host, pole sleduje přes DELETE. |
| Nabídka diakritiky | `screens/AccentPicker`, `models/AccentCharacters` | Katalog znaků je neměnný. Picker vlastní popup, snímek textu a výběr; nahrazuje znak před skutečným kurzorem, zachová suffix a odmítne zastaralou akci. |
| Cykly diakritiky | `screens/AccentCyclePicker` | Vlastní ALT/long-press výběr, snímek pole, popup a timeout. Náhrada běží po výchozím handleru klávesnice, bez odloženého přepisu; zánik cíle/rootu ruší celý režim. |
| Výběr a editace | `widgets/TextSelection`, `screens/TextEditMenu` | Výběr vlastní pozorované odkazy a časování dvojkliku. Menu zachytí původní i mirror pole, text, kurzor a rozsah; odmítne staré akce a zruší se při změně vazby klávesnice. |
| Psaní zprávy | `screens/ChatComposer`, `widgets/FkeyShape` | Každý panel vlastní composer, jeho výšku, widgety, události a generaci konverzace. Odeslání pracuje s kopií textu; nezmaže nový draft ani neobnoví zaniklou vazbu. Canvas uvolní svůj buffer při DELETE. |
| Schránka | `models/TextClipboard` | Vlastní pevný buffer bez LVGL a alokací. Ořez zachová celé UTF-8 znaky; volitelně odstraní recolor značky a dekóduje escapované mřížky. Toast dodává koordinátor. |
| Aktualizační panel | `screens/FirmwareUpdatePanel`, `platform/OtaCapability` | Vlastní vazby About, příkazy a převzetí výsledků i po zavření stránky. Při detach odpojí callbacky; starý strom neřídí nový panel. Platformní adaptér ověřuje velikost i skutečnou polohu OTA slotu. |
| Aktualizace firmwaru | `services/FirmwareUpdateJobs`, `ReleaseMonitor`, `ReleaseListing` | Neměnné požadavky, atomický přechod Queued/Running/Ready a kopie výsledku. UI vlastní intervaly a generaci kanálu; stará odpověď nemění novou volbu. Průběh je samostatný atomický údaj. |
| Přenos aktualizace | `platform/esp32/FirmwareUpdateTransport` | Používá socket a HTTP klient sdíleného executorového vlákna. Vlastní HTTP/OTA/SD průběh a mapování desky na artefakt; nemá závislost na LVGL ani `UiDevice.h`. |
| Sdílená síťová fronta | `platform/esp32/SharedNetworkExecutor`, `services/TileRequestLedger` | Executor vlastní task, frontu, klienty a atomovou diagnostiku. Ledger rozlišuje celé souřadnice a token dokončení; rezervace před publikací vylučuje závod s okamžitým dokončením. Krátký zámek neobaluje queue API. Task má životnost procesu, bez stop/join. |
| Výběr vydání | `screens/ReleasePicker`, `ConfirmDialog` | Picker i potvrzení vlastní zachycenou verzi a kanál. Staré stromy odmítají události. Potvrzení uchová kopii akce i přes reentrantní zavření nebo otevření náhradního dialogu. |
| Rádiová viditelnost | `models/Sightline`, `services/SightlineJob`, `screens/SightlineScreen`, `platform/esp32/SightlineTransport` | Model bez LVGL; neměnná cesta a server, atomické předání výsledku. Obrazovka přijímá jen svůj request ID, body čar patří konkrétnímu grafu do DELETE. HTTP má omezený buffer a používá existující worker. |
| Systémové informace | `models/SystemDiagnostics`, `screens/SystemInfoScreen`, `platform/DeviceDiagnostics` | Hodnotový snímek odděluje sběr hardwarových údajů od bezpečně omezeného textu a LVGL. Obrazovka vlastní odkazy a časování; stall ring vlastní kopie tagů. |
| Kapacita SD | `services/StorageUsage` | Worker vlastní pracovní výsledek, UI publikovaný snímek a interval. Atomické mount notifikace zneplatní queued/stale práci bez zápisu do UI dat. Lifecycle čeká po celý queued/running stav. |
| Wi-Fi scan | `services/WifiScanJob`, `platform/esp32/WifiScanTransport` | Oddělené snímky workeru a UI, atomický claim/cancel a zveřejnění celé odpovědi. Adaptér zachovává bounded scan/retry a pravidla Pageru; hlavní vlákno stále řídí odpojení a opětovné připojení. |
| Wi-Fi formuláře | `screens/WifiFormsScreen` | Vlastní seznam, Join/Hidden/Details sheet a řádkové kopie SSID. Příkazy dohledávají aktuální síť podle SSID; chybné uložení nezavře draft. Rádio a reconnect zůstávají platformní politikou. |
| Přenos dlaždic | `platform/esp32/TileFetchTransport` | Vlastní cache validaci, HTTP a kontrolovaný zápis/cleanup. Borrowed filesystem a kopie prefixu/stylu/serveru platí jen uvnitř scoped lease; publikace proběhne až po zavření souboru, socketu a lease. |
| Bluetooth formuláře | `screens/BluetoothSettingsScreen` | Vlastní PIN, nastavení a párovací stránku. Hodnotová identita zařízení a token potvrzení přežijí změnu pořadí scanu; žádné akce podle uloženého indexu. Politika rádia a směrování kláves zůstávají v adaptéru. |
| Plán telemetrie | `services/TelemetryPolling` | Vlastní parser, migraci, osm záznamů, konečné rozpočty a časování. Pozdní načtení SD sloučí čekající změny; chyby mají odstup opakování. Host ukládá ověřenou náhradou souboru a vyhledává kontakt pro odeslání. |
| Zamykací overlay | `screens/LockScreen` | Vlastní tapetu, LVGL cache/deskriptor, hodiny a odpočet. Ruší cache před uvolněním; sleduje i externí smazání stromu. Host dodává board layout, čas a nejvýše jednou za sekundu počet zpráv; napájecí politika zůstává v koordinátoru. |
| První spuštění | `screens/SetupWizardScreen` | Vlastní čtyři kroky, UTF-8 draft a vybraný region. Každý přechod zneplatní staré pokračování, chybný zápis ponechá krok otevřený. Boot politika rádia a preference zůstávají v Host. |
| Blokovaní uživatelé | `screens/BlockedUsersScreen` | Vlastní stránku a kopie cílových klíčů/jmen. Kontexty řádků mají životnost stránky; při odloženém DELETE jsou staré akce odpojené. Host zachovává párové odblokování a správu horní lišty. |
| Hodiny | `models/ClockTime`, `services/ClockSettings`, `screens/ClockSettingsScreen` | Validace kalendáře a RTC floor, preference a platformní TZ, formulář a picker s vlastními pozorovanými odkazy. Příkazy RTC a synchronizaci klávesnice dodává Host. |
| GPS | `models/GpsStatus`, `services/GpsSettings`, `screens/GpsSettingsScreen` | Model vlastní začátek akvizice a omezené formátování; služba čte hodnotový snímek a ukládá preference. Obrazovka vlastní callbacky, dynamické rozložení a životnost dropdownu. |
| Zvuk a upozornění | `models/NotificationPolicy`, `services/SoundSettings`, `screens/SoundSettingsScreen` | Jednotná DND/preview/arrival pravidla, preference a generace blikání bez LVGL. Formulář vlastní všechny odkazy i submenu; výběr WAV předává hodnotový cílový slot, který správce souborů zachytí při otevření dialogu. |
| Baterie a kalibrace | `models/BatteryModel`, `services/BatterySettings`, `screens/BatterySettingsScreen` | Služba vlastní filtr, publikované napětí a časovaný kalibrační požadavek bez delay. Formulář vlastní token a ruší jej při detach/DELETE; hardwarové čtení, SOC a sleep poskytuje Host. |
| Historie baterie | `models/BatteryHistory`, `services/BatteryHistory`, `screens/BatteryHistoryScreen`, `widgets/ChartTicks` | Parser, odhad a omezená historie; synchronní služba zachytí backend jednou, ověří staging a zachová zálohu. Obrazovka vlastní popup a potvrzení s cílem podle zobrazené historie; dočasný buffer se uvolní po naplnění grafu. |
| Akce zprávy | `screens/MessageActionMenu` | Vlastní snímek zprávy, menu a retry potvrzení; před příkazem ověří sekvenci záznamu a aktuální konverzaci. |
| Metadata a trasování | `screens/MessageInfoScreen` | Vlastní detail zprávy, posuvné tělo a výsledek trasování. Replay používá zachycenou zprávu, síťové příkazy a čtení mesh procházejí Host. |
| Vzhled/platforma | `theme/Theme`, `theme/Fonts`, `widgets/Styles`, adaptéry `platform/esp32` a `platform/desktop` | Palety, fonty, škálování, alokace, čas/yield, konfigurace a transport. |

Cesty jsou relativní k `src/ui-touch/`. `UITask` zachovává veřejné rozhraní pro
main, mesh a konzoli. PlatformIO i simulátor zahrnují nové zdrojové soubory.

## Pravidla životnosti a vláken

- `SoundSettings` běží na UI vláknu. Náhledy a příchozí zvuky používají tutéž
  kontrolu master mute/DND. Čas z hardwaru je lokální minuta nebo -1 pro
  nedůvěryhodné hodiny; nulové okno DND je neaktivní. Blikání přepíná nejvýše
  jednou na tick, při neúspěšném zápisu opakuje po 50 ms, po úspěchu po 220 ms.
  Nová sekvence i vypnutí mění generaci; návrat starého I/O callbacku ji nepřepíše.
  Formulář i jeho nabídka mají vlastní generaci; DELETE nejprve vyčistí ObjectRef.

- `BatterySettings` používá jen UI vlákno. `tick` odebere maximálně jeden vzorek
  po 20 ms, neprovádí doháněcí burst; zpožděná smyčka tedy kalibraci prodlouží.
  Reset a nový požadavek mění ID, formulář ruší jen vlastní čekající ID.
  Stav a výsledek se předávají hodnotou; služba neuchovává LVGL objekty.

- LVGL a model zpráv patří UI vláknu. `UiApplication::post()` předává invalidace,
  nikoliv obecné mesh zprávy. Není náhradou synchronizace transportu.
- `receiveMessage()` je synchronní vstup na UI vláknu. Každá událost vlastní
  text i metadata a `MessageIngress` vytváří vlastní připravený snímek před
  callbacky. Vnořený příjem nemění první zprávu. Žádný následný příjem nečte
  čas, trasu ani scope ze společného slotu „poslední zprávy“. Staré metody
  `notify()` + `newMsg*()` zůstávají kompatibilními adaptéry; nový mesh příjem
  používá výhradně explicitní událost. Nejde o frontu mezi různými vlákny.
- Historie používá jeden atomický stav Idle/Queued/Running. Claim a cancel
  soutěží o tentýž stav; během aktivní úlohy nesmí UI přepsat její snapshot.
  Worker publikuje výsledek a případný identifikátor segmentu k opravě; tabulku
  mění UI. Externí executor musí skončit před zánikem služby.
- Inventář aplikací používá Idle/Queued/Running/Ready. Žádosti, publikování,
  invalidace a aktualizace po instalaci patří UI. Worker volá pouze `runPending()`
  a zapisuje pracovní snímek. Executor musí skončit před zánikem služby.
  Synchronní fallback při otevření draweru ještě zůstává; nemůže souběžně
  zapisovat do snímku běžícího workeru.
- Úlohy obchodu používají Idle/Queued/Running/Ready s neměnným požadavkem.
  Ready zůstává obsazené do převzetí UI i při zavřené obrazovce. OOM spuštění
  executorů ukončí čekající požadavek chybou, neponechá nekonečné načítání.
  `AppStoreScreen` odmítá události ze zavřeného stromu a při externím DELETE
  ruší svůj časovač a vypůjčené navigační odkazy. Desktop testuje tuto skutečnou
  obrazovku i s vypnutým Lua runtime; HTTP nahrazuje deterministický backend.
- Před asynchronním smazáním dialog zruší svoji akci. Callback starého stromu
  nesmí ovládat náhradní obrazovku. Vyčleněné formuláře ověřují příslušnost události.
- Mapová obrazovka ruší časovače a pomocný canvas při přestavbě i externím
  smazání stránky. Dlaždice invalidují LVGL cache před uvolněním pixelů.
- Vlastnící třídy nelze kopírovat. `MapScreen` je výslovně jedna instance pro
  zařízení; jeho stav není exportován jako zapisovatelné globální proměnné.
- Časová osa ruší render a odložený refresh při zavření, shutdownu a DELETE
  seznamu. Bubliny porovnávají zachycenou sekvenci zprávy s aktuálním ring slotem.
  Menu zprávy navíc ověřuje aktivní konverzaci před akcí a potvrzením retry.
- Navigační skupinu vytváří a naplňuje `FocusTargets`. Její vypůjčený ukazatel
  slouží pro LVGL vstup a focus; členství se mění přes `add/clear`.
- `FocusNavigation` vlastní tuto kolekci a její přestavby. `ObjectRef` nikdy
  nemaže vypůjčený widget, pouze odpojí svůj DELETE callback. Kontexty i filtry
  navigace se používají synchronně na UI vlákně; filtr nesmí měnit strom.
- `ThreadListScreen` uchová kontext každého řádku do jeho DELETE. Odpojení
  vlastníka zneplatní akce, takže ještě živý starý strom nemůže ovládat nový.
- `LanguageFile` zveřejňuje tabulku až po úplném čtení a parsování. Při zániku
  ji odpojí od překladače před uvolněním řetězců. Změna jazyka stále používá reboot.
- `ThreadActionMenu` nepoužívá sdílený globální index pro potvrzení. Každé
  potvrzení vychází ze zachycené identity a ověřuje ji znovu při provedení.
  Picker vrací identifikátor požadavku; starý výsledek se nepoužije pro novou nabídku.
- Přepnutí a zavření konverzace ruší textové pickery a návrhy pro její composer.
  Smazání pole rovněž zruší popup. Před callbackem vložení se původní popup
  odpojí; nově otevřený picker pak zůstane živý. Řízení klávesami používá příkazy,
  nikoliv zapisovatelné globální ukazatele na grid a jeho aktuální výběr.
- Návrhy zmínek a diakritiky porovnají text i pozici kurzoru před úpravou.
  Cílové pole sledují přes DELETE; odpojené staré řádky neovládají novou nabídku.
  Před zpětným voláním uvolní svůj popup; Host synchronizuje přímé a mirror pole.
- `AccentCyclePicker` ruší svůj timeout při zavření, změně konverzace a DELETE.
  Ztracený fokus ALT režimu nevyžaduje exportovat cílový ukazatel; pokračování
  cyklu používá interní pozorovaný odkaz. Long-press nemá odložený callback.
- `TextSelection` zneplatňuje výběr při změně textu, sleduje i cíl dvojkliku
  a soustředí závislost na LVGL 8 selection fields na jedno místo. Omezená pole
  zachovají nativní pravidla LVGL, s kontrolou zániku nebo reentrantní editace
  po každém kroku; neomezená používají jednu změnu. `TextEditMenu` před akcí
  odpojí svůj strom a ponechá callbackem otevřené náhradní menu živé.
- `SightlineJob` nepřijme další požadavek před převzetím výsledku. Zavření LOS
  nezasahuje do běžícího workeru; UI starý výsledek odebere a zahodí. HTTP adaptér
  nečte preference ani LVGL a nedotýká se bodů grafu. Obrazovka je jedna instance
  pro zařízení, její pozorované odkazy se odpojují před externím zavřením stromu.
- `StorageUsage::invalidate` lze volat z mount notifikace na jiném vlákně.
  Samotné `poll`, `refresh` a čtení snímku patří UI; ty jediné mohou měnit
  zveřejněné kapacity a plánování. `SystemInfoScreen` sleduje DELETE těla i štítků
  a po reentrantním čtení ověří generaci před použitím snímku.
- `MessageStore` nemá friend vazby. Čtená vlákna a ring jsou pouze const pohledy;
  přejmenování, vazby kontaktů, mazání, kontrola sekvence a obnova ring geometrie
  patří modelu. Import běží synchronně na UI vlákně; nejde o souběžnou transakci.
- Migrace uloží index vláken před commit markerem segmentů. Neúplná migrace
  ponechá starý zdroj autoritativní i po restartu s již uloženým indexem.
  Platný marker s prázdným segmentovým úložištěm nesmí obnovit smazanou historii
  ze starého kombinovaného souboru. Binární layout ani názvy souborů se nemění.
- `FirmwareUpdatePanel` sleduje DELETE a při detach odpojí vlastní event callbacky.
  UI smyčka přebírá výsledek i bez otevřeného About; zavření stránky nezruší
  firmware job. Přepnutí kanálu nepřepíše zachycenou verzi/kanál běžící instalace.
- `WifiScanJob::snapshot` čte jen UI. Worker mění oddělený snímek až do Ready;
  `poll` jej zkopíruje a uvolní další požadavek. Timeout může zrušit pouze Queued,
  nikoliv již převzatou práci. Neúspěšný start workeru publikuje prázdný výsledek.
- Hodiny a GPS odpojí vlastní callbacky při detach i DELETE těla. Reentrantní
  Host čtení/příkaz nesmí pokračovat zápisem do náhradního formuláře. Picker zón
  před uložením znovu ověří generaci po zavření; GPS detach zavře i seznam dropdownu.
- `GpsStatus` patří UI vláknu; začátek akvizice předává hardware lifecycle,
  nikoliv první otevření stránky. Formátovaný stav nevyvozuje příjem NMEA ani
  zdraví přijímače z počtu satelitů použitých ve fixu. `ClockTime` odmítá normalizaci
  zadaného data při převodu; použitá DST pravidla dodává platformní libc.
- Callback DELETE nesmí odstranit sebe ani dřívější deskriptor ze stejného
  mazaného objektu: LVGL 8 by přeskočilo další pozorovatele. Hodiny, GPS a graf
  registrují `ObjectRef` před úklidem; odkaz se nejprve zneplatní a úklid už
  nezasahuje do seznamu callbacků kořene. Explicitní close odpojuje callbacky
  před zahájením mazání a toto omezení se na něj nevztahuje.
- `BatteryHistory` používá synchronně UI vlákno a jeden backend po celou operaci.
  Před publikací čte staging zpět; běžné selhání rename vrátí původní soubor.
  Při neúspěšném rollbacku zachová `.bak`/`.tmp`, které další append/clear odmítne
  přepsat. Potvrzení mazání patří konkrétní generaci grafu a zachycenému backendu.

- Nastavení klávesnice drží odložený zápis podsvitu ve službě, nikoli ve formuláři.
  `tick()` ukládá jednou po 1200 ms od poslední změny, i pokud byl mezitím formulář
  zavřen. Zachytávání nové klávesy naopak zaniká s formulářem. Staré preference
  vracející `void` neposkytují signál chyby zápisu a služba to nemůže doplnit.
- Formulář displeje před uložením synchronizuje mirror a znovu ověřuje generaci
  i identitu pole. Vypnutí náhledů nevymaže volbu zobrazení na zamčeném zařízení.
  Preference se stejnou RAM cache zůstávají netransakční; callbacky platformy
  udržují běhový stav v souladu s touto cache i po ohlášené chybě persistence.

- Uložení barvy má pořadí sync/validate/save/close/apply/restart. Generace se
  ověřuje po každém volání Host; starý picker nesmí zavřít či restartovat novou
  instanci. Chyba zápisu zachová otevřený draft. DELETE odpojuje potomky bez
  změny právě procházeného seznamu událostí kořene.
- Popisek tapety je `ObjectRef` formuláře. Oznámení ze správce souborů po zániku
  formuláře je no-op. Starý nevolaný rekurzivní scanner SD už neexistuje.
- Potvrzení neomezené historie a obnovy SD patří obecnému formuláři. Nový výběr
  zneplatní starý požadavek i bez výměny formuláře; close callback nesmí starou
  akci znovu uplatnit. Konzole restartuje až po úspěšném uložení. Volba SD
  předává i chybu bootovacího zápisu; existující setter přitom může již změnit
  běžnou preferenci a není atomickou transakcí obou úložišť.

## Kompatibilita a dosud neoddělené části

### Rozpracované hranice úložiště — 27. 9. 2026

`StorageAccess` řídí přijetí práce po dobu změny filesystemu. Worker získá
`StorageLease` před převzetím úlohy; při odmítnutí ponechá práci ve frontě.
Lease končí až po uzavření souborů. `StorageTransition` nejprve zavře přijetí
nové práce a teprve `enter()` po odchodu čtenářů dovolí změnu mountu. Samotná
připravenost rezervace ještě nepovoluje vnořené operace UI. Vnořená operace
stejného tasku smí půjčit aktivní rezervaci jen uvnitř jejího rozsahu.

`StorageMaintenance` patří UI tasku a drží čekající rezervaci napříč ticky.
Aktivní `Attempt` má blokový rozsah; po návratu z ticku není povolen výhradní
přístup. Timeout čekající rezervaci uvolní, aby například pozastavené audio
s otevřeným souborem nezablokovalo úložiště trvale. Rozběhnutý webový přenos
zatím tvoří explicitní doplňkovou podmínku vyprázdnění; nový přenos při
zavřeném přístupu nezačne. Tato brána není mutex pro jednotlivé FAT operace.

Preference se před změnou backendu vyprázdní s otevřeným přístupem. Na jejich
asynchronní writer se nesmí čekat uvnitř výhradní rezervace. Synchronní
ukládání před restartem ruší čekající údržbu, jejíž další tick by jinak nemohl
proběhnout. Po formátu/remountu historie ruší nepřevzatý starý descriptor,
znovu sestaví segmenty a dokončí index i commit marker; chybový zápis nesmí
vynulovat příznak zbývající práce.

`BackNavigation` soustředí odlišná pořadí návratu jednotlivých desek a BLE.
Jedna akce zavře nejvýše jednu vrstvu; blokující overlay spotřebuje vstup.
Výjimkou je úklid zastaralého flagu posunu mapy, po němž smí směrování
pokračovat pouze při explicitním potvrzení callbacku, že nezměnil kontext UI.
Router neuchovává odkazy na widgety mezi událostmi.

Protokol, Lua rozhraní a binární formáty historie se nemění. ACK/delivery zůstává
stavem RAM. Arduino remove/rename není transakce odolná proti výpadku napájení.
Instalace nepředstavuje transakci při výpadku napájení: přetrvávající `.bak`/`.tmp`
soubory se automaticky nepřepisují a vyžadují obnovu.
Kopie adresářového stromu rovněž není transakční; neúspěch může nechat část
cílového stromu. Zdroj se při neúplné kopii nemaže.

Úplné rozdělení monolitu ještě není dokončené. V `UITask.cpp` zůstává především
vstupní vazby a směrování kláves, pravidla přechodů mezi obrazovkami,
zbývající About shell a export crash reportu, řízení změn síťového backendu,
mount/recovery SD a část nastavení. Executor i transport dlaždic mají vlastní moduly. Také úplná
inicializace a hlavní smyčka stále patří koordinátoru. `platform/UiDevice.h`
zůstává širokým přechodovým includem pro část služeb a formulářů; mapová obrazovka
a vrstva dlaždic, terminál, správce souborů, obchod a nové chatové moduly už jej nepotřebují.

Podrobný backlog s důvody a prioritami je v [UI-REFACTOR-REPORT.md](UI-REFACTOR-REPORT.md#zbývající-refaktor-a-proč).
Po iteraci obecného nastavení dne 21. 9. 2026 uživatel nejprve požádal pokračování
pozastavit a následně schválil další ohraničené kolo s Luna Max. Toto kolo
oddělilo katalog a UI záloh i menu akcí kontaktu. Po dosažení 90 % týdenního
využití byl dokončen rozpracovaný celek, ověřen simulátor a vydán nový lokální
firmware; toto předchozí kolo skončilo na 91 %. Dne 24. 9. 2026 uživatel
obnovil práci s GPT-6 Sol/Luna agenty a hranicí 50 % celkového týdenního využití
(výchozí stav 11 %). Kolo skončilo na samostatně ověřitelném celku před touto
hranicí; finální využití, testy a lokální app i merged BIN/JSON uvádí report.
`UITask.cpp` má 32 798 řádků, v tomto kole ubylo 3 251 a přibylo 13 modulů.
Úplné rozdělení monolitu není dokončené. Další plošný refaktor se automaticky
nerozbíhá; nový funkční vývoj může pokračovat podle dohody v reportu.

## Ověření bez rádia

```powershell
python simulator/run.py --test
```

Příkaz přeloží skutečné sdílené UI, spustí C++ testy modelů a anglický i český
scénář s LVGL. Smoke režim používá paměťový filesystem pro skutečné loadery,
segmentový writer, migrace a testy chyb. Běžný interaktivní simulátor má storage
standardně nepřipojené. Paměťový FS neověřuje chování skutečného FAT, SD ani výpadky
napájení. Snímky jsou v `.sim-cache/test-artifacts/` a `cs/`.

```powershell
python scripts/test_ui_models.py
```

Samostatná sada používá C++11, aktivní aserce, `-Wall -Wextra -Werror` a zahrnuje
závod claim/cancel. Scénáře s LVGL navíc zkouší reálné dekódování PNG, limity a
životnost mapy, výměnu popupů, souborové chyby a asynchronní historii přes
stejný executor vstup jako FreeRTOS. Desktop neověřuje skutečné RF a periferie.
