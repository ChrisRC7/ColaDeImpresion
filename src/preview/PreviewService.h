#pragma once
// Servicio de vistas previas: las genera en un hilo de fondo y las guarda en
// una caché LRU limitada por memoria, para no regenerarlas en cada refresco de
// la lista. La UI pide con Request() y recibe un aviso por callback cuando
// la vista previa está lista; luego la obtiene con Find().

#include <functional>
#include <list>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

#include "preview/PreviewBuilder.h"
#include "util/WorkQueue.h"

namespace cola::preview {

class PreviewService {
public:
    // Se invoca desde el hilo de fondo con la clave lista: reenviar a la UI con PostMessage.
    using ReadyCallback = std::function<void(const std::wstring& key)>;

    explicit PreviewService(ReadyCallback onReady, size_t memoryBudgetBytes = 384ull * 1024 * 1024);

    // Devuelve la vista previa cacheada o nullptr. Una con lectura incompleta
    // vale kIncompleteRetryMs; después se olvida y el próximo refresco la pide de nuevo.
    DocumentPtr Find(const std::wstring& key);
    static constexpr std::uint64_t kIncompleteRetryMs = 15000;

    // Encola la generación si no está cacheada ni pendiente.
    // urgent = descarta las solicitudes pendientes anteriores (el usuario cambió de trabajo).
    void Request(const PreviewRequest& request, bool urgent);

    bool IsPending(const std::wstring& key);
    void Clear();

private:
    void Store(const std::wstring& key, DocumentPtr doc);

    ReadyCallback onReady_;
    size_t budget_;
    size_t used_ = 0;
    std::mutex mutex_;
    std::list<std::pair<std::wstring, DocumentPtr>> lru_;  // frente = más reciente
    std::unordered_map<std::wstring, std::list<std::pair<std::wstring, DocumentPtr>>::iterator> index_;
    std::set<std::wstring> pending_;
    util::WorkQueue worker_{true};  // último miembro: se destruye (join) primero
};

}  // namespace cola::preview
