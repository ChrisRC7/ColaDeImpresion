// Implementación de los métodos pequeños de los tipos del dominio
// (core/PrintTypes.h) y de la conciliación entre la lista de trabajos del
// servidor y la de la caché del cliente (D28). Lógica pura: sin llamadas a
// winspool, probada en tests/.
#include "core/PrintTypes.h"

#include <algorithm>

namespace cola::core {

std::wstring PrinterInfo::DisplayName() const {
    // "\\\\SERVIDOR\\Impresora" -> "Impresora"
    // Se busca la primera barra después de las dos iniciales (fin del nombre
    // del servidor) y se devuelve lo que sigue. Si no hay tal barra (solo
    // "\\\\SERVIDOR") o el nombre es local, se devuelve completo.
    if (name.size() > 2 && name[0] == L'\\' && name[1] == L'\\') {
        size_t slash = name.find(L'\\', 2);
        if (slash != std::wstring::npos) return name.substr(slash + 1);
    }
    return name;
}

// Clave "<impresora>#<id>": el id solo es único dentro del servidor, así que
// se antepone la impresora para que no choquen trabajos de colas distintas en
// la vista "Todas" ni en las cachés que usan la clave.
std::wstring JobInfo::Key() const { return printer + L"#" + std::to_wstring(id); }

std::vector<JobInfo> ReconcileJobLists(std::vector<JobInfo> server, const std::vector<JobInfo>& cached) {
    // De la caché solo interesan las copias locales de Client Side Rendering
    // (esta PC todavía las convierte y envía); cualquier otro trabajo que solo
    // esté en la caché es un resto viejo que el servidor ya no tiene.
    for (const auto& job : cached) {
        if (!(job.rawStatus & kJobStatusRenderingLocally)) continue;
        // Si el servidor ya muestra ese id, su versión manda (es la verdadera).
        bool known = std::any_of(server.begin(), server.end(), [&](const JobInfo& j) { return j.id == job.id; });
        if (!known) server.push_back(job);
    }
    // Orden estable por posición en la cola: los agregados quedan en su lugar
    // relativo y, a igual posición, se conserva el orden en que llegaron.
    std::stable_sort(server.begin(), server.end(),
                     [](const JobInfo& a, const JobInfo& b) { return a.position < b.position; });
    return server;
}

const PrinterInfo* QueueSnapshot::FindPrinter(const std::wstring& name) const {
    for (const auto& p : printers)
        if (p.name == name) return &p;
    return nullptr;
}

}  // namespace cola::core
