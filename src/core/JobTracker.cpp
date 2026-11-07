// Implementación del seguimiento de trabajos entre instantáneas (ver
// JobTracker.h) y de la heurística que clasifica el resultado de un trabajo
// que salió de la cola (D13, D47, D52). El spooler no dice "se imprimió" o
// "se canceló" cuando un trabajo desaparece: se deduce de lo último visto.
#include "core/JobTracker.h"

#include <algorithm>

#include "core/JobStatus.h"

namespace cola::core {

config::JobOutcome GuessOutcome(const JobInfo& last, std::uint32_t printerProblems, bool cancelledByUs,
                                bool releasedByUs) {
    using config::JobOutcome;
    // Cancelado: lo canceló esta app o se lo vio borrándose (otro operador o
    // el propio usuario desde su PC).
    if (cancelledByUs || last.state == JobState::Deleting) return JobOutcome::Cancelled;
    // Se lo vio imprimiéndose, impreso o con páginas ya impresas.
    if (last.state == JobState::Printing || last.state == JobState::Printed || last.pagesPrinted > 0)
        return JobOutcome::Printed;
    // Lo último visto fue un error y desapareció: se considera fallido.
    if (last.state == JobState::Error) return JobOutcome::Failed;
    // El operador lo liberó y salió de la cola: se imprimió (aunque lo último
    // que se vio fuera "en pausa" con la impresora en pausa).
    if (releasedByUs) return JobOutcome::Printed;
    // Trabajo corto que se imprimió entre dos refrescos: era el primero de la
    // cola y la impresora estaba operativa.
    const std::uint32_t blocking = ProblemOffline | ProblemPaused | ProblemNotAvailable | ProblemError |
                                   ProblemPaperJam | ProblemPaperOut;
    if ((last.state == JobState::Queued || last.state == JobState::Spooling) && last.position <= 1 &&
        (printerProblems & blocking) == 0)
        return JobOutcome::Printed;
    // Cualquier otro caso (en pausa, no era el primero, impresora detenida):
    // alguien lo quitó de la cola sin que se pueda saber más.
    return JobOutcome::Removed;
}

std::vector<JobEvent> JobTracker::Update(const QueueSnapshot& snapshot) {
    std::vector<JobEvent> events;
    // Cambio de servidor: las claves de uno no tienen relación con las del
    // otro; se empieza de cero (la primera instantánea del nuevo es la base).
    if (snapshot.server != server_) {
        Reset();
        server_ = snapshot.server;
    }

    // 1) Trabajos actuales por clave, sin las copias locales de CSR.
    std::unordered_map<std::wstring, JobInfo> current;
    current.reserve(snapshot.jobs.size());
    for (const auto& job : snapshot.jobs) {
        // La copia local de Client Side Rendering no es otro trabajo: es el
        // mismo que esta PC todavía envía al servidor (que aparece aparte).
        // Contarla duplicaba el trabajo en el historial como "retirado".
        if (job.rawStatus & kJobStatusRenderingLocally) continue;
        JobInfo& kept = current.emplace(job.Key(), job).first->second;
        // Al imprimirse el spooler puede informar menos bytes o páginas (0 al
        // final): el historial describe el trabajo completo, no su avance.
        if (auto it = last_.find(kept.Key()); it != last_.end()) {
            kept.sizeBytes = std::max(kept.sizeBytes, it->second.sizeBytes);
            kept.totalPages = std::max(kept.totalPages, it->second.totalPages);
        }
    }

    // Si el servidor no respondió, no interpretamos la lista vacía como "todo terminó".
    const bool reliable = snapshot.error.empty();

    // 2) Comparación con la instantánea anterior (solo si hay una base y la
    //    actual es fiable; la primera vez solo se memoriza, sin avisar de
    //    "nuevos" todos los trabajos que ya estaban).
    if (initialized_ && reliable) {
        // 2a) Nuevos y que entran en error.
        for (const auto& [key, job] : current) {
            auto it = last_.find(key);
            if (it == last_.end()) {
                // Nuevo; si ya llega con problemas se avisa también el error.
                events.push_back({JobEvent::Kind::Added, job, {}});
                if (IsAttentionState(job.state)) events.push_back({JobEvent::Kind::EnteredError, job, {}});
            } else if (job.state == JobState::Error && it->second.state != JobState::Error) {
                // Conocido que pasa a error: se avisa solo en la transición.
                events.push_back({JobEvent::Kind::EnteredError, job, {}});
            }
        }
        // 2b) Los que estaban y ya no están: terminados (con su resultado).
        for (const auto& [key, job] : last_) {
            if (current.count(key)) continue;
            // Solo cuenta como terminado si su impresora sigue existiendo y respondiendo.
            const PrinterInfo* printer = snapshot.FindPrinter(job.printer);
            // La impresora ya no existe (se quitó la cola o cambió la vista):
            // el trabajo se olvida sin registrarlo, porque no se sabe qué pasó.
            if (!printer) continue;
            if (printer->problems & ProblemNotAvailable) {
                // Sin respuesta de su impresora (corte de red...) no se sabe si
                // terminó: se sigue recordando hasta que vuelva. Si no, un trabajo
                // impreso durante el corte no llegaba nunca al historial y los
                // demás se avisaban otra vez como nuevos al volver.
                current.emplace(key, job);
                continue;
            }
            // Se juzga con los problemas que tenía la impresora en la
            // instantánea anterior (cuando se vio el trabajo por última vez).
            auto problems = printerProblems_.count(job.printer) ? printerProblems_[job.printer] : 0u;
            // erase devuelve si estaba: consume la marca de una vez.
            const bool ours = cancelledByUs_.erase(key) > 0;
            const bool released = releasedByUs_.erase(key) > 0;
            events.push_back({JobEvent::Kind::Finished, job, GuessOutcome(job, problems, ours, released)});
        }
    }

    // 3) La instantánea fiable pasa a ser la base de la próxima comparación.
    if (reliable) {
        last_ = std::move(current);
        // De una impresora que no responde se conserva el estado de antes del
        // corte: al volver, GuessOutcome decide con él (si no, un trabajo que
        // estaba primero en una cola operativa quedaba "Retirado").
        std::unordered_map<std::wstring, std::uint32_t> problems;
        for (const auto& p : snapshot.printers) {
            auto old = printerProblems_.find(p.name);
            problems[p.name] = (p.problems & ProblemNotAvailable) && old != printerProblems_.end() ? old->second
                                                                                                  : p.problems;
        }
        printerProblems_ = std::move(problems);
        initialized_ = true;
    }
    return events;
}

void JobTracker::Reset() {
    last_.clear();
    printerProblems_.clear();
    cancelledByUs_.clear();
    releasedByUs_.clear();
    initialized_ = false;
}

}  // namespace cola::core
