# Envía trabajos de prueba desde Word (COM) a una impresora, siempre "imprimir a
# archivo" para que no salga papel ni se abran diálogos. Un vigilante
# (cola_jobinspect --hold) los pausa e inspecciona.
#
# Sirve para la matriz de pruebas de docs/DATOS_DE_IMPRESION.md (D24): copias en
# el DEVMODE o repetidas por Word, intercalado, rango de páginas, N páginas por
# hoja hecho por Word, papel A4/personalizado y orientación.
#
# Parámetros:
#   -Printer  impresora destino (nombre como lo muestra Word/Windows).
#   -Out      carpeta donde quedan los .docx y los .prn (se crea si no existe).
#   -Prefix   prefijo del nombre de cada documento (para distinguir series).
#   -Only     número de una sola prueba ("01".."07"); vacío = todas.
#
# Notas (GUIA_IA §6): guardar este archivo con BOM UTF-8 (PowerShell 5.1 lee
# mal los acentos sin él); los métodos de Word por COM se llaman con
# InvokeMember y valores desenvueltos (ver Plain/Call/CallNamed). Con el
# vigilante pausando los trabajos, PrintOut a archivo espera a que se reanuden:
# usar cola_jobinspect --hold con --release.
param([string]$Printer, [string]$Out, [string]$Prefix = "W", [string]$Only = "")
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $Out | Out-Null
# Word invisible y sin cuadros de alerta (DisplayAlerts = wdAlertsNone): la
# serie corre sin intervención. ActivePrinter cambia la impresora solo para
# esta instancia de Word.
$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0
$word.ActivePrinter = $Printer

# Devuelve una copia del arreglo con cada valor "desenvuelto" (BaseObject).
# La coma de "return ,$out" evita que PowerShell desarme el arreglo al devolverlo.
function Plain([object[]]$values) {
    # PowerShell envuelve los valores en PSObject y COM los recibe con otro tipo.
    $out = New-Object object[] $values.Count
    for ($i = 0; $i -lt $values.Count; $i++) { $out[$i] = ([psobject]$values[$i]).psobject.BaseObject }
    return ,$out
}

# Llama a un método COM con parámetros posicionales mediante reflexión
# (InvokeMember), sin el enlace de PowerShell que exige [ref] en Word.
function Call($obj, [string]$method, [object[]]$params) {
    $params = Plain $params
    [System.__ComObject].InvokeMember($method, [Reflection.BindingFlags]::InvokeMethod, $null, $obj, $params)
}

# Igual que Call pero con parámetros con nombre (PrintOut tiene 17 opcionales):
# las claves del diccionario ordenado son los nombres y sus valores, los argumentos.
function CallNamed($obj, [string]$method, [System.Collections.Specialized.OrderedDictionary]$named) {
    $names = [string[]]@($named.Keys)
    $values = Plain @($named.Values)
    [System.__ComObject].InvokeMember($method, [Reflection.BindingFlags]::InvokeMethod, $null, $obj, $values, $null, $null, $names)
}

# Crea un documento nuevo con $pages páginas numeradas ("Página N de M"), en
# papel Carta, A4 o personalizado ($wMm x $hMm milímetros; Word mide en
# puntos: 72 por pulgada) y opcionalmente apaisado (wdOrientLandscape = 1).
function New-Doc([int]$pages, [string]$size, [bool]$landscape, [double]$wMm = 0, [double]$hMm = 0) {
    $doc = $word.Documents.Add()
    $ps = $doc.PageSetup
    if ($size -eq "Letter") { $ps.PaperSize = 2 }        # wdPaperLetter
    elseif ($size -eq "A4") { $ps.PaperSize = 7 }        # wdPaperA4
    elseif ($size -eq "Custom") { $ps.PageWidth = $wMm * 72 / 25.4; $ps.PageHeight = $hMm * 72 / 25.4 }
    if ($landscape) { $ps.Orientation = 1 }
    $sel = $word.Selection
    for ($p = 1; $p -le $pages; $p++) {
        $sel.Font.Size = 28
        $sel.TypeText("Página $p de $pages")
        $sel.TypeParagraph()
        $sel.Font.Size = 11
        $sel.TypeText("Prueba de cola de impresión LAIMI — texto de relleno para la página $p.")
        if ($p -lt $pages) { Call $sel "InsertBreak" @(7) | Out-Null }       # wdPageBreak
    }
    return $doc
}

# PrintOut(Background, Append, Range, OutputFileName, From, To, Item, Copies, Pages, PageType,
#          PrintToFile, Collate, FileName, ActivePrinterMacGX, ManualDuplexPrint, PrintZoomColumn, PrintZoomRow)
# Imprime $doc a archivo ($Out\$name.prn) con las copias, intercalado, rango
# de páginas ($pages, p. ej. "2-3") y páginas por hoja de Word ($zoomCol x
# $zoomRow) indicados. Background = $false: PrintOut vuelve cuando el trabajo
# ya está en la cola. Espera 1,5 s para que el vigilante lo vea por separado.
function Send($doc, [string]$name, [int]$copies, [bool]$collate, [string]$pages = "", [int]$zoomCol = 0, [int]$zoomRow = 0) {
    $file = Join-Path $Out ("$name.prn")
    $range = if ($pages) { 4 } else { 0 }               # wdPrintRangeOfPages / wdPrintAllDocument
    # El nombre del documento en la cola sale del título de la ventana: se guarda con ese nombre.
    Call $doc "SaveAs2" @((Join-Path $Out "$name.docx")) | Out-Null
    $named = [ordered]@{ Background = $false; Range = $range; OutputFileName = $file; Copies = $copies; PrintToFile = $true; Collate = $collate }
    if ($pages) { $named["Pages"] = $pages }
    if ($zoomCol -gt 0) { $named["PrintZoomColumn"] = $zoomCol; $named["PrintZoomRow"] = $zoomRow }
    CallNamed $doc "PrintOut" $named | Out-Null
    Write-Host "Enviado $name"
    Start-Sleep -Milliseconds 1500
}

# Serie de pruebas: cada línea crea el documento, lo envía y lo cierra sin
# guardar cambios (Close 0 = wdDoNotSaveChanges). El nombre describe lo que
# se espera ver en la cola (páginas, copias, intercalado, papel...).
try {
    if (-not $Only -or $Only -eq "01") { $d = New-Doc 1 "Letter" $false;  Send $d "${Prefix}01 1pag x10 copias Carta" 10 $true; Call $d "Close" @(0) }
    if (-not $Only -or $Only -eq "02") { $d = New-Doc 3 "Letter" $false;  Send $d "${Prefix}02 3pag x2 copias intercaladas" 2 $true; Call $d "Close" @(0) }
    if (-not $Only -or $Only -eq "03") { $d = New-Doc 3 "Letter" $false;  Send $d "${Prefix}03 3pag x2 copias sin intercalar" 2 $false; Call $d "Close" @(0) }
    if (-not $Only -or $Only -eq "04") { $d = New-Doc 5 "Letter" $false;  Send $d "${Prefix}04 5pag rango 2-3" 1 $true "2-3"; Call $d "Close" @(0) }
    if (-not $Only -or $Only -eq "05") { $d = New-Doc 4 "Letter" $false;  Send $d "${Prefix}05 4pag 2 por hoja (Word)" 1 $true "" 2 1; Call $d "Close" @(0) }
    if (-not $Only -or $Only -eq "06") { $d = New-Doc 1 "A4" $true;       Send $d "${Prefix}06 1pag A4 paisaje x3" 3 $true; Call $d "Close" @(0) }
    if (-not $Only -or $Only -eq "07") { $d = New-Doc 2 "Custom" $false 100 150; Send $d "${Prefix}07 2pag personalizado 100x150" 1 $true; Call $d "Close" @(0) }
} finally {
    # Siempre se cierra Word (también si una prueba falla) y se libera el
    # objeto COM para que no quede un WINWORD.EXE oculto.
    Call $word "Quit" @(0)
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null
}
