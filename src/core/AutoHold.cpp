#include "core/AutoHold.h"

namespace cola::core {

bool ShouldHold(const JobInfo& job) {
    switch (job.state) {
        case JobState::Paused:
        case JobState::Deleting:
        case JobState::Printed: return false;
        default: return true;
    }
}

std::vector<std::uint32_t> AutoHoldTracker::Process(const std::wstring& printer, const std::vector<JobInfo>& jobs,
                                                    bool enabled, bool holdExisting) {
    std::vector<std::uint32_t> toHold;
    auto found = seen_.find(printer);
    const bool firstTime = found == seen_.end();
    std::set<std::uint32_t> current;

    for (const auto& job : jobs) {
        current.insert(job.id);
        const std::wstring key = job.Key();
        // Un retenido que ya no está en pausa fue liberado por un operador.
        if (held_.count(key) && job.state != JobState::Paused) held_.erase(key);
        if (firstTime) {
            if (holdExisting && ShouldHold(job)) toHold.push_back(job.id);
            continue;
        }
        if (found->second.count(job.id)) continue;
        if (enabled && ShouldHold(job)) toHold.push_back(job.id);
    }

    // Olvida los trabajos que ya salieron de la cola.
    if (!firstTime) {
        for (std::uint32_t id : found->second) {
            if (!current.count(id)) {
                JobInfo gone;
                gone.printer = printer;
                gone.id = id;
                held_.erase(gone.Key());
            }
        }
    }
    seen_[printer] = std::move(current);
    return toHold;
}

void AutoHoldTracker::Annotate(std::vector<JobInfo>& jobs) const {
    for (auto& job : jobs) job.heldByApp = job.state == JobState::Paused && IsHeld(job.Key());
}

void AutoHoldTracker::Reset() {
    seen_.clear();
    held_.clear();
}

}  // namespace cola::core
