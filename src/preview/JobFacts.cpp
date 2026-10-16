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
    // Si se puede leer NO forma parte de la clave: al liberar el trabajo (pasa a
    // imprimirse) sus datos analizados deben seguir valiendo, para la fila y
    // para el historial cuando sale de la cola.
    return util::Format(L"%ls#%u|%llu|%u", job.printer.c_str(), job.id,
                        static_cast<unsigned long long>(job.sizeBytes), spooling);
}

std::wstring JobFactsService::IdKey(const core::JobInfo& job) {
    return util::Format(L"%ls#%u", job.printer.c_str(), job.id);
}

void JobFactsService::Remember(const core::JobInfo& job, const std::shared_ptr<const core::ContentFacts>& facts) {
    if (!facts || !facts->analyzed) return;
    auto& slot = latest_[IdKey(job)];
    if (!slot || !slot->complete || facts->complete) slot = facts;
}

std::shared_ptr<const core::ContentFacts> JobFactsService::Find(const core::JobInfo& job) {
    std::lock_guard lock(mutex_);
    auto it = cache_.find(Key(job));
    std::shared_ptr<const core::ContentFacts> exact = it == cache_.end() ? nullptr : it->second;
    if (exact && exact->analyzed) return exact;
    // Mientras se imprime, el spooler puede informar otro tamaño (0 bytes al
    // final): sigue siendo el mismo trabajo, ya recibido, y valen sus datos.
    if (!(job.rawStatus & JOB_STATUS_SPOOLING)) {
        auto last = latest_.find(IdKey(job));
        if (last != latest_.end()) return last->second;
    }
    return exact;
}

void JobFactsService::Request(const std::vector<core::JobInfo>& jobs, const std::wstring& server,
                              const std::wstring& overrideDir) {
    std::lock_guard lock(mutex_);
    // Olvida los trabajos que ya no están (o que cambiaron: su clave es otra).
    std::set<std::wstring> alive, aliveIds;
    for (const auto& job : jobs) {
        alive.insert(Key(job));
        aliveIds.insert(IdKey(job));
    }
    for (auto it = cache_.begin(); it != cache_.end();) it = alive.count(it->first) ? std::next(it) : cache_.erase(it);
    for (auto it = latest_.begin(); it != latest_.end();)
        it = aliveIds.count(it->first) ? std::next(it) : latest_.erase(it);
    for (auto it = retries_.begin(); it != retries_.end();)
        it = alive.count(it->first) ? std::next(it) : retries_.erase(it);
    const std::uint64_t now = GetTickCount64();

    for (const auto& job : jobs) {
        const std::wstring key = Key(job);
        // Recibiéndose (ReadPrinter esperaría) o sin pausar (la lectura le quitaría
        // datos a la impresora): se analiza cuando se pueda. Un análisis vacío
        // (lectura cortada al reanudarlo, sin permiso...) se olvida para
        // reintentarlo si se vuelve a pausar; uno bueno se conserva.
        if (!core::CanReadJobData(job.rawStatus)) {
            auto it = cache_.find(key);
            if (it != cache_.end() && !it->second->analyzed) cache_.erase(it);
            continue;
        }
        if (cache_.count(key) || pending_.count(key)) continue;
        if (auto r = retries_.find(key); r != retries_.end() && now < r->second.notBefore) continue;
        pending_.insert(key);
        worker_.Post([this, job, key, server, overrideDir] {
            auto facts = std::make_shared<core::ContentFacts>();
            SpoolSearch search{server, job.printer, overrideDir};
            SpoolData spool = ReadJobSpool(search, job, kMaxBytes, false);
            if (spool.error == 0 && !spool.bytes.empty()) AnalyzeSpoolData(spool, *facts);
            // Sin datos (sin permiso, trabajo ya impreso...) se guarda igual el
            // resultado vacío para no reintentar en cada refresco. Una lectura
            // incompleta no: se reintenta más tarde unas pocas veces.
            {
                std::lock_guard lock(mutex_);
                pending_.erase(key);
                if (spool.incomplete) {
                    Retry& retry = retries_[key];
                    if (++retry.rounds < kIncompleteRounds) {
                        retry.notBefore = GetTickCount64() + kIncompleteRetryMs;
                        return;
                    }
                }
                Remember(job, facts);
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
    Remember(job, slot);
}

void JobFactsService::Clear() {
    std::lock_guard lock(mutex_);
    worker_.Clear();
    pending_.clear();
    retries_.clear();
    cache_.clear();
    latest_.clear();
}

}  // namespace cola::preview
