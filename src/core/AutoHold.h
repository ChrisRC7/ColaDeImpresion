#pragma once
// Retención automática de trabajos: decide qué trabajos recién llegados hay
// que pausar y recuerda cuáles pausó la app ("retenidos"). Lógica pura, la usa
// QueueMonitor en su hilo y se prueba en tests/.
//
// Reglas:
//  - Se retienen los trabajos NUEVOS. La primera vez que se ve una impresora
//    (al iniciar o al conectar) sus trabajos existentes se retienen solo si se
//    pide "holdExisting" (opción "al iniciar, pausar todo"); si no, solo se registran.
//  - Cada trabajo se evalúa una sola vez: si un operador lo reanuda, no se
//    vuelve a pausar.
//  - Se retienen aunque ya hayan empezado a imprimirse (se ganó la carrera por
//    poco); no se tocan los que están pausados, cancelándose o impresos.
//  - Nunca se retiene un trabajo que esta PC todavía procesa (Client Side
//    Rendering, JOB_STATUS_RENDERING_LOCALLY): es la copia local, en EMF, que el
//    spooler de esta PC está convirtiendo y enviando al servidor (pesa mucho
//    más que el trabajo real: 190 MB contra 71 MB con un PDF de 50 páginas).
//    Pausarlo a mitad del envío hacía que la impresora recibiera datos
//    incompletos (hojas en blanco y la impresora se reiniciaba). Se retiene el
//    trabajo del servidor, que aparece apenas empieza a llegar.

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/PrintTypes.h"

namespace cola::core {

class AutoHoldTracker {
public:
    // Registra los trabajos de una impresora y devuelve los ids a pausar.
    //  enabled:       retener los trabajos que llegan después de la primera vez.
    //  holdExisting:  la primera vez que se ve la impresora (al iniciar/conectar),
    //                 retener también los trabajos que ya estaban en la cola.
    std::vector<std::uint32_t> Process(const std::wstring& printer, const std::vector<JobInfo>& jobs, bool enabled,
                                       bool holdExisting = false);

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
