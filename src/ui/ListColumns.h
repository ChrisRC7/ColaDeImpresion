#pragma once
// Columnas de la tabla de trabajos: definición, distribución en el ancho de la
// ventana y orden elegido por el operador. Lógica pura (sin ventanas) para
// poder probarla; JobListView solo la dibuja y la usa al arrastrar.
//
// Orden de las columnas: como en la cola de Windows, el operador arrastra el
// título de una columna a otra posición. Se guarda una sola lista de ids
// (config.json "columnOrder") con las columnas de la cola y las del historial
// (sus ids no se repiten: "history.*"). Vacía = orden predeterminado.

#include <map>
#include <string>
#include <vector>

namespace cola::ui {

enum class Align { Left, Center, Right };

struct ListColumn {
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
    size_t column;
    int left;
    int right;
};

// Distribuye las columnas en un ancho de ventana (px), en el orden en que
// vienen. userWidths: anchos en DIP elegidos por el operador (por id). Reglas:
//  - Nunca se oculta una columna: si no caben en el ancho, se salen por la
//    derecha y la tabla muestra la barra horizontal (como la cola de Windows).
//  - La columna flexible (width 0) ocupa el espacio libre (como mínimo 140
//    DIP) salvo que el operador le haya dado un ancho.
std::vector<ColumnSlot> LayoutColumns(const std::vector<ListColumn>& columns,
                                      const std::map<std::wstring, int>& userWidths, int width);

// Orden completo a partir del guardado: descarta ids desconocidos o repetidos
// y agrega los que falten (p. ej. una columna nueva de otra versión) detrás de
// la columna que los precede en el orden predeterminado. Vacío = defaults.
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
std::vector<size_t> ColumnDisplayOrder(const std::vector<ListColumn>& columns, const std::vector<std::wstring>& order);

// Mueve `column` (índice de columna) dentro del orden de presentación para que
// quede antes (o después, si after) de `anchor`. Si alguno no está, no cambia nada.
std::vector<size_t> MoveDisplayColumn(std::vector<size_t> display, size_t column, size_t anchor, bool after);

}  // namespace cola::ui
