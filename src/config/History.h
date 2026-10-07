#pragma once
// Historial de trabajos que salieron de la cola (impresos, cancelados, con error)
// y estadísticas derivadas (páginas por día / semana, por usuario).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cola::config {

enum class JobOutcome { Printed, Cancelled, Failed, Removed };

const wchar_t* JobOutcomeToString(JobOutcome outcome);  // clave estable para JSON
JobOutcome JobOutcomeFromString(const std::wstring& text);

struct HistoryEntry {
    std::int64_t timeUnix = 0;  // momento en que salió de la cola (UTC)
    std::wstring server;
    std::wstring printer;
    std::wstring document;
    std::wstring user;
    std::wstring machine;
    std::uint32_t pages = 0;  // para las estadísticas
    std::uint64_t sizeBytes = 0;
    JobOutcome outcome = JobOutcome::Printed;
    // Lo que mostraba la tabla de la cola al salir el trabajo, para que el
    // historial conserve todas sus columnas. Vacío / 0 en registros anteriores.
    std::uint32_t position = 0;     // columna "#"
    std::wstring printPages;        // "Páginas" (páginas × copias, o "—")
    std::wstring paper;
    std::wstring orientation;
    std::wstring duplex;
    std::wstring source;            // origen del papel
    std::int64_t submittedUnix = 0;  // "Enviado" (UTC)
};

struct HistoryStats {
    std::uint64_t pagesToday = 0;
    std::uint64_t pagesWeek = 0;  // últimos 7 días incluyendo hoy
    std::uint64_t jobsToday = 0;
    std::map<std::wstring, std::uint64_t> pagesPerDay;   // "2026-09-19" -> páginas
    std::map<std::wstring, std::uint64_t> pagesPerUser;  // últimos 7 días
};

class History {
public:
    explicit History(std::wstring path = {}, size_t limit = 2000);

    bool Load();
    bool Save();
    bool IsDirty() const { return dirty_; }

    void Add(HistoryEntry entry);
    void Clear();
    void SetLimit(size_t limit);

    // Más recientes primero.
    const std::vector<HistoryEntry>& Entries() const { return entries_; }

    // nowUnix se inyecta para poder probarlo; solo cuenta trabajos impresos.
    HistoryStats ComputeStats(std::int64_t nowUnix) const;

private:
    std::wstring path_;
    size_t limit_;
    std::vector<HistoryEntry> entries_;
    bool dirty_ = false;
};

}  // namespace cola::config
