#pragma once
// Ventana principal: une los módulos (monitor de cola, vistas previas,
// acciones, historial, configuración) con los controles de la interfaz.
// Toda la lógica de "qué pasa cuando..." vive aquí; los controles solo dibujan
// y avisan por callbacks, y los módulos core/preview no conocen la UI.

#include <windows.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/JobActions.h"
#include "config/History.h"
#include "config/Settings.h"
#include "core/CarnetService.h"
#include "core/JobTracker.h"
#include "core/PrintRelease.h"
#include "core/QueueMonitor.h"
#include "input/Shortcuts.h"
#include "preview/JobFacts.h"
#include "preview/PreviewService.h"
#include "ui/HeaderBar.h"
#include "ui/HelpOverlay.h"
#include "ui/JobListView.h"
#include "ui/PreviewPane.h"
#include "ui/PrinterBar.h"
#include "ui/StatusBar.h"
#include "ui/TrayIcon.h"
#include "ui/Window.h"

namespace cola::app {

// Opciones de arranque que vienen de la línea de comandos (las interpreta
// app/Application.cpp y se aplican en MainWindow::Create).
struct StartupOptions {
    bool startInTray = false;  // --tray: arrancar oculta en la bandeja (inicio con Windows)
    std::wstring server;  // --server
    // true si se pasó --server (aunque sea vacío = este equipo); sin él se usa
    // el último servidor guardado en config.json.
    bool hasServer = false;
    bool laimi = false;  // --laimi: el servidor del LAIMI configurado
};

// Ventana de nivel superior de la app (una sola instancia por sesión). Es
// dueña de todos los módulos y controles (ver los miembros al final) y vive
// durante toda la ejecución: la crea Application y su destrucción termina el
// bucle de mensajes.
class MainWindow : public ui::Window {
public:
    // Nombre de la clase de ventana; Application lo usa con FindWindowW para
    // encontrar una instancia ya abierta.
    static constexpr wchar_t kClassName[] = L"ColaDeImpresionMainWindow";
    static UINT ShowInstanceMessage();  // mensaje registrado para traer la ventana al frente

    MainWindow();
    // Detiene los hilos de los servicios antes de destruir los controles.
    ~MainWindow() override;

    // Carga configuración, historial y atajos, crea la ventana y sus
    // controles, arranca el monitor de la cola contra el servidor elegido y
    // la muestra (o la deja en la bandeja con --tray). false si falla la
    // creación de la ventana.
    bool Create(const StartupOptions& options);

    // Procesa atajos antes de que el mensaje llegue al control con foco.
    bool PreTranslate(MSG& msg);

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    // --- Configuración / estado general ---
    // Resuelve el tema (sistema/claro/oscuro) y lo aplica a Style y a la ventana.
    void ApplyTheme();
    // Reaplica los textos tras cambiar de idioma (controles, filas, panel).
    void ApplyLanguage();
    // Avisa a todos los controles que cambió el estilo (tema, DPI o idioma),
    // rehace el layout y refresca cabecera, barra de estado y título.
    void OnStyleChanged();
    // Guarda config.json (se llama tras cada cambio de opción).
    void SaveSettings();
    // Guarda posición, tamaño y estado maximizado de la ventana en settings_.
    void SaveWindowPlacement();
    // Pone la ventana donde estaba la última vez (centrada si no hay posición
    // guardada o cae en un monitor que ya no está); show = false la deja oculta.
    void RestoreWindowPlacement(bool show);

    // --- Servidor / impresoras ---
    // Conecta (o reconecta) al servidor de impresión ("" = este equipo):
    // reinicia el estado de la conexión anterior y le pide al monitor que
    // cambie de servidor.
    void ConnectTo(const std::wstring& server);
    // Guarda el servidor del botón del LAIMI (servidor-laimi.txt o, si no se
    // puede escribir, config.json) y se conecta a él.
    void SetLaimiServer(const std::wstring& text);
    // Nombre del servidor para mostrar ("Este equipo" si es el local).
    std::wstring ServerDisplayName() const;
    // Filtra la lista por impresora (kAllPrinters = todas) y lo recuerda.
    void SelectPrinter(const std::wstring& printer);
    // Pasa a la impresora siguiente/anterior (delta = ±1).
    void CyclePrinter(int delta);
    // Acceso rápido: barra superior con las impresoras fijadas por el operador.
    void UpdatePrinterBar();
    // Fija o quita la impresora del acceso rápido y guarda la lista.
    void TogglePinnedPrinter(const std::wstring& printer);
    // Menú para fijar/quitar impresoras del acceso rápido.
    void ShowQuickAccessMenu(POINT screen);
    // Menú contextual de un chip de la barra (fijar/quitar, pausar, reanudar, vaciar).
    void ShowPrinterChipMenu(const std::wstring& printer, POINT screen);
    // Ctrl+1..9: elige la n-ésima impresora del acceso rápido.
    bool SelectQuickAccess(int index);
    std::wstring EffectivePrinter() const;  // impresora para acciones de impresora

    // --- Datos ---
    // Llegó una instantánea nueva del monitor (WM_APP_SNAPSHOT, en el hilo
    // de la UI): eventos al historial, problemas de impresoras, filas,
    // cabecera, barra de estado y vista previa.
    void OnSnapshot(core::SnapshotPtr snapshot);
    // Eventos de trabajos (nuevo, impreso, cancelado...) que detectó el
    // JobTracker: historial y notificaciones.
    void ProcessEvents(const std::vector<core::JobEvent>& events);
    // Notifica los problemas de impresora que aparecieron desde la
    // instantánea anterior (atasco, sin papel...); no avisa de lo que ya
    // estaba mal al conectar.
    void CheckPrinterProblems();
    // Rearma las filas de la lista (cola u historial) con el filtro, la
    // impresora y el orden actuales, conservando la selección.
    void RebuildRows();
    // Cabecera: impresora elegida, servidor o problema, y vista (cola/historial).
    void UpdateHeader();
    // Barra inferior: conteos, conexión, problemas y mensajes pasajeros.
    void UpdateStatus();
    // Total de páginas de los trabajos seleccionados en la barra inferior.
    void UpdateSelectionTotal();
    // Título de la ventana (barra de tareas) con el servidor y si es admin.
    void UpdateTitle();
    // Trabajo de la instantánea actual por su clave (JobInfo::Key), o nullptr.
    const core::JobInfo* FindJob(const std::wstring& key) const;
    // Copias de los trabajos seleccionados (vacío en el historial). Copias y
    // no punteros porque suelen viajar al hilo de acciones.
    std::vector<core::JobInfo> SelectedJobs() const;
    // Trabajo de la fila con el foco, o nullptr (apunta a la instantánea actual).
    const core::JobInfo* FocusedJob() const;

    // --- Vista previa ---
    // Muestra en el panel el trabajo (o registro del historial) con el foco
    // y pide su vista previa al PreviewService si cambió.
    void UpdatePreview();
    // Datos que necesita el servicio de vista previa para leer el trabajo.
    preview::PreviewRequest MakePreviewRequest(const core::JobInfo& job) const;
    // Líneas de "detalles" del panel (documento, usuario, equipo, páginas,
    // configuración...); doc puede ser nullptr si la vista previa no llegó.
    std::vector<std::wstring> JobDetails(const core::JobInfo& job, const preview::PreviewDocument* doc) const;

    // --- Comandos ---
    // Ejecuta un comando (de un atajo, un botón o un menú): punto único de
    // entrada para las acciones del usuario.
    void Execute(input::Command command);
    // Aplica una acción de trabajo (pausar, reanudar, reiniciar...) a la selección.
    void RunOnSelection(JobAction action);
    // Ejecuta una acción sobre trabajos y le avisa al historial cuáles liberó el operador.
    void RunJobAction(JobAction action, std::vector<core::JobInfo> jobs);
    // Pausa o reanuda los trabajos seleccionados (y opcionalmente la impresora).
    void TogglePauseSelection(bool alsoPrinter);
    // Reanuda los trabajos seleccionados y sus impresoras; cada impresora se
    // vuelve a pausar sola cuando todos sus trabajos liberados se enviaron.
    void PrintSelectionAndRepause();
    // Revisa qué impresoras de "imprimir y volver a pausar" ya enviaron
    // todos sus trabajos liberados y las vuelve a pausar.
    void RepauseReleasedPrinters();
    // Botones de pausa de la cabecera: qué harán según la selección y las impresoras.
    void UpdatePauseButtons();
    // Columnas: menú del encabezado (clic derecho) y volver a los anchos predeterminados.
    void ShowColumnHeaderMenu(POINT screen);
    void ResetColumnWidths();
    void ResetColumnOrder();
    // Cancela los trabajos (con confirmación si está activada) y los marca
    // como cancelados por el operador para el historial.
    void CancelSelection(const std::vector<core::JobInfo>& jobs);
    // Sube (direction < 0) o baja el trabajo con el foco una posición en la cola.
    void MoveFocused(int direction);
    // Menú de impresoras destino para reenviar el trabajo con el foco.
    void StartRedirect(POINT screen);
    // Acción sobre UNA impresora (la elegida o la del trabajo con el foco),
    // p. ej. vaciarla, con confirmación.
    void RunPrinterAction(JobAction action);
    // Impresoras afectadas por pausar/reanudar: la elegida, o todas en la vista "Todas".
    std::vector<std::wstring> TargetPrinters() const;
    // Alterna la pausa de las impresoras afectadas: si alguna está en pausa
    // las reanuda; si ninguna, las pausa (D18).
    void TogglePrinterPause();
    // printers vacío = las impresoras de TargetPrinters().
    void SetPrintersPaused(bool pause, std::vector<std::wstring> printers = {});
    // Resultado de una acción en segundo plano (WM_APP_ACTION, hilo de la
    // UI): mensaje, permisos de administración aprendidos y refresco.
    void OnActionResult(const ActionResult& result);
    // Vista rápida: el panel de detalles ocupa todo el cuerpo de la ventana.
    void ToggleQuickLook();
    // Alterna entre la cola y el historial.
    void ToggleHistory();
    // Pide dónde guardar y exporta todo el historial a un libro de Excel.
    void ExportHistory();
    // Alterna entre claro y oscuro explícitos (deja de seguir al sistema).
    void ToggleTheme();
    // Texto del cuadro de búsqueda: filtra la lista.
    void SetFilter(const std::wstring& filter);
    // Orden de la cola (equipo, hora, usuario...). delta rota entre criterios.
    void CycleSort(int delta);
    // Fija el criterio (id de texto) y la dirección, lo guarda y rearma la
    // lista; announce = avisarlo en la barra de estado.
    void SetSort(const std::wstring& sortId, bool descending, bool announce = true);
    // Clic en el encabezado de una columna de la lista.
    void OnColumnClicked(int sortId);
    // Carpeta de spool alternativa configurada para el servidor activo.
    std::wstring SpoolOverrideDir() const;
    // Mensaje temporal en la barra de estado (4 s, 8 s si es un error); tone
    // elige el color y el ícono.
    void ShowTransient(const std::wstring& text, ui::Tone tone);

    // --- Menús ---
    // Se muestran en coordenadas de pantalla y son modales (TrackPopupMenu
    // con TPM_RETURNCMD: devuelven el id elegido).
    void ShowPrinterMenu(POINT screen);     // botón de impresora de la cabecera
    void ShowMainMenu(POINT screen);        // botón ☰
    void ShowJobContextMenu(POINT screen);  // clic derecho en la lista
    void ShowTrayMenu();                    // clic derecho en el ícono de la bandeja
    // Atiende un id del menú principal, del acceso rápido o de los chips.
    void HandleMenuCommand(UINT id);

    // --- Ventana ---
    // Reparte el área cliente entre cabecera, barra, lista, panel y estado.
    void Layout();
    // Muestra la ventana (desde la bandeja, minimizada o detrás de otras) y la trae al frente.
    void ShowFromTray();
    // Oculta la ventana; sigue el ícono de la bandeja y el monitor corriendo.
    void HideToTray();
    // Salir de verdad (no a la bandeja): marca reallyQuit_ y cierra.
    void Quit();
    // Relanza la app como administrador ("runas", pide UAC) y cierra esta instancia.
    void RestartAsAdmin();
    // Notificación de la bandeja (globo/toast); error = ícono de error.
    void Notify(const std::wstring& title, const std::wstring& text, bool error);
    // Pide al directorio el carnet de los usuarios del historial que no lo tienen.
    void RequestHistoryCarnets();
    // Llegaron carnets nuevos: completa el historial y redibuja.
    void OnCarnets();

    // Módulos
    config::Settings settings_;     // config.json (opciones del usuario)
    config::History history_;       // historial.json (trabajos vistos)
    input::ShortcutMap shortcuts_;  // atajos actuales (predeterminados + personalizados)
    preview::RendererPaths renderers_;  // rutas a los renderizadores externos, si están
    // Hilo que consulta el spooler y publica instantáneas (WM_APP_SNAPSHOT).
    std::unique_ptr<core::QueueMonitor> monitor_;
    // Hilo que lee y renderiza las vistas previas.
    std::unique_ptr<preview::PreviewService> previews_;
    // Copias y páginas reales de todos los trabajos (para la columna "Páginas").
    std::unique_ptr<preview::JobFactsService> facts_;
    // Carnet de cada usuario, leído del Active Directory (solo lectura, D56).
    std::unique_ptr<core::CarnetService> carnets_;
    // Ejecuta las acciones sobre trabajos/impresoras en segundo plano.
    std::unique_ptr<JobActionRunner> actions_;
    // Compara instantáneas sucesivas y deduce eventos (impreso, cancelado...).
    core::JobTracker tracker_;
    core::PrintReleaseTracker release_;  // trabajos de "imprimir y volver a pausar"

    // Controles
    ui::HeaderBar header_;       // barra superior (servidor, impresora, búsqueda, botones)
    ui::PrinterBar printerBar_;  // acceso rápido a impresoras fijadas
    ui::JobListView list_;       // lista de trabajos o del historial
    ui::PreviewPane preview_;    // panel de detalles y vista previa
    ui::StatusBar status_;       // barra inferior
    ui::HelpOverlay help_;       // panel de atajos (F1)
    ui::TrayIcon tray_;          // ícono del área de notificación
    HICON icon_ = nullptr;       // ícono grande de la ventana
    HICON iconSmall_ = nullptr;  // ícono pequeño (barra de título y bandeja)

    // Estado
    core::SnapshotPtr snapshot_;  // última instantánea recibida (nullptr hasta la primera)
    std::wstring filter_;         // texto de búsqueda (ya recortado)
    bool historyView_ = false;    // mostrando el historial en vez de la cola
    bool quickLook_ = false;      // vista rápida: el panel ocupa todo el cuerpo
    // Salir de verdad: WM_CLOSE destruye la ventana aunque esté activo
    // "minimizar a la bandeja".
    bool reallyQuit_ = false;
    bool connecting_ = true;  // esperando la primera instantánea del servidor
    bool editingLaimi_ = false;  // el cuadro de servidor cambia el del LAIMI (no conecta a otro)
    bool startPausePending_ = false;  // pausar impresoras al recibir la primera instantánea
    bool startPauseInFlight_ = false; // su resultado se informa sin diálogos
    // Clave de la vista previa mostrada (servidor|impresora|id|...): evita
    // pedirla de nuevo si no cambió y descarta respuestas de otro trabajo.
    std::wstring currentPreviewKey_;
    std::map<std::wstring, bool> adminRights_;           // impresora -> puede administrar
    std::map<std::wstring, std::uint32_t> lastProblems_;  // para notificar problemas nuevos
    std::wstring transientText_;  // mensaje temporal de la barra de estado ("" = ninguno)
    ui::Tone transientTone_ = ui::Tone::Neutral;
    RECT splitter_{};  // divisor entre lista y panel (vacío si el panel no se ve)
    bool draggingSplitter_ = false;  // arrastrando el divisor
    std::vector<std::wstring> menuPrinters_;  // impresoras listadas en el último menú
    std::wstring chipPrinter_;                // impresora del menú contextual de la barra
    std::vector<std::wstring> menuServers_;  // servidores listados en el último menú principal
    // Copia del trabajo que se está reenviando (el menú de destinos es modal
    // y la instantánea puede cambiar mientras está abierto).
    core::JobInfo redirectJob_;
};

}  // namespace cola::app
