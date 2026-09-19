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
    JobInfo job;  // último estado conocido del trabajo
    config::JobOutcome outcome = config::JobOutcome::Printed;  // solo para Finished
};

class JobTracker {
public:
    // Devuelve los eventos entre la instantánea anterior y la nueva.
    // La primera llamada (o tras Reset) no genera eventos "Added".
    std::vector<JobEvent> Update(const QueueSnapshot& snapshot);

    // La app marca los trabajos que ella misma canceló para clasificarlos bien.
    void MarkCancelledByUser(const std::wstring& jobKey) { cancelledByUs_.insert(jobKey); }

    void Reset();

private:
    std::unordered_map<std::wstring, JobInfo> last_;
    std::unordered_map<std::wstring, std::uint32_t> printerProblems_;
    std::set<std::wstring> cancelledByUs_;
    bool initialized_ = false;
    std::wstring server_;
};

// Heurística de resultado para un trabajo que desapareció de la cola.
config::JobOutcome GuessOutcome(const JobInfo& last, std::uint32_t printerProblems, bool cancelledByUs);

}  // namespace cola::core
