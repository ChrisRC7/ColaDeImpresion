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
    .\scripts\build.ps1 -Package        # además arma dist\ limpia (sin logs ni historial) y "dist\ColaDeImpresion <versión>.zip"
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
    # Además de las rutas habituales, "Herramientas\winlibs" junto a la carpeta del proyecto.
    $sibling = Join-Path (Split-Path -Parent $root) "Herramientas\winlibs\mingw64"
    $candidates = @($env:COLA_MINGW, "D:\Programacion\Herramientas\winlibs\mingw64", $sibling, "C:\mingw64", "C:\msys64\ucrt64")
    foreach ($c in $candidates) {
        # Concatenación simple: Join-Path falla si la unidad no existe (p. ej. un USB retirado).
        if ($c -and (Test-Path "$c\bin\cmake.exe" -ErrorAction SilentlyContinue)) {
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
    $thirdParty = Join-Path $root "third_party\renderers"
    # third_party no se versiona: si falta pero el paquete anterior trae los
    # renderizadores, se rescatan antes de borrar dist\ (si no, el paquete nuevo
    # quedaría sin vista previa de PCL/PostScript/XPS).
    $oldRenderers = Join-Path $dist "renderers"
    if (-not (Test-Path (Join-Path $thirdParty "gswin64c.exe")) -and (Test-Path (Join-Path $oldRenderers "gswin64c.exe"))) {
        New-Item -ItemType Directory $thirdParty -Force | Out-Null
        Copy-Item (Join-Path $oldRenderers "*") $thirdParty
        Write-Host "Renderizadores recuperados del paquete anterior en $thirdParty"
    }
    if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
    New-Item -ItemType Directory $dist | Out-Null
    Copy-Item $exe.FullName $dist
    Copy-Item (Join-Path $root "README.md") $dist
    Copy-Item (Join-Path $root "docs\MANUAL.md") $dist -ErrorAction SilentlyContinue
    New-Item -ItemType Directory (Join-Path $dist "renderers") | Out-Null
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
    Copy-Item (Join-Path $root "docs\DATOS_DE_IMPRESION.md") $dist -ErrorAction SilentlyContinue
    New-Item -ItemType Directory (Join-Path $dist "herramientas") -Force | Out-Null
    foreach ($tool in @("cola_testjob.exe", "cola_jobinspect.exe")) {
        $found = Get-ChildItem -Path $buildDir -Recurse -Filter $tool | Select-Object -First 1
        if ($found) { Copy-Item $found.FullName (Join-Path $dist "herramientas") }
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
   - VARIAS COPIAS ABIERTAS (0.5.9): con la app abierta en dos PCs a la vez,
     envíe un PDF de varios MB a una cara a una Epson. En las dos PCs la
     columna Doble cara debe decir "Una cara" (antes una de ellas podía
     marcarlo como RAW y mostrar "Borde largo", la configuración de la cola).
     En app.log puede aparecer "Datos del trabajo N incompletos": es normal,
     se reintenta solo. Mientras no se pudieron leer los datos de una Epson,
     Doble cara muestra "—". Al liberarlo (F11) debe imprimirse bien, sin
     hojas en blanco.
   - ACERCA DE (0.5.7 y 0.5.8): en la cabecera, a la derecha del botón de
     modo claro/oscuro, hay un botón con un signo "!" en un círculo. Abre la
     ventana "Acerca de" con la versión y los autores principales. También
     está en el menú (F10) > "Acerca de…". Esc o Cerrar la cierran.
   - EXPORTAR UN RANGO DE FECHAS (0.5.6): en el historial pulse "Exportar a
     Excel" (o Ctrl+Shift+E). Aparece un cuadro: "Todo el historial" o
     "Solo un rango de fechas". Elija Desde y Hasta (solo aparecen los días
     con registros) y vea abajo cuántos registros entran. Exporte: el
     archivo se llama "Historial de impresión <desde> a <hasta>.xlsx", la
     hoja Historial trae solo esos días y el Resumen dice el período.
   - RESULTADO EN EL HISTORIAL (0.5.5): libere un trabajo con F11 (o
     Ctrl+R) y espere a que se imprima. En el historial (Ctrl+H) debe decir
     "Impreso" (antes quedaba como "Retirado" si se imprimía muy rápido) y
     debe aparecer UNA sola vez (antes la copia que esta PC envía al
     servidor se agregaba como otro registro "Retirado", con equipo
     \\PC y tamaño 0). Al exportar a Excel, las hojas "Por papel", "Por
     usuario", etc. tienen datos; si no hay impresos, lo dicen.
   - RESUMEN DE EXCEL EN HOJAS (0.5.4): exporte el historial (Ctrl+Shift+E)
     y abra el archivo. La hoja "Resumen" tiene solo los datos generales y
     cada tabla está en su propia hoja: "Por resultado", "Por papel", "Por
     impresora", "Por doble cara", "Por orientación", "Por origen del papel",
     "Por usuario", "Por equipo", "Por día" y "Por hora". Al bajar en una
     lista larga (usuarios, equipos) el título y los encabezados quedan fijos.
   - PÁGINAS DE LAS EPSON (0.5.2 y 0.5.3): envíe a LAIMI 1 - INK documentos
     de 1, 2 y 3 páginas. La columna Páginas debe decir 1, 2 y 3 (antes decía 2, 2
     y 4: Windows cuenta el dorso en blanco de la última hoja a doble cara).
     El panel de detalles (Ctrl+B) también debe decir "1 pág." arriba (0.5.3).
     En la pestaña Configuración (F7), "Páginas en los datos" dice "Páginas
     contadas en los datos (ESC/Page)" y, si difiere, cuánto informa el
     spooler. Documentos de más de 16 MB (unas 20 páginas) siguen usando el
     número del spooler, marcado como estimado.
   - PÁGINAS EN EL HISTORIAL (0.5.1): libere un trabajo de 2 páginas x 2
     copias (o 4 páginas a doble cara) y espere a que se imprima. En el
     historial (Ctrl+H) la columna Páginas debe decir lo mismo que decía en la
     cola (4), y el Tamaño no debe quedar en "—".
   - TOTAL DE LA SELECCIÓN (0.5.1): seleccione uno o varios trabajos (Ctrl+A,
     Mayús+clic o arrastrando). Abajo a la izquierda aparece "Selección: N
     trabajo(s) · X páginas a imprimir" con la suma de la columna Páginas.
     También funciona en el historial.
   - EXPORTAR A EXCEL (0.5.1): en el historial, botón "Exportar a Excel"
     (o Ctrl+Shift+E). Guarde el archivo y ábralo: hoja "Historial" con todos
     los registros, hoja "Resumen" con los datos generales y una hoja por
     tabla (páginas impresas por papel, impresora, doble cara, usuario,
     equipo, día y hora).
   - HISTORIAL COMPLETO: imprima o cancele un trabajo y abra el historial
     (Ctrl+H). Debe tener las mismas columnas que tenía en la cola (#,
     Páginas = páginas x copias, Papel, Orientación, Doble cara, Origen del
     papel, Tamaño, Enviado) más Resultado y Finalizado. Los registros de
     versiones anteriores muestran "—" en las columnas nuevas.
   - CAMBIAR EL TAMAÑO DE LA VENTANA: arrastre el borde para achicarla y
     agrandarla; la búsqueda, los botones de pausa y los íconos de la derecha
     deben acomodarse mientras se arrastra, sin encimarse.
   - HOJAS EN BLANCO (0.4.9): imprima un documento grande (p. ej. un PDF de
     50 páginas) con la app abierta y libérelo; debe salir completo.
   - COLUMNAS: arrastre el borde derecho del título de una columna para
     cambiar su ancho (doble clic en el borde = ajustar al contenido). Si no
     caben, aparece una barra horizontal. Clic derecho en los títulos >
     "Restablecer el ancho de las columnas".
   - NINGUNA COLUMNA DESAPARECE: achique la ventana o abra el panel de
     detalles (Ctrl+B): todas las columnas siguen (también Enviado) y aparece
     la barra horizontal para llegar a las de la derecha.
   - PANEL DE ATAJOS (F1): desplácese con la rueda del mouse (o el touchpad)
     y arrastrando la barra de la derecha hasta el final de la lista; ya no
     debe volver sola a la fila elegida.
   - MOVER COLUMNAS: arrastre el título de una columna a la izquierda o a la
     derecha (una línea azul marca dónde va a quedar) y suéltelo. El orden se
     guarda y se mantiene al volver a abrir. Un clic sin arrastrar sigue
     ordenando. Clic derecho en los títulos > "Restablecer el orden de las
     columnas" vuelve al orden original. Pruébelo en la vista "Todas", con
     una sola impresora y en el historial (Ctrl+H).
   - BOTONES DE PAUSA junto a la búsqueda: "Pausar / Reanudar trabajos" (los
     seleccionados) y "Pausar / Reanudar impresora".
   - IMPRIMIR Y VOLVER A PAUSAR: seleccione uno o varios trabajos retenidos
     y presione F11 (o Ctrl+Enter). Se reanudan esos trabajos y su impresora;
     cuando todos terminaron de enviarse, la impresora vuelve a quedar en
     pausa sola. La barra inferior lo indica mientras tanto. También en el
     clic derecho y en el menú.
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
- La vista previa lee los datos del trabajo CON LA API DEL SPOOLER (como lo
  hace el propio Windows): basta con tener "Administrar documentos" en la cola;
  ya no hace falta ser administrador del servidor ni acceder a \\SERVIDOR\C$.
  Si eso falla, se intenta el archivo de spool como antes.
- Los trabajos de las Epson del LAIMI llegan en ESC/Page (lenguaje propio de
  Epson, generado en la PC del estudiante): para ellos la vista previa dice
  "no disponible" y explica por qué. Lo que sí se ve siempre es la pestaña
  CONFIGURACIÓN (F7): papel, orientación, doble cara, copias, páginas, caras y
  hojas previstas, cada dato con su origen y su fiabilidad
  (Fiable / Estimado / No determinable). Detalle en DATOS_DE_IMPRESION.md.
- Si no se pueden leer los datos, el panel dice qué se intentó y por qué falló.

OPCIONAL
- herramientas\cola_testjob.exe crea trabajos de prueba que quedan en cola
  sin imprimir (se abortan solos). Ejemplo, en una consola:
    herramientas\cola_testjob.exe "\\vtec-laimiprint\NOMBRE-IMPRESORA" --jobs 2 --pages 3 --seconds 120 --pause
  ¡Sin --pause y con --finish SÍ se imprimen de verdad!
- herramientas\cola_jobinspect.exe muestra en consola la misma ficha
  "Configuración" para cada trabajo (y con --csv arma la matriz de pruebas):
    herramientas\cola_jobinspect.exe "\\vtec-laimiprint\NOMBRE-IMPRESORA" --data --csv pruebas.csv --label "1 pag x 10 copias"
  Ver la sección "Pruebas pendientes en el LAIMI" de DATOS_DE_IMPRESION.md.
"@
    # Paquete limpio: sin configuración, historial ni logs de pruebas (en modo
    # portable se crean junto al .exe la primera vez que se abre).
    Get-ChildItem $dist -Recurse -File -Include "app.log*", "historial.json", "config.json" | Remove-Item -Force

    # .zip con todo el contenido de dist\ (dentro, una carpeta con la versión).
    # Las rutas internas se escriben con "/" (CreateFromDirectory de .NET
    # Framework usa "\", que otros descompresores no entienden).
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $version = ([regex]::Match((Get-Content (Join-Path $root "CMakeLists.txt") -Raw),
                               "project\(ColaDeImpresion VERSION ([0-9.]+)")).Groups[1].Value
    $zipName = "ColaDeImpresion $version.zip"
    $files = Get-ChildItem $dist -Recurse -File
    $zipPath = Join-Path $dist $zipName
    $zip = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($dist.Length + 1).Replace("\", "/")
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName,
                "ColaDeImpresion $version/$relative", [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $zip.Dispose() }
    Write-Host "Distribución lista en $dist (y $zipName)"
}

if ($Installer) {
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { throw "No se encontró Inno Setup 6 (ISCC.exe). Descárguelo de https://jrsoftware.org/isinfo.php" }
    & $iscc (Join-Path $root "installer\ColaDeImpresion.iss")
    if ($LASTEXITCODE -ne 0) { throw "Falló el instalador" }
}
