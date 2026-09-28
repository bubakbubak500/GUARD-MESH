# Guard-Mesh-Flasher 1.1.0 — 28. 9. 2026

Záloha a obnova pro T-Deck / T-Deck Plus s GUARD-MESH a kompatibilním WadaMesh.
Postup použití a omezení jsou v [návodu](../desktop-flasher/README.md).

## Změny

- Výchozí instalace vytvoří trvalý `.gmbak`, obnoví NVS/SPIFFS po zápisu firmwaru
  a ověří data před spuštěním rádia. Lze zálohovat i obnovovat samostatně.
- Import nejprve uloží aktuální stav. Kontroluje MAC stejného zařízení,
  nešifrovanou 16MiB flash a shodné datové oddíly; porovnáno s WadaMesh `beta_83`.
- Volitelná záloha SD profilu `meshcomod` ze čtečky v PC. Obnova zachová
  předchozí složku na kartě. USB spojení samo SD data nepřenáší.
- Jediné otevřené spojení esptool udržuje stub a zvolenou rychlost po celou
  operaci. Při chybě zápisu/ověření zůstává trvalá záloha dostupná.
- Verze balíčků a Inno Setup vychází z jednoho `__version__`.

## Ověření

- 34 automatických testů: archivy a SHA-256, poškozená metadata, cesty mimo
  profil, nedostupná SD složka, zachování původních SD dat, shoda oddílů,
  simulovaná flash, jiné rádio, plný disk, chyby čtení/zápisu/ověření,
  neměnná pracovní kopie a skutečné Tk ovládací prvky.
- Skutečný parser esptool 4.8.1 ověřen se simulovaným zařízením: stejné spojení,
  jedno nastavení přenosové rychlosti, žádný restart před dokončením.
- Windows instalátor a portable ZIP sestaveny s přibaleným posledním firmwarem.
  Kontrola spuštění zabalené aplikace ověřila verzi 1.1.0, výchozí zálohování,
  datová tlačítka, knihovnu s jedním firmwarem, hledání a viditelnost protokolu.

Přibalený firmware je nezměněný Guardian A3 Black ze zdrojového commitu
`4798eb92fe3e5b95e6680c58e1b24791a1100ad4` (vydání `guardian-2026.09.28`).
Toto vydání mění pouze flasher a dokumentaci. Fyzická záloha a obnova přes USB
na rádiu dosud nebyla ověřena. Zálohy nejsou šifrované; obsahují soukromou identitu,
zprávy i případná hesla. Nepatří do Git repozitáře ani mezi veřejné release soubory.
