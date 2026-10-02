# Envía trabajos de prueba desde Word (COM) a una impresora, siempre "imprimir a
# archivo" para que no salga papel ni se abran diálogos. Un vigilante
# (cola_jobinspect --hold) los pausa e inspecciona.
param([string]$Printer, [string]$Out, [string]$Prefix = "W", [string]$Only = "")
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $Out | Out-Null
$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0
$word.ActivePrinter = $Printer

function Plain([object[]]$values) {
    # PowerShell envuelve los valores en PSObject y COM los recibe con otro tipo.
    $out = New-Object object[] $values.Count
    for ($i = 0; $i -lt $values.Count; $i++) { $out[$i] = ([psobject]$values[$i]).psobject.BaseObject }
    return ,$out
}

function Call($obj, [string]$method, [object[]]$params) {
    $params = Plain $params
    [System.__ComObject].InvokeMember($method, [Reflection.BindingFlags]::InvokeMethod, $null, $obj, $params)
}

function CallNamed($obj, [string]$method, [System.Collections.Specialized.OrderedDictionary]$named) {
    $names = [string[]]@($named.Keys)
    $values = Plain @($named.Values)
    [System.__ComObject].InvokeMember($method, [Reflection.BindingFlags]::InvokeMethod, $null, $obj, $values, $null, $null, $names)
}

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

try {
    if (-not $Only -or $Only -eq "01") { $d = New-Doc 1 "Letter" $false;  Send $d "${Prefix}01 1pag x10 copias Carta" 10 $true; Call $d "Close" @(0) }
    if (-not $Only -or $Only -eq "02") { $d = New-Doc 3 "Letter" $false;  Send $d "${Prefix}02 3pag x2 copias intercaladas" 2 $true; Call $d "Close" @(0) }
    if (-not $Only -or $Only -eq "03") { $d = New-Doc 3 "Letter" $false;  Send $d "${Prefix}03 3pag x2 copias sin intercalar" 2 $false; Call $d "Close" @(0) }
    if (-not $Only -or $Only -eq "04") { $d = New-Doc 5 "Letter" $false;  Send $d "${Prefix}04 5pag rango 2-3" 1 $true "2-3"; Call $d "Close" @(0) }
    if (-not $Only -or $Only -eq "05") { $d = New-Doc 4 "Letter" $false;  Send $d "${Prefix}05 4pag 2 por hoja (Word)" 1 $true "" 2 1; Call $d "Close" @(0) }
    if (-not $Only -or $Only -eq "06") { $d = New-Doc 1 "A4" $true;       Send $d "${Prefix}06 1pag A4 paisaje x3" 3 $true; Call $d "Close" @(0) }
    if (-not $Only -or $Only -eq "07") { $d = New-Doc 2 "Custom" $false 100 150; Send $d "${Prefix}07 2pag personalizado 100x150" 1 $true; Call $d "Close" @(0) }
} finally {
    Call $word "Quit" @(0)
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null
}
