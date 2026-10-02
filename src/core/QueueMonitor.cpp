#include "core/QueueMonitor.h"

#include <winspool.h>

#include <algorithm>

#include "core/Spooler.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::core {

namespace {
// Límite de WaitForMultipleObjects (64) menos los eventos propios y el del servidor.
constexpr size_t kMaxNotifiedPrinters = MAXIMUM_WAIT_OBJECTS - 3;
// Tiempo para agrupar ráfagas de notificaciones antes de publicar.
constexpr DWORD kDebounceMs = 120;
// Cada cuánto se vuelve a enumerar la lista de impresoras del servidor.
constexpr DWORD kPrinterReloadMs = 30000;
// Veces seguidas que el spooler debe reportar más trabajos de los enumerados
// para considerarlo un problema real (entre las dos llamadas un trabajo puede
// terminar, y eso no es un error).
constexpr int kMissingStreakToReport = 2;
}  // namespace

struct QueueMonitor::Watched {
    PrinterInfo info;
    PrinterHandle handle;
    PrinterHandle adminHandle;  // para pausar trabajos ajenos (retención); se abre al necesitarlo
    bool adminTried = false;
    // Impresoras de un servidor: la misma cola abierta con el nombre DNS completo
    // del servidor, que consulta al servidor en vez de la caché del cliente
    // (ver spooler::CacheBypassName). Es la lista que manda.
    PrinterHandle direct;
    std::wstring directName;
    bool directResolved = false;
    ULONGLONG directRetryAt = 0;  // si no se pudo abrir, cuándo reintentar (GetTickCount64)
    size_t lastCacheDiff = 0;     // para no repetir el mismo aviso en el log
    // Nombres de bandejas y papeles del controlador (se leen una vez).
    bool formsLoaded = false;
    std::vector<std::pair<short, std::wstring>> bins;
    std::vector<DriverPaper> papers;
    HANDLE change = INVALID_HANDLE_VALUE;
    std::vector<JobInfo> jobs;
    bool dirty = true;
    std::uint32_t missing = 0;  // trabajos que el spooler cuenta y no se pudieron enumerar
    int missingStreak = 0;

    ~Watched() {
        if (change != INVALID_HANDLE_VALUE) FindClosePrinterChangeNotification(change);
    }
};

QueueMonitor::QueueMonitor(SnapshotCallback callback) : callback_(std::move(callback)) {
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    refreshEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

QueueMonitor::~QueueMonitor() {
    Stop();
    CloseHandle(stopEvent_);
    CloseHandle(refreshEvent_);
}

void QueueMonitor::Start(const std::wstring& server, int pollIntervalMs) {
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
            w = std::make_unique<Watched>();
            if (!w->handle.Open(info.name, false))
                util::LogError(L"OpenPrinter(%ls) falló: %lu", info.name.c_str(), GetLastError());
        }
        w->info = std::move(info);
        w->dirty = true;
        if (w->handle && w->change == INVALID_HANDLE_VALUE && next.size() < kMaxNotifiedPrinters) {
            w->change = FindFirstPrinterChangeNotification(w->handle.get(),
                                                           PRINTER_CHANGE_JOB | PRINTER_CHANGE_SET_PRINTER, 0, nullptr);
            if (w->change == INVALID_HANDLE_VALUE)
                util::LogError(L"Sin notificaciones para %ls (%lu); se usará sondeo", w->info.name.c_str(),
                               GetLastError());
        }
        next.push_back(std::move(w));
    }
    watched = std::move(next);
    return true;
}

void QueueMonitor::RefreshPrinter(Watched& w) {
    w.dirty = false;
    if (!w.handle) {
        // Reintenta abrir (la impresora pudo estar inaccesible temporalmente).
        if (!w.handle.Open(w.info.name, false)) {
            w.jobs.clear();
            w.info.problems |= ProblemNotAvailable;
            return;
        }
    }
    spooler::QueryPrinter(w.handle.get(), server_, w.info);
    if (!w.formsLoaded) {
        w.formsLoaded = true;
        spooler::GetDriverForms(w.info.name, w.bins, w.papers);
    }
    w.info.bins = w.bins;
    w.info.papers = w.papers;
    DWORD err = ERROR_SUCCESS;
    std::vector<JobInfo> cached = spooler::EnumerateJobs(w.handle.get(), w.info.name, w.info.problems, &err);
    w.jobs = cached;

    // Impresora de un servidor: el handle de la conexión responde desde la caché
    // del cliente, que a veces no recibe los trabajos enviados desde otras PCs
    // hasta el siguiente evento. La lista que manda es la del servidor.
    if (err == ERROR_SUCCESS && !w.directResolved) {
        w.directResolved = true;
        w.directName = spooler::CacheBypassName(w.info.name);
        if (!w.directName.empty())
            util::LogInfo(L"%ls: los trabajos se consultan en %ls (sin la caché del cliente)", w.info.name.c_str(),
                          w.directName.c_str());
    }
    if (err == ERROR_SUCCESS && !w.directName.empty()) {
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
            auto serverJobs = spooler::EnumerateJobs(w.direct.get(), w.info.name, w.info.problems, &directErr);
            if (directErr == ERROR_SUCCESS) {
                w.jobs = ReconcileJobLists(std::move(serverJobs), cached);
                const size_t diff = w.jobs.size() > cached.size() ? w.jobs.size() - cached.size()
                                                                   : cached.size() - w.jobs.size();
                if (diff != w.lastCacheDiff && diff > 0)
                    util::LogInfo(L"%ls: la caché del cliente muestra %zu trabajo(s) y el servidor %zu",
                                  w.info.name.c_str(), cached.size(), w.jobs.size());
                w.lastCacheDiff = diff;
            } else {
                util::LogError(L"EnumJobs(%ls) falló: %lu; se usa la caché del cliente", w.directName.c_str(),
                               directErr);
                w.direct.Close();
                w.directRetryAt = GetTickCount64() + 5000;
            }
        }
    }
    if (err == ERROR_SUCCESS && w.info.jobCount > w.jobs.size()) {
        DWORD retryErr = ERROR_SUCCESS;
        HANDLE source = w.direct ? w.direct.get() : w.handle.get();
        auto retry = spooler::EnumerateJobs(source, w.info.name, w.info.problems, &retryErr);
        if (retryErr == ERROR_SUCCESS && w.direct) retry = ReconcileJobLists(std::move(retry), cached);
        if (retryErr == ERROR_SUCCESS && retry.size() > w.jobs.size()) w.jobs = std::move(retry);
    }
    if (err == ERROR_SUCCESS && w.info.jobCount > w.jobs.size()) {
        w.missing = w.info.jobCount - static_cast<std::uint32_t>(w.jobs.size());
        if (++w.missingStreak == kMissingStreakToReport)
            util::LogError(L"%ls: el spooler reporta %u trabajos y solo se enumeraron %zu", w.info.name.c_str(),
                           w.info.jobCount, w.jobs.size());
    } else {
        w.missing = 0;
        w.missingStreak = 0;
    }
    if (err == ERROR_SUCCESS) ApplyAutoHold(w);
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
    const std::vector<std::uint32_t> toHold = hold_.Process(w.info.name, w.jobs, autoHold_, holdExisting_);
    for (std::uint32_t id : toHold) {
        if (!w.adminTried) {
            w.adminTried = true;
            w.adminHandle.OpenBest(w.info.name);
        }
        HANDLE h = w.adminHandle ? w.adminHandle.get() : w.handle.get();
        auto job = std::find_if(w.jobs.begin(), w.jobs.end(), [id](const JobInfo& j) { return j.id == id; });
        if (SetJobW(h, id, 0, nullptr, JOB_CONTROL_PAUSE)) {
            if (job != w.jobs.end()) {
                hold_.MarkHeld(job->Key());
                job->state = JobState::Paused;  // el próximo refresco lo confirma
                util::LogInfo(L"Retenido: %ls#%u '%ls' de %ls", w.info.name.c_str(), id, job->document.c_str(),
                              job->user.c_str());
            }
        } else {
            ++holdFailures_;
            holdError_ = GetLastError();
            util::LogError(L"No se pudo retener %ls#%u: %lu", w.info.name.c_str(), id, holdError_);
        }
    }
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
    for (const auto& w : watched) {
        snapshot->printers.push_back(w->info);
        snapshot->jobs.insert(snapshot->jobs.end(), w->jobs.begin(), w->jobs.end());
        if (w->missingStreak >= kMissingStreakToReport) snapshot->missingJobs += w->missing;
        if (!w->handle) ++snapshot->unreachablePrinters;
    }
    if (callback_) callback_(std::move(snapshot));
}

void QueueMonitor::Run() {
    util::LogInfo(L"Monitor iniciado para '%ls'", server_.empty() ? L"(local)" : server_.c_str());
    std::vector<std::unique_ptr<Watched>> watched;
    std::wstring error;
    DWORD lastPoll = GetTickCount();
    DWORD lastReload = lastPoll;
    hold_.Reset();
    holdFailures_ = 0;
    holdError_ = 0;

    // Notificación a nivel de servidor para altas/bajas de impresoras.
    PrinterHandle serverHandle;
    HANDLE serverChange = INVALID_HANDLE_VALUE;
    if (serverHandle.Open(server_, false)) {
        serverChange = FindFirstPrinterChangeNotification(serverHandle.get(), PRINTER_CHANGE_PRINTER, 0, nullptr);
    }

    bool reload = true;
    for (;;) {
        if (reload) {
            ReloadPrinters(watched, error);
            lastReload = GetTickCount();
            reload = false;
        }
        for (auto& w : watched)
            if (w->dirty) RefreshPrinter(*w);

        bool notifying = std::any_of(watched.begin(), watched.end(),
                                     [](const auto& w) { return w->change != INVALID_HANDLE_VALUE; });
        Publish(watched, error, notifying);

        // Arma la lista de objetos a esperar.
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

        bool newJob = false;
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
            for (auto& w : watched) w->dirty = true;
            lastPoll = GetTickCount();
            reload = true;
        } else if (rc > WAIT_OBJECT_0 + 1 && rc < WAIT_OBJECT_0 + handles.size()) {
            rearm(rc - WAIT_OBJECT_0);
            // Agrupa las notificaciones que lleguen en ráfaga, salvo si hay que
            // retener un trabajo nuevo: ahí cada milisegundo cuenta.
            for (int burst = 0; burst < 64 && !(newJob && autoHold_); ++burst) {
                DWORD more = WaitForMultipleObjects(static_cast<DWORD>(handles.size() - 2), handles.data() + 2,
                                                    FALSE, kDebounceMs);
                if (more == WAIT_TIMEOUT || more == WAIT_FAILED) break;
                rearm(more - WAIT_OBJECT_0 + 2);
            }
        } else if (rc == WAIT_FAILED) {
            util::LogError(L"WaitForMultipleObjects falló: %lu", GetLastError());
            Sleep(interval);
            for (auto& w : watched) w->dirty = true;
            lastPoll = GetTickCount();
        }
    }

    if (serverChange != INVALID_HANDLE_VALUE) FindClosePrinterChangeNotification(serverChange);
    util::LogInfo(L"Monitor detenido para '%ls'", server_.empty() ? L"(local)" : server_.c_str());
}

}  // namespace cola::core
