; TickerBar installer — Inno Setup 6 script (free: https://jrsoftware.org/isdl.php)
; Build via build-release.bat, or manually: ISCC TickerBar.iss
; Produces dist\TickerBar-Setup.exe — the single file to send a friend.
; The app is built with a STATIC MSVC runtime (/MT), so normally the exe
; needs nothing else. If dist\vc_redist.x64.exe is present at compile time
; (dynamic-link fallback), it is bundled and installed silently instead.

#define MyAppName "TickerBar"
; The version is SINGLE-SOURCED from the root CMakeLists.txt
; project(VERSION). build-release.bat and CI pass it in as
; ISCC /DMyAppVersion=<version>. The literal below is ONLY the
; fallback for a bare manual `ISCC TickerBar.iss` with no /D.
#ifndef MyAppVersion
#define MyAppVersion "0.6.18"
#endif
#define MyAppExeName "TickerBar.exe"

[Setup]
AppId={{4C9A2E71-8B3F-4D6A-9E21-5F7C0A1B8D34}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=dist
OutputBaseFilename=TickerBar-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
; NOTE: the exe is unsigned, so Windows SmartScreen will warn recipients
; on first run ("More info" -> "Run anyway"). That is expected for free
; unsigned software and is explained in README_TEST.md.

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "dist\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
#if FileExists("dist\vc_redist.x64.exe")
Source: "dist\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall
#endif

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
#if FileExists("dist\vc_redist.x64.exe")
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Visual C++ runtime..."; Flags: waituntilterminated
#endif
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
; Updater relaunch (ported from MeterBar, where it landed with its
; v0.19 updater work): the entry above is (correctly) SKIPPED for
; silent installs — postinstall offers are not shown silently — so an
; in-app update (which runs this installer with /SILENT) would leave
; the app closed. This second entry has NO skipifsilent and is gated
; to silent installs only (WizardSilent), so a silent update relaunches
; the new version while interactive installs keep the behaviour above.
; Fiona reported 2026-10-31 that after updating TickerBar — in-app,
; and even after a manual install of the 0.5.0 Setup.exe — the app
; still reported 0.4.0 and kept offering the 0.5.0 update; the missing
; relaunch entry was the defect.
Filename: "{app}\{#MyAppExeName}"; Flags: nowait; Check: WizardSilent
