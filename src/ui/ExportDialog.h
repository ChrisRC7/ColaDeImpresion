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

// Un día del historial que tiene al menos un registro (lo arma MainWindow a
// partir de config::History). Es lo que se ofrece en "Desde" y "Hasta".
struct ExportDay {
    std::wstring label;  // "09/10/2026"
    std::uint32_t records = 0;  // cantidad de registros de ese día
};

// Lo que eligió el usuario. MainWindow::ExportHistory arma una nueva cada vez
// (todo el historial, con el rango propuesto de punta a punta).
struct ExportChoice {
    bool all = true;  // true = todo el historial (from/to se ignoran)
    size_t from = 0;  // índices en days (del más antiguo al más reciente), from <= to
    size_t to = 0;
};

// Deja from <= to y ambos dentro de days; si days está vacío, vuelve a "todo".
// fromChanged indica qué extremo acaba de mover el usuario: si los extremos
// se cruzan, ese manda y el otro se ajusta a él.
void NormalizeExportChoice(ExportChoice& choice, size_t dayCount, bool fromChanged);

// Registros que exportaría la elección (suma de los días incluidos).
std::uint32_t ExportChoiceRecords(const std::vector<ExportDay>& days, const ExportChoice& choice);

// Muestra el diálogo (days del más antiguo al más reciente). true = exportar
// con la elección que queda en choice; false = canceló.
bool ShowExportDialog(HWND owner, const std::vector<ExportDay>& days, ExportChoice& choice);

}  // namespace cola::ui
