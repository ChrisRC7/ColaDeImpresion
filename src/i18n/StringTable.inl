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
X(PreviewSourceSpool, L"Datos del spool (%ls)", L"Spool data (%ls)")
X(PreviewSourceOriginal, L"Archivo original", L"Original file")
X(PreviewSourceThumbnail, L"Miniatura de Windows (solo primera página)", L"Windows thumbnail (first page only)")
X(PreviewAccessDenied,
  L"No se pudo leer el archivo de spool: acceso denegado. Ejecute la aplicación como administrador del servidor o configure una carpeta de spool alternativa.",
  L"Could not read the spool file: access denied. Run the app as a server administrator or configure an alternate spool folder.")
X(PreviewSpoolMissing, L"El archivo de spool ya no existe (el trabajo terminó o el servidor no lo conserva).",
  L"The spool file no longer exists (the job finished or the server does not keep it).")
X(PreviewNeedsRenderer,
  L"Los datos están en %ls. Para verlos instale Ghostscript/GhostPCL en el equipo (se detectan automáticamente).",
  L"The data is %ls. Install Ghostscript/GhostPCL on this computer to view it (auto-detected).")
X(PreviewRenderFailed, L"No se pudo renderizar el documento (%ls).", L"The document could not be rendered (%ls).")
X(PreviewUnsupported, L"No hay vista previa disponible para este formato (%ls).", L"No preview available for this format (%ls).")
X(PreviewTruncated, L"Se muestran solo las primeras %u páginas.", L"Only the first %u pages are shown.")
X(PreviewDetails, L"%ls · %ls · %ls", L"%ls · %ls · %ls")

// --- Acciones y mensajes ---
X(ConfirmCancelOne, L"¿Cancelar el trabajo \"%ls\"?", L"Cancel the job \"%ls\"?")
X(ConfirmCancelMany, L"¿Cancelar %u trabajos seleccionados?", L"Cancel %u selected jobs?")
X(ConfirmPurge, L"¿Eliminar TODOS los trabajos de \"%ls\"?", L"Delete ALL jobs of \"%ls\"?")
X(ConfirmRedirectRaw,
  L"El trabajo contiene datos %ls generados para el driver \"%ls\". Enviarlos tal cual a \"%ls\" solo funciona si ambas impresoras usan el mismo lenguaje. ¿Continuar?",
  L"The job contains %ls data generated for driver \"%ls\". Sending it as-is to \"%ls\" only works if both printers use the same language. Continue?")
X(ActionFailed, L"No se pudo completar la acción: %ls", L"The action could not be completed: %ls")
X(ActionDenied, L"Acceso denegado. Para gestionar trabajos de otros usuarios necesita el permiso \"Administrar documentos\" en la impresora (o ejecutar como administrador).",
  L"Access denied. Managing other users' jobs requires the \"Manage documents\" permission on the printer (or running as administrator).")
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

// --- Menú principal ---
X(MenuConnect, L"Conectar a servidor…\tCtrl+O", L"Connect to server…\tCtrl+O")
X(MenuServers, L"Servidores recientes", L"Recent servers")
X(MenuRefresh, L"Actualizar", L"Refresh")
X(MenuHistory, L"Ver historial", L"Show history")
X(MenuQueue, L"Ver cola", L"Show queue")
X(MenuPausePrinter, L"Pausar impresora", L"Pause printer")
X(MenuResumePrinter, L"Reanudar impresora", L"Resume printer")
X(MenuPurge, L"Vaciar cola de la impresora…", L"Purge printer queue…")
X(MenuThemeSystem, L"Tema: seguir al sistema", L"Theme: follow system")
X(MenuThemeLight, L"Tema: claro", L"Theme: light")
X(MenuThemeDark, L"Tema: oscuro", L"Theme: dark")
X(MenuLanguage, L"Language: English", L"Idioma: Español")
X(MenuConfirmCancel, L"Confirmar antes de cancelar", L"Confirm before cancelling")
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
X(CtxPause, L"Pausar", L"Pause")
X(CtxResume, L"Reanudar", L"Resume")
X(CtxRestart, L"Reiniciar", L"Restart")
X(CtxCancel, L"Cancelar", L"Cancel")
X(CtxMoveUp, L"Subir en la cola", L"Move up")
X(CtxMoveDown, L"Bajar en la cola", L"Move down")
X(CtxRedirect, L"Reenviar a otra impresora…", L"Redirect to another printer…")
X(CtxPreview, L"Vista previa", L"Preview")
X(CtxCancelUser, L"Cancelar todos los trabajos de %ls", L"Cancel all jobs from %ls")

// --- Comandos (panel de ayuda) ---
X(CmdPause, L"Pausar trabajo", L"Pause job")
X(CmdResume, L"Reanudar trabajo", L"Resume job")
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
X(CmdPausePrinter, L"Pausar impresora", L"Pause printer")
X(CmdResumePrinter, L"Reanudar impresora", L"Resume printer")
X(CmdSelectAll, L"Seleccionar todo", L"Select all")
X(CmdMenu, L"Menú principal", L"Main menu")
X(CmdTogglePane, L"Mostrar/ocultar vista previa", L"Show/hide preview pane")

// --- Panel de ayuda ---
X(HelpTitle, L"Atajos de teclado", L"Keyboard shortcuts")
X(HelpFooter, L"↑↓ elegir · Enter reasignar · Supr quitar · R restaurar predeterminados · Esc cerrar",
  L"↑↓ select · Enter reassign · Del clear · R restore defaults · Esc close")
X(HelpUnassigned, L"(sin asignar)", L"(unassigned)")
X(HelpFixedKeys, L"Fijos: Esc cierra / limpia · clic derecho: menú del trabajo · Ctrl+rueda: zoom",
  L"Fixed: Esc closes / clears · right click: job menu · Ctrl+wheel: zoom")

// --- Tooltips de la cabecera ---
X(TipPrinter, L"Elegir impresora (Ctrl+E)", L"Choose printer (Ctrl+E)")
X(TipTheme, L"Modo claro/oscuro (Ctrl+D)", L"Light/dark mode (Ctrl+D)")
X(TipMenu, L"Menú (F10)", L"Menu (F10)")
X(TipHistory, L"Historial (Ctrl+H)", L"History (Ctrl+H)")
X(TipHelp, L"Atajos (F1)", L"Shortcuts (F1)")
