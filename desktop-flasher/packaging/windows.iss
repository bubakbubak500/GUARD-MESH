; SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AppSource
  #error AppSource is required
#endif
#ifndef OutputPath
  #error OutputPath is required
#endif
#ifndef AppVersion
  #error AppVersion is required
#endif
[Setup]
AppId={{CA117386-CE29-4D36-B696-F31B05273F14}
AppName=Guard-Mesh-Flasher
AppVersion={#AppVersion}
AppPublisher=GUARD-MESH
DefaultDirName={localappdata}\Programs\Guard-Mesh-Flasher
DefaultGroupName=Guard-Mesh-Flasher
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputPath}
OutputBaseFilename=Guard-Mesh-Flasher-{#AppVersion}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\Guard-Mesh-Flasher.exe
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "czech"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Vytvořit zástupce na ploše"; Flags: unchecked

[Files]
Source: "{#AppSource}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Guard-Mesh-Flasher"; Filename: "{app}\Guard-Mesh-Flasher.exe"
Name: "{autodesktop}\Guard-Mesh-Flasher"; Filename: "{app}\Guard-Mesh-Flasher.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Guard-Mesh-Flasher.exe"; Description: "Spustit Guard-Mesh-Flasher"; Flags: nowait postinstall skipifsilent
