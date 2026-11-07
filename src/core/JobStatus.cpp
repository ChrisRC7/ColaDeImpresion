// Traducción de los flags del spooler a estados simplificados y reglas de
// seguridad para leer/tocar trabajos (ver JobStatus.h). Lógica pura sobre
// máscaras de bits: no llama a winspool, solo usa sus constantes.
#include "core/JobStatus.h"

#include <winspool.h>

namespace cola::core {

JobState JobStateFromFlags(std::uint32_t s, std::uint32_t printerProblems) {
    // El orden importa: primero lo más específico / grave.
    // Borrándose gana a todo: aunque además esté en pausa o con error, el
    // trabajo va a desaparecer y no tiene sentido mostrar otra cosa.
    if (s & (JOB_STATUS_DELETING | JOB_STATUS_DELETED)) return JobState::Deleting;
    // Errores propios del trabajo (incluye BLOCKED_DEVQ: el procesador de
    // impresión no pudo procesarlo, p. ej. datos incompatibles).
    if (s & (JOB_STATUS_ERROR | JOB_STATUS_PAPEROUT | JOB_STATUS_USER_INTERVENTION | JOB_STATUS_BLOCKED_DEVQ))
        return JobState::Error;
    if (s & JOB_STATUS_OFFLINE) return JobState::Offline;
    // La pausa va antes que "imprimiendo"/"recibiéndose": un trabajo retenido
    // mientras todavía llega (PAUSED | SPOOLING) se muestra como pausado.
    if (s & JOB_STATUS_PAUSED) return JobState::Paused;
    if (s & JOB_STATUS_RESTART) return JobState::Restarting;
    if (s & JOB_STATUS_PRINTING) {
        // Imprimiendo pero la impresora reporta un problema: es un error visible.
        if (printerProblems & (ProblemPaperJam | ProblemPaperOut | ProblemNoToner | ProblemDoorOpen |
                               ProblemUserIntervention | ProblemError))
            return JobState::Error;
        if (printerProblems & ProblemOffline) return JobState::Offline;
        return JobState::Printing;
    }
    // COMPLETE = el spooler terminó de enviarlo al puerto; PRINTED = impreso
    // (solo se ve si la cola conserva los documentos impresos).
    if (s & (JOB_STATUS_PRINTED | JOB_STATUS_COMPLETE)) return JobState::Printed;
    if (s & JOB_STATUS_SPOOLING) return JobState::Spooling;
    // En espera pero con la impresora fuera de línea: no va a salir, se avisa.
    if (printerProblems & ProblemOffline) return JobState::Offline;
    return JobState::Queued;
}

bool CanReadJobData(std::uint32_t s) {
    // Primero las prohibiciones (D39): pueden venir combinadas con PAUSED,
    // p. ej. un trabajo retenido mientras todavía se recibe (PAUSED | SPOOLING):
    // ReadPrinter se quedaría esperando datos que aún no llegan.
    // RENDERING_LOCALLY es la copia local de CSR: leerla le quita datos al envío.
    if (s & (JOB_STATUS_SPOOLING | JOB_STATUS_PRINTING | JOB_STATUS_DELETING | JOB_STATUS_DELETED |
             JOB_STATUS_RESTART | kJobStatusRenderingLocally))
        return false;
    // Después, solo los estados en que el spooler seguro no lo está enviando:
    // en pausa o ya impreso. Un trabajo "en cola" sin pausa podría empezar a
    // imprimirse en cualquier momento, así que tampoco se lee.
    return (s & (JOB_STATUS_PAUSED | JOB_STATUS_PRINTED | JOB_STATUS_COMPLETE)) != 0;
}

// Copia local de Client Side Rendering (D50): basta con el flag 0x4000.
bool IsLocalCsrCopy(std::uint32_t s) { return (s & kJobStatusRenderingLocally) != 0; }

std::uint32_t PrinterProblemsFromFlags(std::uint32_t s, std::uint32_t attributes) {
    // Traducción uno a uno de cada bit relevante; los demás PRINTER_STATUS_*
    // (calentando, ocupada, en espera...) son normales y no se reportan.
    std::uint32_t p = ProblemNone;
    // Fuera de línea por estado del dispositivo o porque el usuario marcó
    // "usar impresora sin conexión" (atributo WORK_OFFLINE).
    if ((s & PRINTER_STATUS_OFFLINE) || (attributes & PRINTER_ATTRIBUTE_WORK_OFFLINE)) p |= ProblemOffline;
    if (s & PRINTER_STATUS_PAPER_JAM) p |= ProblemPaperJam;
    if (s & PRINTER_STATUS_PAPER_OUT) p |= ProblemPaperOut;
    if (s & PRINTER_STATUS_TONER_LOW) p |= ProblemTonerLow;
    if (s & PRINTER_STATUS_NO_TONER) p |= ProblemNoToner;
    if (s & PRINTER_STATUS_DOOR_OPEN) p |= ProblemDoorOpen;
    if (s & PRINTER_STATUS_USER_INTERVENTION) p |= ProblemUserIntervention;
    if (s & PRINTER_STATUS_ERROR) p |= ProblemError;
    if (s & PRINTER_STATUS_PAUSED) p |= ProblemPaused;
    // SERVER_UNKNOWN: el cliente no sabe el estado del servidor (conexión caída).
    if (s & (PRINTER_STATUS_NOT_AVAILABLE | PRINTER_STATUS_SERVER_UNKNOWN)) p |= ProblemNotAvailable;
    if (s & PRINTER_STATUS_OUTPUT_BIN_FULL) p |= ProblemOutputBinFull;
    if (s & PRINTER_STATUS_PAPER_PROBLEM) p |= ProblemPaperProblem;
    if (s & PRINTER_STATUS_OUT_OF_MEMORY) p |= ProblemOutOfMemory;
    return p;
}

bool IsAttentionState(JobState state) { return state == JobState::Error || state == JobState::Offline; }

PrinterProblem MostSevereProblem(std::uint32_t problems) {
    // De mayor a menor gravedad: primero lo que impide imprimir del todo (no
    // disponible, fuera de línea), luego lo que requiere ir a la impresora
    // (atasco, puerta, sin tóner/papel...), y al final los avisos (pausa,
    // tóner bajo). Se devuelve el primero presente en la máscara.
    static const PrinterProblem order[] = {
        ProblemNotAvailable, ProblemOffline,       ProblemPaperJam,    ProblemDoorOpen,
        ProblemNoToner,      ProblemPaperOut,      ProblemPaperProblem, ProblemUserIntervention,
        ProblemOutOfMemory,  ProblemOutputBinFull, ProblemError,       ProblemPaused,
        ProblemTonerLow,
    };
    for (PrinterProblem p : order)
        if (problems & p) return p;
    return ProblemNone;
}

}  // namespace cola::core
