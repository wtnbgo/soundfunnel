; SPDX-License-Identifier: GPL-3.0-or-later
; Inno Setup script for SoundFunnel (Windows installer).
;
; Build (after building the Release exe):
;   iscc /DAppVersion=0.1.0 /DSourceExe=..\..\build\SoundFunnel_artefacts\Release\SoundFunnel.exe packaging\windows\SoundFunnel.iss
; The installer is written to build\installer\.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceExe
  #define SourceExe "..\..\build\SoundFunnel_artefacts\Release\SoundFunnel.exe"
#endif

[Setup]
AppId={{6B3C1D2E-8F4A-4E7B-9C51-2D7A0F3E9B64}
AppName=SoundFunnel
AppVersion={#AppVersion}
AppPublisher=SoundFunnel contributors
AppPublisherURL=https://github.com/wtnbgo/soundfunnel
AppSupportURL=https://github.com/wtnbgo/soundfunnel/issues
DefaultDirName={autopf}\SoundFunnel
DefaultGroupName=SoundFunnel
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputDir=..\..\build\installer
OutputBaseFilename=SoundFunnel-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\SoundFunnel.exe
CloseApplications=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[CustomMessages]
english.AutoStart=Start SoundFunnel when I sign in
japanese.AutoStart=サインイン時に SoundFunnel を起動する
english.Firewall=Allow SoundFunnel through Windows Firewall (UDP on the local network)
japanese.Firewall=Windows ファイアウォールで SoundFunnel の通信を許可する (LAN 内の UDP)
english.LaunchNow=Start SoundFunnel now
japanese.LaunchNow=SoundFunnel を今すぐ起動する

[Tasks]
Name: "autostart"; Description: "{cm:AutoStart}"
Name: "firewall"; Description: "{cm:Firewall}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\SoundFunnel"; Filename: "{app}\SoundFunnel.exe"
Name: "{autodesktop}\SoundFunnel"; Filename: "{app}\SoundFunnel.exe"; Tasks: desktopicon

[Registry]
; same value the app writes from its own "start at sign-in" checkbox (src/Autostart.cpp)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "SoundFunnel"; \
    ValueData: """{app}\SoundFunnel.exe"" --background"; Tasks: autostart; Flags: uninsdeletevalue
; removes the value on uninstall even when it was turned on from the app instead of the installer
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "SoundFunnel"; Flags: uninsdeletevalue

[Run]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""SoundFunnel"""; Flags: runhidden; Tasks: firewall
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""SoundFunnel"" dir=in action=allow protocol=UDP profile=private,domain remoteip=localsubnet program=""{app}\SoundFunnel.exe"" enable=yes"; \
    Flags: runhidden; Tasks: firewall
Filename: "{app}\SoundFunnel.exe"; Description: "{cm:LaunchNow}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""SoundFunnel"""; Flags: runhidden; RunOnceId: "DelFirewallRule"
