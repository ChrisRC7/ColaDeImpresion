<#
.SYNOPSIS
    Descarga Ghostscript, GhostPCL y GhostXPS (Artifex) y los prepara en
    third_party\renderers para que build.ps1 -Package los copie a dist\renderers.

.DESCRIPTION
    - Ghostscript (PostScript/PDF) viene como instalador firmado: se verifica la
      firma de Artifex y se extraen solo gswin64c.exe + gsdll64.dll con 7-Zip.
    - GhostPCL (PCL 5 / PCL XL) y GhostXPS (XPS) vienen como ZIP portables.
    - Se copian las DLL del runtime de Visual C++ (msvcp140, vcruntime140,
      vcruntime140_1) desde System32 para que funcionen en equipos que no lo
      tengan instalado (despliegue local permitido por Microsoft).
    Licencia de Ghostscript/GhostPCL/GhostXPS: AGPL (se usan como programas externos).

.EXAMPLE
    .\scripts\get-renderers.ps1
    .\scripts\get-renderers.ps1 -Tag gs10080 -Version 10.08.0

.NOTES
    Decisión D7: los intérpretes se ejecutan como procesos externos (no se
    enlazan), así la licencia AGPL no alcanza al código de la app.
    third_party\ no se versiona; build.ps1 -Package la copia a dist\renderers
    (o la rescata de un dist\ anterior si falta). Requiere 7-Zip e Internet.
#>
param(
    # Etiqueta del release de ghostpdl-downloads en GitHub (gs + versión sin puntos).
    [string]$Tag = "gs10080",
    # Versión con puntos, usada en los nombres de los ZIP de GhostPCL/GhostXPS.
    # Debe corresponder a $Tag.
    [string]$Version = "10.08.0"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
# Destino final (se borra y se rehace) y carpeta temporal de descargas.
$out = Join-Path $root "third_party\renderers"
$work = Join-Path $env:TEMP "cola-renderers"
$base = "https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/download/$Tag"

# 7-Zip hace falta para extraer archivos del instalador NSIS de Ghostscript
# sin ejecutarlo. Se busca en Program Files y luego en el PATH.
$sevenZip = @("$env:ProgramFiles\7-Zip\7z.exe", "${env:ProgramFiles(x86)}\7-Zip\7z.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $sevenZip) { $sevenZip = (Get-Command 7z -ErrorAction SilentlyContinue).Source }
if (-not $sevenZip) { throw "Se necesita 7-Zip para extraer el instalador de Ghostscript." }

if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory $out | Out-Null

# Nombre del instalador de Ghostscript: "gs10080" -> "gs10080w64.exe".
$gsExe = "gs$($Tag.Substring(2))w64.exe"
$files = @($gsExe, "ghostpcl-$Version-win64.zip", "ghostxps-$Version-win64.zip")
foreach ($f in $files) {
    Write-Host "Descargando $f..."
    Invoke-WebRequest -Uri "$base/$f" -OutFile (Join-Path $work $f) -UseBasicParsing
}

# --- Ghostscript ---------------------------------------------------------------
# Antes de extraer nada se exige una firma Authenticode válida de Artifex
# (protege contra una descarga alterada). Solo se extraen la versión de
# consola (gswin64c.exe), su DLL y la licencia.
$sig = Get-AuthenticodeSignature (Join-Path $work $gsExe)
if ($sig.Status -ne "Valid" -or $sig.SignerCertificate.Subject -notmatch "Artifex") {
    throw "La firma del instalador de Ghostscript no es válida: $($sig.Status)"
}
& $sevenZip x -y "-o$work\gs" (Join-Path $work $gsExe) "bin\gswin64c.exe" "bin\gsdll64.dll" "doc\COPYING" | Out-Null
Copy-Item "$work\gs\bin\gswin64c.exe", "$work\gs\bin\gsdll64.dll" $out
Copy-Item "$work\gs\doc\COPYING" (Join-Path $out "LICENSE-ghostscript.txt") -ErrorAction SilentlyContinue

# --- GhostPCL y GhostXPS -------------------------------------------------------
# ZIP portables: se copian todos sus .exe y .dll (sin subcarpetas) a $out.
foreach ($pkg in @("ghostpcl", "ghostxps")) {
    Expand-Archive -Path (Join-Path $work "$pkg-$Version-win64.zip") -DestinationPath $work -Force
    $dir = Join-Path $work "$pkg-$Version-win64"
    Get-ChildItem $dir -Include *.exe, *.dll -Recurse | Copy-Item -Destination $out
    Copy-Item (Join-Path $dir "LICENSE") (Join-Path $out "LICENSE-$pkg.txt") -ErrorAction SilentlyContinue
}

# --- Runtime de Visual C++ (despliegue local) ------------------------------------
# Los binarios de Artifex se compilan con MSVC; las DLL se toman de la PC que
# ejecuta este script (si falta alguna solo se avisa).
foreach ($dll in @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
    $src = Join-Path $env:SystemRoot "System32\$dll"
    if (Test-Path $src) { Copy-Item $src $out }
    else { Write-Warning "No se encontró $dll en System32: instale el runtime de Visual C++ 2015-2022 x64." }
}

# Nota que acompaña a los renderizadores (termina en dist\renderers).
Set-Content -Path (Join-Path $out "LEEME.txt") -Encoding utf8 -Value @"
Renderizadores opcionales para la vista previa (Artifex, versión $Version):
  gswin64c.exe   + gsdll64.dll    Ghostscript  -> PostScript y PDF
  gpcl6win64.exe + gpcl6dll64.dll GhostPCL     -> PCL 5 y PCL XL (PCL 6)
  gxpswin64.exe  + gxpsdll64.dll  GhostXPS     -> XPS
  msvcp140 / vcruntime140*.dll    runtime de Visual C++ que necesitan los anteriores
La aplicación los detecta automáticamente en esta carpeta (renderers\ junto al .exe).
Licencia: GNU AGPL v3 (ver LICENSE-*.txt). Se ejecutan como programas externos.
Origen: https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/tag/$Tag
"@

# Limpieza de los temporales y resumen de lo que quedó.
Remove-Item $work -Recurse -Force
Write-Host "Renderizadores listos en $out"
Get-ChildItem $out | Format-Table Name, Length -AutoSize
