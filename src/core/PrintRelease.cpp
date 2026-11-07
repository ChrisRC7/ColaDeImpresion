// Implementación de "imprimir y volver a pausar" (D35): decide, con cada
// instantánea, qué impresoras ya terminaron sus trabajos liberados. Ver los
// criterios en PrintRelease.h.
#include "core/PrintRelease.h"

#include <winspool.h>

namespace cola::core {

namespace {
// Flags con que el spooler indica que el trabajo ya se envió o se va: a
// partir de ahí la impresora no necesita seguir abierta por él.
constexpr std::uint32_t kSentFlags = JOB_STATUS_COMPLETE | JOB_STATUS_PRINTED | JOB_STATUS_DELETING | JOB_STATUS_DELETED;

// true si el trabajo ya terminó de enviarse o se está borrando (por flags
// crudos o por el estado simplificado).
bool AlreadyGone(const JobInfo& job) {
    return (job.rawStatus & kSentFlags) != 0 || job.state == JobState::Printed || job.state == JobState::Deleting;
}
}  // namespace

std::size_t PrintReleaseTracker::Start(const std::vector<JobInfo>& jobs, std::uint64_t nowMs) {
    std::size_t added = 0;
    for (const auto& job : jobs) {
        // Uno ya enviado o cancelándose no mantendría la impresora abierta.
        if (AlreadyGone(job)) continue;
        Tracked t;
        t.startedMs = nowMs;
        pending_[job.printer][job.id] = t;  // si ya estaba, se reinicia su plazo
        ++added;
    }
    return added;
}

std::vector<std::wstring> PrintReleaseTracker::Update(const QueueSnapshot& snapshot, std::uint64_t nowMs) {
    std::vector<std::wstring> done;
    // Con error global la lista puede estar vacía o parcial: no se decide nada.
    if (!snapshot.error.empty()) return done;

    // Se recorre cada impresora con trabajos liberados pendientes.
    for (auto it = pending_.begin(); it != pending_.end();) {
        const std::wstring& printer = it->first;
        const PrinterInfo* info = snapshot.FindPrinter(printer);
        // Sin la impresora o sin su lista de trabajos no se puede saber nada.
        if (!info || (info->problems & ProblemNotAvailable)) {
            ++it;
            continue;
        }
        // Trabajos de esta impresora presentes en la instantánea, por id.
        std::map<std::uint32_t, const JobInfo*> present;
        for (const auto& job : snapshot.jobs)
            if (job.printer == printer) present[job.id] = &job;
        // Si el spooler cuenta más trabajos de los enumerados, un ausente puede seguir en la cola.
        const bool complete = info->jobCount <= present.size();

        auto& jobs = it->second;
        for (auto j = jobs.begin(); j != jobs.end();) {
            auto found = present.find(j->first);
            bool finished = false;
            if (found == present.end()) {
                // Ya no está: terminó, salvo que la lista esté incompleta.
                finished = complete;
            } else if (AlreadyGone(*found->second)) {
                // El spooler lo marca enviado/impreso/borrándose.
                finished = true;
            } else if (found->second->state == JobState::Paused) {
                // Pausado de nuevo por alguien, o la reanudación nunca llegó.
                finished = j->second.seenRunning || nowMs - j->second.startedMs >= kResumeGraceMs;
            } else {
                // En cola o imprimiéndose sin pausa: la liberación funcionó.
                j->second.seenRunning = true;
            }
            j = finished ? jobs.erase(j) : std::next(j);
        }
        // Sin trabajos pendientes: hay que volver a pausar esta impresora.
        if (jobs.empty()) {
            done.push_back(printer);
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
    return done;
}

std::size_t PrintReleaseTracker::PendingJobs() const {
    std::size_t n = 0;
    for (const auto& [printer, jobs] : pending_) n += jobs.size();
    return n;
}

}  // namespace cola::core
