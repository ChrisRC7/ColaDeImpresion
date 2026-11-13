#pragma once
// Monitor de la cola en tiempo real.
//
// Corre en su propio hilo. Usa FindFirstPrinterChangeNotification sobre cada
// impresora del servidor para enterarse al instante de trabajos nuevos o
// cambios de estado, y además hace un sondeo periódico de respaldo (por si las
// notificaciones remotas están bloqueadas por el firewall). Cada vez que algo
// cambia publica una QueueSnapshot nueva mediante el callback.
//
// Decisiones: D5 (notificaciones + sondeo de respaldo con vencimiento propio),
// D17/D18 (retención automática de trabajos nuevos desde este mismo hilo, para
// ganar la carrera con el spooler), D21 (trabajos que el spooler cuenta y no se
// enumeran), D28/D51 (lista consultada con el nombre DNS completo para saltarse
// la caché de Client Side Rendering), D34 (bandejas y papeles del controlador,
// una vez por cola).
//
// Hilos: Start/Stop/RequestRefresh y los Set* se llaman desde la UI; Run y todo
// lo privado corre en el hilo propio. Los ajustes compartidos son atómicos y la
// comunicación hacia el hilo va por eventos Win32 (stopEvent_, refreshEvent_).

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

// Vigila todas las impresoras de un servidor y publica instantáneas.
class QueueMonitor {
public:
    // Se invoca desde el hilo del monitor: el receptor debe reenviar al hilo UI
    // (p. ej. con PostMessage) y no bloquear.
    using SnapshotCallback = std::function<void(SnapshotPtr)>;

    // Crea los eventos de parada y refresco; no arranca el hilo (ver Start).
    explicit QueueMonitor(SnapshotCallback callback);
    // Detiene el hilo (espera a que termine) y cierra los eventos.
    ~QueueMonitor();

    QueueMonitor(const QueueMonitor&) = delete;
    QueueMonitor& operator=(const QueueMonitor&) = delete;

    // Empieza a vigilar un servidor ("" = local). Si ya vigilaba otro, lo detiene.
    // server se normaliza ("srv" -> "\\\\srv"); pollIntervalMs es el sondeo de
    // respaldo (mínimo efectivo 500 ms).
    void Start(const std::wstring& server, int pollIntervalMs);
    // Pide al hilo que termine y espera (join). Puede tardar lo que dure la
    // llamada al spooler en curso. Sin efecto si no estaba corriendo.
    void Stop();

    // Fuerza un refresco completo lo antes posible (p. ej. tras una acción).
    // También vuelve a enumerar las impresoras del servidor.
    void RequestRefresh();
    // Cambia el intervalo de sondeo; se aplica en la próxima espera.
    void SetPollInterval(int ms) { pollMs_ = ms; }

    // Retención automática: pausa cada trabajo nuevo apenas llega (ver AutoHold.h).
    void SetAutoHold(bool enabled) { autoHold_ = enabled; }
    // Al (re)conectar, retener también los trabajos que ya estaban en la cola.
    // (Solo afecta a las impresoras que se vean por primera vez después.)
    void SetHoldExistingOnStart(bool enabled) { holdExisting_ = enabled; }

    // Servidor vigilado (normalizado). Leer solo desde el hilo que llama a Start.
    const std::wstring& server() const { return server_; }
    // true si el hilo del monitor está en marcha (entre Start y Stop). Igual
    // que server(), solo desde el hilo que llama a Start/Stop.
    bool IsRunning() const { return thread_.joinable(); }

private:
    struct Watched;  // impresora vigilada (handle + notificación + últimos datos)

    // Bucle del hilo: refresca, publica y espera notificaciones/sondeo/parada.
    void Run();
    // Vuelve a enumerar las impresoras del servidor conservando el estado de
    // las que siguen (handles, notificaciones). false + error si falla.
    bool ReloadPrinters(std::vector<std::unique_ptr<Watched>>& watched, std::wstring& error);
    // Actualiza estado y trabajos de una impresora y aplica la retención.
    void RefreshPrinter(Watched& w);
    // Pausa los trabajos que AutoHoldTracker decide retener y anota heldByApp.
    // partialList: la lista de w puede estar incompleta (F6); ver AutoHoldTracker::Process.
    void ApplyAutoHold(Watched& w, bool partialList = false);
    // Arma una QueueSnapshot con todas las impresoras y la entrega al callback.
    void Publish(const std::vector<std::unique_ptr<Watched>>& watched, const std::wstring& error, bool notifying);

    SnapshotCallback callback_;  // receptor de las instantáneas (desde este hilo)
    std::wstring server_;        // servidor normalizado ("" = local)
    std::atomic<int> pollMs_{3000};       // intervalo del sondeo de respaldo (D5)
    std::atomic<bool> autoHold_{false};   // retener trabajos nuevos (D17)
    std::atomic<bool> holdExisting_{false};  // retener también los ya encolados al conectar (D18)
    AutoHoldTracker hold_;           // solo se usa en el hilo del monitor
    std::uint32_t holdFailures_ = 0;  // trabajos que no se pudieron pausar (acumulado en la sesión)
    unsigned long holdError_ = 0;     // último error Win32 al retener (para la barra de estado)
    std::thread thread_;              // hilo de Run (joinable mientras vigila)
    HANDLE stopEvent_ = nullptr;      // evento manual: pide terminar el hilo
    HANDLE refreshEvent_ = nullptr;   // evento automático: pide un refresco completo
    std::uint64_t sequence_ = 0;      // número de la última instantánea publicada
};

}  // namespace cola::core
