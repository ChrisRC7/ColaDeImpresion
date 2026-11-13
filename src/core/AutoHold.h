#pragma once
// Retención automática de trabajos: decide qué trabajos recién llegados hay
// que pausar y recuerda cuáles pausó la app ("retenidos"). Lógica pura, la usa
// QueueMonitor en su hilo y se prueba en tests/.
//
// Reglas:
//  - Se retienen los trabajos NUEVOS. La primera vez que se ve una impresora
//    (al iniciar o al conectar) sus trabajos existentes se retienen solo si se
//    pide "holdExisting" (opción "al iniciar, pausar todo"); si no, solo se registran.
//  - Cada trabajo se evalúa una sola vez: si un operador lo reanuda, no se
//    vuelve a pausar. Única excepción: si la pausa falló por un error
//    pasajero, se reintenta en los refrescos siguientes (RetryLater, M6).
//  - Se retienen aunque ya hayan empezado a imprimirse (se ganó la carrera por
//    poco); no se tocan los que están pausados, cancelándose o impresos.
//  - Nunca se retiene un trabajo que esta PC todavía procesa (Client Side
//    Rendering, JOB_STATUS_RENDERING_LOCALLY): es la copia local, en EMF, que el
//    spooler de esta PC está convirtiendo y enviando al servidor (pesa mucho
//    más que el trabajo real: 190 MB contra 71 MB con un PDF de 50 páginas).
//    Pausarlo a mitad del envío hacía que la impresora recibiera datos
//    incompletos (hojas en blanco y la impresora se reiniciaba). Se retiene el
//    trabajo del servidor, que aparece apenas empieza a llegar.
//
// Contexto: D17 (retención automática, pedido del usuario: nada se imprime sin
// que un operador lo libere) y D18 (al iniciar/conectar se pausa todo). Esta
// clase solo DECIDE; la pausa real (SetJob con JOB_CONTROL_PAUSE) la hace el
// QueueMonitor y, si funciona, llama a MarkHeld. Limitación conocida: solo
// funciona con la app abierta y un trabajo muy pequeño puede empezar a
// imprimirse antes de que se vea (se pausa igual).
//
// Hilos: no es thread-safe; la usa únicamente el hilo del QueueMonitor.

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/PrintTypes.h"

namespace cola::core {

// Memoria de la retención automática: qué trabajos ya se evaluaron por
// impresora y cuáles quedaron pausados por la app (para mostrarlos como
// "Retenido").
class AutoHoldTracker {
public:
    // Registra los trabajos de una impresora y devuelve los ids a pausar.
    //  printer:       PrinterInfo::name de la cola (clave de seen_).
    //  jobs:          todos los trabajos actuales de ESA impresora.
    //  enabled:       retener los trabajos que llegan después de la primera vez.
    //  holdExisting:  la primera vez que se ve la impresora (al iniciar/conectar),
    //                 retener también los trabajos que ya estaban en la cola.
    // Efectos: actualiza seen_ con los ids actuales y limpia de held_ los
    // trabajos liberados por un operador o que ya salieron de la cola.
    // Devuelve los ids que el llamador debe pausar (y luego MarkHeld si pudo).
    //  partialList:   la lista puede no traer todos los trabajos (caché del
    //                 cliente con la consulta directa caída, F6): los que no
    //                 aparecen NO se olvidan, así al volver la lista completa
    //                 no se toman por nuevos ni se vuelven a pausar.
    std::vector<std::uint32_t> Process(const std::wstring& printer, const std::vector<JobInfo>& jobs, bool enabled,
                                       bool holdExisting = false, bool partialList = false);

    // La pausa de un trabajo devuelto por Process falló por un error pasajero
    // (M6). Devuelve true si se va a reintentar: el trabajo deja de contarse
    // como evaluado y Process lo devolverá otra vez en el próximo refresco
    // (si la retención sigue activa y todavía conviene pausarlo). false si ya
    // se agotaron los kMaxHoldAttempts intentos: no se vuelve a probar.
    bool RetryLater(const std::wstring& printer, std::uint32_t id);
    // Intentos de pausa por trabajo (el primero más los reintentos).
    static constexpr int kMaxHoldAttempts = 3;

    // Anota que la app pausó el trabajo (clave JobInfo::Key) con éxito.
    void MarkHeld(const std::wstring& jobKey) { held_.insert(jobKey); }
    // true si el trabajo figura como retenido por la app.
    bool IsHeld(const std::wstring& jobKey) const { return held_.count(jobKey) > 0; }

    // Marca jobs[i].heldByApp según corresponda.
    // Solo es "retenido" si además sigue en pausa: uno que la app pausó y luego
    // alguien reanudó deja de mostrarse así aunque Process aún no lo limpiara.
    void Annotate(std::vector<JobInfo>& jobs) const;

    // Olvida todo (al cambiar de servidor): la próxima vez cada impresora
    // vuelve a ser "nueva" y sus trabajos existentes se tratan como tales.
    void Reset();

private:
    std::map<std::wstring, std::set<std::uint32_t>> seen_;  // impresora -> ids ya evaluados
    std::set<std::wstring> held_;                           // claves de trabajos que pausó la app
    // impresora -> (id -> pausas fallidas); se limpia cuando el trabajo sale (M6).
    std::map<std::wstring, std::map<std::uint32_t, int>> failedAttempts_;
};

// true si conviene pausar este trabajo: no es la copia local de CSR (D39) y no
// está ya pausado, borrándose ni impreso. Los que se imprimen sí se pausan
// (la carrera se perdió por poco y aún se puede frenar el resto).
bool ShouldHold(const JobInfo& job);

}  // namespace cola::core
