// Lógica de columnas de la tabla (sin ventanas): reparto del ancho, y
// normalización, fusión y aplicación del orden elegido por el operador.
// Probada en tests/ (D36, D38, D40).

#include "ui/ListColumns.h"

#include <algorithm>
#include <set>

#include "ui/Style.h"

namespace cola::ui {

std::vector<ColumnSlot> LayoutColumns(const std::vector<ListColumn>& columns,
                                      const std::map<std::wstring, int>& userWidths, int width) {
    const auto& style = Style::Get();
    // Medidas en DIP convertidas al DPI actual.
    const int gap = style.Px(12);   // separación entre columnas
    const int left = style.Px(18);  // margen (el estado se ve en su columna, sin ícono aparte)
    const int right = width - style.Px(16);  // borde derecho útil (margen derecho de 16 DIP)
    const int flexMin = style.Px(140);       // mínimo de la flexible (Documento), D40

    // Ancho elegido por el operador (px), o -1.
    // Los anchos se guardan en DIP (config.json) para que valgan con cualquier
    // escala; aquí se convierten a px.
    auto user = [&](size_t i) {
        if (columns[i].id.empty()) return -1;
        auto it = userWidths.find(columns[i].id);
        return it == userWidths.end() ? -1 : style.Px(it->second);
    };

    // Primera pasada: suma de lo que ocupan las columnas de ancho conocido
    // (más un gap por columna) y cuántas son flexibles.
    int fixed = 0;
    int flexCount = 0;
    for (size_t i = 0; i < columns.size(); ++i) {
        const int u = user(i);
        if (u >= 0) fixed += u + gap;
        else if (columns[i].width) fixed += style.Px(columns[i].width) + gap;
        else {
            fixed += gap;
            ++flexCount;
        }
    }
    // El espacio sobrante se reparte entre las flexibles, nunca por debajo del
    // mínimo: si la ventana es angosta, la suma pasa del ancho y la tabla
    // muestra la barra horizontal en vez de ocultar columnas (D40).
    const int flexWidth = flexCount ? std::max(flexMin, (right - left - fixed) / flexCount) : 0;

    // Segunda pasada: posiciones consecutivas de izquierda a derecha.
    // Prioridad del ancho: el del operador, luego el fijo de la definición y
    // por último el flexible calculado.
    std::vector<ColumnSlot> slots;
    int x = left;
    for (size_t i = 0; i < columns.size(); ++i) {
        const int u = user(i);
        const int w = u >= 0 ? u : columns[i].width ? style.Px(columns[i].width) : flexWidth;
        slots.push_back({i, x, x + w});
        x += w + gap;
    }
    return slots;
}

std::vector<std::wstring> NormalizeColumnOrder(const std::vector<std::wstring>& saved,
                                               const std::vector<std::wstring>& defaults) {
    const std::set<std::wstring> known(defaults.begin(), defaults.end());
    std::vector<std::wstring> order;
    std::set<std::wstring> seen;
    // Se conserva lo guardado que siga existiendo, sin repetir: un id que ya no
    // existe (columna quitada en otra versión) o editado a mano se descarta.
    for (const auto& id : saved)
        if (known.count(id) && seen.insert(id).second) order.push_back(id);
    // Las que faltan van detrás de la que las precede en el orden predeterminado.
    // Se recorre hacia atrás desde la anterior en `defaults` hasta encontrar
    // una que ya esté en `order`; si ninguna está, la columna va al principio.
    // Como se procesan en orden, una columna faltante puede anclarse en otra
    // faltante recién insertada (así un bloque nuevo conserva su orden interno).
    for (size_t i = 0; i < defaults.size(); ++i) {
        if (seen.count(defaults[i])) continue;
        auto at = order.begin();
        for (size_t prev = i; prev-- > 0;) {
            auto it = std::find(order.begin(), order.end(), defaults[prev]);
            if (it != order.end()) {
                at = std::next(it);
                break;
            }
        }
        order.insert(at, defaults[i]);
        seen.insert(defaults[i]);
    }
    return order;
}

std::vector<std::wstring> MergeColumnOrder(const std::vector<std::wstring>& order,
                                           const std::vector<std::wstring>& view) {
    const std::set<std::wstring> inView(view.begin(), view.end());
    std::vector<std::wstring> merged = order;
    // Cada "casilla" del orden completo que pertenece a esta vista recibe, en
    // secuencia, el siguiente id del orden nuevo de la vista. Las casillas de
    // otras columnas (de la otra vista o no visibles ahora) quedan intactas.
    // Ejemplo: order = A P B C (P no visible), view = C A B → C P A B.
    size_t next = 0;
    for (auto& id : merged)
        if (inView.count(id) && next < view.size()) id = view[next++];
    return merged;
}

std::vector<size_t> ColumnDisplayOrder(const std::vector<ListColumn>& columns, const std::vector<std::wstring>& order) {
    // Posición de un id en el orden guardado; -1 si no tiene id o no aparece.
    // (Búsqueda lineal: son pocas columnas.)
    auto rank = [&order](const std::wstring& id) -> std::ptrdiff_t {
        if (id.empty()) return -1;
        auto it = std::find(order.begin(), order.end(), id);
        return it == order.end() ? -1 : it - order.begin();
    };
    // Las columnas que están en el orden se reparten en los lugares que ocupan
    // entre todas; las demás no se mueven.
    // 1) Índices de las columnas con lugar en el orden guardado.
    std::vector<size_t> ranked;
    for (size_t i = 0; i < columns.size(); ++i)
        if (rank(columns[i].id) >= 0) ranked.push_back(i);
    // 2) Esos mismos índices ordenados según el orden guardado.
    std::vector<size_t> sorted = ranked;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](size_t a, size_t b) { return rank(columns[a].id) < rank(columns[b].id); });
    // 3) Se recorre la definición: cada lugar ocupado por una columna con
    //    rango recibe la siguiente de `sorted`; las sin rango quedan donde están.
    std::vector<size_t> display;
    size_t next = 0;
    for (size_t i = 0; i < columns.size(); ++i)
        display.push_back(rank(columns[i].id) >= 0 ? sorted[next++] : i);
    return display;
}

std::vector<size_t> MoveDisplayColumn(std::vector<size_t> display, size_t column, size_t anchor, bool after) {
    // Soltar una columna sobre sí misma no cambia nada.
    if (column == anchor) return display;
    auto from = std::find(display.begin(), display.end(), column);
    if (from == display.end() || std::find(display.begin(), display.end(), anchor) == display.end()) return display;
    // Se quita primero y se busca el ancla después: al borrar, las posiciones
    // posteriores se corren y un iterador obtenido antes quedaría inválido.
    display.erase(from);
    auto at = std::find(display.begin(), display.end(), anchor);
    display.insert(after ? std::next(at) : at, column);
    return display;
}

}  // namespace cola::ui
