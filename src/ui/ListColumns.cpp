#include "ui/ListColumns.h"

#include <algorithm>
#include <set>

#include "ui/Style.h"

namespace cola::ui {

std::vector<ColumnSlot> LayoutColumns(const std::vector<ListColumn>& columns,
                                      const std::map<std::wstring, int>& userWidths, int width) {
    const auto& style = Style::Get();
    const int gap = style.Px(12);
    const int left = style.Px(18);  // margen (el estado se ve en su columna, sin ícono aparte)
    const int right = width - style.Px(16);
    const int flexMin = style.Px(140);

    // Ancho elegido por el operador (px), o -1.
    auto user = [&](size_t i) {
        if (columns[i].id.empty()) return -1;
        auto it = userWidths.find(columns[i].id);
        return it == userWidths.end() ? -1 : style.Px(it->second);
    };

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
    const int flexWidth = flexCount ? std::max(flexMin, (right - left - fixed) / flexCount) : 0;

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
    for (const auto& id : saved)
        if (known.count(id) && seen.insert(id).second) order.push_back(id);
    // Las que faltan van detrás de la que las precede en el orden predeterminado.
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
    size_t next = 0;
    for (auto& id : merged)
        if (inView.count(id) && next < view.size()) id = view[next++];
    return merged;
}

std::vector<size_t> ColumnDisplayOrder(const std::vector<ListColumn>& columns, const std::vector<std::wstring>& order) {
    auto rank = [&order](const std::wstring& id) -> std::ptrdiff_t {
        if (id.empty()) return -1;
        auto it = std::find(order.begin(), order.end(), id);
        return it == order.end() ? -1 : it - order.begin();
    };
    // Las columnas que están en el orden se reparten en los lugares que ocupan
    // entre todas; las demás no se mueven.
    std::vector<size_t> ranked;
    for (size_t i = 0; i < columns.size(); ++i)
        if (rank(columns[i].id) >= 0) ranked.push_back(i);
    std::vector<size_t> sorted = ranked;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](size_t a, size_t b) { return rank(columns[a].id) < rank(columns[b].id); });
    std::vector<size_t> display;
    size_t next = 0;
    for (size_t i = 0; i < columns.size(); ++i)
        display.push_back(rank(columns[i].id) >= 0 ? sorted[next++] : i);
    return display;
}

std::vector<size_t> MoveDisplayColumn(std::vector<size_t> display, size_t column, size_t anchor, bool after) {
    if (column == anchor) return display;
    auto from = std::find(display.begin(), display.end(), column);
    if (from == display.end() || std::find(display.begin(), display.end(), anchor) == display.end()) return display;
    display.erase(from);
    auto at = std::find(display.begin(), display.end(), anchor);
    display.insert(after ? std::next(at) : at, column);
    return display;
}

}  // namespace cola::ui
