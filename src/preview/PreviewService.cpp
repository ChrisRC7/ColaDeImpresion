// Caché LRU de vistas previas y cola de generación en segundo plano.
// Ver PreviewService.h.
#include "preview/PreviewService.h"

#include <exception>

#include "i18n/Strings.h"
#include "util/Log.h"
#include "util/Strings.h"

namespace cola::preview {

// El hilo de trabajo (worker_) ya arrancó al construirse el miembro.
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
        // Ya está hecha o en camino: nada que hacer.
        if (index_.count(key) || pending_.count(key)) return;
        if (urgent) {
            // Las tareas encoladas se descartan; vaciar pending_ también hace
            // que una tarea que ya salió de la cola pero aún no empezó se
            // detenga (ver la comprobación al inicio de la tarea).
            worker_.Clear();
            pending_.clear();
        }
        pending_.insert(key);
    }
    // La tarea copia el pedido completo (no comparte nada con la UI). Corre
    // en el hilo del servicio: genera, guarda y avisa.
    worker_.Post([this, request, key] {
        {
            std::lock_guard lock(mutex_);
            if (!pending_.count(key)) return;  // se descartó mientras esperaba
        }
        // Si BuildPreview lanza (p. ej. std::bad_alloc con una imagen enorme),
        // WorkQueue atraparía la excepción pero la clave quedaría en pending_
        // para siempre y el panel en "Cargando…" (M5): se guarda un documento
        // con el mensaje de error y se avisa igual.
        DocumentPtr doc;
        try {
            doc = BuildPreview(request);
        } catch (const std::exception& e) {
            util::LogError(L"La vista previa de %ls falló con una excepción: %hs", key.c_str(), e.what());
        } catch (...) {
            util::LogError(L"La vista previa de %ls falló con una excepción desconocida", key.c_str());
        }
        if (!doc) {
            auto failed = std::make_shared<PreviewDocument>();
            failed->totalPages = request.job.totalPages;
            failed->message = util::Format(i18n::Tr(i18n::S::PreviewRenderFailed), L"error interno");
            doc = std::move(failed);
        }
        Store(key, doc);
        if (onReady_) onReady_(key);
    });
}

void PreviewService::Store(const std::wstring& key, DocumentPtr doc) {
    std::lock_guard lock(mutex_);
    pending_.erase(key);
    if (index_.count(key)) return;  // otra generación de la misma clave ganó (raro)
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

// Los documentos que la UI todavía tenga (DocumentPtr) siguen vivos hasta
// que los suelte: la caché solo deja de referenciarlos.
void PreviewService::Clear() {
    std::lock_guard lock(mutex_);
    worker_.Clear();
    pending_.clear();
    lru_.clear();
    index_.clear();
    used_ = 0;
}

}  // namespace cola::preview
