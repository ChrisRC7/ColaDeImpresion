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

// Hilos: Find y Request se llaman desde la UI; Lookup corre en worker_. El
// estado compartido (cache_, pending_, retryAfter_, failing_) va bajo mutex_.
class CarnetService {
public:
    // Se invoca desde el hilo de fondo cuando hay carnets nuevos: reenviar a la UI.
    using ReadyCallback = std::function<void()>;

    // onReady: aviso de carnets nuevos (la UI vuelve a armar filas e historial).
    explicit CarnetService(ReadyCallback onReady);

    // Carnet del usuario de un trabajo; "" si no tiene, no está en el
    // directorio o todavía no se consultó.
    std::wstring Find(const std::wstring& user);

    // Encola la consulta de los usuarios que todavía no se conocen.
    // users son los usuarios tal como vienen del spooler (se normalizan con
    // AccountName). No bloquea: la búsqueda se hace en worker_.
    void Request(const std::vector<std::wstring>& users);

    // Espera tras un fallo del directorio antes de volver a consultar (1 minuto).
    static constexpr std::uint64_t kRetryMs = 60000;

private:
    // Tarea de worker_: busca las cuentas en el directorio y llena la caché.
    void Lookup(const std::vector<std::wstring>& accounts);

    ReadyCallback onReady_;  // aviso a la UI (se llama desde worker_)
    std::mutex mutex_;       // protege cache_, pending_, retryAfter_ y failing_
    std::map<std::wstring, std::wstring> cache_;  // cuenta en minúsculas -> carnet ("" = sin carnet)
    std::set<std::wstring> pending_;  // cuentas ya encoladas cuya respuesta no llegó (evita repetirlas)
    std::uint64_t retryAfter_ = 0;  // GetTickCount64(): no consultar antes (tras un fallo)
    bool failing_ = false;          // para registrar en app.log solo el primer fallo seguido
    DirectoryReader reader_;        // se usa solo desde el hilo de fondo
    util::WorkQueue worker_;        // último miembro: se destruye primero
};

}  // namespace cola::core
