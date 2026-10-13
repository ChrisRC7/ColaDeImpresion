; Instalador de Cola de Impresión (Inno Setup 6).
; Compilar con:  .\scripts\build.ps1 -Installer
; (primero genera dist\ con el ejecutable y luego ejecuta ISCC sobre este archivo)

#define AppName "Cola de Impresión"
#define AppVersion "0.5.8"
#define AppPublisher "ChrisCorp"
#define AppExe "ColaDeImpresion.exe"

[Setup]
AppId={{6C3B7F2E-4A8D-4E51-9C0B-1B7C1D5A2F10}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\ColaDeImpresion
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=ColaDeImpresion-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\{#AppExe}
SetupIconFile=..\res\app.ico
MinVersion=10.0
; Metadatos del propio instalador (sin ellos SmartScreen lo ve como anónimo).
VersionInfoCompany={#AppPublisher}
VersionInfoProductName={#AppName}
VersionInfoDescription=Instalador de {#AppName}
VersionInfoVersion={#AppVersion}
VersionInfoProductVersion={#AppVersion}
VersionInfoCopyright=Copyright (C) 2026 {#AppPublisher}
AppPublisherURL=https://github.com/ChrisRC7/ColaDeImpresion
AppSupportURL=https://github.com/ChrisRC7/ColaDeImpresion
AppUpdatesURL=https://github.com/ChrisRC7/ColaDeImpresion

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Iniciar con Windows (minimizada en la bandeja)"; GroupDescription: "Opciones:"; Flags: unchecked

[Files]
Source: "..\dist\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\MANUAL.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\renderers\*"; DestDir: "{app}\renderers"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; Mismo valor que usa la opción "Iniciar con Windows" de la aplicación (HKCU\...\Run).
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "ColaDeImpresion"; \
    ValueData: """{app}\{#AppExe}"" --tray"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C taskkill /IM {#AppExe} /F"; Flags: runhidden; RunOnceId: "StopApp"
