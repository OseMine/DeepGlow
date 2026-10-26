; Inno Setup script for DeepGlow 2
; Compile with: iscc DeepGlow2.iss

#define MyAppName "DeepGlow 2"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "DeepGlow"
#define MyAppURL "https://github.com/your-org/DeepGlow"

[Setup]
AppId={{E9F7C3A2-8B42-4D61-9E0A-7F52DB86A413}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppURL={#MyAppURL}
DefaultDirName={pf64}\Common Files\OFX\Plugins
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\..
OutputBaseFilename=DeepGlow2-Setup-Windows
Compression=lzma2/max
SolidCompression=yes
PrivilegesRequired=admin

[Files]
Source: "..\..\bundle\DeepGlow2.ofx.bundle\*"; DestDir: "{app}\DeepGlow2.ofx.bundle"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
Filename: "{cmd}"; Parameters: "/c echo DeepGlow 2 installed. Please restart DaVinci Resolve."; Flags: runhidden waituntilterminated
