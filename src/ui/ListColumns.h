#pragma once
// Columnas de la tabla de trabajos: definición, distribución en el ancho de la
// ventana y orden elegido por el operador. Lógica pura (sin ventanas) para
// poder probarla; JobListView solo la dibuja y la usa al arrastrar.
//
// Orden de las columnas: como en la cola de Windows, el operador arrastra el
// título de una columna a otra posición. Se guarda una sola lista de ids
// (config.json "columnOrder") con las columnas de la cola y las del historial
// (sus ids no se repiten: "history.*"). Vacía = orden predeterminado.
//
// Decisiones relacionadas (docs/CONTEXTO.md): D30 (tabla con encabezado
// clicable), D36 (anchos redimensionables guardados en "columnWidths"), D38
// (orden por arrastre) y D40 (nunca se ocultan columnas: barra horizontal).
// Las definiciones concretas (JobColumns/HistoryColumns) están en app/RowBuilder.

#include <map>
#include <string>
#include <vector>

namespace cola::ui {

// Alineación horizontal del título y de las celdas de una columna (números a
// la derecha, textos a la izquierda, valores cortos centrados).
enum class Align { Left, Center, Right };

// Definición de una columna de la tabla (cola o historial).
struct ListColumn {
    // Título visible en el encabezado (ya traducido).
    std::wstring label;
    int width = 0;          // en DIP; 0 = flexible (ocupa el espacio que sobre)
    Align align = Align::Left;  // del encabezado y de los valores
    bool pill = false;      // la celda se dibuja como insignia de estado (con el tono de la fila)
    bool emphasis = false;  // texto principal de la fila (semibold)
    int sortId = -1;        // identificador que se devuelve al hacer clic; -1 = no ordenable
    std::wstring id;        // identificador estable para guardar ancho y orden ("" = no se guarda ni se mueve)
};

// Posición horizontal de una columna visible (px, sin el desplazamiento horizontal).
struct ColumnSlot {
    size_t column;  // índice de la columna en el vector que se pasó a LayoutColumns
    int left;       // x del borde izquierdo del contenido (px, coordenadas de la tabla sin desplazar)
    int right;      // x del borde derecho (exclusivo); entre columnas hay un espacio `gap`
};

// Distribuye las columnas en un ancho de ventana (px), en el orden en que
// vienen. userWidths: anchos en DIP elegidos por el operador (por id). Reglas:
//  - Nunca se oculta una columna: si no caben en el ancho, se salen por la
//    derecha y la tabla muestra la barra horizontal (como la cola de Windows).
//  - La columna flexible (width 0) ocupa el espacio libre (como mínimo 140
//    DIP) salvo que el operador le haya dado un ancho.
// `width` es el ancho visible del área de la tabla en px. Devuelve una
// posición por columna, en el mismo orden; el `right` de la última puede
// pasar de `width` (eso define el ancho del desplazamiento horizontal).
std::vector<ColumnSlot> LayoutColumns(const std::vector<ListColumn>& columns,
                                      const std::map<std::wstring, int>& userWidths, int width);

// Orden completo a partir del guardado: descarta ids desconocidos o repetidos
// y agrega los que falten (p. ej. una columna nueva de otra versión) detrás de
// la columna que los precede en el orden predeterminado. Vacío = defaults.
// `saved` es lo leído de config.json; `defaults`, los ids en el orden
// predeterminado (cola + historial). El resultado contiene exactamente los
// ids de `defaults`, cada uno una vez.
std::vector<std::wstring> NormalizeColumnOrder(const std::vector<std::wstring>& saved,
                                               const std::vector<std::wstring>& defaults);

// Guarda el orden nuevo de una vista (cola o historial) dentro del orden
// completo: las posiciones de las columnas de esa vista se ocupan con `view`
// en secuencia y las demás no se mueven. Así una columna que no se ve en ese
// momento (Impresora, solo en "Todas") conserva su lugar entre sus vecinas.
// `order` debe estar normalizado (contener todos los ids de `view`).
std::vector<std::wstring> MergeColumnOrder(const std::vector<std::wstring>& order,
                                           const std::vector<std::wstring>& view);

// Índices de `columns` en el orden de presentación según `order`. Las columnas
// sin id o que no están en `order` conservan su lugar relativo.
// Ejemplo: display = {2, 0, 1} significa "dibujar primero la columna 2". Las
// filas no se reordenan: la tabla dibuja la celda columns[display[k]] en el
// lugar k (D38), así RowBuilder sigue generando las celdas en el orden fijo.
std::vector<size_t> ColumnDisplayOrder(const std::vector<ListColumn>& columns, const std::vector<std::wstring>& order);

// Mueve `column` (índice de columna) dentro del orden de presentación para que
// quede antes (o después, si after) de `anchor`. Si alguno no está, no cambia nada.
// Lo usa la tabla al soltar el título arrastrado: `anchor` es la columna sobre
// la que se soltó y `after` indica si fue en su mitad derecha. Devuelve el
// orden de presentación nuevo (recibe una copia, no modifica el original).
std::vector<size_t> MoveDisplayColumn(std::vector<size_t> display, size_t column, size_t anchor, bool after);

}  // namespace cola::ui
