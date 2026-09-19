#include "config/History.h"

#include "config/Json.h"
#include "util/Format.h"

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

History::History(std::wstring path, size_t limit) : path_(std::move(path)), limit_(limit) {}

bool History::Load() {
    entries_.clear();
    JsonValue json;
    if (path_.empty() || !LoadJsonFile(path_, json)) return false;
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
        entries_.push_back(std::move(e));
    }
    if (entries_.size() > limit_) entries_.resize(limit_);
    dirty_ = false;
    return true;
}

bool History::Save() {
    if (path_.empty()) return false;
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
        std::wstring day = util::FormatDateKey(util::FromUnixSeconds(e.timeUnix));
        stats.pagesPerDay[day] += e.pages;
        if (day == today) {
            stats.pagesToday += e.pages;
            ++stats.jobsToday;
        }
        if (e.timeUnix > weekAgo) {
            stats.pagesWeek += e.pages;
            stats.pagesPerUser[e.user] += e.pages;
        }
    }
    return stats;
}

}  // namespace cola::config
