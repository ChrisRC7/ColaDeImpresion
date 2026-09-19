#pragma once
// Traducción de los flags del spooler (JOB_STATUS_*, PRINTER_STATUS_*) a
// estados simplificados. Es lógica pura, cubierta por pruebas unitarias.

#include <cstdint>

#include "core/PrintTypes.h"

namespace cola::core {

JobState JobStateFromFlags(std::uint32_t jobStatus, std::uint32_t printerProblems);
std::uint32_t PrinterProblemsFromFlags(std::uint32_t printerStatus, std::uint32_t attributes);

// true si el estado requiere atención del operador.
bool IsAttentionState(JobState state);

// Problema más grave de la impresora (para mostrar un único mensaje), o ProblemNone.
PrinterProblem MostSevereProblem(std::uint32_t problems);

}  // namespace cola::core
