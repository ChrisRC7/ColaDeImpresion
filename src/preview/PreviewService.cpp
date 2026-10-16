#include "preview/PreviewService.h"

#include "util/Log.h"

namespace cola::preview {

PreviewService::PreviewService(ReadyCallback onReady, size_t memoryBudgetBytes)
    : onReady_(std::move(onReady)), budget_(memoryBudgetBytes) {}

DocumentPtr PreviewService::Find(const std::wstring& key) {
    std::lock_guard lock(mutex_);
    auto it = index_.find(key);
    if (it == index_.end()) return nullptr;
    // Lectura incompleta (otra copia de la app leía el trabajo): pasado un rato
    // se olvida para que el próximo refresco la vuelva a pedir.
    const auto& doc = it->second->second;
    if (doc->readIncomplete && GetTickCount64() - doc->builtTick > kIncompleteRetryMs) {
        used_ -= std::min(used_, doc->MemoryBytes());
        lru_.erase(it->second);
        index_.erase(it);
        return nullptr;
    }
    lru_.splice(lru_.begin(), lru_, it->second);  // marca como usado recientemente
    return it->second->second;
}

bool PreviewService::IsPending(const std::wstring& key) {
    std::lock_guard lock(mutex_);
    return pending_.count(key) > 0;
}

void PreviewService::Request(const PreviewRequest& request, bool urgent) {
    const std::wstring key = request.Key();
    {
        std::lock_guard lock(mutex_);
        if (index_.count(key) || pending_.count(key)) return;
        if (urgent) {
            worker_.Clear();
            pending_.clear();
        }
        pending_.insert(key);
    }
    worker_.Post([this, request, key] {
        {
            std::lock_guard lock(mutex_);
            if (!pending_.count(key)) return;  // se descartó mientras esperaba
        }
        DocumentPtr doc = BuildPreview(request);
        Store(key, doc);
        if (onReady_) onReady_(key);
    });
}

void PreviewService::Store(const std::wstring& key, DocumentPtr doc) {
    std::lock_guard lock(mutex_);
    pending_.erase(key);
    if (index_.count(key)) return;
    used_ += doc->MemoryBytes();
    lru_.emplace_front(key, std::move(doc));
    index_[key] = lru_.begin();
    // Libera los menos usados hasta entrar en el presupuesto (siempre conserva el más nuevo).
    while (used_ > budget_ && lru_.size() > 1) {
        auto& victim = lru_.back();
        used_ -= std::min(used_, victim.second->MemoryBytes());
        index_.erase(victim.first);
        lru_.pop_back();
    }
}

void PreviewService::Clear() {
    std::lock_guard lock(mutex_);
    worker_.Clear();
    pending_.clear();
    lru_.clear();
    index_.clear();
    used_ = 0;
}

}  // namespace cola::preview
