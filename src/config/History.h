#pragma once
// Historial de trabajos que salieron de la cola (impresos, cancelados, con error)
// y estadísticas derivadas (páginas por día / semana, por usuario).
//
// D13: el spooler no guarda historial, así que lo arma core/JobTracker
// comparando instantáneas de la cola; cuando un trabajo desaparece,
// app/RowBuilder::MakeHistoryEntry crea el registro con lo que mostraba la
// tabla (D41) y MainWindow lo agrega aquí. Se persiste en historial.json
// (config::HistoryFile(), en DataDir()) y se exporta a Excel (D43).
// La clase no es segura entre hilos: se usa solo desde el hilo de la UI.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cola::config {

// Resultado con que un trabajo salió de la cola (lo decide JobTracker con
// heurísticas, D13/D47):
//   Printed   - se imprimió (o lo liberó el operador y salió de la cola).
//   Cancelled - se canceló/eliminó antes de imprimirse.
//   Failed    - salió con estado de error.
//   Removed   - desapareció sin poder saber por qué ("Retirado").
enum class JobOutcome { Printed, Cancelled, Failed, Removed };

const wchar_t* JobOutcomeToString(JobOutcome outcome);  // clave estable para JSON
// Inversa de JobOutcomeToString; un texto desconocido (o vacío) da Printed.
JobOutcome JobOutcomeFromString(const std::wstring& text);

// Un registro del historial. Los textos se guardan ya traducidos (en el
// idioma activo al registrarlo), tal como se veían en la tabla.
struct HistoryEntry {
    std::int64_t timeUnix = 0;  // momento en que salió de la cola (UTC)
    std::wstring server;        // servidor de impresión ("\\SRV" o "" = local)
    std::wstring printer;       // nombre de la cola
    std::wstring document;      // nombre del documento según el spooler
    std::wstring user;          // usuario que envió el trabajo
    std::wstring carnet;  // del Active Directory (0.5.17, D56); "" = no se conoce
    std::wstring machine;       // equipo de origen ("\\PC")
    std::uint32_t pages = 0;  // para las estadísticas
                              // (TotalPages del spooler; ver PrintPagesCount)
    std::uint64_t sizeBytes = 0;  // tamaño del trabajo (el mayor visto, D42)
    JobOutcome outcome = JobOutcome::Printed;
    // Lo que mostraba la tabla de la cola al salir el trabajo, para que el
    // historial conserve todas sus columnas. Vacío / 0 en registros anteriores.
    std::uint32_t position = 0;     // columna "#"
    std::wstring printPages;        // "Páginas" (páginas × copias, o "—")
    std::wstring paper;             // columna "Papel"
    std::wstring orientation;       // columna "Orientación"
    std::wstring duplex;            // columna "Doble cara"
    std::wstring binding;  // presentación: "Cuaderno" / "Libreta" / "No aplica" (0.5.14)
    std::wstring source;            // origen del papel
    std::int64_t submittedUnix = 0;  // "Enviado" (UTC)

    // Páginas que mostraba la columna "Páginas" (páginas × copias). En registros
    // anteriores a esa columna, las del spooler. 0 = no determinable ("—").
    // Se obtiene volviendo a leer el número del texto printPages; cualquier
    // carácter que no sea un dígito (el "—") o un valor que no quepa en 32
    // bits da 0.
    std::uint32_t PrintPagesCount() const;
};

// Las estadísticas cuentan las páginas como la columna "Páginas" (PrintPagesCount).
// Solo consideran los trabajos con resultado Printed. Días y "hoy" se
// calculan con la hora local del equipo.
struct HistoryStats {
    std::uint64_t pagesToday = 0;
    std::uint64_t pagesWeek = 0;  // últimos 7 días incluyendo hoy
                                  // (en rigor: las últimas 7 × 24 h)
    std::uint64_t jobsToday = 0;  // trabajos impresos hoy
    std::map<std::wstring, std::uint64_t> pagesPerDay;   // "2026-09-19" -> páginas
                                                         // (todo el historial)
    std::map<std::wstring, std::uint64_t> pagesPerUser;  // últimos 7 días
};

// Lista de registros en memoria + su archivo. Los más recientes van primero
// y se conservan como máximo `limit` (los más viejos se descartan).
class History {
public:
    // path vacío = historial solo en memoria (Load/Save no hacen nada). El
    // límite predeterminado de 2000 se puede cambiar desde la configuración.
    explicit History(std::wstring path = {}, size_t limit = 2000);

    // Si el archivo existe pero no se puede leer, se copia aparte
    // (BackupPath) antes de que Save lo reemplace; si ni eso se puede, Save
    // no lo toca. Así un archivo dañado nunca se pisa con un historial vacío.
    // Reemplaza lo que hubiera en memoria. false si no hay ruta, el archivo
    // no existe (primer uso) o no se pudo leer; en todos los casos el
    // historial queda vacío y se puede seguir usando. Reintenta 2 veces con
    // 200 ms de espera (D52): bloquea al hilo que llama ese tiempo.
    bool Load();
    // Escribe el archivo completo de forma atómica (SaveJsonFile). false si no
    // hay ruta, si el archivo está protegido (ver Load) o si falla la
    // escritura. Si tiene éxito, limpia la marca de "sin guardar".
    bool Save();
    // true si hay cambios en memoria que todavía no se guardaron.
    bool IsDirty() const { return dirty_; }

    // Agrega un registro al principio (es el más reciente) y recorta al límite.
    void Add(HistoryEntry entry);
    // Completa el carnet de los registros que no lo tienen (las consultas al
    // directorio llegan después, y los registros anteriores a la 0.5.17 no lo
    // traen). carnetOf(usuario) devuelve "" si no se conoce. true si cambió alguno.
    // Es una plantilla para aceptar cualquier función o lambda sin el costo
    // de std::function (se llama una vez por registro).
    template <typename CarnetOf>
    bool FillCarnets(CarnetOf&& carnetOf) {
        bool changed = false;
        for (auto& e : entries_) {
            // Los que ya tienen carnet no se tocan; sin usuario no hay qué buscar.
            if (!e.carnet.empty() || e.user.empty()) continue;
            std::wstring carnet = carnetOf(e.user);
            if (carnet.empty()) continue;
            e.carnet = std::move(carnet);
            changed = true;
        }
        if (changed) dirty_ = true;
        return changed;
    }
    // Vacía el historial (menú "Borrar historial"); queda pendiente de guardar.
    void Clear();
    // Cambia el máximo de registros; si hay más, descarta los más viejos.
    void SetLimit(size_t limit);

    // Más recientes primero.
    const std::vector<HistoryEntry>& Entries() const { return entries_; }

    // nowUnix se inyecta para poder probarlo; solo cuenta trabajos impresos.
    HistoryStats ComputeStats(std::int64_t nowUnix) const;

    // Dónde se copia un historial.json ilegible: "historial.json.danado"
    // (cada copia nueva reemplaza la anterior).
    std::wstring BackupPath() const { return path_ + L".danado"; }

private:
    std::wstring path_;                  // ruta de historial.json ("" = solo memoria)
    size_t limit_;                       // máximo de registros que se conservan
    std::vector<HistoryEntry> entries_;  // más recientes primero
    bool dirty_ = false;                 // hay cambios sin guardar
    bool protectFile_ = false;  // el archivo no se pudo leer ni copiar aparte: no se reemplaza
};

}  // namespace cola::config
