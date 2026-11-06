// Servicio de carnets: caché + consultas al Active Directory en un hilo de
// fondo (D56). El hilo de la interfaz solo lee la caché (Find) y pide lo que
// falta (Request); la red se toca únicamente desde worker_.

#include "core/CarnetService.h"

#include <windows.h>  // GetTickCount64

#include "util/Log.h"
#include "util/Strings.h"

namespace cola::core {

CarnetService::CarnetService(ReadyCallback onReady) : onReady_(std::move(onReady)) {}

std::wstring CarnetService::Find(const std::wstring& user) {
    // Misma normalización que Request: "ESTUDIANTES\Ana" y "ana" son la misma cuenta.
    const std::wstring account = util::ToLower(AccountName(user));
    if (account.empty()) return {};
    // Lo llama el hilo de la interfaz mientras el de fondo puede estar escribiendo.
    std::lock_guard lock(mutex_);
    auto it = cache_.find(account);
    // No consultado todavía, o consultado sin carnet: "" en ambos casos.
    return it != cache_.end() ? it->second : std::wstring();
}

void CarnetService::Request(const std::vector<std::wstring>& users) {
    // Se llama en cada refresco de la cola: casi siempre todo está en caché y
    // no se encola nada. Solo las cuentas nuevas viajan al directorio.
    std::vector<std::wstring> missing;
    {
        std::lock_guard lock(mutex_);
        // Tras un fallo (fuera del dominio, red caída) se espera kRetryMs antes
        // de volver a intentar, para no encolar una consulta en cada refresco.
        if (GetTickCount64() < retryAfter_) return;
        for (const auto& user : users) {
            const std::wstring account = util::ToLower(AccountName(user));
            // Sin cuenta válida, ya conocida o ya pedida (en camino): se omite.
            if (account.empty() || cache_.count(account) || pending_.count(account)) continue;
            pending_.insert(account);
            missing.push_back(account);
        }
    }
    if (missing.empty()) return;
    // Todas las cuentas nuevas de este refresco van en una sola tarea (y en
    // una sola búsqueda por lote en DirectoryReader).
    worker_.Post([this, missing = std::move(missing)] { Lookup(missing); });
}

void CarnetService::Lookup(const std::vector<std::wstring>& accounts) {
    // Hilo de fondo: la consulta de red se hace sin tener tomado mutex_, así
    // Find (hilo de la interfaz) nunca espera por el directorio.
    std::map<std::wstring, std::wstring> found;
    std::wstring error;
    const bool ok = reader_.FindCarnets(accounts, found, error);
    {
        std::lock_guard lock(mutex_);
        for (const auto& account : accounts) {
            pending_.erase(account);
            // Si la consulta falló no se guarda nada: se reintentará más tarde.
            if (!ok) continue;
            // Consulta hecha: se guarda el carnet, o "" si la cuenta no existe o
            // no lo tiene, para no volver a preguntar por ella en la sesión.
            auto it = found.find(account);
            cache_[account] = it != found.end() ? it->second : std::wstring();
        }
        if (!ok) {
            retryAfter_ = GetTickCount64() + kRetryMs;
            // Solo el primer fallo de una racha va a app.log (no uno por minuto).
            if (!failing_)
                util::LogError(L"No se pudo consultar el carnet en el Active Directory (%ls); se reintenta en %llu s",
                               error.c_str(), static_cast<unsigned long long>(kRetryMs / 1000));
            failing_ = true;
            return;
        }
        if (failing_) util::LogInfo(L"El Active Directory volvió a responder");
        failing_ = false;
    }
    util::LogInfo(L"Carnets consultados en el Active Directory: %zu de %zu usuario(s)", found.size(), accounts.size());
    // Avisa a la interfaz (por PostMessage) solo si hay algo nuevo que mostrar.
    if (!found.empty() && onReady_) onReady_();
}

}  // namespace cola::core
