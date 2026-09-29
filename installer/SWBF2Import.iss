; ============================================================================
;  SWBF2Import.iss - Setup fuer das 3ds-Max-Plugin (Inno Setup 6)
;
;  Gebaut von installer\BAUE_RELEASE.bat (nicht von Hand aufrufen - das Skript
;  stellt vorher das Paket in dist\paket\SWBF2Import zusammen).
;
;  Ziel (Autodesk "Packaging Plug-ins"): 3ds Max durchsucht
;    %ProgramData%\Autodesk\ApplicationPlugins\<name>   alle Benutzer (Admin)
;    %AppData%\Autodesk\ApplicationPlugins\<name>       nur dieser Benutzer
;  {autoappdata} ist je nach Installationsart genau einer der beiden Orte
;  (PrivilegesRequiredOverridesAllowed=dialog: der Benutzer waehlt beim Start).
;
;  Die .dlu braucht die Visual-C++-Laufzeit v14 (MSVCP140, VCRUNTIME140,
;  VCRUNTIME140_1), gebaut mit MSVC 14.50 - die Laufzeit muss mindestens so
;  neu sein. Liegt vc_redist.x64.exe (von Microsoft signiert) in redist\, wird
;  sie mitgeliefert und bei Bedarf installiert. Nichts wird zur Laufzeit aus
;  dem Internet geladen.
; ============================================================================

#ifndef AppVer
  #define AppVer "1.43.0"
#endif
#ifndef PaketDir
  #define PaketDir "..\dist\paket\SWBF2Import"
#endif
#define RedistDatei "redist\vc_redist.x64.exe"
#define LaufzeitMinor 50

[Setup]
; AppId = UpgradeCode aus PackageContents.xml - bleibt fuer immer gleich,
; damit jede neue Fassung die alte ersetzt statt daneben zu liegen.
AppId={{7C41A9E2-58D6-4B13-8F0A-2D97E5C4B681}
AppName=SWBF2 Import for 3ds Max
AppVersion={#AppVer}
AppVerName=SWBF2 Import {#AppVer} for 3ds Max
AppPublisher=DH
AppPublisherURL=https://github.com/DennisHerrm
AppSupportURL=https://github.com/DennisHerrm
VersionInfoVersion={#AppVer}.0
VersionInfoProductVersion={#AppVer}.0
VersionInfoDescription=SWBF2 Import for 3ds Max - Setup
VersionInfoCompany=DH
VersionInfoCopyright=DH
VersionInfoProductName=SWBF2 Import for 3ds Max
DefaultDirName={autoappdata}\Autodesk\ApplicationPlugins\SWBF2Import
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
UsePreviousPrivileges=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=..\dist
OutputBaseFilename=SWBF2Import-{#AppVer}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=SWBF2 Import {#AppVer} (3ds Max)
UninstallDisplayIcon={sys}\shell32.dll,-16770
CloseApplications=no
SetupLogging=yes

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
de.MaxLaeuft=3ds Max läuft noch und hält die alte Plugin-Datei fest.%n%nBitte alle 3ds-Max-Fenster schließen und dann auf „Wiederholen“ klicken.
en.MaxLaeuft=3ds Max is still running and holds the old plugin file.%n%nPlease close all 3ds Max windows, then click "Retry".
de.LaufzeitInstallieren=Visual C++-Laufzeit von Microsoft wird installiert ...
en.LaufzeitInstallieren=Installing the Microsoft Visual C++ runtime ...
de.LaufzeitFehlt=Die Visual C++-Laufzeit (2015-2022, Version 14.{#LaufzeitMinor} oder neuer) fehlt auf diesem PC. Ohne sie kann 3ds Max das Plugin nicht laden.%n%nBei einer Installation nur für diesen Benutzer darf das Setup sie nicht installieren. Bitte einmal von Microsoft installieren:%nhttps://aka.ms/vc14/vc_redist.x64.exe
en.LaufzeitFehlt=The Visual C++ runtime (2015-2022, version 14.{#LaufzeitMinor} or newer) is missing on this PC. Without it 3ds Max cannot load the plugin.%n%nA per-user installation is not allowed to install it. Please install it once from Microsoft:%nhttps://aka.ms/vc14/vc_redist.x64.exe
de.Fertig=Starte 3ds Max neu. Das Menü „EAfront Tool“ und der Import „Star Wars Battlefront II“ sind dann da.
en.Fertig=Restart 3ds Max. The "EAfront Tool" menu and the "Star Wars Battlefront II" importer will be there.

[InstallDelete]
; Reste einer aelteren Fassung (etwa ein nicht mehr gebauter Jahrgang) muessen
; weg - Max laedt jede .dlu, die im Paket liegt.
Type: filesandordirs; Name: "{app}\Contents"

[Files]
Source: "{#PaketDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
#if FileExists(AddBackslash(SourcePath) + RedistDatei)
Source: "{#RedistDatei}"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: LaufzeitNoetig
#endif

[Run]
#if FileExists(AddBackslash(SourcePath) + RedistDatei)
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "{cm:LaufzeitInstallieren}"; Flags: waituntilterminated; Check: LaufzeitNoetig and IsAdminInstallMode
#endif

[UninstallDelete]
Type: filesandordirs; Name: "{app}\Contents"

[Code]
// Laeuft irgendwo 3dsmax.exe? (WMI - gleich fuer alle Max-Jahrgaenge)
function MaxLaeuft(): Boolean;
var
  Locator, Dienst, Liste: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Dienst := Locator.ConnectServer('.', 'root\CIMV2');
    Liste := Dienst.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''3dsmax.exe''');
    Result := Liste.Count > 0;
  except
    Result := False;
  end;
end;

function WartenBisMaxZu(): Boolean;
begin
  Result := True;
#ifdef OhneMaxPruefung
  Exit;   // nur fuer den automatischen Test (ISCC /DOhneMaxPruefung), nie im Release
#endif
  while MaxLaeuft() do
  begin
    if SuppressibleMsgBox(CustomMessage('MaxLaeuft'), mbError, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

// Visual-C++-Laufzeit v14 da und neu genug?
function LaufzeitNoetig(): Boolean;
var
  Installiert, Minor: Cardinal;
begin
  Result := True;
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installiert) and
     (Installiert = 1) and
     RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Minor', Minor) and
     (Minor >= {#LaufzeitMinor}) then
    Result := False;
end;

function InitializeSetup(): Boolean;
begin
  Result := WartenBisMaxZu();
end;

function InitializeUninstall(): Boolean;
begin
  Result := WartenBisMaxZu();
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if LaufzeitNoetig() and not IsAdminInstallMode() then
      SuppressibleMsgBox(CustomMessage('LaufzeitFehlt'), mbInformation, MB_OK, IDOK);
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo + NewLine + NewLine + CustomMessage('Fertig');
end;
