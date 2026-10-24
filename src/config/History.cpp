#include "config/History.h"

#include <windows.h>

#include "config/Json.h"
#include "util/Format.h"
#include "util/Log.h"

namespace cola::config {

const wchar_t* JobOutcomeToString(JobOutcome outcome) {
    switch (outcome) {
        case JobOutcome::Cancelled: return L"cancelled";
        case JobOutcome::Failed: return L"failed";
        case JobOutcome::Removed: return L"removed";
        default: return L"printed";
    }
}

JobOutcome JobOutcomeFromString(const std::wstring& text) {
    if (text == L"cancelled") return JobOutcome::Cancelled;
    if (text == L"failed") return JobOutcome::Failed;
    if (text == L"removed") return JobOutcome::Removed;
    return JobOutcome::Printed;
}

std::uint32_t HistoryEntry::PrintPagesCount() const {
    if (printPages.empty()) return pages;  // registro de una versión anterior
    std::uint64_t value = 0;
    for (wchar_t c : printPages) {
        if (c < L'0' || c > L'9') return 0;  // "—": no determinable
        value = value * 10 + static_cast<std::uint64_t>(c - L'0');
        if (value > 0xFFFFFFFFull) return 0;
    }
    return static_cast<std::uint32_t>(value);
}

History::History(std::wstring path, size_t limit) : path_(std::move(path)), limit_(limit) {}

bool History::Load() {
    entries_.clear();
    protectFile_ = false;
    JsonValue json;
    if (path_.empty()) return false;
    std::wstring error;
    // Un bloqueo momentáneo (antivirus, otra copia de la app reemplazándolo) no
    // es un archivo dañado: se reintenta antes de copiarlo aparte.
    bool loaded = LoadJsonFile(path_, json, &error);
    for (int attempt = 0; !loaded && attempt < 2 && GetFileAttributesW(path_.c_str()) != INVALID_FILE_ATTRIBUTES;
         ++attempt) {
        Sleep(200);
        json = JsonValue();
        loaded = LoadJsonFile(path_, json, &error);
    }
    if (!loaded) {
        if (GetFileAttributesW(path_.c_str()) != INVALID_FILE_ATTRIBUTES) {
            // Existe pero no se pudo leer (dañado, bloqueado...): se conserva una copia.
            if (CopyFileW(path_.c_str(), BackupPath().c_str(), FALSE)) {
                util::LogError(L"No se pudo leer el historial %ls (%ls); copia en %ls", path_.c_str(), error.c_str(),
                               BackupPath().c_str());
            } else {
                protectFile_ = true;
                util::LogError(L"No se pudo leer el historial %ls (%ls) ni copiarlo: no se reemplazará",
                               path_.c_str(), error.c_str());
            }
        }
        return false;
    }
    for (const auto& item : json[L"entries"].AsArray()) {
        HistoryEntry e;
        e.timeUnix = item[L"time"].AsInt64();
        e.server = item[L"server"].AsString();
        e.printer = item[L"printer"].AsString();
        e.document = item[L"document"].AsString();
        e.user = item[L"user"].AsString();
        e.machine = item[L"machine"].AsString();
        e.pages = static_cast<std::uint32_t>(item[L"pages"].AsInt64());
        e.sizeBytes = static_cast<std::uint64_t>(item[L"size"].AsInt64());
        e.outcome = JobOutcomeFromString(item[L"outcome"].AsString());
        e.position = static_cast<std::uint32_t>(item[L"position"].AsInt64());
        e.printPages = item[L"printPages"].AsString();
        e.paper = item[L"paper"].AsString();
        e.orientation = item[L"orientation"].AsString();
        e.duplex = item[L"duplex"].AsString();
        e.source = item[L"source"].AsString();
        e.submittedUnix = item[L"submitted"].AsInt64();
        entries_.push_back(std::move(e));
    }
    if (entries_.size() > limit_) entries_.resize(limit_);
    dirty_ = false;
    return true;
}

bool History::Save() {
    if (path_.empty() || protectFile_) return false;
    JsonValue json = JsonValue::MakeObject();
    json[L"version"] = 1;
    JsonValue list = JsonValue::MakeArray();
    for (const auto& e : entries_) {
        JsonValue item = JsonValue::MakeObject();
        item[L"time"] = static_cast<long long>(e.timeUnix);
        item[L"server"] = e.server;
        item[L"printer"] = e.printer;
        item[L"document"] = e.document;
        item[L"user"] = e.user;
        item[L"machine"] = e.machine;
        item[L"pages"] = static_cast<long long>(e.pages);
        item[L"size"] = static_cast<long long>(e.sizeBytes);
        item[L"outcome"] = JobOutcomeToString(e.outcome);
        item[L"position"] = static_cast<long long>(e.position);
        item[L"printPages"] = e.printPages;
        item[L"paper"] = e.paper;
        item[L"orientation"] = e.orientation;
        item[L"duplex"] = e.duplex;
        item[L"source"] = e.source;
        item[L"submitted"] = static_cast<long long>(e.submittedUnix);
        list.Push(std::move(item));
    }
    json[L"entries"] = list;
    bool ok = SaveJsonFile(path_, json);
    if (ok) dirty_ = false;
    return ok;
}

void History::Add(HistoryEntry entry) {
    entries_.insert(entries_.begin(), std::move(entry));
    if (entries_.size() > limit_) entries_.resize(limit_);
    dirty_ = true;
}

void History::Clear() {
    entries_.clear();
    dirty_ = true;
}

void History::SetLimit(size_t limit) {
    limit_ = limit;
    if (entries_.size() > limit_) {
        entries_.resize(limit_);
        dirty_ = true;
    }
}

HistoryStats History::ComputeStats(std::int64_t nowUnix) const {
    HistoryStats stats;
    const std::wstring today = util::FormatDateKey(util::FromUnixSeconds(nowUnix));
    const std::int64_t weekAgo = nowUnix - 7 * 24 * 3600;
    for (const auto& e : entries_) {
        if (e.outcome != JobOutcome::Printed) continue;
        const std::uint32_t pages = e.PrintPagesCount();
        std::wstring day = util::FormatDateKey(util::FromUnixSeconds(e.timeUnix));
        stats.pagesPerDay[day] += pages;
        if (day == today) {
            stats.pagesToday += pages;
            ++stats.jobsToday;
        }
        if (e.timeUnix > weekAgo) {
            stats.pagesWeek += pages;
            stats.pagesPerUser[e.user] += pages;
        }
    }
    return stats;
}

}  // namespace cola::config
