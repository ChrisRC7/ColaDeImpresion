#pragma once
// Servicio de vistas previas: las genera en un hilo de fondo y las guarda en
// una caché LRU limitada por memoria, para no regenerarlas en cada refresco de
// la lista. La UI pide con Request() y recibe un aviso por callback cuando
// la vista previa está lista; luego la obtiene con Find().
//
// Hilos: Find/Request/IsPending/Clear se llaman desde la UI y solo toman el
// candado un instante (nunca hacen E/S). BuildPreview corre en el hilo
// propio del servicio (util::WorkQueue con COM inicializado), de a una
// vista previa a la vez. mutex_ protege la caché y el conjunto de pendientes.
// Las claves son PreviewRequest::Key(): si el trabajo cambia, cambia la clave
// y la entrada vieja queda sin usar hasta que la LRU la desaloja.

#include <functional>
#include <list>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

#include "preview/PreviewBuilder.h"
#include "util/WorkQueue.h"

namespace cola::preview {

// Dueño de la caché de vistas previas y del hilo que las genera. Lo crea y
// lo posee MainWindow (capa app).
class PreviewService {
public:
    // Se invoca desde el hilo de fondo con la clave lista: reenviar a la UI con PostMessage.
    using ReadyCallback = std::function<void(const std::wstring& key)>;

    // onReady: aviso de "vista previa lista". memoryBudgetBytes: memoria
    // máxima aproximada de la caché (384 MB por defecto).
    explicit PreviewService(ReadyCallback onReady, size_t memoryBudgetBytes = 384ull * 1024 * 1024);

    // Devuelve la vista previa cacheada o nullptr. Una con lectura incompleta
    // vale kIncompleteRetryMs; después se olvida y el próximo refresco la pide de nuevo.
    // Encontrarla la marca como la más reciente de la LRU.
    DocumentPtr Find(const std::wstring& key);
    // Vida de una vista previa con lectura incompleta (15 s, D48).
    static constexpr std::uint64_t kIncompleteRetryMs = 15000;

    // Encola la generación si no está cacheada ni pendiente.
    // urgent = descarta las solicitudes pendientes anteriores (el usuario cambió de trabajo).
    // (La que ya se está generando sigue hasta el final.) Hilo de la UI.
    void Request(const PreviewRequest& request, bool urgent);

    // true si la clave está encolada o generándose (la UI muestra "cargando").
    bool IsPending(const std::wstring& key);
    // Vacía la caché y descarta lo pendiente (p. ej. al cambiar de servidor).
    void Clear();

private:
    // Guarda el documento recién generado y desaloja los menos usados si se
    // pasa del presupuesto. Hilo de fondo.
    void Store(const std::wstring& key, DocumentPtr doc);

    ReadyCallback onReady_;   // aviso a la UI (se llama sin el candado tomado)
    size_t budget_;           // presupuesto de memoria en bytes
    size_t used_ = 0;         // memoria aproximada en uso (suma de MemoryBytes)
    std::mutex mutex_;        // protege lru_, index_, pending_ y used_
    std::list<std::pair<std::wstring, DocumentPtr>> lru_;  // frente = más reciente
    // clave -> posición en lru_ (búsqueda O(1) y movimiento al frente con splice).
    std::unordered_map<std::wstring, std::list<std::pair<std::wstring, DocumentPtr>>::iterator> index_;
    std::set<std::wstring> pending_;  // claves encoladas o en generación
    util::WorkQueue worker_{true};  // último miembro: se destruye (join) primero
};

}  // namespace cola::preview
