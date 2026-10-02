// Tabla de textos de la interfaz. Formato: X(Id, "español", "inglés")
// Para agregar un texto: añadir una línea aquí y usar i18n::Tr(i18n::S::Id).
// Los textos con %ls / %u / %d se usan con util::Format.

// --- General ---
X(AppTitle, L"Cola de impresión", L"Print queue")
X(LocalServer, L"Este equipo", L"This computer")
X(AllPrinters, L"Todas las impresoras", L"All printers")
X(Unknown, L"Desconocido", L"Unknown")
X(Yes, L"Sí", L"Yes")
X(No, L"No", L"No")

// --- Columnas ---
X(ColDocument, L"Documento", L"Document")
X(ColPrinter, L"Impresora", L"Printer")
X(ColUser, L"Usuario", L"User")
X(ColPages, L"Páginas", L"Pages")
X(ColSize, L"Tamaño", L"Size")
X(ColStatus, L"Estado", L"Status")
X(ColSubmitted, L"Enviado", L"Submitted")
X(ColTime, L"Fecha", L"Date")
X(ColResult, L"Resultado", L"Result")
X(ColNumber, L"#", L"#")
X(ColMachine, L"Equipo", L"Computer")
X(ColPaper, L"Papel", L"Paper")
X(ColOrientation, L"Orientación", L"Orientation")
X(ColDuplex, L"Doble cara", L"Duplex")
X(ColSource, L"Origen del papel", L"Paper source")

// --- Páginas ---
X(PagesCount, L"%u págs.", L"%u pp.")
X(PagesOne, L"1 pág.", L"1 page")
X(PagesProgress, L"%u/%u págs.", L"%u/%u pp.")
X(PagesUnknown, L"— págs.", L"— pp.")
X(DetailSubmitted, L"Enviado %ls", L"Submitted %ls")
X(DetailPosition, L"Posición %u · prioridad %u", L"Position %u · priority %u")
X(DetailFormat, L"Datos: %ls", L"Data: %ls")

// --- Estados de trabajo ---
X(StateQueued, L"En espera", L"Waiting")
X(StateSpooling, L"Recibiendo", L"Spooling")
X(StatePrinting, L"Imprimiendo", L"Printing")
X(StatePaused, L"Pausado", L"Paused")
X(StateError, L"Error", L"Error")
X(StateOffline, L"Sin conexión", L"Offline")
X(StateDeleting, L"Cancelando", L"Cancelling")
X(StatePrinted, L"Impreso", L"Printed")
X(StateRestarting, L"Reiniciando", L"Restarting")
X(StateHeld, L"Retenido", L"Held")

// --- Problemas de impresora ---
X(ProblemOffline, L"Impresora fuera de línea", L"Printer offline")
X(ProblemPaperJam, L"Papel atascado", L"Paper jam")
X(ProblemPaperOut, L"Sin papel", L"Out of paper")
X(ProblemTonerLow, L"Tóner/tinta bajo", L"Toner/ink low")
X(ProblemNoToner, L"Sin tóner/tinta", L"Out of toner/ink")
X(ProblemDoorOpen, L"Puerta abierta", L"Door open")
X(ProblemUserIntervention, L"Requiere intervención", L"Needs attention")
X(ProblemError, L"Error en la impresora", L"Printer error")
X(ProblemPaused, L"Cola en pausa", L"Queue paused")
X(ProblemNotAvailable, L"Impresora no disponible", L"Printer unavailable")
X(ProblemOutputBinFull, L"Bandeja de salida llena", L"Output bin full")
X(ProblemPaperProblem, L"Problema con el papel", L"Paper problem")
X(ProblemOutOfMemory, L"Impresora sin memoria", L"Printer out of memory")

// --- Resultados de historial ---
X(OutcomePrinted, L"Impreso", L"Printed")
X(OutcomeCancelled, L"Cancelado", L"Cancelled")
X(OutcomeFailed, L"Falló", L"Failed")
X(OutcomeRemoved, L"Retirado", L"Removed")

// --- Orden de la cola ---
X(SortQueue, L"Orden de la cola", L"Queue order")
X(SortMachine, L"Equipo", L"Computer")
X(SortTime, L"Hora de envío", L"Time submitted")
X(SortUser, L"Usuario", L"User")
X(SortDocument, L"Documento", L"Document")
X(SortStatus, L"Estado", L"Status")
X(SortSize, L"Tamaño", L"Size")
X(SortPages, L"Páginas a imprimir", L"Pages to print")
X(SortPrinter, L"Impresora", L"Printer")
X(SortPaper, L"Tamaño de papel", L"Paper size")
X(SortOrientation, L"Orientación", L"Orientation")
X(SortDuplex, L"Doble cara", L"Duplex")
X(SortSource, L"Origen del papel", L"Paper source")

// --- Barra de estado ---
X(StatusJobs, L"%u trabajos", L"%u jobs")
X(StatusOneJob, L"1 trabajo", L"1 job")
X(StatusNoJobs, L"Cola vacía", L"Queue empty")
X(StatusPrinting, L"%u imprimiendo", L"%u printing")
X(StatusPaused, L"%u en pausa", L"%u paused")
X(StatusErrors, L"%u con error", L"%u with errors")
X(StatusServer, L"Servidor: %ls", L"Server: %ls")
X(StatusPolling, L"Actualización por sondeo", L"Polling for updates")
X(StatusLive, L"En vivo", L"Live")
X(StatusFiltered, L"Filtro: \"%ls\" (%u de %u)", L"Filter: \"%ls\" (%u of %u)")
X(StatusNoAdmin, L"Sin permisos de administración: solo puede gestionar sus propios trabajos",
  L"No admin rights: you can only manage your own jobs")
X(StatusConnecting, L"Conectando…", L"Connecting…")
X(StatusAutoHold, L"Retención activa", L"Holding new jobs")
X(StatusAutoHoldFailed, L"No se pudieron retener %u trabajos: %ls", L"%u jobs could not be held: %ls")
X(StatusHeldCount, L"%u retenidos", L"%u held")
X(StatusOtherPrinters, L"%u en otras impresoras", L"%u on other printers")
X(StatusMissingJobs, L"%u trabajos que el spooler cuenta no se pudieron leer (vea app.log)",
  L"%u jobs counted by the spooler could not be read (see app.log)")
X(StatusUnreachablePrinters, L"%u impresoras inaccesibles", L"%u printers unreachable")
X(StatusSortedBy, L"Orden: %ls", L"Sorted by: %ls")
X(StatusSortedByDesc, L"Orden: %ls (inverso)", L"Sorted by: %ls (reversed)")
X(StatusHistory, L"Historial · Hoy: %llu págs. (%llu trabajos) · 7 días: %llu págs.",
  L"History · Today: %llu pages (%llu jobs) · 7 days: %llu pages")

// --- Búsqueda / entrada ---
X(SearchPlaceholder, L"Buscar documento, usuario o equipo  (Ctrl+F)", L"Search document, user or computer  (Ctrl+F)")
X(ServerPlaceholder, L"Nombre del servidor, p. ej. \\\\SERVIDOR-LAIMI (vacío = este equipo) y Enter",
  L"Server name, e.g. \\\\PRINT-SERVER (empty = this computer) and Enter")

// --- Lista vacía ---
X(EmptyQueueTitle, L"No hay trabajos en la cola", L"No jobs in the queue")
X(EmptyQueueHint, L"Los trabajos nuevos aparecerán aquí automáticamente", L"New jobs will appear here automatically")
X(EmptyFilter, L"Ningún trabajo coincide con el filtro", L"No job matches the filter")
X(EmptyHistory, L"El historial está vacío", L"History is empty")
X(ServerError, L"No se pudo conectar con el servidor", L"Could not connect to the server")

// --- Vista previa ---
X(PreviewNoSelection, L"Seleccione un trabajo para ver la vista previa", L"Select a job to preview it")
X(PreviewLoading, L"Generando vista previa…", L"Generating preview…")
X(PreviewPage, L"Página %u de %u", L"Page %u of %u")
X(PreviewPagePartial, L"Página %u de %u (%u disponibles)", L"Page %u of %u (%u available)")
X(PreviewZoom, L"%d %%", L"%d %%")
X(PreviewSourceSpool, L"Archivo de spool (%ls)", L"Spool file (%ls)")
X(PreviewSourceOriginal, L"Archivo original", L"Original file")
X(PreviewSourceThumbnail, L"Miniatura de Windows (solo primera página)", L"Windows thumbnail (first page only)")
X(PreviewAccessDenied,
  L"Sin permiso para leer los datos de este trabajo. Hace falta ser el dueño del trabajo o tener «Administrar documentos» en la cola (no hace falta ser administrador del servidor).",
  L"No permission to read this job's data. You must own the job or have \"Manage documents\" on the queue (server administrator rights are not required).")
X(PreviewSpoolMissing,
  L"No se pudieron leer los datos del trabajo %u.",
  L"The data of job %u could not be read.")
X(PreviewSpoolEmpty, L"El archivo de spool todavía está vacío: el trabajo sigue llegando al servidor.",
  L"The spool file is still empty: the job is still arriving at the server.")
X(PreviewSearchedIn, L"Se intentó:", L"Tried:")
X(PreviewStillSpooling,
  L"El trabajo todavía se está recibiendo. La vista previa se generará cuando termine de llegar al servidor.",
  L"The job is still being received. The preview will be generated when it has fully arrived at the server.")
X(PreviewNotAvailableFormat,
  L"Vista previa no disponible: los datos están en %ls, el lenguaje propio de la impresora que generó su controlador en el equipo del usuario. No hay un intérprete que permita mostrarlo fielmente. La configuración solicitada está en la pestaña «Configuración» (F7).",
  L"Preview not available: the data is %ls, the printer's own language generated by its driver on the user's computer. There is no interpreter that can show it faithfully. The requested settings are in the \"Settings\" tab (F7).")
X(PreviewNotAvailableUnknown,
  L"Vista previa no disponible: no se reconoce el formato de los datos del trabajo. La configuración solicitada está en la pestaña «Configuración» (F7).",
  L"Preview not available: the job data format is not recognized. The requested settings are in the \"Settings\" tab (F7).")
X(PreviewSourceApi, L"Datos leídos con la API del spooler (%ls)", L"Data read through the spooler API (%ls)")
X(PreviewNeedsRenderer,
  L"Los datos están en %ls. Para verlos instale Ghostscript/GhostPCL en el equipo (se detectan automáticamente).",
  L"The data is %ls. Install Ghostscript/GhostPCL on this computer to view it (auto-detected).")
X(PreviewRenderFailed, L"No se pudo renderizar el documento (%ls).", L"The document could not be rendered (%ls).")
X(PreviewUnsupported, L"No hay vista previa disponible para este formato (%ls).", L"No preview available for this format (%ls).")
X(PreviewTruncated, L"Se muestran solo las primeras %u páginas.", L"Only the first %u pages are shown.")
X(PreviewDetails, L"%ls · %ls · %ls", L"%ls · %ls · %ls")
X(PreviewTabPreview, L"Vista previa", L"Preview")
X(PreviewTabSheet, L"Configuración", L"Settings")

// --- Ficha "Configuración" del trabajo ---
X(SheetEmpty, L"Sin datos del trabajo", L"No job data")
X(SheetLegend, L"Fiable: confirmado · Estimado: supuesto o sin confirmar · No determinable: falta información",
  L"Reliable: confirmed · Estimated: assumed or unconfirmed · Cannot be determined: missing information")
X(SheetSectionJob, L"Trabajo", L"Job")
X(SheetSectionSettings, L"Configuración solicitada", L"Requested settings")
X(SheetSectionPages, L"Páginas, caras y hojas", L"Pages, sides and sheets")
X(SheetSectionData, L"Datos y vista previa", L"Data and preview")
X(SheetId, L"ID del trabajo", L"Job ID")
X(SheetDocument, L"Documento", L"Document")
X(SheetUser, L"Usuario", L"User")
X(SheetMachine, L"Equipo de origen", L"Source computer")
X(SheetPrinter, L"Impresora", L"Printer")
X(SheetStatus, L"Estado", L"Status")
X(SheetSubmitted, L"Fecha de envío", L"Submitted")
X(SheetSize, L"Tamaño del trabajo", L"Job size")
X(SheetDatatype, L"Tipo de datos", L"Data type")
X(SheetDriver, L"Controlador", L"Driver")
X(SheetProcessor, L"Procesador de impresión", L"Print processor")
X(SheetPriority, L"Posición / prioridad", L"Position / priority")
X(SheetPaper, L"Tamaño de papel", L"Paper size")
X(SheetOrientation, L"Orientación", L"Orientation")
X(SheetDuplex, L"Doble cara", L"Duplex")
X(SheetCopies, L"Copias solicitadas", L"Requested copies")
X(SheetCollate, L"Intercalar copias", L"Collate copies")
X(SheetColor, L"Color", L"Color")
X(SheetSource, L"Origen del papel", L"Paper source")
X(SheetQuality, L"Calidad", L"Quality")
X(SheetScale, L"Escala", L"Scale")
X(SheetMedia, L"Tipo de medio", L"Media type")
X(SheetNup, L"Páginas por cara", L"Pages per side")
X(SheetInData, L"Opciones en los datos", L"Options in the data")
X(SheetDefaults, L"Predeterminados de la cola", L"Queue defaults")
X(SheetDevMode, L"DEVMODE del trabajo", L"Job DEVMODE")
X(SheetDataPages, L"Páginas en los datos", L"Pages in the data")
X(SheetLogical, L"Páginas lógicas (una copia)", L"Logical pages (one copy)")
X(SheetAppCopies, L"Copias hechas por la aplicación", L"Copies made by the application")
X(SheetPrintPages, L"Páginas a imprimir", L"Pages to print")
X(OriginCalcPrintPages, L"Cálculo: %u página(s) en los datos × %u copia(s)", L"Calculation: %u page(s) in the data × %u copy(ies)")
X(SheetSides, L"Caras físicas previstas", L"Expected physical sides")
X(SheetSheets, L"Hojas físicas previstas", L"Expected physical sheets")
X(SheetProgress, L"Progreso del spooler", L"Spooler progress")
X(SheetFormat, L"Formato de los datos", L"Data format")
X(SheetDataOrigin, L"Lectura de los datos", L"Data read through")
X(SheetPreview, L"Vista previa", L"Preview")
X(ValueNotDeclared, L"No declarado", L"Not declared")
X(ValueUndetermined, L"No determinable", L"Cannot be determined")
X(ValueNoDevMode, L"El trabajo no trae DEVMODE", L"The job has no DEVMODE")
X(ValuePortrait, L"Vertical (retrato)", L"Portrait")
X(ValueLandscape, L"Horizontal (paisaje)", L"Landscape")
X(ValueSimplex, L"Una cara", L"One-sided")
X(ValueDuplexLongShort, L"Borde largo", L"Long edge")
X(ValueDuplexShortShort, L"Borde corto", L"Short edge")
X(ValuePortraitShort, L"Vertical", L"Portrait")
X(ValueLandscapeShort, L"Horizontal", L"Landscape")
X(ValueDuplexLong, L"Doble cara, borde largo", L"Two-sided, long edge")
X(ValueDuplexShort, L"Doble cara, borde corto", L"Two-sided, short edge")
X(ValueColor, L"Color", L"Color")
X(ValueMono, L"Blanco y negro", L"Black and white")
X(ValueCollateYes, L"Sí (juegos completos)", L"Yes (complete sets)")
X(ValueCollateNo, L"No (página por página)", L"No (page by page)")
X(ValueCustomPaper, L"Personalizado", L"Custom")
X(ValuePaperCode, L"Código %d", L"Code %d")
X(ValueDefaultsSame, L"El trabajo trae exactamente la configuración predeterminada de la cola",
  L"The job carries exactly the queue's default settings")
X(ValueDefaultsDiffer, L"Difiere de la configuración predeterminada de la cola",
  L"Differs from the queue's default settings")
X(DetailDefaultsSame,
  L"Puede que el usuario no cambiara nada o, en trabajos RAW, que el DEVMODE del servidor no refleje lo elegido en el equipo del usuario.",
  L"The user may not have changed anything or, for RAW jobs, the server DEVMODE may not reflect what was chosen on the user's computer.")
X(ValueDevModeTech, L"dmFields 0x%08lX · dmSize %u · privados %u B · versión %04X/%04X",
  L"dmFields 0x%08lX · dmSize %u · private %u B · version %04X/%04X")
X(ValueProgress, L"%u de %u páginas enviadas", L"%u of %u pages sent")
X(ValueDataNotRead, L"No leídos", L"Not read")
X(ValueReadApi, L"API del spooler (OpenPrinter «Impresora,Job N» + ReadPrinter)",
  L"Spooler API (OpenPrinter \"Printer,Job N\" + ReadPrinter)")
X(ValueReadFile, L"Archivo de spool: %ls", L"Spool file: %ls")
X(ValuePreviewYes, L"Disponible (%u págs.)", L"Available (%u pp.)")
X(ValuePreviewNo, L"No disponible", L"Not available")
X(BadgeReliable, L"Fiable", L"Reliable")
X(BadgeEstimated, L"Estimado", L"Estimated")
X(BadgeUnknown, L"No determinable", L"Undetermined")
X(BinUpper, L"Bandeja superior / única", L"Upper / only tray")
X(BinLower, L"Bandeja inferior", L"Lower tray")
X(BinMiddle, L"Bandeja central", L"Middle tray")
X(BinManual, L"Alimentación manual", L"Manual feed")
X(BinEnvelope, L"Sobres", L"Envelope")
X(BinEnvManual, L"Sobres (manual)", L"Envelope (manual)")
X(BinAuto, L"Selección automática", L"Automatic")
X(BinTractor, L"Tractor", L"Tractor")
X(BinLargeCapacity, L"Gran capacidad", L"Large capacity")
X(BinCassette, L"Casete", L"Cassette")
X(BinFormSource, L"Según el formulario", L"By form")
X(BinDriver, L"Bandeja propia del controlador (%d)", L"Driver-specific tray (%d)")
X(QualityDraft, L"Borrador", L"Draft")
X(QualityLow, L"Baja", L"Low")
X(QualityMedium, L"Media", L"Medium")
X(QualityHigh, L"Alta", L"High")
X(QualityDpi, L"%d ppp", L"%d dpi")
X(MediaStandard, L"Normal", L"Plain")
X(MediaTransparency, L"Transparencia", L"Transparency")
X(MediaGlossy, L"Brillante", L"Glossy")
X(MediaDriver, L"Tipo propio del controlador (%lu)", L"Driver-specific type (%lu)")
X(SourcePjl, L"PJL", L"PJL")
X(SourceEjl, L"EJL de Epson", L"Epson EJL")
X(SourceEjlVerified, L"EJL de Epson (verificado con el controlador %ls)", L"Epson EJL (verified with the %ls driver)")
X(SourcePrintTicket, L"PrintTicket XPS", L"XPS PrintTicket")
X(SourceDsc, L"Comentarios DSC de PostScript", L"PostScript DSC comments")
X(OriginDevMode, L"DEVMODE del trabajo (%ls)", L"Job DEVMODE (%ls)")
X(OriginDevModeAndContent, L"DEVMODE del trabajo (%ls) y %ls coinciden", L"Job DEVMODE (%ls) and %ls agree")
X(OriginContent, L"%ls en los datos del trabajo", L"%ls in the job data")
X(OriginSpoolDevMode, L"DEVMODE dentro del spool EMF (EMRI_DEVMODE: %ls)", L"DEVMODE inside the EMF spool (EMRI_DEVMODE: %ls)")
X(OriginSpoolDevModeRecords, L"Registros EMRI_DEVMODE del spool EMF", L"EMRI_DEVMODE records of the EMF spool")
X(OriginDevModeTechSpool,
  L"DEVMODE del spool EMF (EMRI_DEVMODE); cada campo se usa solo si dmFields lo declara y cabe en dmSize",
  L"EMF spool DEVMODE (EMRI_DEVMODE); each field is used only if dmFields declares it and it fits within dmSize")
X(SheetJobDevMode, L"DEVMODE de JOB_INFO_2", L"JOB_INFO_2 DEVMODE")
X(ValueJobDevModeStale, L"No coincide con el efectivo: %ls", L"Does not match the effective one: %ls")
X(DetailJobDevModeStale,
  L"JOB_INFO_2.pDevMode conserva el DEVMODE con el que se abrió el trabajo; la aplicación lo cambió después (ResetDC) y el procesador de impresión aplica el del spool. Comprobado con Word.",
  L"JOB_INFO_2.pDevMode keeps the DEVMODE the job was opened with; the application changed it later (ResetDC) and the print processor applies the spool one. Verified with Word.")
X(SheetDevModeChanges, L"Cambios a mitad del trabajo", L"Mid-job changes")
X(ValueDevModeChanges, L"Sí: la configuración cambia entre páginas; se muestra la de la primera",
  L"Yes: the settings change between pages; the first page's are shown")
X(OriginSpoolerPages, L"JOB_INFO_2.TotalPages", L"JOB_INFO_2.TotalPages")
X(OriginContentPages, L"Páginas contadas en los datos (%ls)", L"Pages counted in the data (%ls)")
X(OriginContentRepeat, L"Secuencia de páginas repetida en los datos", L"Repeated page sequence in the data")
X(OriginDriverSheets, L"Hojas que declaró el controlador en los datos (EJL «JI PAGES»; incluye copias y doble cara)",
  L"Sheets declared by the driver in the data (EJL \"JI PAGES\"; includes copies and duplex)")
X(OriginAssumed, L"Supuesto", L"Assumed")
X(OriginSidesFromSheets, L"A una cara, las caras son las hojas que declaró el controlador (EJL «JI PAGES»)",
  L"One-sided: the sides are the sheets declared by the driver (EJL \"JI PAGES\")")
X(NoteSheetsMismatch, L"El cálculo con páginas y copias daba %u: se usa el dato del controlador (p. ej. varias páginas por hoja)",
  L"The calculation from pages and copies gave %u: the driver figure is used (e.g. several pages per sheet)")
X(NoteDriverSheetsHint, L"El controlador declara %u hojas (indicio)", L"The driver declares %u sheets (hint)")
X(SheetDriverSheets, L"Hojas según el controlador", L"Sheets according to the driver")
X(OriginSinglePage, L"Una sola página lógica ocupa una cara por copia con cualquier N-up",
  L"A single logical page takes one side per copy with any N-up")
X(OriginCalcSides, L"Cálculo: %u cara(s) por copia × %u copia(s)", L"Calculation: %u side(s) per copy × %u copy(ies)")
X(OriginCalcSheets, L"Cálculo: %u hoja(s) por copia × %u copia(s), %ls",
  L"Calculation: %u sheet(s) per copy × %u copy(ies), %ls")
X(OriginPrinterDevMode, L"Comparado con PRINTER_INFO_2.pDevMode de la cola", L"Compared with the queue's PRINTER_INFO_2.pDevMode")
X(OriginDevModeTech, L"Validación: cada campo se usa solo si dmFields lo declara y cabe en dmSize",
  L"Validation: each field is used only if dmFields declares it and it fits within dmSize")
X(OriginSize, L"JOB_INFO_2.Size (bytes recibidos por el spooler; campo de 32 bits)",
  L"JOB_INFO_2.Size (bytes received by the spooler; 32-bit field)")
X(OriginDatatype, L"JOB_INFO_2.pDatatype (el formato real se detecta por el contenido)",
  L"JOB_INFO_2.pDatatype (the actual format is detected from the content)")
X(OriginFormatDetected, L"Detectado por el contenido de los datos", L"Detected from the data content")
X(NoteDataTruncated, L"Detectado; los datos se leyeron solo en parte", L"Detected; the data was only partially read")
X(NoteContentSays, L"%ls dice: %ls", L"%ls says: %ls")
X(NoteDevModeSays, L"El DEVMODE dice: %ls", L"The DEVMODE says: %ls")
X(NoteCopiesConflict, L"El DEVMODE pide %d copias y los datos %u", L"The DEVMODE asks for %d copies and the data %u")
X(NotePagesMismatch, L"El spooler informa %u páginas y en los datos hay %u", L"The spooler reports %u pages and the data has %u")
X(NoteProgress, L"JOB_INFO_2.PagesPrinted / TotalPages: páginas enviadas por el spooler, no hojas entregadas",
  L"JOB_INFO_2.PagesPrinted / TotalPages: pages sent by the spooler, not sheets delivered")
X(ReasonNoDevMode, L"El trabajo no trae DEVMODE.", L"The job has no DEVMODE.")
X(ReasonFieldNotDeclared, L"El DEVMODE no declara este campo (dmFields).", L"The DEVMODE does not declare this field (dmFields).")
X(ReasonStillSpooling, L"El trabajo todavía se está recibiendo.", L"The job is still being received.")
X(ReasonPagesUnknown, L"El spooler no informa páginas y los datos no se pudieron contar.",
  L"The spooler reports no pages and the data could not be counted.")
X(ReasonPossibleAppCopies, L"No se pudo comprobar si la aplicación repitió páginas para hacer copias.",
  L"It could not be checked whether the application repeated pages to make copies.")
X(ReasonRepeatDetected, L"Los datos repiten la misma secuencia de páginas: probablemente la aplicación hizo las copias.",
  L"The data repeats the same page sequence: the application probably made the copies.")
X(ReasonRawUnverified,
  L"Trabajo RAW: el controlador ya aplicó las opciones en el equipo del usuario y los datos no lo confirman.",
  L"RAW job: the driver already applied the options on the user's computer and the data does not confirm it.")
X(ReasonXpsTicketUnread, L"XPS: no se pudo leer el PrintTicket, que es el que manda.",
  L"XPS: the PrintTicket, which takes precedence, could not be read.")
X(ReasonConflict, L"El DEVMODE y los datos no coinciden; se muestra lo que dicen los datos.",
  L"The DEVMODE and the data disagree; the data value is shown.")
X(ReasonUndocumentedHint, L"Solo hay un indicio en una sintaxis sin documentación pública (EJL de Epson).",
  L"Only a hint in a syntax without public documentation (Epson EJL).")
X(ReasonHintAgrees, L"Coincide con el EJL de Epson, pero esa sintaxis no tiene documentación pública: no se considera confirmado.",
  L"Matches the Epson EJL, but that syntax has no public documentation: not considered confirmed.")
X(ReasonDataNotRead, L"Todavía no se leyeron los datos del trabajo: el DEVMODE de JOB_INFO_2 puede no ser el que se aplica.",
  L"The job data has not been read yet: the JOB_INFO_2 DEVMODE may not be the one applied.")
X(ReasonNupPrivate, L"Las páginas por cara son una opción privada del controlador: se supone 1 por cara.",
  L"Pages per side is a private driver option: 1 per side is assumed.")
X(ReasonKindUnknown, L"No se sabe en qué formato están los datos.", L"The data format is unknown.")
X(ReasonDependsOnInputs, L"Se calcula con valores estimados o no determinables.",
  L"Calculated from estimated or undetermined values.")
X(ReasonPagesMismatch, L"El spooler y los datos no coinciden en el número de páginas.",
  L"The spooler and the data disagree on the number of pages.")
X(SummaryCopy, L"%u copia", L"%u copy")
X(SummaryCopies, L"%u copias", L"%u copies")
X(SummarySide, L"%u cara", L"%u side")
X(SummarySides, L"%u caras", L"%u sides")
X(SummaryPrintPages, L"%u págs. a imprimir", L"%u pages to print")
X(SummarySidesUnknown, L"caras: no determinable", L"sides: undetermined")

// --- Acciones y mensajes ---
X(ConfirmCancelOne, L"¿Cancelar el trabajo \"%ls\"?", L"Cancel the job \"%ls\"?")
X(ConfirmCancelMany, L"¿Cancelar %u trabajos seleccionados?", L"Cancel %u selected jobs?")
X(ConfirmPurge, L"¿Eliminar TODOS los trabajos de \"%ls\"?", L"Delete ALL jobs of \"%ls\"?")
X(ConfirmRedirectRaw,
  L"El trabajo contiene datos %ls generados para el driver \"%ls\". Enviarlos tal cual a \"%ls\" solo funciona si ambas impresoras usan el mismo lenguaje. ¿Continuar?",
  L"The job contains %ls data generated for driver \"%ls\". Sending it as-is to \"%ls\" only works if both printers use the same language. Continue?")
X(ActionFailed, L"No se pudo completar la acción: %ls", L"The action could not be completed: %ls")
X(ActionDenied, L"Acceso denegado. Gestionar trabajos de otros usuarios o reordenar la cola requiere el permiso \"Administrar documentos\" en la impresora (o ejecutar como administrador).",
  L"Access denied. Managing other users' jobs or reordering the queue requires the \"Manage documents\" permission on the printer (or running as administrator).")
X(PrintersPaused, L"Impresoras en pausa: %u", L"Printers paused: %u")
X(PrintersResumed, L"Impresoras reanudadas: %u", L"Printers resumed: %u")
X(NoPrinters, L"No hay impresoras en este servidor.", L"There are no printers on this server.")
X(RedirectDone, L"Trabajo reenviado a %ls (%u páginas).", L"Job redirected to %ls (%u pages).")
X(RedirectNoData, L"No se pudieron leer los datos del trabajo para reenviarlo.", L"The job data could not be read for redirection.")
X(RedirectUnsupported, L"No se puede reenviar este formato (%ls) a una impresora con otro driver sin Ghostscript/GhostPCL.",
  L"This format (%ls) cannot be redirected to a printer with a different driver without Ghostscript/GhostPCL.")
X(RedirectChoose, L"Reenviar a…", L"Redirect to…")
X(NoJobSelected, L"No hay ningún trabajo seleccionado.", L"No job selected.")
X(ShortcutPrompt, L"Presione la nueva combinación para \"%ls\" (Esc para cancelar, Supr para quitar)",
  L"Press the new shortcut for \"%ls\" (Esc to cancel, Del to clear)")
X(ShortcutConflict, L"La combinación %ls ya estaba asignada a \"%ls\"; se reasignó.", L"%ls was assigned to \"%ls\"; it has been reassigned.")

// --- Notificaciones ---
X(NotifyJobDone, L"Impreso: %ls", L"Printed: %ls")
X(NotifyJobError, L"Error al imprimir: %ls", L"Print error: %ls")
X(NotifyPrinterProblem, L"%ls: %ls", L"%ls: %ls")
X(NotifyNewJob, L"Nuevo trabajo de %ls: %ls", L"New job from %ls: %ls")
X(NotifyHeldJob, L"Trabajo retenido de %ls: %ls (Ctrl+R para liberarlo)", L"Job held from %ls: %ls (Ctrl+R to release)")

// --- Menú principal ---
X(MenuConnect, L"Conectar a servidor…\tCtrl+O", L"Connect to server…\tCtrl+O")
X(MenuServers, L"Servidores recientes", L"Recent servers")
X(MenuRefresh, L"Actualizar", L"Refresh")
X(MenuHistory, L"Ver historial", L"Show history")
X(MenuQueue, L"Ver cola", L"Show queue")
X(MenuPausePrinter, L"Pausar / reanudar impresora", L"Pause / resume printer")
X(MenuPauseBoth, L"Pausar / reanudar trabajo e impresora", L"Pause / resume job and printer")
X(MenuResumePrinter, L"Reanudar impresora", L"Resume printer")
X(MenuPurge, L"Vaciar cola de la impresora…", L"Purge printer queue…")
X(MenuThemeSystem, L"Tema: seguir al sistema", L"Theme: follow system")
X(MenuThemeLight, L"Tema: claro", L"Theme: light")
X(MenuThemeDark, L"Tema: oscuro", L"Theme: dark")
X(MenuLanguage, L"Language: English", L"Idioma: Español")
X(MenuConfirmCancel, L"Confirmar antes de cancelar", L"Confirm before cancelling")
X(MenuAutoHold, L"Retener (pausar) todos los trabajos nuevos", L"Hold (pause) all new jobs")
X(MenuPauseOnStart, L"Al iniciar: pausar impresoras y retener trabajos en cola", L"On start: pause printers and hold queued jobs")
X(MenuPreviewPane, L"Mostrar panel de vista previa", L"Show preview pane")
X(MenuNotifyNone, L"Notificaciones: ninguna", L"Notifications: none")
X(MenuNotifyErrors, L"Notificaciones: solo errores", L"Notifications: errors only")
X(MenuNotifyAll, L"Notificaciones: todas", L"Notifications: all")
X(MenuStartWithWindows, L"Iniciar con Windows", L"Start with Windows")
X(MenuMinimizeToTray, L"Cerrar a la bandeja del sistema", L"Close to system tray")
X(MenuClearHistory, L"Borrar historial", L"Clear history")
X(MenuOpenDataFolder, L"Abrir carpeta de configuración", L"Open settings folder")
X(MenuRunAsAdmin, L"Reiniciar como administrador", L"Restart as administrator")
X(MenuShortcuts, L"Atajos de teclado…", L"Keyboard shortcuts…")
X(MenuExit, L"Salir", L"Exit")
X(TrayShow, L"Mostrar", L"Show")

// --- Menú contextual de trabajos ---
X(CtxPause, L"Pausar / reanudar", L"Pause / resume")
X(CtxPauseBoth, L"Pausar / reanudar trabajo e impresora", L"Pause / resume job and printer")
X(CtxResume, L"Reanudar", L"Resume")
X(CtxRestart, L"Reiniciar", L"Restart")
X(CtxCancel, L"Cancelar", L"Cancel")
X(CtxMoveUp, L"Subir en la cola", L"Move up")
X(CtxMoveDown, L"Bajar en la cola", L"Move down")
X(CtxRedirect, L"Reenviar a otra impresora…", L"Redirect to another printer…")
X(CtxPreview, L"Vista previa", L"Preview")
X(CtxCancelUser, L"Cancelar todos los trabajos de %ls", L"Cancel all jobs from %ls")

// --- Menú: orden y acceso rápido ---
X(MenuSortBy, L"Ordenar la cola por", L"Sort the queue by")
X(MenuSortDescending, L"Orden inverso", L"Reverse order")
X(MenuQuickAccess, L"Acceso rápido a impresoras", L"Printer quick access")
X(MenuQuickAccessHint, L"Elija las impresoras que aparecen arriba", L"Choose the printers shown at the top")
X(MenuShowPrinterBar, L"Barra de acceso rápido", L"Quick access bar")
X(MenuPinPrinter, L"Fijar en el acceso rápido", L"Pin to quick access")
X(MenuUnpinPrinter, L"Quitar del acceso rápido", L"Unpin from quick access")
X(QuickAccessEmpty, L"Ninguna impresora fijada todavía", L"No printers pinned yet")

// --- Comandos (panel de ayuda) ---
X(CmdPause, L"Pausar / reanudar trabajo", L"Pause / resume job")
X(CmdResume, L"Reanudar trabajo", L"Resume job")
X(CmdTogglePauseBoth, L"Pausar / reanudar trabajo e impresora", L"Pause / resume job and printer")
X(CmdClearSelection, L"Quitar la selección", L"Clear selection")
X(CmdCancel, L"Cancelar trabajo", L"Cancel job")
X(CmdRestart, L"Reiniciar trabajo", L"Restart job")
X(CmdNavUp, L"Trabajo anterior", L"Previous job")
X(CmdNavDown, L"Trabajo siguiente", L"Next job")
X(CmdNavFirst, L"Primer trabajo", L"First job")
X(CmdNavLast, L"Último trabajo", L"Last job")
X(CmdMoveUp, L"Subir prioridad (posición)", L"Move up in queue")
X(CmdMoveDown, L"Bajar prioridad (posición)", L"Move down in queue")
X(CmdQuickLook, L"Vista previa rápida", L"Quick preview")
X(CmdToggleTheme, L"Alternar modo claro/oscuro", L"Toggle light/dark mode")
X(CmdFind, L"Buscar / filtrar", L"Search / filter")
X(CmdHelp, L"Mostrar atajos", L"Show shortcuts")
X(CmdRefresh, L"Actualizar", L"Refresh")
X(CmdRedirect, L"Reenviar a otra impresora", L"Redirect to another printer")
X(CmdNextPrinter, L"Impresora siguiente", L"Next printer")
X(CmdPrevPrinter, L"Impresora anterior", L"Previous printer")
X(CmdChoosePrinter, L"Elegir impresora", L"Choose printer")
X(CmdConnect, L"Conectar a servidor", L"Connect to server")
X(CmdHistory, L"Alternar cola / historial", L"Toggle queue / history")
X(CmdNextPage, L"Página siguiente (vista previa)", L"Next page (preview)")
X(CmdPrevPage, L"Página anterior (vista previa)", L"Previous page (preview)")
X(CmdZoomIn, L"Acercar", L"Zoom in")
X(CmdZoomOut, L"Alejar", L"Zoom out")
X(CmdZoomReset, L"Ajustar a la ventana", L"Fit to window")
X(CmdPausePrinter, L"Pausar / reanudar impresora", L"Pause / resume printer")
X(CmdResumePrinter, L"Reanudar impresora", L"Resume printer")
X(CmdSelectAll, L"Seleccionar todo", L"Select all")
X(CmdMenu, L"Menú principal", L"Main menu")
X(CmdTogglePane, L"Mostrar/ocultar vista previa", L"Show/hide preview pane")
X(CmdToggleSheet, L"Vista previa / configuración de impresión del trabajo", L"Preview / job print settings")
X(CmdSortCycle, L"Cambiar el orden de la cola", L"Change the queue order")
X(CmdSortReverse, L"Invertir el orden de la cola", L"Reverse the queue order")
X(CmdPinPrinter, L"Fijar / quitar la impresora del acceso rápido", L"Pin / unpin the printer in quick access")
X(CmdToggleAutoHold, L"Activar/desactivar retención de trabajos nuevos", L"Toggle holding of new jobs")

// --- Panel de ayuda ---
X(HelpTitle, L"Atajos de teclado", L"Keyboard shortcuts")
X(HelpFooter, L"↑↓ o clic: elegir · Enter, doble clic o clic en la tecla: reasignar · Supr quitar · R restaurar · Esc cerrar",
  L"↑↓ or click: select · Enter, double click or click the key: reassign · Del clear · R restore · Esc close")
X(HelpUnassigned, L"(sin asignar)", L"(unassigned)")
X(HelpFixedKeys,
  L"Fijos: Esc cierra / limpia · Ctrl+1…9 impresora del acceso rápido · clic derecho: menú del trabajo · Ctrl+rueda: zoom",
  L"Fixed: Esc closes / clears · Ctrl+1…9 quick access printer · right click: job menu · Ctrl+wheel: zoom")

// --- Tooltips de la cabecera ---
X(TipPrinter, L"Elegir impresora (Ctrl+E)", L"Choose printer (Ctrl+E)")
X(TipTheme, L"Modo claro/oscuro (Ctrl+D)", L"Light/dark mode (Ctrl+D)")
X(TipMenu, L"Menú (F10)", L"Menu (F10)")
X(TipHistory, L"Historial (Ctrl+H)", L"History (Ctrl+H)")
X(TipQuickAccess, L"Elegir las impresoras del acceso rápido", L"Choose the quick access printers")
X(TipHelp, L"Atajos (F1)", L"Shortcuts (F1)")
X(TipDetails, L"Mostrar u ocultar los detalles del trabajo: vista previa y configuración (Ctrl+B)",
  L"Show or hide the job details: preview and settings (Ctrl+B)")
