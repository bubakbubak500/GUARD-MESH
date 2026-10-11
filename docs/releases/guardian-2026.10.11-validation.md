# Guardian 2026.10.11 — záznam ověření

Záznam review a softwarového ověření verze `guardian-2026.10.11`.
Finální zdrojový commit, obrazy a jejich kontrolní součty uvádí `validation.json`
u release artefaktů. Testy simulátoru nepotvrzují chování fyzických zařízení.
Níže uvedené logy jsou místní podklady v `.sim-cache`, nejsou publikované.

## Review a opravené nálezy

První skutečné externí CLI `/review` bylo dokončeno a jeho výstup je v
[codex-review.log](../../.sim-cache/guardian-tests/codex-review.log). Našlo
nedostatečné okno Eventů (`MaxEvents` 8 místo 20) a ztrátu výsledku
`command.changed`, který přišel před odpovědí `control.execute`. Obě vady
jsou opravené. Časné výsledky se nyní trvale ukládají před posunem kurzoru
a ACK; k povelu se přiřadí až podle přesného `command_id` z odpovědi.
Ztracená odpověď ani restart samy znovu nespustí provozní povel.

Nález o inzerování v3 na deskách mimo T-Deck byl falešný poplach:
celý `GuardianBLEInterface.cpp` je pod podmínkou `LILYGO_TDECK`.
Finální opakování externího CLI zastavilo automatické schvalování, protože
by odeslalo podklad patche externímu backendu. **Druhé CLI review nebylo
dokončeno a nemá výsledek PASS.** Náhradní kontrolou bylo nezávislé interní
review konkrétních souborů a regresní testy.

Další opravené nálezy:

- Odpojení během ukládání Eventu již nezanechá nevyzvednutou RPC chybu,
  která blokovala další spojení; vlastník požadavku převezme chybu v dalším ticku.
- CRC snapshotu nyní chrání také generaci a hlavičku. Nový formát `GV4S`
  kontroluje magic, generaci, délku a obsah; starý nechráněný `GV3S` odmítá.
- Klepnutí na již vybrané rádio nemění ani nemaže rozepsané hodnoty formuláře.
- Odmítnutý pokus o zrušení AutoTune po zestárnutí dat zachová rozepsaného partnera.

Test souběhu odpojení i test změny bitů hlavičky před příslušnou opravou
selhaly a po opravě prošly. Interní kontrola ověřila také uložení časných
výsledků přes restart, zachování tokenu a polí při výslovném retry,
ochranu dokončeného výsledku před starší odpovědí `pending` a zastavení
kurzoru při překročení 20 kandidátů při zachování zobrazení nouzové zprávy.

## Dokončené testy a následné kontroly

- **PASS:** `python scripts/test_guardian_status.py`, všech sedm nativních
  sad: protokol, transport, RPC, v3 modely, persistentní sloty, Eventy a klient.
- **PASS:** `python simulator/run.py --test`, modely/služby a celé integrační
  sady anglického UI, českého UI a klávesové navigace. Build i běh skončily
  úspěšně; důkaz je v [simulator-final.log](../../.sim-cache/guardian-tests/simulator-final.log).
- **PASS:** nouzové zobrazení v simulátoru ovládané skutečným touch vstupem,
  včetně ručního zámku, přečtení a selhání uložení. Při selhání uložení
  upozornění zůstává nepřečtené a alarm aktivní.
- **PASS:** cílená česká Guardian sada po dvou posledních opravách formulářů.
- **PASS:** cílená anglická Guardian sada po těchto opravách. Celá dříve
  úspěšná sada jim předcházela; následná celá anglická UI regrese prošla také
  s okamžitou diagnostikou LVGL assertů.

## Meze ukládání a front

Klient uchovává nejvýše **20 upozornění**, **20 časných kandidátů povelů**
a **64 ID událostí** pro deduplikaci. Historie z PC se načítá po stránkách;
celá historie PC není uložena v RAM zařízení. Dva střídavé persistentní
sloty mají každý limit **128 KiB obsahu + 20 B hlavičky**.
Transportní fronta přijímá nejvýše **20 Eventů** a společný limit **64 KiB**
pro dokončené Eventy a rozpracovaný rámec. Jeden JSON rámec má limit 32 KiB.

## Buildy a podmínky vydání

Embedded buildy obou desek prošly: T-Deck / T-Deck Plus a Heltec V4 TFT.
Guardian BLE v3 běží pouze na T-Decku; Heltec dostává společné OTA rozhraní.
Oba buildy používají tag `guardian-2026.10.11` a aplikace se vejdou do oddílu
`0x3e0000`. Finální sestavení a balíčkování se provádí z uzavřeného zdrojového
commitu, uvedeného v `validation.json`.

`scripts/package_guardian_release.py` kontroluje přesný vestavěný tag,
ESP32-S3 chip ID 9, limit oddílu, úplnou shodu aplikace v merged obrazu
na offsetu `0x10000`, ELF a SHA-256 všech kopírovaných podkladů.
Nový balíček nezaměňuje ani nepřepisuje existující release složku.
GitHub OTA vyžaduje stabilní release, přesný název assetu a odpovídající
SHA-256 digest v metadatech assetu. Ověření obrazu není fyzická instalace.

**Hardware nezkoušeno.** Reálné BLE spojení s PC Guardianem, zvuk a probuzení
T-Decku ani fyzická USB/OTA instalace, restart a následný provoz na obou
deskách nejsou tímto záznamem potvrzené.
