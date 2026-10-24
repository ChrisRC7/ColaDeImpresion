#pragma once
// Traducción de los flags del spooler (JOB_STATUS_*, PRINTER_STATUS_*) a
// estados simplificados. Es lógica pura, cubierta por pruebas unitarias.

#include <cstdint>

#include "core/PrintTypes.h"

namespace cola::core {

JobState JobStateFromFlags(std::uint32_t jobStatus, std::uint32_t printerProblems);
std::uint32_t PrinterProblemsFromFlags(std::uint32_t printerStatus, std::uint32_t attributes);

// true si los datos del trabajo se pueden leer (ReadPrinter) sin competir con
// el spooler. Las lecturas de un mismo trabajo comparten su avance: si la app
// lee mientras el procesador de impresión envía el trabajo a la impresora, a
// esta le llegan datos incompletos (hojas en blanco, la impresora se reinicia).
// Solo se lee un trabajo en pausa o ya impreso, y nunca uno que se recibe, se
// imprime, se borra, se reinicia o que esta PC todavía procesa (CSR).
bool CanReadJobData(std::uint32_t jobStatus);

// true si el trabajo es la copia local de Client Side Rendering
// (JOB_STATUS_RENDERING_LOCALLY): esta PC todavía lo convierte y lo envía al
// servidor. Nunca se pausa, reanuda, cancela, mueve ni reenvía: cortarlo a
// mitad del envío deja al servidor con datos incompletos (hojas en blanco).
// Se actúa sobre el trabajo del servidor, que aparece apenas empieza a llegar.
bool IsLocalCsrCopy(std::uint32_t jobStatus);

// true si el estado requiere atención del operador.
bool IsAttentionState(JobState state);

// Problema más grave de la impresora (para mostrar un único mensaje), o ProblemNone.
PrinterProblem MostSevereProblem(std::uint32_t problems);

}  // namespace cola::core
