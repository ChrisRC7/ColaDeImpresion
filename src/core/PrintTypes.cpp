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

std::vector<JobInfo> MergeJobLists(std::vector<JobInfo> cached, const std::vector<JobInfo>& direct) {
    for (const auto& job : direct) {
        bool known = std::any_of(cached.begin(), cached.end(), [&](const JobInfo& j) { return j.id == job.id; });
        if (!known) cached.push_back(job);
    }
    std::stable_sort(cached.begin(), cached.end(),
                     [](const JobInfo& a, const JobInfo& b) { return a.position < b.position; });
    return cached;
}

const PrinterInfo* QueueSnapshot::FindPrinter(const std::wstring& name) const {
    for (const auto& p : printers)
        if (p.name == name) return &p;
    return nullptr;
}

}  // namespace cola::core
