#include "core/PrintTypes.h"

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

const PrinterInfo* QueueSnapshot::FindPrinter(const std::wstring& name) const {
    for (const auto& p : printers)
        if (p.name == name) return &p;
    return nullptr;
}

}  // namespace cola::core
