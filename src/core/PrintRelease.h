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

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/PrintTypes.h"

namespace cola::core {

class PrintReleaseTracker {
public:
    // Tiempo máximo para ver corriendo un trabajo recién liberado.
    static constexpr std::uint64_t kResumeGraceMs = 15000;

    // Registra trabajos liberados (los ya impresos o cancelándose se ignoran).
    // nowMs: reloj monótono (GetTickCount64). Devuelve cuántos se registraron.
    std::size_t Start(const std::vector<JobInfo>& jobs, std::uint64_t nowMs);

    // Revisa una instantánea y devuelve las impresoras cuyos trabajos liberados
    // ya terminaron todos (se dejan de seguir: hay que volver a pausarlas).
    std::vector<std::wstring> Update(const QueueSnapshot& snapshot, std::uint64_t nowMs);

    bool Active() const { return !pending_.empty(); }
    std::size_t PendingJobs() const;
    void Reset() { pending_.clear(); }

private:
    struct Tracked {
        std::uint64_t startedMs = 0;
        bool seenRunning = false;  // ya se lo vio sin pausa después de liberarlo
    };
    std::map<std::wstring, std::map<std::uint32_t, Tracked>> pending_;  // impresora -> id -> estado
};

}  // namespace cola::core
