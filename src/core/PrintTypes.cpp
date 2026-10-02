#include "core/PrintTypes.h"

#include <algorithm>

namespace cola::core {

std::wstring PrinterInfo::DisplayName() const {
    // "\\\\SERVIDOR\\Impresora" -> "Impresora"
    if (name.size() > 2 && name[0] == L'\\' && name[1] == L'\\') {
        size_t slash = name.find(L'\\', 2);
        if (slash != std::wstring::npos) return name.substr(slash + 1);
    }
    return name;
}

std::wstring JobInfo::Key() const { return printer + L"#" + std::to_wstring(id); }

std::vector<JobInfo> ReconcileJobLists(std::vector<JobInfo> server, const std::vector<JobInfo>& cached) {
    for (const auto& job : cached) {
        if (!(job.rawStatus & kJobStatusRenderingLocally)) continue;
        bool known = std::any_of(server.begin(), server.end(), [&](const JobInfo& j) { return j.id == job.id; });
        if (!known) server.push_back(job);
    }
    std::stable_sort(server.begin(), server.end(),
                     [](const JobInfo& a, const JobInfo& b) { return a.position < b.position; });
    return server;
}

const PrinterInfo* QueueSnapshot::FindPrinter(const std::wstring& name) const {
    for (const auto& p : printers)
        if (p.name == name) return &p;
    return nullptr;
}

}  // namespace cola::core
