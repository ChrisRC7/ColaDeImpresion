#pragma once
// Traducción de los flags del spooler (JOB_STATUS_*, PRINTER_STATUS_*) a
// estados simplificados. Es lógica pura, cubierta por pruebas unitarias.
// Además concentra las dos reglas de seguridad sobre los trabajos que más bugs
// del LAIMI evitaron: cuándo se pueden leer sus datos (CanReadJobData, D39) y
// cuándo un trabajo es la copia local de Client Side Rendering que no se debe
// tocar (IsLocalCsrCopy, D50).

#include <cstdint>

#include "core/PrintTypes.h"

namespace cola::core {

// Estado simplificado de un trabajo a partir de JOB_INFO_2.Status (jobStatus,
// flags JOB_STATUS_*) y de los problemas de su impresora (máscara
// PrinterProblem). Un trabajo puede tener varios flags; se elige uno con un
// orden de gravedad/especificidad (borrándose > error > fuera de línea >
// pausa > reinicio > imprimiendo > impreso > recibiéndose > en cola). Si se
// imprime pero la impresora tiene un problema, se muestra como error/fuera de
// línea para que el operador lo vea en la fila del trabajo.
JobState JobStateFromFlags(std::uint32_t jobStatus, std::uint32_t printerProblems);
// Máscara PrinterProblem a partir de PRINTER_INFO_2.Status (PRINTER_STATUS_*)
// y PRINTER_INFO_2.Attributes (para PRINTER_ATTRIBUTE_WORK_OFFLINE, el "usar
// impresora sin conexión" que el usuario puede marcar).
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
// (Error o Offline: la UI los resalta y las notificaciones los avisan.)
bool IsAttentionState(JobState state);

// Problema más grave de la impresora (para mostrar un único mensaje), o ProblemNone.
// "problems" es una máscara PrinterProblem; se recorre una lista fija de mayor
// a menor gravedad (no disponible, fuera de línea, atasco, puerta... y al final
// la pausa y el tóner bajo, que solo son avisos).
PrinterProblem MostSevereProblem(std::uint32_t problems);

}  // namespace cola::core
