<#
.SYNOPSIS
    Compila Cola de Impresión, ejecuta las pruebas y (opcional) prepara la carpeta dist.

.DESCRIPTION
    Busca una toolchain en este orden:
      1. cmake + compilador ya disponibles en el PATH
      2. WinLibs portable en D:\Programacion\Herramientas\winlibs\mingw64 (o $env:COLA_MINGW)
    Con Visual Studio también funciona: abrir "Developer PowerShell" y ejecutar este script.

.EXAMPLE
    .\scripts\build.ps1                 # Release + pruebas
    .\scripts\build.ps1 -Config Debug   # Debug
    .\scripts\build.ps1 -Package        # además copia el .exe y la documentación a dist\
    .\scripts\build.ps1 -Installer      # además compila el instalador (requiere Inno Setup 6)
#>
param(
    [ValidateSet("Release", "Debug")] [string]$Config = "Release",
    [switch]$Package,
    [switch]$Installer,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root ("build-" + $Config.ToLower())

# --- Toolchain ---------------------------------------------------------------
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $candidates = @($env:COLA_MINGW, "D:\Programacion\Herramientas\winlibs\mingw64", "C:\mingw64", "C:\msys64\ucrt64")
    foreach ($c in $candidates) {
        if ($c -and (Test-Path (Join-Path $c "bin\cmake.exe"))) {
            $env:PATH = (Join-Path $c "bin") + ";" + $env:PATH
            Write-Host "Usando toolchain: $c"
            break
        }
    }
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "No se encontró cmake. Instale WinLibs (https://winlibs.com) o Visual Studio y vuelva a intentar."
}

$generator = @()
if (Get-Command ninja -ErrorAction SilentlyContinue) { $generator = @("-G", "Ninja") }

# --- Compilación -------------------------------------------------------------
cmake -S $root -B $buildDir @generator "-DCMAKE_BUILD_TYPE=$Config" -DCOLA_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "Falló la configuración de CMake" }
cmake --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "Falló la compilación" }

# --- Pruebas -----------------------------------------------------------------
if (-not $SkipTests) {
    $tests = Get-ChildItem -Path $buildDir -Recurse -Filter cola_tests.exe | Select-Object -First 1
    & $tests.FullName
    if ($LASTEXITCODE -ne 0) { throw "Hay pruebas fallidas" }
}

$exe = Get-ChildItem -Path $buildDir -Recurse -Filter ColaDeImpresion.exe | Select-Object -First 1
Write-Host "`nEjecutable: $($exe.FullName)"

# --- Carpeta de distribución -------------------------------------------------
if ($Package -or $Installer) {
    $dist = Join-Path $root "dist"
    if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
    New-Item -ItemType Directory $dist | Out-Null
    Copy-Item $exe.FullName $dist
    Copy-Item (Join-Path $root "README.md") $dist
    Copy-Item (Join-Path $root "docs\MANUAL.md") $dist -ErrorAction SilentlyContinue
    New-Item -ItemType Directory (Join-Path $dist "renderers") | Out-Null
    $thirdParty = Join-Path $root "third_party\renderers"
    if (Test-Path (Join-Path $thirdParty "gswin64c.exe")) {
        # Preparados con scripts\get-renderers.ps1
        Copy-Item (Join-Path $thirdParty "*") (Join-Path $dist "renderers")
        Write-Host "Renderizadores (Ghostscript/GhostPCL/GhostXPS) incluidos."
    } else {
        Set-Content -Path (Join-Path $dist "renderers\LEEME.txt") -Encoding utf8 -Value @"
Copie aquí (opcional) gswin64c.exe, gpcl6win64.exe o gxpswin64.exe y sus DLL
para habilitar la vista previa de trabajos PostScript, PCL y XPS
(o ejecute scripts\get-renderers.ps1 antes de build.ps1 -Package).
También se detectan automáticamente si Ghostscript está instalado en
C:\Program Files\gs\.
"@
    }

    # --- Extras para llevar en USB y probar en el LAIMI (el instalador los ignora) ---
    # Modo portable: config, historial y app.log quedan junto al .exe.
    Set-Content -Path (Join-Path $dist "portable.txt") -Encoding utf8 -Value `
        "Si este archivo existe, la configuración, el historial y app.log se guardan en esta carpeta."
    # Lanzador que conecta directo al servidor del laboratorio.
    Set-Content -Path (Join-Path $dist "Abrir vtec-laimiprint.bat") -Encoding ascii -Value @"
@echo off
start "" "%~dp0ColaDeImpresion.exe" --server vtec-laimiprint
"@
    # Windows marca los archivos que vienen de un USB o de Internet (Mark of the
    # Web) y eso dispara SmartScreen; este .bat quita la marca de toda la carpeta.
    Set-Content -Path (Join-Path $dist "Desbloquear archivos.bat") -Encoding ascii -Value @"
@echo off
rem Quita la marca "archivo de otro equipo" (Mark of the Web) de esta carpeta.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Get-ChildItem -LiteralPath '%~dp0' -Recurse -File | Unblock-File; Write-Host 'Listo: archivos desbloqueados.'"
pause
"@
    Copy-Item (Join-Path $root "docs\PRUEBAS.md") $dist -ErrorAction SilentlyContinue
    $testjob = Get-ChildItem -Path $buildDir -Recurse -Filter cola_testjob.exe | Select-Object -First 1
    if ($testjob) {
        New-Item -ItemType Directory (Join-Path $dist "herramientas") | Out-Null
        Copy-Item $testjob.FullName (Join-Path $dist "herramientas")
    }
    Set-Content -Path (Join-Path $dist "LEEME - PRUEBA EN EL LAIMI.txt") -Encoding utf8 -Value @"
COLA DE IMPRESIÓN — PRUEBA EN EL LAIMI
======================================

1. Copie esta carpeta completa al USB (o al Escritorio de la PC del laboratorio
   si no se permite ejecutar programas desde USB).

2. Si la copió desde un USB o la descargó, ejecute una vez
   "Desbloquear archivos.bat": quita la marca que Windows pone a los archivos
   de otro equipo y que hace aparecer la advertencia de SmartScreen.

3. Doble clic en "Abrir vtec-laimiprint.bat"
   (o abra ColaDeImpresion.exe y presione Ctrl+O, escriba vtec-laimiprint y Enter).
   Si aun así Windows muestra "Windows protegió su PC": Más información >
   Ejecutar de todas formas. El programa no está firmado digitalmente todavía;
   sus datos (empresa ChrisCorp, versión, descripción) se ven en Propiedades >
   Detalles del ejecutable.

4. Revise la barra inferior:
   - "En vivo" o "Actualización por sondeo" (ambos funcionan).
   - "Sin permisos de administración": se ven los trabajos, pero no se pueden
     cancelar/reordenar los de otros usuarios con esta cuenta.

   - "Retención activa": todo trabajo nuevo queda en pausa ("Retenido") hasta
     que alguien lo libere con Ctrl+R. Se activa/desactiva con Ctrl+Shift+H
     o desde el menú. Solo funciona mientras la app esté abierta.

   - Si aparece "N trabajos que el spooler cuenta no se pudieron leer", la cola
     de Windows tiene trabajos que la app no logró enumerar: anótelo y traiga
     app.log.

5. Novedades de esta versión que conviene probar:
   - ACCESO RÁPIDO: la barra bajo la cabecera. Con el botón "+" elija qué
     impresoras aparecen ahí; un clic filtra la cola por esa impresora y
     Ctrl+1…Ctrl+9 hacen lo mismo con el teclado. Clic derecho sobre una:
     pausar, reanudar, vaciar o quitarla de la barra.
   - ORDEN DE LA COLA: F6 cambia el criterio (cola, equipo, hora, usuario,
     documento, estado, tamaño) y Shift+F6 lo invierte. También en
     Menú > Ordenar la cola por. El criterio activo se ve abajo a la derecha.

6. Pruebe lo que indica PRUEBAS.md (sección 3) y anote los resultados.
   F1 muestra todos los atajos de teclado.

7. Al terminar, cierre la app desde el ícono de la bandeja (clic derecho > Salir)
   y traiga de vuelta esta carpeta: app.log, config.json e historial.json
   sirven para revisar cualquier problema.

VISTA PREVIA
- La carpeta "renderers" trae Ghostscript, GhostPCL y GhostXPS para ver los
  trabajos PCL, PostScript, PDF y XPS (ver renderers\LEEME.txt). No la separe
  del .exe. Si falta, esos trabajos se listan igual pero sin vista previa.
- La vista previa lee el archivo de spool que guarda EL SERVIDOR; no depende de
  la PC donde se imprimió. Para leerlo hace falta permiso sobre la carpeta de
  spool: ejecutando la app EN el servidor basta con ser administrador local ahí
  (Menú > Reiniciar como administrador); desde otra PC hay que ser
  administrador de vtec-laimiprint (se usa \\vtec-laimiprint\C$).
- Si no encuentra el archivo, el panel ahora dice exactamente en qué carpetas
  buscó y por qué falló cada una. Si el motivo es "acceso denegado", es
  permisos; si dice que no existe, anote el texto y tráigalo junto con app.log.
- También se puede indicar una carpeta de spool alternativa en config.json
  ("spoolDirOverrides").

OPCIONAL
- herramientas\cola_testjob.exe crea trabajos de prueba que quedan en cola
  sin imprimir (se abortan solos). Ejemplo, en una consola:
    herramientas\cola_testjob.exe "\\vtec-laimiprint\NOMBRE-IMPRESORA" --jobs 2 --pages 3 --seconds 120 --pause
  ¡Sin --pause y con --finish SÍ se imprimen de verdad!
"@
    Write-Host "Distribución lista en $dist"
}

if ($Installer) {
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { throw "No se encontró Inno Setup 6 (ISCC.exe). Descárguelo de https://jrsoftware.org/isinfo.php" }
    & $iscc (Join-Path $root "installer\ColaDeImpresion.iss")
    if ($LASTEXITCODE -ne 0) { throw "Falló el instalador" }
}
