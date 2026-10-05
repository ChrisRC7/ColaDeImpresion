#include "core/JobStatus.h"

#include <winspool.h>

namespace cola::core {

JobState JobStateFromFlags(std::uint32_t s, std::uint32_t printerProblems) {
    // El orden importa: primero lo más específico / grave.
    if (s & (JOB_STATUS_DELETING | JOB_STATUS_DELETED)) return JobState::Deleting;
    if (s & (JOB_STATUS_ERROR | JOB_STATUS_PAPEROUT | JOB_STATUS_USER_INTERVENTION | JOB_STATUS_BLOCKED_DEVQ))
        return JobState::Error;
    if (s & JOB_STATUS_OFFLINE) return JobState::Offline;
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
    if (s & (JOB_STATUS_PRINTED | JOB_STATUS_COMPLETE)) return JobState::Printed;
    if (s & JOB_STATUS_SPOOLING) return JobState::Spooling;
    if (printerProblems & ProblemOffline) return JobState::Offline;
    return JobState::Queued;
}

bool CanReadJobData(std::uint32_t s) {
    if (s & (JOB_STATUS_SPOOLING | JOB_STATUS_PRINTING | JOB_STATUS_DELETING | JOB_STATUS_DELETED |
             JOB_STATUS_RESTART | kJobStatusRenderingLocally))
        return false;
    return (s & (JOB_STATUS_PAUSED | JOB_STATUS_PRINTED | JOB_STATUS_COMPLETE)) != 0;
}

std::uint32_t PrinterProblemsFromFlags(std::uint32_t s, std::uint32_t attributes) {
    std::uint32_t p = ProblemNone;
    if ((s & PRINTER_STATUS_OFFLINE) || (attributes & PRINTER_ATTRIBUTE_WORK_OFFLINE)) p |= ProblemOffline;
    if (s & PRINTER_STATUS_PAPER_JAM) p |= ProblemPaperJam;
    if (s & PRINTER_STATUS_PAPER_OUT) p |= ProblemPaperOut;
    if (s & PRINTER_STATUS_TONER_LOW) p |= ProblemTonerLow;
    if (s & PRINTER_STATUS_NO_TONER) p |= ProblemNoToner;
    if (s & PRINTER_STATUS_DOOR_OPEN) p |= ProblemDoorOpen;
    if (s & PRINTER_STATUS_USER_INTERVENTION) p |= ProblemUserIntervention;
    if (s & PRINTER_STATUS_ERROR) p |= ProblemError;
    if (s & PRINTER_STATUS_PAUSED) p |= ProblemPaused;
    if (s & (PRINTER_STATUS_NOT_AVAILABLE | PRINTER_STATUS_SERVER_UNKNOWN)) p |= ProblemNotAvailable;
    if (s & PRINTER_STATUS_OUTPUT_BIN_FULL) p |= ProblemOutputBinFull;
    if (s & PRINTER_STATUS_PAPER_PROBLEM) p |= ProblemPaperProblem;
    if (s & PRINTER_STATUS_OUT_OF_MEMORY) p |= ProblemOutOfMemory;
    return p;
}

bool IsAttentionState(JobState state) { return state == JobState::Error || state == JobState::Offline; }

PrinterProblem MostSevereProblem(std::uint32_t problems) {
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
