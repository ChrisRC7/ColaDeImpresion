#pragma once
// "Imprimir y volver a pausar": recuerda los trabajos que un operador liberó
// junto con su impresora y avisa cuándo volver a pausar cada impresora (cuando
// todos sus trabajos liberados ya se enviaron). Lógica pura: la usa
// MainWindow con cada instantánea y se prueba en tests/.
//
// Un trabajo liberado se da por terminado cuando:
//  - el spooler lo marca enviado / impreso / borrándose (JOB_STATUS_COMPLETE,
//    _PRINTED, _DELETING, _DELETED), o
//  - ya no está en la cola (solo si la lista de esa impresora está completa:
//    impresora disponible y sin trabajos que el spooler cuente y no se enumeraron), o
//  - alguien lo volvió a pausar después de haberse visto corriendo, o sigue en
//    pausa pasado kResumeGraceMs (la reanudación falló, p. ej. por permisos).
// Mientras una instantánea traiga un error global no se decide nada.
//
// Contexto: D35 (Ctrl+Enter / F11). La impresora se vuelve a pausar SIEMPRE al
// terminar, aunque antes no estuviera en pausa: con la retención y la pausa de
// inicio (D17/D18) lo normal es que todo esté en pausa. Los criterios evitan
// dos errores opuestos: pausar la impresora antes de tiempo (una instantánea
// incompleta haría "desaparecer" trabajos) y dejarla abierta para siempre
// (reanudación fallida o trabajo re-pausado). Esta clase solo decide; la
// pausa la ejecuta MainWindow por medio de JobActionRunner.
//
// Hilos: no es thread-safe; la usa solo el hilo de la UI.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/PrintTypes.h"

namespace cola::core {

// Seguimiento de los trabajos liberados con "imprimir y volver a pausar",
// agrupados por impresora.
class PrintReleaseTracker {
public:
    // Tiempo máximo para ver corriendo un trabajo recién liberado.
    // Si pasado este plazo sigue en pausa, se asume que la reanudación falló y
    // la impresora se vuelve a pausar (no se deja abierta indefinidamente).
    static constexpr std::uint64_t kResumeGraceMs = 15000;

    // Registra trabajos liberados (los ya impresos o cancelándose se ignoran).
    // nowMs: reloj monótono (GetTickCount64). Devuelve cuántos se registraron.
    // Se llama desde la UI justo al liberar, con las filas seleccionadas (ya
    // sin las copias locales de CSR, D50).
    std::size_t Start(const std::vector<JobInfo>& jobs, std::uint64_t nowMs);

    // Revisa una instantánea y devuelve las impresoras cuyos trabajos liberados
    // ya terminaron todos (se dejan de seguir: hay que volver a pausarlas).
    // Devuelve PrinterInfo::name de cada una; vacío si nada terminó o si la
    // instantánea trae un error global.
    std::vector<std::wstring> Update(const QueueSnapshot& snapshot, std::uint64_t nowMs);

    // true mientras quede algún trabajo liberado sin terminar.
    bool Active() const { return !pending_.empty(); }
    // Cantidad total de trabajos liberados que se siguen (todas las impresoras).
    std::size_t PendingJobs() const;
    // Deja de seguir todo (p. ej. al cambiar de servidor).
    void Reset() { pending_.clear(); }

private:
    // Estado de un trabajo liberado.
    struct Tracked {
        std::uint64_t startedMs = 0;  // cuándo se liberó (GetTickCount64)
        bool seenRunning = false;  // ya se lo vio sin pausa después de liberarlo
    };
    std::map<std::wstring, std::map<std::uint32_t, Tracked>> pending_;  // impresora -> id -> estado
};

}  // namespace cola::core
