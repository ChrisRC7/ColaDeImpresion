#pragma once
// Compara instantáneas consecutivas de la cola y genera eventos:
// trabajo nuevo, trabajo que entra en error, trabajo que sale de la cola
// (con una estimación de si se imprimió o se canceló). Es lógica pura y se
// prueba en tests/.

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/History.h"
#include "core/PrintTypes.h"

namespace cola::core {

struct JobEvent {
    enum class Kind { Added, EnteredError, Finished };
    Kind kind = Kind::Added;
    // Último estado conocido del trabajo, con el mayor tamaño y la mayor
    // cantidad de páginas que tuvo (al imprimirse el spooler puede bajarlos).
    JobInfo job;
    config::JobOutcome outcome = config::JobOutcome::Printed;  // solo para Finished
};

class JobTracker {
public:
    // Devuelve los eventos entre la instantánea anterior y la nueva.
    // La primera llamada (o tras Reset) no genera eventos "Added".
    std::vector<JobEvent> Update(const QueueSnapshot& snapshot);

    // La app marca los trabajos que ella misma canceló para clasificarlos bien.
    void MarkCancelledByUser(const std::wstring& jobKey) { cancelledByUs_.insert(jobKey); }
    // ...y los que liberó (reanudar, reiniciar, imprimir y volver a pausar):
    // un trabajo corto se imprime y sale de la cola antes del siguiente
    // refresco, así que lo último que se vio de él es "en pausa" con la
    // impresora en pausa, y sin esta marca quedaba como "retirado".
    void MarkReleasedByUser(const std::wstring& jobKey) { releasedByUs_.insert(jobKey); }
    void ForgetReleasedByUser(const std::wstring& jobKey) { releasedByUs_.erase(jobKey); }

    void Reset();

private:
    std::unordered_map<std::wstring, JobInfo> last_;
    std::unordered_map<std::wstring, std::uint32_t> printerProblems_;
    std::set<std::wstring> cancelledByUs_;
    std::set<std::wstring> releasedByUs_;
    bool initialized_ = false;
    std::wstring server_;
};

// Heurística de resultado para un trabajo que desapareció de la cola.
config::JobOutcome GuessOutcome(const JobInfo& last, std::uint32_t printerProblems, bool cancelledByUs,
                                bool releasedByUs = false);

}  // namespace cola::core
