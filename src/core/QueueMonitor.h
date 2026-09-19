#pragma once
// Monitor de la cola en tiempo real.
//
// Corre en su propio hilo. Usa FindFirstPrinterChangeNotification sobre cada
// impresora del servidor para enterarse al instante de trabajos nuevos o
// cambios de estado, y además hace un sondeo periódico de respaldo (por si las
// notificaciones remotas están bloqueadas por el firewall). Cada vez que algo
// cambia publica una QueueSnapshot nueva mediante el callback.

#include <windows.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/AutoHold.h"
#include "core/PrintTypes.h"

namespace cola::core {

class QueueMonitor {
public:
    // Se invoca desde el hilo del monitor: el receptor debe reenviar al hilo UI
    // (p. ej. con PostMessage) y no bloquear.
    using SnapshotCallback = std::function<void(SnapshotPtr)>;

    explicit QueueMonitor(SnapshotCallback callback);
    ~QueueMonitor();

    QueueMonitor(const QueueMonitor&) = delete;
    QueueMonitor& operator=(const QueueMonitor&) = delete;

    // Empieza a vigilar un servidor ("" = local). Si ya vigilaba otro, lo detiene.
    void Start(const std::wstring& server, int pollIntervalMs);
    void Stop();

    // Fuerza un refresco completo lo antes posible (p. ej. tras una acción).
    void RequestRefresh();
    void SetPollInterval(int ms) { pollMs_ = ms; }

    // Retención automática: pausa cada trabajo nuevo apenas llega (ver AutoHold.h).
    void SetAutoHold(bool enabled) { autoHold_ = enabled; }

    const std::wstring& server() const { return server_; }

private:
    struct Watched;  // impresora vigilada (handle + notificación + últimos datos)

    void Run();
    bool ReloadPrinters(std::vector<std::unique_ptr<Watched>>& watched, std::wstring& error);
    void RefreshPrinter(Watched& w);
    void ApplyAutoHold(Watched& w);
    void Publish(const std::vector<std::unique_ptr<Watched>>& watched, const std::wstring& error, bool notifying);

    SnapshotCallback callback_;
    std::wstring server_;
    std::atomic<int> pollMs_{3000};
    std::atomic<bool> autoHold_{false};
    AutoHoldTracker hold_;           // solo se usa en el hilo del monitor
    std::uint32_t holdFailures_ = 0;
    unsigned long holdError_ = 0;
    std::thread thread_;
    HANDLE stopEvent_ = nullptr;
    HANDLE refreshEvent_ = nullptr;
    std::uint64_t sequence_ = 0;
};

}  // namespace cola::core
