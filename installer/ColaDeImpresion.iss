; Instalador de Cola de Impresión (Inno Setup 6).
; Compilar con:  .\scripts\build.ps1 -Installer
; (primero genera dist\ con el ejecutable y luego ejecuta ISCC sobre este archivo)
; Las rutas relativas (..\dist, ..\res) son relativas a esta carpeta installer\.
; El instalador queda en dist\ColaDeImpresion-<versión>-setup.exe.

; Constantes del preprocesador de Inno Setup ({#Nombre} las expande).
; AppVersion se actualiza a mano en cada versión (igual que CMakeLists.txt y res\app.rc).
#define AppName "Cola de Impresión"
#define AppVersion "0.5.18"
#define AppPublisher "ChrisCorp"
#define AppExe "ColaDeImpresion.exe"

[Setup]
; AppId identifica la aplicación para actualizaciones y desinstalación: NO
; cambiarlo nunca. La "{{" inicial es la forma de escribir una "{" literal.
AppId={{6C3B7F2E-4A8D-4E51-9C0B-1B7C1D5A2F10}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
; {autopf} = Program Files (o la carpeta de programas del usuario si se
; instala sin administrador). En Program Files la app no puede escribir junto
; al .exe: historial y log van a %APPDATA% (sin portable.txt) y el servidor
; del LAIMI cambiado desde la app va a config.json del usuario (D54).
DefaultDirName={autopf}\ColaDeImpresion
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=ColaDeImpresion-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Solo x64 (el .exe es de 64 bits); x64compatible incluye Windows ARM64 con emulación x64.
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
; Por defecto pide administrador (para Program Files), pero el asistente deja
; elegir instalar solo para el usuario actual sin permisos elevados.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\{#AppExe}
SetupIconFile=..\res\app.ico
; Windows 10 o posterior.
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
; Idiomas del asistente; el primero (español) es el predeterminado.
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
; Casillas opcionales del asistente, ambas desmarcadas por defecto.
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Iniciar con Windows (minimizada en la bandeja)"; GroupDescription: "Opciones:"; Flags: unchecked

[Files]
; Todo sale de dist\ (armada por build.ps1). No se instalan portable.txt,
; servidor-laimi.txt ni los extras de prueba del LAIMI: el instalador ignora
; esos archivos. MANUAL.md y los renderizadores son opcionales
; (skipifsourcedoesntexist).
Source: "..\dist\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\MANUAL.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\renderers\*"; DestDir: "{app}\renderers"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[Icons]
; Accesos en el menú Inicio (aplicación y desinstalador) y, si se eligió, en el Escritorio.
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; Mismo valor que usa la opción "Iniciar con Windows" de la aplicación (HKCU\...\Run).
; (Ver config/Autostart.cpp.) Solo con la tarea "autostart"; uninsdeletevalue
; lo borra al desinstalar. La "\" final de la línea siguiente la continúa.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "ColaDeImpresion"; \
    ValueData: """{app}\{#AppExe}"" --tray"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
; Casilla "Ejecutar" al final del asistente (no en instalaciones silenciosas).
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Cierra la app antes de desinstalar (suele quedar abierta en la bandeja y
; bloquearía el borrado del .exe).
Filename: "{cmd}"; Parameters: "/C taskkill /IM {#AppExe} /F"; Flags: runhidden; RunOnceId: "StopApp"
