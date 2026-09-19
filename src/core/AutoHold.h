#pragma once
// Retención automática de trabajos: decide qué trabajos recién llegados hay
// que pausar y recuerda cuáles pausó la app ("retenidos"). Lógica pura, la usa
// QueueMonitor en su hilo y se prueba en tests/.
//
// Reglas:
//  - Solo se retienen trabajos NUEVOS: la primera vez que se ve una impresora
//    (al iniciar o al conectar) sus trabajos existentes se registran sin tocarlos.
//  - Cada trabajo se evalúa una sola vez: si un operador lo reanuda, no se
//    vuelve a pausar.
//  - Se retienen aunque ya hayan empezado a imprimirse (se ganó la carrera por
//    poco); no se tocan los que están pausados, cancelándose o impresos.

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/PrintTypes.h"

namespace cola::core {

class AutoHoldTracker {
public:
    // Registra los trabajos de una impresora y devuelve los ids a pausar
    // (vacío si la retención está desactivada o es la primera vez que se ve).
    std::vector<std::uint32_t> Process(const std::wstring& printer, const std::vector<JobInfo>& jobs, bool enabled);

    void MarkHeld(const std::wstring& jobKey) { held_.insert(jobKey); }
    bool IsHeld(const std::wstring& jobKey) const { return held_.count(jobKey) > 0; }

    // Marca jobs[i].heldByApp según corresponda.
    void Annotate(std::vector<JobInfo>& jobs) const;

    void Reset();

private:
    std::map<std::wstring, std::set<std::uint32_t>> seen_;  // impresora -> ids ya evaluados
    std::set<std::wstring> held_;                           // claves de trabajos que pausó la app
};

bool ShouldHold(const JobInfo& job);

}  // namespace cola::core
