#include "core/JobTracker.h"

#include "core/JobStatus.h"

namespace cola::core {

config::JobOutcome GuessOutcome(const JobInfo& last, std::uint32_t printerProblems, bool cancelledByUs) {
    using config::JobOutcome;
    if (cancelledByUs || last.state == JobState::Deleting) return JobOutcome::Cancelled;
    if (last.state == JobState::Printing || last.state == JobState::Printed || last.pagesPrinted > 0)
        return JobOutcome::Printed;
    if (last.state == JobState::Error) return JobOutcome::Failed;
    // Trabajo corto que se imprimió entre dos refrescos: era el primero de la
    // cola y la impresora estaba operativa.
    const std::uint32_t blocking = ProblemOffline | ProblemPaused | ProblemNotAvailable | ProblemError |
                                   ProblemPaperJam | ProblemPaperOut;
    if ((last.state == JobState::Queued || last.state == JobState::Spooling) && last.position <= 1 &&
        (printerProblems & blocking) == 0)
        return JobOutcome::Printed;
    return JobOutcome::Removed;
}

std::vector<JobEvent> JobTracker::Update(const QueueSnapshot& snapshot) {
    std::vector<JobEvent> events;
    if (snapshot.server != server_) {
        Reset();
        server_ = snapshot.server;
    }

    std::unordered_map<std::wstring, JobInfo> current;
    current.reserve(snapshot.jobs.size());
    for (const auto& job : snapshot.jobs) current.emplace(job.Key(), job);

    // Si el servidor no respondió, no interpretamos la lista vacía como "todo terminó".
    const bool reliable = snapshot.error.empty();

    if (initialized_ && reliable) {
        for (const auto& [key, job] : current) {
            auto it = last_.find(key);
            if (it == last_.end()) {
                events.push_back({JobEvent::Kind::Added, job, {}});
                if (IsAttentionState(job.state)) events.push_back({JobEvent::Kind::EnteredError, job, {}});
            } else if (job.state == JobState::Error && it->second.state != JobState::Error) {
                events.push_back({JobEvent::Kind::EnteredError, job, {}});
            }
        }
        for (const auto& [key, job] : last_) {
            if (current.count(key)) continue;
            // Solo cuenta como terminado si su impresora sigue existiendo y respondiendo.
            const PrinterInfo* printer = snapshot.FindPrinter(job.printer);
            if (!printer || (printer->problems & ProblemNotAvailable)) continue;
            auto problems = printerProblems_.count(job.printer) ? printerProblems_[job.printer] : 0u;
            bool ours = cancelledByUs_.erase(key) > 0;
            events.push_back({JobEvent::Kind::Finished, job, GuessOutcome(job, problems, ours)});
        }
    }

    if (reliable) {
        last_ = std::move(current);
        printerProblems_.clear();
        for (const auto& p : snapshot.printers) printerProblems_[p.name] = p.problems;
        initialized_ = true;
    }
    return events;
}

void JobTracker::Reset() {
    last_.clear();
    printerProblems_.clear();
    cancelledByUs_.clear();
    initialized_ = false;
}

}  // namespace cola::core
