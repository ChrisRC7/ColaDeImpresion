// Implementación del monitor de la cola (ver QueueMonitor.h). Todo lo de este
// archivo, salvo el constructor, el destructor, Start, Stop y RequestRefresh,
// corre en el hilo propio del monitor: aquí sí se llama al spooler.
#include "core/QueueMonitor.h"

#include <winspool.h>

#include <algorithm>

#include "core/Spooler.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::core {

namespace {
// Límite de WaitForMultipleObjects (64) menos los eventos propios y el del servidor.
// Las impresoras que pasen de este número se vigilan solo por sondeo.
constexpr size_t kMaxNotifiedPrinters = MAXIMUM_WAIT_OBJECTS - 3;
// Tiempo para agrupar ráfagas de notificaciones antes de publicar.
// (Un trabajo que llega genera varias notificaciones seguidas; publicar una
// instantánea por cada una haría trabajar a la UI sin necesidad.)
constexpr DWORD kDebounceMs = 120;
// Cada cuánto se vuelve a enumerar la lista de impresoras del servidor.
constexpr DWORD kPrinterReloadMs = 30000;
// Veces seguidas que el spooler debe reportar más trabajos de los enumerados
// para considerarlo un problema real (entre las dos llamadas un trabajo puede
// terminar, y eso no es un error).
constexpr int kMissingStreakToReport = 2;
}  // namespace

// Estado por impresora vigilada. Vive mientras la impresora siga en el
// servidor (ReloadPrinters lo traslada de una enumeración a la siguiente).
struct QueueMonitor::Watched {
    PrinterInfo info;      // últimos datos de la impresora
    // Handle con el nombre de la conexión (acceso de uso): estado, trabajos de
    // la caché del cliente y notificaciones.
    PrinterHandle handle;
    PrinterHandle adminHandle;  // para pausar trabajos ajenos (retención); se abre al necesitarlo
    bool adminTried = false;    // ya se intentó abrir adminHandle (no se reintenta)
    // Impresoras de un servidor: la misma cola abierta con el nombre DNS completo
    // del servidor, que consulta al servidor en vez de la caché del cliente
    // (ver spooler::CacheBypassName). Es la lista que manda.
    PrinterHandle direct;
    std::wstring directName;      // "\\\\srv.dominio\\Cola" ("" = sin alternativa todavía)
    bool directResolved = false;  // ya se obtuvo directName (no se vuelve a pedir)
    ULONGLONG directRetryAt = 0;  // si no se pudo abrir, cuándo reintentar (GetTickCount64)
    size_t lastCacheDiff = 0;     // para no repetir el mismo aviso en el log
    // Nombres de bandejas y papeles del controlador (se leen una vez).
    bool formsLoaded = false;  // ya se intentó (aunque haya fallado: no se repite)
    std::vector<std::pair<short, std::wstring>> bins;  // copia que se pone en cada info
    std::vector<DriverPaper> papers;
    // Notificación de cambios (FindFirstPrinterChangeNotification) o
    // INVALID_HANDLE_VALUE si no hay (sin soporte, límite de 64 o se rompió).
    HANDLE change = INVALID_HANDLE_VALUE;
    std::vector<JobInfo> jobs;  // últimos trabajos (ya conciliados y anotados)
    bool dirty = true;          // hay que refrescarla en la próxima vuelta
    std::uint32_t missing = 0;  // trabajos que el spooler cuenta y no se pudieron enumerar
    int missingStreak = 0;      // refrescos seguidos con trabajos faltantes (D21)

    // Cierra la notificación; los PrinterHandle se cierran solos.
    ~Watched() {
        if (change != INVALID_HANDLE_VALUE) FindClosePrinterChangeNotification(change);
    }
};

QueueMonitor::QueueMonitor(SnapshotCallback callback) : callback_(std::move(callback)) {
    // Parada: reinicio manual (queda señalado hasta que Start lo reinicie, así
    // ninguna espera intermedia se lo "come"). Refresco: reinicio automático
    // (cada SetEvent produce un único despertar).
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    refreshEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

QueueMonitor::~QueueMonitor() {
    Stop();
    CloseHandle(stopEvent_);
    CloseHandle(refreshEvent_);
}

void QueueMonitor::Start(const std::wstring& server, int pollIntervalMs) {
    // Se detiene el hilo anterior antes de tocar server_ (lo lee Run).
    Stop();
    server_ = spooler::NormalizeServerName(server);
    pollMs_ = pollIntervalMs;
    ResetEvent(stopEvent_);
    thread_ = std::thread([this] { Run(); });
}

void QueueMonitor::Stop() {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_);
    thread_.join();
}

void QueueMonitor::RequestRefresh() { SetEvent(refreshEvent_); }

bool QueueMonitor::ReloadPrinters(std::vector<std::unique_ptr<Watched>>& watched, std::wstring& error) {
    DWORD err = ERROR_SUCCESS;
    std::vector<PrinterInfo> printers = spooler::EnumeratePrinters(server_, &err);
    // Servidor inaccesible: se conservan las impresoras anteriores y el error
    // pasa a la instantánea (el JobTracker no interpreta eso como "terminaron").
    if (err != ERROR_SUCCESS) {
        error = util::Win32ErrorMessage(err);
        return false;
    }
    error.clear();

    // Conserva los handles de impresoras que siguen existiendo.
    std::vector<std::unique_ptr<Watched>> next;
    for (auto& info : printers) {
        auto it = std::find_if(watched.begin(), watched.end(),
                               [&](const std::unique_ptr<Watched>& w) { return w && w->info.name == info.name; });
        std::unique_ptr<Watched> w;
        if (it != watched.end()) {
            w = std::move(*it);
        } else {
            // Impresora nueva: se abre con acceso de uso (basta para ver la cola
            // y recibir notificaciones). Si falla, RefreshPrinter reintentará.
            w = std::make_unique<Watched>();
            if (!w->handle.Open(info.name, false))
                util::LogError(L"OpenPrinter(%ls) falló: %lu", info.name.c_str(), GetLastError());
        }
        w->info = std::move(info);
        w->dirty = true;
        // Notificación de cambios de trabajos y de la impresora (pausa,
        // estado). Solo para las primeras kMaxNotifiedPrinters; si no se puede
        // (servidores que bloquean RPC de vuelta), queda el sondeo.
        if (w->handle && w->change == INVALID_HANDLE_VALUE && next.size() < kMaxNotifiedPrinters) {
            w->change = FindFirstPrinterChangeNotification(w->handle.get(),
                                                           PRINTER_CHANGE_JOB | PRINTER_CHANGE_SET_PRINTER, 0, nullptr);
            if (w->change == INVALID_HANDLE_VALUE)
                util::LogError(L"Sin notificaciones para %ls (%lu); se usará sondeo", w->info.name.c_str(),
                               GetLastError());
        }
        next.push_back(std::move(w));
    }
    // Las impresoras que ya no están se destruyen aquí (cierran sus handles y
    // notificaciones al salir 'watched' anterior).
    watched = std::move(next);
    return true;
}

void QueueMonitor::RefreshPrinter(Watched& w) {
    // Pasos: (1) reabrir si hace falta, (2) estado de la impresora y formularios
    // del controlador, (3) trabajos por la conexión (caché), (4) trabajos por el
    // nombre DNS completo y conciliación (D28), (5) reintento y aviso de
    // trabajos faltantes (D21), (6) retención automática, (7) manejo de errores.
    w.dirty = false;
    if (!w.handle) {
        // Reintenta abrir (la impresora pudo estar inaccesible temporalmente).
        if (!w.handle.Open(w.info.name, false)) {
            w.jobs.clear();
            w.info.problems |= ProblemNotAvailable;
            return;
        }
    }
    // Si GetPrinter falla se conservan los datos anteriores de info.
    spooler::QueryPrinter(w.handle.get(), server_, w.info);
    // Bandejas y papeles (D34): una sola vez por cola, en este hilo, porque
    // DeviceCapabilities carga el controlador y puede tardar.
    if (!w.formsLoaded) {
        w.formsLoaded = true;
        spooler::GetDriverForms(w.info.name, w.bins, w.papers);
    }
    // QueryPrinter reescribe info: se vuelven a poner las listas guardadas.
    w.info.bins = w.bins;
    w.info.papers = w.papers;
    DWORD err = ERROR_SUCCESS;
    // Lista de la caché del cliente (por el nombre de la conexión). Se usa
    // tal cual en impresoras locales y como respaldo si falla la directa.
    std::vector<JobInfo> cached = spooler::EnumerateJobs(w.handle.get(), w.info.name, w.info.problems, &err);
    w.jobs = cached;

    // Impresora de un servidor: el handle de la conexión responde desde la caché
    // del cliente, que a veces no recibe los trabajos enviados desde otras PCs
    // hasta el siguiente evento. La lista que manda es la del servidor.
    if (err == ERROR_SUCCESS && !w.directResolved && GetTickCount64() >= w.directRetryAt) {
        w.directName = spooler::CacheBypassName(w.info.name);
        // Sin nombre puede ser una cola de este equipo (no hace falta) o un DNS
        // que todavía no responde (al iniciar con Windows): se vuelve a probar en
        // un minuto. Antes quedaba con la caché del cliente toda la sesión.
        w.directResolved = !w.directName.empty();
        if (w.directResolved)
            util::LogInfo(L"%ls: los trabajos se consultan en %ls (sin la caché del cliente)", w.info.name.c_str(),
                          w.directName.c_str());
        else
            w.directRetryAt = GetTickCount64() + 60000;
    }
    if (err == ERROR_SUCCESS && !w.directName.empty()) {
        // Se abre (o reabre tras un fallo, respetando la espera) la cola por
        // el nombre DNS completo.
        if (!w.direct && GetTickCount64() >= w.directRetryAt && !w.direct.Open(w.directName, false)) {
            util::LogError(L"OpenPrinter(%ls) falló: %lu; se usa la caché del cliente", w.directName.c_str(),
                           GetLastError());
            w.directRetryAt = GetTickCount64() + 60000;
        }
        if (w.direct) {
            // Estado y cantidad de trabajos también del servidor (el nombre se conserva).
            PrinterInfo fresh = w.info;
            if (spooler::QueryPrinter(w.direct.get(), server_, fresh)) {
                w.info.rawStatus = fresh.rawStatus;
                w.info.problems = fresh.problems;
                w.info.jobCount = fresh.jobCount;
            }
            DWORD directErr = ERROR_SUCCESS;
            // Los trabajos llevan el nombre de la CONEXIÓN (w.info.name), no el
            // DNS completo: las acciones y la retención usan ese nombre (D28).
            auto serverJobs = spooler::EnumerateJobs(w.direct.get(), w.info.name, w.info.problems, &directErr);
            if (directErr == ERROR_SUCCESS) {
                // Manda la del servidor; de la caché solo las copias locales de CSR.
                w.jobs = ReconcileJobLists(std::move(serverJobs), cached);
                // Diferencia entre caché y servidor: solo diagnóstico en app.log,
                // y solo cuando cambia (si no, se repetiría en cada refresco).
                const size_t diff = w.jobs.size() > cached.size() ? w.jobs.size() - cached.size()
                                                                   : cached.size() - w.jobs.size();
                if (diff != w.lastCacheDiff && diff > 0)
                    util::LogInfo(L"%ls: la caché del cliente muestra %zu trabajo(s) y el servidor %zu",
                                  w.info.name.c_str(), cached.size(), w.jobs.size());
                w.lastCacheDiff = diff;
            } else {
                // Falló la consulta directa: esta vez se queda la lista de la
                // caché y se reabre el handle directo en 5 s.
                util::LogError(L"EnumJobs(%ls) falló: %lu; se usa la caché del cliente", w.directName.c_str(),
                               directErr);
                w.direct.Close();
                w.directRetryAt = GetTickCount64() + 5000;
            }
        }
    }
    // El spooler dice tener más trabajos (cJobs) que los enumerados: puede ser
    // una carrera (llegó uno entre GetPrinter y EnumJobs), así que se vuelve a
    // enumerar una vez y se queda la lista más larga.
    if (err == ERROR_SUCCESS && w.info.jobCount > w.jobs.size()) {
        DWORD retryErr = ERROR_SUCCESS;
        HANDLE source = w.direct ? w.direct.get() : w.handle.get();
        auto retry = spooler::EnumerateJobs(source, w.info.name, w.info.problems, &retryErr);
        if (retryErr == ERROR_SUCCESS && w.direct) retry = ReconcileJobLists(std::move(retry), cached);
        if (retryErr == ERROR_SUCCESS && retry.size() > w.jobs.size()) w.jobs = std::move(retry);
    }
    // Siguen faltando: se cuenta y, si se repite kMissingStreakToReport veces
    // seguidas, se registra en el log (una vez por racha) y Publish lo avisa.
    if (err == ERROR_SUCCESS && w.info.jobCount > w.jobs.size()) {
        w.missing = w.info.jobCount - static_cast<std::uint32_t>(w.jobs.size());
        if (++w.missingStreak == kMissingStreakToReport)
            util::LogError(L"%ls: el spooler reporta %u trabajos y solo se enumeraron %zu", w.info.name.c_str(),
                           w.info.jobCount, w.jobs.size());
    } else {
        w.missing = 0;
        w.missingStreak = 0;
    }
    // La retención solo con una lista válida (una vacía por error haría creer
    // que todos los trabajos salieron).
    if (err == ERROR_SUCCESS) ApplyAutoHold(w);
    // Error al enumerar por la conexión: la impresora se marca no disponible y
    // se cierra todo para empezar limpio en el próximo refresco (sin
    // notificación, esta impresora queda por sondeo hasta la próxima recarga).
    if (err != ERROR_SUCCESS) {
        util::LogError(L"EnumJobs(%ls) falló: %lu", w.info.name.c_str(), err);
        w.info.problems |= ProblemNotAvailable;
        w.handle.Close();  // se reabrirá en el próximo refresco
        if (w.change != INVALID_HANDLE_VALUE) {
            FindClosePrinterChangeNotification(w.change);
            w.change = INVALID_HANDLE_VALUE;
        }
    }
}

void QueueMonitor::ApplyAutoHold(Watched& w) {
    // Decide qué pausar (trabajos nuevos, sin la copia local de CSR: D39).
    const std::vector<std::uint32_t> toHold = hold_.Process(w.info.name, w.jobs, autoHold_, holdExisting_);
    for (std::uint32_t id : toHold) {
        // El handle con administración se abre solo la primera vez que hace
        // falta (la mayoría de los refrescos no retienen nada). Si no se logra,
        // se usa el de uso: el spooler igual permite pausar con "Administrar
        // documentos" o si el trabajo es del propio operador.
        if (!w.adminTried) {
            w.adminTried = true;
            w.adminHandle.OpenBest(w.info.name);
        }
        HANDLE h = w.adminHandle ? w.adminHandle.get() : w.handle.get();
        auto job = std::find_if(w.jobs.begin(), w.jobs.end(), [id](const JobInfo& j) { return j.id == id; });
        // SetJob directo (sin spooler::PauseJob) para no abrir un handle nuevo
        // en cada trabajo: aquí la velocidad importa (carrera con el spooler).
        // Va por el nombre de la conexión, como las demás acciones.
        if (SetJobW(h, id, 0, nullptr, JOB_CONTROL_PAUSE)) {
            if (job != w.jobs.end()) {
                hold_.MarkHeld(job->Key());
                job->state = JobState::Paused;  // el próximo refresco lo confirma
                util::LogInfo(L"Retenido: %ls#%u '%ls' de %ls", w.info.name.c_str(), id, job->document.c_str(),
                              job->user.c_str());
            }
        } else {
            // Fallo (normalmente ERROR_ACCESS_DENIED): se cuenta para la barra
            // de estado. No se reintenta: el tracker ya lo dio por evaluado.
            ++holdFailures_;
            holdError_ = GetLastError();
            util::LogError(L"No se pudo retener %ls#%u: %lu", w.info.name.c_str(), id, holdError_);
        }
    }
    // Marca heldByApp en los trabajos que siguen en pausa por la retención.
    hold_.Annotate(w.jobs);
}

void QueueMonitor::Publish(const std::vector<std::unique_ptr<Watched>>& watched, const std::wstring& error,
                           bool notifying) {
    auto snapshot = std::make_shared<QueueSnapshot>();
    snapshot->server = server_;
    snapshot->error = error;
    snapshot->sequence = ++sequence_;
    snapshot->notificationsActive = notifying;
    snapshot->autoHold = autoHold_;
    snapshot->holdFailures = holdFailures_;
    snapshot->holdError = holdError_;
    // Copia los datos de cada impresora (la instantánea es inmutable y
    // sobrevive a este hilo). Los trabajos faltantes solo cuentan tras la racha.
    for (const auto& w : watched) {
        snapshot->printers.push_back(w->info);
        snapshot->jobs.insert(snapshot->jobs.end(), w->jobs.begin(), w->jobs.end());
        if (w->missingStreak >= kMissingStreakToReport) snapshot->missingJobs += w->missing;
        if (!w->handle) ++snapshot->unreachablePrinters;
    }
    // El receptor (MainWindow) la reenvía al hilo de la UI con PostMessage.
    if (callback_) callback_(std::move(snapshot));
}

void QueueMonitor::Run() {
    util::LogInfo(L"Monitor iniciado para '%ls'", server_.empty() ? L"(local)" : server_.c_str());
    std::vector<std::unique_ptr<Watched>> watched;
    std::wstring error;  // error global de la última enumeración de impresoras
    // GetTickCount (32 bits) a propósito: las restas sin signo funcionan aunque
    // el contador dé la vuelta a los 49,7 días.
    DWORD lastPoll = GetTickCount();
    DWORD lastReload = lastPoll;
    // Nueva conexión: la retención empieza de cero (D18: los trabajos
    // existentes solo se retienen con holdExisting_).
    hold_.Reset();
    holdFailures_ = 0;
    holdError_ = 0;

    // Notificación a nivel de servidor para altas/bajas de impresoras.
    PrinterHandle serverHandle;
    HANDLE serverChange = INVALID_HANDLE_VALUE;
    if (serverHandle.Open(server_, false)) {
        serverChange = FindFirstPrinterChangeNotification(serverHandle.get(), PRINTER_CHANGE_PRINTER, 0, nullptr);
    }

    // Bucle principal: (1) recargar impresoras si toca, (2) refrescar las
    // marcadas, (3) publicar, (4) esperar el próximo motivo para despertar.
    bool reload = true;
    for (;;) {
        if (reload) {
            ReloadPrinters(watched, error);
            lastReload = GetTickCount();
            reload = false;
        }
        for (auto& w : watched)
            if (w->dirty) RefreshPrinter(*w);

        // "En vivo" en la barra de estado si al menos una impresora tiene
        // notificaciones; si no, "Actualización por sondeo" (D5).
        bool notifying = std::any_of(watched.begin(), watched.end(),
                                     [](const auto& w) { return w->change != INVALID_HANDLE_VALUE; });
        Publish(watched, error, notifying);

        // Arma la lista de objetos a esperar.
        // Orden fijo: [0] parada, [1] refresco, [2] servidor (si hay), luego
        // una notificación por impresora. 'owners' dice de qué impresora es
        // cada índice (nullptr para los eventos propios y el del servidor).
        std::vector<HANDLE> handles = {stopEvent_, refreshEvent_};
        std::vector<Watched*> owners = {nullptr, nullptr};
        if (serverChange != INVALID_HANDLE_VALUE) {
            handles.push_back(serverChange);
            owners.push_back(nullptr);
        }
        const size_t firstPrinter = handles.size();
        for (auto& w : watched) {
            if (w->change != INVALID_HANDLE_VALUE && handles.size() < MAXIMUM_WAIT_OBJECTS) {
                handles.push_back(w->change);
                owners.push_back(w.get());
            }
        }

        // El sondeo de respaldo tiene su propio vencimiento: antes cada
        // notificación reiniciaba la espera, así que una impresora "ruidosa"
        // podía dejar a las demás sin refrescar (sus trabajos no aparecían).
        const DWORD interval = static_cast<DWORD>(std::max(500, pollMs_.load()));
        const DWORD since = GetTickCount() - lastPoll;
        const DWORD timeout = since >= interval ? 0 : interval - since;

        DWORD rc = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, timeout);
        if (rc == WAIT_OBJECT_0) break;  // stop

        // newJob: alguna notificación trajo PRINTER_CHANGE_ADD_JOB (D17: se
        // salta el agrupamiento para retenerlo cuanto antes).
        bool newJob = false;
        // Atiende la notificación señalada en 'index': FindNextPrinterChange-
        // Notification la rearma (obligatorio para recibir la siguiente) y dice
        // qué cambió. Marca la impresora para refrescar o pide recargar la lista.
        auto rearm = [&](DWORD index) {
            DWORD change = 0;
            bool ok = FindNextPrinterChangeNotification(handles[index], &change, nullptr, nullptr) != FALSE;
            if (index >= firstPrinter) {
                Watched* w = owners[index];
                w->dirty = true;
                if (change & PRINTER_CHANGE_ADD_JOB) newJob = true;
                if (!ok && w->change != INVALID_HANDLE_VALUE) {
                    // La notificación se rompió (p. ej. se perdió la conexión): pasa a sondeo.
                    FindClosePrinterChangeNotification(w->change);
                    w->change = INVALID_HANDLE_VALUE;
                    handles[index] = refreshEvent_;  // marcador inofensivo para esta vuelta
                }
            } else if (handles[index] == serverChange) {
                // Se agregó/quitó/cambió una impresora del servidor.
                reload = true;
                if (!ok) {
                    FindClosePrinterChangeNotification(serverChange);
                    serverChange = INVALID_HANDLE_VALUE;
                    handles[index] = refreshEvent_;
                }
            }
        };

        if (rc == WAIT_TIMEOUT) {
            // Sondeo de respaldo: refresca todo.
            for (auto& w : watched) w->dirty = true;
            lastPoll = GetTickCount();
            if (GetTickCount() - lastReload >= kPrinterReloadMs) reload = true;
            if (error.size()) reload = true;  // reintenta conectar al servidor
        } else if (rc == WAIT_OBJECT_0 + 1) {
            // RequestRefresh: todo, incluida la lista de impresoras. Cuenta
            // como sondeo (reinicia su vencimiento).
            for (auto& w : watched) w->dirty = true;
            lastPoll = GetTickCount();
            reload = true;
        } else if (rc > WAIT_OBJECT_0 + 1 && rc < WAIT_OBJECT_0 + handles.size()) {
            rearm(rc - WAIT_OBJECT_0);
            // Agrupa las notificaciones que lleguen en ráfaga, salvo si hay que
            // retener un trabajo nuevo: ahí cada milisegundo cuenta.
            // Se espera solo sobre las notificaciones (desde el índice 2, sin
            // parada ni refresco) con kDebounceMs de margen, hasta 64 veces; el
            // índice devuelto se corrige sumando esos 2. Nota: durante la ráfaga
            // no se mira la parada (como mucho unos segundos de demora en Stop).
            for (int burst = 0; burst < 64 && !(newJob && autoHold_); ++burst) {
                DWORD more = WaitForMultipleObjects(static_cast<DWORD>(handles.size() - 2), handles.data() + 2,
                                                    FALSE, kDebounceMs);
                if (more == WAIT_TIMEOUT || more == WAIT_FAILED) break;
                rearm(more - WAIT_OBJECT_0 + 2);
            }
        } else if (rc == WAIT_FAILED) {
            // Algún handle quedó inválido: se espera un intervalo (para no girar
            // en vacío) y se refresca todo, lo que reconstruye la lista.
            util::LogError(L"WaitForMultipleObjects falló: %lu", GetLastError());
            Sleep(interval);
            for (auto& w : watched) w->dirty = true;
            lastPoll = GetTickCount();
        }
    }

    // Limpieza: la notificación del servidor a mano; 'watched' y los handles
    // se cierran solos al salir de la función (RAII).
    if (serverChange != INVALID_HANDLE_VALUE) FindClosePrinterChangeNotification(serverChange);
    util::LogInfo(L"Monitor detenido para '%ls'", server_.empty() ? L"(local)" : server_.c_str());
}

}  // namespace cola::core
