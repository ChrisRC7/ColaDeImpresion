#include "preview/JobFacts.h"

#include <winspool.h>

#include "core/JobStatus.h"
#include "preview/PreviewBuilder.h"
#include "preview/SpoolFile.h"
#include "util/Strings.h"

namespace cola::preview {

JobFactsService::JobFactsService(ReadyCallback onReady) : onReady_(std::move(onReady)) {}

std::wstring JobFactsService::Key(const core::JobInfo& job) {
    const unsigned spooling = (job.rawStatus & JOB_STATUS_SPOOLING) ? 1u : 0u;
    // Si se puede leer forma parte de la clave: al volver a pausarlo se analiza.
    const unsigned readable = core::CanReadJobData(job.rawStatus) ? 1u : 0u;
    return util::Format(L"%ls#%u|%llu|%u|%u", job.printer.c_str(), job.id,
                        static_cast<unsigned long long>(job.sizeBytes), spooling, readable);
}

std::shared_ptr<const core::ContentFacts> JobFactsService::Find(const core::JobInfo& job) {
    std::lock_guard lock(mutex_);
    auto it = cache_.find(Key(job));
    return it == cache_.end() ? nullptr : it->second;
}

void JobFactsService::Request(const std::vector<core::JobInfo>& jobs, const std::wstring& server,
                              const std::wstring& overrideDir) {
    std::lock_guard lock(mutex_);
    // Olvida los trabajos que ya no están (o que cambiaron: su clave es otra).
    std::set<std::wstring> alive;
    for (const auto& job : jobs) alive.insert(Key(job));
    for (auto it = cache_.begin(); it != cache_.end();) it = alive.count(it->first) ? std::next(it) : cache_.erase(it);

    for (const auto& job : jobs) {
        // Recibiéndose (ReadPrinter esperaría) o sin pausar (la lectura le quitaría
        // datos a la impresora): se analiza cuando se pueda.
        if (!core::CanReadJobData(job.rawStatus)) continue;
        const std::wstring key = Key(job);
        if (cache_.count(key) || pending_.count(key)) continue;
        pending_.insert(key);
        worker_.Post([this, job, key, server, overrideDir] {
            auto facts = std::make_shared<core::ContentFacts>();
            SpoolSearch search{server, job.printer, overrideDir};
            SpoolData spool = ReadJobSpool(search, job, kMaxBytes, false);
            if (spool.error == 0 && !spool.bytes.empty()) AnalyzeSpoolData(spool, *facts);
            // Sin datos (sin permiso, trabajo ya impreso...) se guarda igual el
            // resultado vacío para no reintentar en cada refresco.
            {
                std::lock_guard lock(mutex_);
                pending_.erase(key);
                cache_[key] = std::move(facts);
            }
            if (onReady_) onReady_();
        });
    }
}

void JobFactsService::Store(const core::JobInfo& job, const core::ContentFacts& facts) {
    if (!facts.analyzed) return;
    std::lock_guard lock(mutex_);
    auto& slot = cache_[Key(job)];
    // Un análisis completo (vista previa) reemplaza al parcial, no al revés.
    if (!slot || !slot->complete || facts.complete) slot = std::make_shared<core::ContentFacts>(facts);
}

void JobFactsService::Clear() {
    std::lock_guard lock(mutex_);
    worker_.Clear();
    pending_.clear();
    cache_.clear();
}

}  // namespace cola::preview
