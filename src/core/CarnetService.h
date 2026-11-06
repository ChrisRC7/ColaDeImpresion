#pragma once
// Carnet de los usuarios de los trabajos, consultado en segundo plano en el
// Active Directory (D56; solo lectura, ver core/Directory.h) y guardado en
// caché durante toda la sesión: cada usuario se consulta una sola vez, y los
// pedidos que llegan juntos se resuelven en una misma búsqueda.
//
// Si el directorio no responde (equipo fuera del dominio, red caída) no se
// vuelve a intentar hasta kRetryMs después, para no frenar nada; mientras
// tanto la columna muestra "—".

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "core/Directory.h"
#include "util/WorkQueue.h"

namespace cola::core {

class CarnetService {
public:
    // Se invoca desde el hilo de fondo cuando hay carnets nuevos: reenviar a la UI.
    using ReadyCallback = std::function<void()>;

    explicit CarnetService(ReadyCallback onReady);

    // Carnet del usuario de un trabajo; "" si no tiene, no está en el
    // directorio o todavía no se consultó.
    std::wstring Find(const std::wstring& user);

    // Encola la consulta de los usuarios que todavía no se conocen.
    void Request(const std::vector<std::wstring>& users);

    static constexpr std::uint64_t kRetryMs = 60000;

private:
    void Lookup(const std::vector<std::wstring>& accounts);

    ReadyCallback onReady_;
    std::mutex mutex_;
    std::map<std::wstring, std::wstring> cache_;  // cuenta en minúsculas -> carnet ("" = sin carnet)
    std::set<std::wstring> pending_;
    std::uint64_t retryAfter_ = 0;  // GetTickCount64(): no consultar antes (tras un fallo)
    bool failing_ = false;          // para registrar en app.log solo el primer fallo seguido
    DirectoryReader reader_;        // se usa solo desde el hilo de fondo
    util::WorkQueue worker_;        // último miembro: se destruye primero
};

}  // namespace cola::core
