#pragma once
// Diálogo modal "Exportar historial a Excel": todo el historial o solo un
// rango de fechas. "Desde" y "Hasta" ofrecen únicamente los días que tienen
// registros (no se puede elegir un rango vacío). Pintado con la paleta de la
// app (claro/oscuro) y manejable con el teclado: Tab / Mayús+Tab recorren,
// ↑/↓ cambian la opción o el día, Espacio/F4/Alt+↓ abren la lista de días,
// Enter exporta y Esc cancela.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cola::ui {

struct ExportDay {
    std::wstring label;  // "09/10/2026"
    std::uint32_t records = 0;
};

struct ExportChoice {
    bool all = true;
    size_t from = 0;  // índices en days (del más antiguo al más reciente), from <= to
    size_t to = 0;
};

// Deja from <= to y ambos dentro de days; si days está vacío, vuelve a "todo".
void NormalizeExportChoice(ExportChoice& choice, size_t dayCount, bool fromChanged);

// Registros que exportaría la elección.
std::uint32_t ExportChoiceRecords(const std::vector<ExportDay>& days, const ExportChoice& choice);

// Muestra el diálogo (days del más antiguo al más reciente). true = exportar
// con la elección que queda en choice; false = canceló.
bool ShowExportDialog(HWND owner, const std::vector<ExportDay>& days, ExportChoice& choice);

}  // namespace cola::ui
