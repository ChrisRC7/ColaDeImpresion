# Configuración de impresión y vista previa por trabajo

Documento técnico del requerimiento "mostrar, por cada trabajo en cola, la
configuración solicitada al imprimir y, cuando sea viable, una vista previa".
Incluye la tabla de campos (origen y fiabilidad), la investigación sobre
páginas/copias, la evaluación de la vista previa por formato, las pruebas
realizadas con sus resultados observados, los permisos y el diseño propuesto
para una vista previa universal.

**Fecha de las pruebas:** 2026-10-02 · **Equipo:** LAIMI1-CTRL1 (Windows 11, operador
sin permisos de administrador del servidor) · **Servidor:** `\\vtec-laimiprint`.

---

## 1. Resumen

- La app muestra, en la pestaña **Configuración** del panel derecho (`F7`), cada
  dato del trabajo con **su origen** y una insignia de **fiabilidad**: *Fiable*,
  *Estimado* (con el motivo) o *No determinable* (con el motivo). El encabezado
  del panel resume papel · orientación · doble cara · copias · caras (con `≈`
  si es estimado).
- Los datos del trabajo se leen con la **API del spooler**:
  `OpenPrinter("Cola,Job N")` + `ReadPrinter`. No se usa la carpeta de spool
  ni `\\SRV\C$`, no hace falta ser administrador del servidor y no choca con los
  bloqueos del spooler sobre `.SHD`/`.SPL` (causa del error *"being used by
  another process"* de la versión 0.2.0). **Verificado contra `vtec-laimiprint`**
  con la cuenta del operador (solo "Administrar documentos").
- **Nunca se multiplica `TotalPages × dmCopies` a ciegas.** Las pruebas muestran
  que según la aplicación y el controlador las copias van en el DEVMODE, dentro
  de los datos, o repetidas como páginas.
- En trabajos EMF el DEVMODE de `JOB_INFO_2` **puede estar desactualizado**
  (Word lo cambia después de `StartDoc`): el efectivo viaja dentro del spool
  (registro `EMRI_DEVMODE`) y es el que se usa. Comprobado contra la salida real.
- Las colas Epson del LAIMI reciben **ESC/Page-Color** (lenguaje propio de
  Epson generado en la PC del estudiante): **sin vista previa posible** (las
  bandas raster usan una compresión propietaria). La configuración sí se
  muestra y, desde la 0.3.1, **copias, doble cara, papel, orientación y hojas
  son fiables**: se leen del EJL, cuyo significado se verificó con el
  controlador real de esas colas (sección 4, hallazgo 11). El DEVMODE del
  servidor no sirve para eso: es el predeterminado de la cola.

## 2. De dónde sale cada dato

| Orden | Fuente | API / formato | Para qué |
|---|---|---|---|
| 1 | `JOB_INFO_2` | `OpenPrinter` (cola) + `EnumJobs` nivel 2 | Metadatos del trabajo y su `pDevMode` |
| 2 | `PRINTER_INFO_2.pDevMode` | `GetPrinter` nivel 2 | **Solo** para comparar: avisa si el trabajo trae exactamente los predeterminados de la cola |
| 3 | Datos del trabajo | `OpenPrinter("Cola,Job N", JOB_READ)` + `ReadPrinter` | Formato real, páginas, opciones declaradas en los datos, vista previa |
| 3a | EMF | MS-EMFSPOOL (documentado): metarchivos por página y registros `EMRI_DEVMODE` | Páginas, DEVMODE efectivo, repeticiones |
| 3b | XPS / OpenXPS | XPS Object Model de Windows (`IXpsOMObjectFactory1`) + Print Schema | Páginas y PrintTicket (copias, papel, orientación, doble cara, N-up, color) |
| 3c | PostScript | Comentarios DSC 3.0 / PPD (documentados) | `%%Pages`, `NumCopies`, `*PageSize`, `%%Orientation`, `*Duplex` |
| 3d | PCL / PCL XL | PJL (documentado por HP) | `COPIES`, `QTY`, `DUPLEX`, `BINDING`, `PAPER`, `ORIENTATION` |
| 3e | ESC/Page (Epson) | EJL (`ESC 01 @EJL`, sin documentación pública) y comandos ESC/Page | Con el controlador EPSON AM-C6000 (verificado): `QT` copias, `PS` papel, `DX`/`BD` doble cara, `GS n poE` orientación, `JI PAGES` hojas físicas. Con otros controladores: solo indicio |
| 4 | Archivo de spool | `\\SRV\C$\...\PRINTERS` o ruta local | Respaldo si la API falla (requiere administrador del servidor) |

El formato se detecta **por el contenido**, no por `pDatatype`: un driver v4
declara `RAW` y envía XPS; un trabajo Epson empieza con `ESC` y antes se
confundía con PCL.

## 3. Tabla de campos: origen y fiabilidad

Fiabilidad: **F** = fiable, **E** = estimado, **N** = no determinable.

| Dato | Origen | EMF | XPS | RAW (PCL/PS/ESC/Page) | Observaciones |
|---|---|---|---|---|---|
| ID, documento, usuario, equipo, impresora | `JOB_INFO_2` (`JobId`, `pDocument`, `pUserName`, `pMachineName`, `pPrinterName`) | F | F | F | El nombre del documento lo pone la aplicación ("Microsoft Word - …"). |
| Estado | `JOB_INFO_2.Status` + `pStatus` | F | F | F | Se muestra también el valor crudo. |
| Fecha de envío | `JOB_INFO_2.Submitted` (UTC → local) | F | F | F | |
| Tamaño | `JOB_INFO_2.Size` | F | F | F | Bytes recibidos por el spooler (32 bits). |
| Tipo de datos | `JOB_INFO_2.pDatatype` | F | F | F | Es lo declarado, no el formato real. |
| Controlador / procesador | `pDriverName`, `pPrintProcessor`, `pParameters` | F | F | F | |
| Páginas en los datos | Conteo en los datos (EMF/XPS/DSC) o `TotalPages` | F | F | F si hay `TotalPages` o `%%Pages`; E con `EJL JI PAGES` | Páginas que generó la aplicación, **incluidas** las que repitió para hacer copias. |
| Páginas lógicas (una copia) | Páginas en los datos − repeticiones detectadas | F si se comparó el contenido; E si hay repetición o no se pudo comprobar | igual | E (no se puede comparar el contenido) | Una sola página siempre es F. |
| Papel | `dmPaperSize`, `dmFormName`, `dmPaperWidth/Length` (décimas de mm) | F (DEVMODE efectivo del spool) | F si el PrintTicket coincide; E si no se leyó | F si PJL/DSC lo confirman; E si solo DEVMODE o EJL | `dmPaperWidth/Length` prevalecen sobre `dmPaperSize`. |
| Orientación | `dmOrientation` | F | F/E | F/E | |
| Doble cara | `dmDuplex` (1 una cara, 2 borde largo, 3 borde corto) | F; E (supuesto una cara) si `DM_DUPLEX` no está | F/E | F/E | |
| Copias | `dmCopies` (solo con `DM_COPIES`) | F | F con PrintTicket | F con PJL/DSC o EJL verificado (`QT`); E si solo hay DEVMODE | En RAW el controlador ya aplicó las copias en el cliente; los datos mandan sobre el DEVMODE del servidor. |
| Intercalar | `dmCollate` | F | F/E | F/E | |
| Color | `dmColor` | F | F/E | F/E | |
| Origen del papel, calidad, escala, tipo de medio | `dmDefaultSource`, `dmPrintQuality`, `dmScale`, `dmMediaType` | informativo | informativo | informativo | Valores ≥ 256 son propios del controlador. |
| Páginas por cara (N-up) | PrintTicket (`PagesPerSheet`) | **N** (opción privada) | F si el ticket lo trae; N si no | N | `dmNup` solo dice *quién* compone, no cuántas. |
| Caras físicas previstas | Cálculo | F con 1 página lógica; E con varias (N-up desconocido) | F con N-up en el ticket | E | Hereda la peor fiabilidad de sus datos. |
| Hojas físicas previstas | Cálculo con la doble cara, o `JI PAGES` del controlador Epson | igual que caras y doble cara | | F con EJL verificado | Cada copia empieza en hoja nueva. `JI PAGES` ya incluye copias, doble cara y N-up. |
| Progreso | `PagesPrinted / TotalPages` | — | — | — | Páginas enviadas por el spooler; **no** son hojas entregadas. |

Validación del DEVMODE (`core/PrintSettings`): cada campo se usa solo si
`dmFields` lo declara **y** cabe dentro de `dmSize`; los valores fuera de rango
(copias < 1, doble cara ∉ 1..3, etc.) se descartan.

## 4. Investigación: TotalPages, copias y DEVMODE

Hallazgos, con la prueba que los respalda (sección 8):

1. **EMF: `TotalPages` no incluye las copias que aplica el controlador.**
   1 página × 10 copias por DEVMODE → `TotalPages = 1`, `dmCopies = 10` (S1).
2. **Si la aplicación hace las copias, `TotalPages` ya las incluye.**
   Misma hoja repetida 10 veces → `TotalPages = 10`, `dmCopies = 1` (S2).
   Word lo hace con controladores que no ofrecen copias (Microsoft Print to
   PDF): 1 pág. × 10 → 10 páginas en los datos (X01). La app detecta la
   repetición comparando las páginas (huella de cada EMF / CRC de cada
   `.fpage`) y lo muestra como **estimado**.
3. **EMF: `JOB_INFO_2.pDevMode` puede no ser el que se aplica.** Word abre el
   trabajo con el DEVMODE anterior y lo cambia con `ResetDC`; ese cambio queda
   como `EMRI_DEVMODE` dentro del spool, **entre** los datos de la página
   (`EMRI_METAFILE_DATA`) y el registro que la cierra (`EMRI_*_METAFILE_EXT`).
   En la serie W01–W07 `JOB_INFO_2` mostraba las copias *del trabajo anterior*
   (W01 pedía 10 y decía 1; W02 pedía 2 y decía 10). La salida PostScript real
   confirmó que se aplican las del spool (`NumCopies 10` en W01).
4. **XPS (driver v4):** `pDatatype = RAW`, procesador `MS_XPS_PROC`, contenido
   OpenXPS. El PrintTicket (`JobCopiesAllDocuments`, `PageMediaSize`,
   `PageOrientation`, `PageOutputColor`) coincidió con el DEVMODE (S3).
5. **RAW por Client Side Rendering (colas del LAIMI):** el trabajo de un
   estudiante en `LAIMI 1 - INK` traía un DEVMODE **idéntico al predeterminado
   de la cola** (Carta, doble cara borde largo, 1 copia), y su EJL decía
   `PS=LT DX=ON BD=LE CO=1 QT=1`. `TotalPages` (16) lo informa el cliente. No se
   puede saber todavía si ese DEVMODE refleja lo elegido por el estudiante o
   solo la configuración de la cola: ver pruebas pendientes P1–P5.
6. **Acrobat** genera él mismo PostScript y lo envía como RAW: `%%Pages` da las
   páginas; `NumCopies` solo aparece cuando hay más de una copia (su ausencia
   no se toma como prueba de 1 copia).
7. **Navegadores (Chrome, Edge):** EMF; con impresión de quiosco se usa el
   papel de la impresora, no el `@page` del CSS.
8. **Word "páginas por hoja"** compone las páginas él mismo: los datos ya traen
   las caras compuestas (W05: 4 páginas → 2).
9. **Tamaños personalizados** los ajusta la aplicación/controlador al
   formulario más cercano (Word → Microsoft Print to PDF: 100×150 mm se imprimió
   en 100×148 "Postal japonesa"; Word → CutePDF: A4). La app muestra el tamaño
   del DEVMODE efectivo, que es el que se usa.
10. **`PagesPrinted`** es un avance del spooler, no un contador de hojas.
11. **EJL del controlador EPSON AM-C6000 (colas LAIMI 1/2 - INK), verificado.**
    Se generaron sus datos "imprimiendo a archivo" desde esta PC (no sale nada
    hacia la impresora) variando una opción por vez:

    | Pedido | EJL / ESC/Page resultante |
    |---|---|
    | 1 pág. × 10 copias | `QT=10`, `JI PAGES="10"`; la página va **una sola vez** en los datos (60 618 vs 60 620 bytes) |
    | 2 págs. × 3 copias | `QT=3`, `JI PAGES="6"` |
    | Doble cara borde largo / corto (2 págs.) | `DX=ON BD=LE` / `DX=ON BD=SE`, `JI PAGES="1"` |
    | A4 / Oficio / Carta | `PS=A4` / `PS=LGL` / `PS=LT` y `GS 14/32/30 psE` |
    | Horizontal | `GS 1 poE` (vertical: `GS 0 poE`) |

    `CO` no cambia nunca; las copias van en `QT`; `JI PAGES` son **hojas
    físicas** (no páginas). El intercalado no se pudo variar: el controlador lo
    fuerza.
12. **Lista de trabajos incompleta desde otras PCs:** en una conexión a una
    impresora compartida (`\\vtec-laimiprint\Cola`), Windows responde
    `EnumJobs` y `GetPrinter` desde la caché del cliente (Client Side
    Rendering), que a veces no recibe los trabajos enviados desde otras PCs
    hasta el siguiente evento. Medido: por el nombre de la conexión cada
    consulta tarda 0,6 ms (no hay ida y vuelta al servidor), **también con
    `OpenPrinter2` + `PRINTER_OPTION_NO_CACHE`** (la 0.3.1 lo usaba y no
    alcanzaba); por el nombre DNS completo
    (`\\vtec-laimiprint.estudiantes.tec.cr\Cola`) tarda 17 ms y consulta al
    servidor; por IP, 3 s. Desde la 0.3.2 la lista que manda es la del nombre
    completo; de la caché solo se agregan los trabajos que esta PC todavía
    procesa (`JOB_STATUS_RENDERING_LOCALLY`).

## 5. Reglas de cálculo

```
páginas en los datos  = conteo en los datos (EMF/XPS/DSC) | TotalPages | contador EJL (estimado)
páginas lógicas       = período de repetición si los datos repiten la secuencia (estimado)
                        | páginas en los datos (fiable solo si se comparó el contenido o es 1)
copias                = DEVMODE efectivo × repeticiones hechas por la aplicación
caras por copia       = ⌈páginas lógicas ÷ páginas por cara⌉   (1 por cara supuesto si se desconoce)
hojas por copia       = doble cara ? ⌈caras por copia ÷ 2⌉ : caras por copia
caras / hojas totales = por copia × copias
```

El caso del enunciado, **1 página × 10 copias**, da **10 caras fiables**
únicamente cuando la página lógica y las copias son fiables (1 página: el N-up
no influye). Si las copias son estimadas (RAW sin confirmar) el resultado se
muestra como `≈10` con el motivo.

## 6. Vista previa por formato

| Formato real | Cuándo aparece | Vista previa | Fidelidad |
|---|---|---|---|
| EMF (NT EMF) | Colas con renderizado en el servidor; colas locales; Chrome/Edge/Word a drivers v3 | Sí, vectorial (nativa) | Alta. Las fuentes incrustadas en el spool no se reproducen: si falta una fuente en el equipo del operador, Windows la sustituye. |
| XPS / OpenXPS | Drivers v4 | Sí con GhostXPS (verificado con OpenXPS); si no, miniatura del Shell | Alta (rasterizada) |
| PostScript | Drivers PostScript, Acrobat | Sí con Ghostscript | Alta (rasterizada) |
| PCL 5 / PCL XL | Drivers PCL (p. ej. Lexmark/HP universales) | Sí con GhostPCL | Alta (rasterizada) |
| PDF directo | Algunos drivers | Sí con Ghostscript | Alta |
| **ESC/Page, ESC/Page-Color, ESC/P-R (Epson)** | **Colas Epson del LAIMI** | **No disponible** | No hay intérprete libre; no se intenta adivinar. |
| Otro / desconocido | — | No disponible | |

Colas del LAIMI observadas:

| Cola | Controlador | Datos que recibe el servidor | Vista previa |
|---|---|---|---|
| `LAIMI 1 - INK`, `LAIMI 2 - INK` | EPSON AM-C6000 Series (v3) | RAW ESC/Page-Color (verificado) | No disponible (compresión propietaria); configuración sí, con copias/doble cara/papel/orientación/hojas fiables |
| `LAIMI1_BN` | Lexmark Universal v2 | Probablemente PCL XL o PostScript (sin verificar) | Previsible con GhostPCL/Ghostscript — P6 |
| `LAIMI 2 - Etiquetas` | EPSON CW-C6500Au | Sin verificar | P7 |

## 7. Prueba reproducible

Herramientas incluidas:

- `cola_jobinspect` imprime, por trabajo, la misma ficha que la app y puede
  agregar una fila a un CSV (matriz de pruebas). `--hold S` vigila la cola,
  pausa cada trabajo nuevo, lo inspecciona cuando termina de recibirse y, con
  `--release`, lo reanuda (necesario si la aplicación "imprime a archivo":
  `EndDoc` espera a que el trabajo termine).
- `tools/pruebas/word_jobs.ps1` envía la serie W01–W07 desde Word por COM
  ("imprimir a archivo": no sale papel ni se abren diálogos).
- `tools/pruebas/pagina.html` es la página de prueba para navegadores.

Pasos (colas locales, sin papel):

```powershell
# Word -> CutePDF (EMF) y Word -> Microsoft Print to PDF (XPS)
.\build\cola_jobinspect.exe "CutePDF Writer" --hold 120 --release --data --csv matriz.csv --label "Word -> CutePDF"
powershell -File tools\pruebas\word_jobs.ps1 -Printer "CutePDF Writer" -Out $env:TEMP\word_out
.\build\cola_jobinspect.exe "Microsoft Print to PDF" --hold 120 --release --data --csv matriz.csv --label "Word -> PDF"
powershell -File tools\pruebas\word_jobs.ps1 -Printer "Microsoft Print to PDF" -Out $env:TEMP\word_pdf -Prefix X

# Lector PDF y navegador (sin imprimir a archivo: se dejan pausados y se cancelan)
.\build\cola_jobinspect.exe "CutePDF Writer" --hold 90 --data --delete --csv matriz.csv --label "Acrobat/navegador"
& "C:\Program Files\Adobe\Acrobat DC\Acrobat\Acrobat.exe" /t "C:\ruta\doc.pdf" "CutePDF Writer"
& "C:\Program Files\Google\Chrome\Application\chrome.exe" --user-data-dir=$env:TEMP\perfil --kiosk-printing --new-window tools\pruebas\pagina.html

# RAW: datos ya generados para la impresora (PJL+PCL, ESC/Page)
.\build\cola_testjob.exe "CutePDF Writer" --raw archivo.prn --name "RAW" --pause --finish
.\build\cola_jobinspect.exe "CutePDF Writer" --data --delete
```

Para contrastar con lo que *realmente* se aplicó, la salida "a archivo" de
CutePDF es PostScript (`%%Pages` y `NumCopies`) y la de Microsoft Print to PDF
es un PDF (contar páginas y `MediaBox`).

## 8. Matriz de pruebas y resultados observados

Abreviaturas: CP = CutePDF Writer (driver PostScript v3, procesador winprint);
MSPDF = Microsoft Print to PDF (driver v4, `MS_XPS_PROC`). "JOB_INFO_2" es el
`dmCopies` que informa `JOB_INFO_2.pDevMode`; "previsto" es lo que muestra la app.

| # | Origen → cola | Formato | Escenario | TotalPages | JOB_INFO_2 dmCopies | Previsto (copias · caras) | Salida real | Vista previa |
|---|---|---|---|---|---|---|---|---|
| S1 | GDI (sonda) → CP | EMF | 1 pág. × 10, copias por DEVMODE, Carta | 1 | 10 | 10 F · **10 F** | — | Sí |
| S2 | GDI (sonda) → CP | EMF | 1 pág. repetida 10 veces por la app | 10 | 1 | 10 E · 10 E (repetición detectada) | — | Sí |
| S3 | GDI (sonda) → MSPDF | XPS | 2 págs. × 3, A4 paisaje | 2 | 3 | 3 F (PrintTicket coincide) · 6 E (N-up) | — | Sí (GhostXPS) |
| W01 | Word → CP | EMF | 1 pág. × 10 | 1 | 1 *(desactualizado)* | 10 F · **10 F** | 1 pág. × `NumCopies 10` = 10 | Sí |
| W02 | Word → CP | EMF | 3 págs. × 2 intercaladas | 3 | 10 *(desact.)* | 2 F · 6 E | 6 págs. (el procesador repitió el juego) | Sí |
| W03 | Word → CP | EMF | 3 págs. × 2 sin intercalar | 3 | 2 | 2 F · 6 E | 3 × `NumCopies 2` = 6 | Sí |
| W04 | Word → CP | EMF | rango 2-3 de 5 págs. | 2 | 2 *(desact.)* | 1 F · 2 E | 2 págs. | Sí |
| W05 | Word → CP | EMF | 4 págs., 2 por hoja (Word) | 2 | 1 | 1 F · 2 E | 2 págs. horizontales | Sí |
| W06 | Word → CP | EMF | 1 pág. A4 paisaje × 3 | 1 | 1 *(desact.)* | 3 F · **3 F** | 1 × `NumCopies 3` = 3 | Sí |
| W07 | Word → CP | EMF | 2 págs. 100×150 mm | 2 | 3 *(desact.)* | 1 F · 2 E | 2 págs. A4 (tamaño no admitido) | Sí |
| X01 | Word → MSPDF | XPS | 1 pág. × 10 | 10 | 1 | 10 E (Word repitió) · 10 E | 10 págs. PDF | Sí |
| X02 | Word → MSPDF | XPS | 3 págs. × 2 intercaladas | 6 | 1 | 2 E · 6 E | 6 págs. | Sí |
| X03 | Word → MSPDF | XPS | 3 págs. × 2 sin intercalar | 3 | 1 | 1 F · 3 E | 3 págs. (Word no hizo las copias) | Sí |
| X04 | Word → MSPDF | XPS | rango 2-3 | 2 | 1 | 1 F · 2 E | 2 págs. | Sí |
| X05 | Word → MSPDF | XPS | 2 por hoja | 2 | 1 | 1 F · 2 E | 2 págs. horizontales | Sí |
| X06 | Word → MSPDF | XPS | 1 pág. A4 paisaje × 3 | 3 | 1 | 3 E · 3 E | 3 págs. horizontales | Sí |
| X07 | Word → MSPDF | XPS | 100×150 mm | 2 | 1 | 1 F · 2 E; papel 100×148 | 2 págs. de 100×148 mm | Sí |
| A1 | Acrobat → CP | RAW PostScript | PDF de 2 págs. | 2 | 1 | 1 E · 2 E; A4/vertical F (DSC) | — | Sí (Ghostscript) |
| A2 | Acrobat → CP | RAW PostScript | PDF de 3 págs. horizontales | 3 | 1 | 1 E · 3 E; A4 vertical F (Acrobat auto-rota) | — | Sí |
| B1 | Chrome → CP | EMF | HTML 2 págs. | 2 | 1 | 1 F · 2 E; A4 | — | Sí |
| B2 | Edge → CP | EMF | HTML 2 págs. | 2 | 1 | 1 F · 2 E; A4 | — | Sí |
| R1 | RAW PJL+PCL → CP | PCL | `COPIES=2 DUPLEX=ON BINDING=LONGEDGE PAPER=LETTER` | 0 | 1 | 2 E (DEVMODE contradice) · N (sin páginas); doble cara largo F | — | Sí (GhostPCL) |
| R2 | Archivo ESC/Page real del servidor → CP | ESC/Page | trabajo de `LAIMI 2 - INK` | 0 | 1 | 1 E · 2 E (EJL `JI PAGES`) | — | **No disponible** (motivo explicado) |
| L1 | **Servidor real** `LAIMI 1 - INK` (trabajo de un estudiante) | ESC/Page-Color | lectura remota por `ReadPrinter`, 6,6 MB | 16 | 1 (= predeterminado de la cola) | 1 E · 16 E · 8 hojas E (doble cara largo, EJL coincide) | — | No disponible |

Cobertura del enunciado: 1 pág. × 10 (S1, W01, X01) · varias págs. × varias
copias (W02, W03, X02) · retrato y paisaje (W06, X06, A2) · Carta (S1, L1),
A4 (S3, W06) y personalizado (W07, X07) · XPS, EMF y RAW (S3, W01, A1, R1, R2,
L1). **Una cara / doble cara borde largo y corto:** ninguna cola local admite
doble cara (sus DEVMODE no declaran `DM_DUPLEX`); se verificó la lectura de la
opción en PJL (R1), en el EJL real (L1, `DX=ON BD=LE`) y en pruebas unitarias
(PrintTicket `TwoSidedLongEdge`, DSC `DuplexTumble`). La prueba con impresión
real a doble cara queda para el LAIMI (P3).

Las predicciones de caras de las 14 pruebas de Word coincidieron con la salida
real en todos los casos.

## 9. Datos que no se pueden recuperar con fiabilidad

| Dato | Por qué | Qué muestra la app |
|---|---|---|
| Páginas por cara (N-up) en EMF y RAW | Es una opción privada del controlador (parte privada del DEVMODE); la parte pública solo tiene `dmNup` (quién compone) | *No determinable*; las caras se estiman con 1 por cara (salvo 1 página lógica) |
| Copias en trabajos ESC/Page de **otros** controladores Epson | Solo se verificó el EPSON AM-C6000 | *Estimado* (indicio EJL) |
| Intercalado en ESC/Page | El controlador lo fuerza; no se pudo variar | Lo del DEVMODE, como estimado |
| Copias hechas por la aplicación sin repetición exacta | Si las páginas repetidas no son idénticas byte a byte no se detectan | Páginas lógicas *Estimado* ("no se pudo comprobar…") |
| Hojas realmente entregadas | Ninguna API del spooler las informa; `PagesPrinted` es avance del spooler | Solo "hojas previstas" |
| Doble cara en controladores sin `DM_DUPLEX` | No hay dato | "Una cara" *Estimado* (supuesto) |
| Tamaño pedido en el documento | La aplicación/controlador lo ajusta a un formulario | El del DEVMODE efectivo (el que se imprime) |
| Trabajos que todavía se reciben | `ReadPrinter` esperaría datos | Mensaje "todavía se está recibiendo"; se analiza al terminar |
| Copias en PostScript cuando son 1 | PSCRIPT solo escribe `NumCopies` si hay más de 1 | *Estimado* desde el DEVMODE |
| Contenido visual de ESC/Page | No hay intérprete | "Vista previa no disponible" + configuración |

## 10. Permisos y seguridad

- **Permiso necesario:** `ReadPrinter` sobre un trabajo exige `JOB_ACCESS_READ`,
  que tienen el dueño del trabajo y quien tiene **"Administrar documentos"** en
  la cola (ese permiso otorga `JOB_ALL_ACCESS`, que incluye `JOB_ACCESS_READ`).
  Es el mismo permiso que ya usan los operadores para pausar/cancelar trabajos
  ajenos. Verificado en remoto contra `vtec-laimiprint` sin ser administrador
  (antes, por `\\vtec-laimiprint\C$`, daba "Acceso denegado").
- **No** se pide elevación al abrir cada trabajo, **no** se cambian permisos de
  la carpeta de spool y **no** se instala nada en el servidor.
- **Confidencialidad:** quien tenga "Administrar documentos" puede ver el
  contenido de todos los trabajos de esa cola. Recomendación: conceder ese
  permiso solo al grupo de operadores. La app no guarda el contenido en disco;
  el log registra el id, el tamaño y el formato leído (y, como antes, el nombre
  del documento).
- **Archivos temporales** (solo para Ghostscript/GhostPCL/GhostXPS y la
  miniatura del Shell): carpeta privada por proceso
  `%TEMP%\ColaDeImpresion\s<pid>-<tick>` con DACL protegida (acceso solo para
  el usuario actual, sin herencia), cada archivo se borra al usarse, la carpeta
  se borra al salir y al iniciar se eliminan restos de ejecuciones anteriores.
- **Memoria:** las páginas renderizadas viven en una caché en memoria (LRU,
  384 MB) que se libera al cerrar.

## 11. Diseño propuesto: vista previa universal (no implementado)

Para las colas Epson el documento llega ya convertido a ESC/Page. Una vista
previa fiel de **todo** exige tener el documento **antes** de que lo transforme
el controlador. Opciones, de menor a mayor impacto:

1. **Renderizar en el servidor (recomendada para un piloto).** Activar la
   directiva "Representar siempre los trabajos de impresión en el servidor"
   (`Configuración del equipo > Plantillas administrativas > Impresoras`) en las
   PCs del laboratorio, y en la cola "Habilitar características avanzadas de
   impresión" (spool EMF). Las PCs envían EMF; el servidor lo guarda como
   `NT EMF` y lo renderiza con el mismo driver Epson. La app ya muestra EMF con
   vista previa vectorial y lee el DEVMODE efectivo (copias, doble cara) con
   fiabilidad *Fiable*. Efectos: más CPU/memoria en `vtec-laimiprint` (el
   driver corre allí), un fallo del driver afecta al servicio del servidor, y el
   tráfico cambia (EMF en lugar de ESC/Page). No instala componentes; es
   reversible. Requiere administrador del dominio/servidor y un piloto en una
   cola.
2. **Cola de captura con liberación (modelo "pull printing").** Los estudiantes
   imprimen a una cola virtual con un driver PostScript genérico y un puerto
   que conserva el trabajo; el operador lo ve (Ghostscript) y la app lo reenvía
   a la Epson (la función de reenvío ya existe). Vista previa fiel de todo,
   pero cambia el flujo de impresión, y el trabajo reenviado queda a nombre del
   operador.
3. **Captura en el cliente** (filtro XPS de un driver v4 o una app de Print
   Workflow en cada PC). Fiel, pero exige desplegar y mantener software en todas
   las PCs del laboratorio.

## 12. Pruebas pendientes en el LAIMI

Con retención activa en la app (los trabajos quedan pausados) y luego
cancelándolos. Anotar en cada caso `JOB_INFO_2` (copias, doble cara, papel),
las "Opciones en los datos" (EJL) y `TotalPages`, con:

```powershell
herramientas\cola_jobinspect.exe "\\vtec-laimiprint\LAIMI 1 - INK" --data --csv laimi.csv --label "P1 1 pag x10"
```

- ~~P1–P5~~ Resueltas sin papel generando los datos del controlador real a
  archivo (hallazgo 11). Queda solo confirmar con un trabajo real de un
  estudiante que la ficha muestre las copias y la doble cara elegidas.
- **P6** `LAIMI1_BN` (Lexmark): formato real y vista previa con GhostPCL/Ghostscript.
- **P7** `LAIMI 2 - Etiquetas`: formato real.
- **P8** Operador sin "Administrar documentos": debe ver el mensaje de permiso.
