// Implementación de la retención automática (D17/D18): decide qué trabajos
// pausar y recuerda los retenidos. Ver las reglas en AutoHold.h.
#include "core/AutoHold.h"

namespace cola::core {

bool ShouldHold(const JobInfo& job) {
    if (job.rawStatus & kJobStatusRenderingLocally) return false;  // ver AutoHold.h
    switch (job.state) {
        // Ya pausado (por alguien), cancelándose o terminado: no hay nada que frenar.
        case JobState::Paused:
        case JobState::Deleting:
        case JobState::Printed: return false;
        // En cola, recibiéndose, imprimiéndose, con error...: se pausa.
        default: return true;
    }
}

std::vector<std::uint32_t> AutoHoldTracker::Process(const std::wstring& printer, const std::vector<JobInfo>& jobs,
                                                    bool enabled, bool holdExisting, bool partialList) {
    std::vector<std::uint32_t> toHold;
    auto found = seen_.find(printer);
    // Primera vez que se ve esta impresora en la sesión (al iniciar, al
    // conectar o cuando aparece una cola nueva): sus trabajos no son "nuevos".
    const bool firstTime = found == seen_.end();
    std::set<std::uint32_t> current;  // ids presentes ahora (reemplaza a seen_[printer])

    for (const auto& job : jobs) {
        current.insert(job.id);
        const std::wstring key = job.Key();
        // Un retenido que ya no está en pausa fue liberado por un operador.
        if (held_.count(key) && job.state != JobState::Paused) held_.erase(key);
        if (firstTime) {
            // Solo se registran; se retienen únicamente con "pausar todo al iniciar".
            if (holdExisting && ShouldHold(job)) toHold.push_back(job.id);
            continue;
        }
        // Ya evaluado antes: no se vuelve a pausar aunque un operador lo haya
        // reanudado (regla "cada trabajo se evalúa una sola vez").
        if (found->second.count(job.id)) continue;
        // Trabajo nuevo desde la última instantánea.
        if (enabled && ShouldHold(job)) toHold.push_back(job.id);
    }

    // Lista parcial (F6): los trabajos que no aparecen pueden seguir en la
    // cola del servidor. No se olvida ninguno: se suman los vistos ahora a los
    // ya evaluados y las marcas de retenido quedan como estaban.
    if (partialList && !firstTime) {
        found->second.insert(current.begin(), current.end());
        return toHold;
    }

    // Olvida los trabajos que ya salieron de la cola.
    if (auto attempts = failedAttempts_.find(printer); attempts != failedAttempts_.end()) {
        std::erase_if(attempts->second, [&](const auto& entry) { return !current.count(entry.first); });
        if (attempts->second.empty()) failedAttempts_.erase(attempts);
    }
    if (!firstTime) {
        for (std::uint32_t id : found->second) {
            if (!current.count(id)) {
                // Se arma un JobInfo mínimo solo para obtener la misma clave
                // (impresora#id) con que se guardó en held_.
                JobInfo gone;
                gone.printer = printer;
                gone.id = id;
                held_.erase(gone.Key());
            }
        }
    }
    // Los ids actuales pasan a ser los "ya evaluados" (incluidos los que se
    // devuelven para pausar; si la pausa falla por un error pasajero, el
    // llamador pide el reintento con RetryLater).
    seen_[printer] = std::move(current);
    return toHold;
}

bool AutoHoldTracker::RetryLater(const std::wstring& printer, std::uint32_t id) {
    if (++failedAttempts_[printer][id] >= kMaxHoldAttempts) return false;
    // Sin el id en los "ya evaluados", el próximo Process lo ve como nuevo.
    if (auto found = seen_.find(printer); found != seen_.end()) found->second.erase(id);
    return true;
}

void AutoHoldTracker::Annotate(std::vector<JobInfo>& jobs) const {
    // heldByApp solo si sigue en pausa y la app lo había retenido.
    for (auto& job : jobs) job.heldByApp = job.state == JobState::Paused && IsHeld(job.Key());
}

void AutoHoldTracker::Reset() {
    seen_.clear();
    held_.clear();
    failedAttempts_.clear();
}

}  // namespace cola::core
