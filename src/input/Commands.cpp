#include "input/Commands.h"

#include <cwchar>

namespace cola::input {

using i18n::S;

namespace {
// El orden debe coincidir con el enum Command (ver el orden por grupos allí).
// El id es estable: es lo que se guarda en config.json cuando el usuario
// reasigna un atajo, así que no debe cambiarse aunque se mueva la fila.
const CommandInfo kCommandTable[] = {
    // --- Trabajos ---
    {Command::Pause, L"pause", S::CmdPause, L"Ctrl+P"},
    {Command::Resume, L"resume", S::CmdResume, L"Ctrl+R"},
    {Command::TogglePauseBoth, L"togglePauseBoth", S::CmdTogglePauseBoth, L"Ctrl+Alt+P, F8"},
    {Command::Cancel, L"cancel", S::CmdCancel, L"Delete"},
    {Command::Restart, L"restart", S::CmdRestart, L"Ctrl+I"},
    {Command::MoveUp, L"moveUp", S::CmdMoveUp, L"Ctrl+Up"},
    {Command::MoveDown, L"moveDown", S::CmdMoveDown, L"Ctrl+Down"},
    {Command::Redirect, L"redirect", S::CmdRedirect, L"Ctrl+T"},
    // --- Impresora ---
    {Command::PausePrinter, L"pausePrinter", S::CmdPausePrinter, L"Ctrl+Shift+P, F9"},
    {Command::ResumePrinter, L"resumePrinter", S::CmdResumePrinter, L"Ctrl+Shift+R"},
    {Command::ToggleAutoHold, L"toggleAutoHold", S::CmdToggleAutoHold, L"Ctrl+Shift+H"},
    {Command::PinPrinter, L"pinPrinter", S::CmdPinPrinter, L"Ctrl+Shift+F"},
    // --- Selección y navegación ---
    {Command::NavUp, L"navUp", S::CmdNavUp, L"Up"},
    {Command::NavDown, L"navDown", S::CmdNavDown, L"Down"},
    {Command::NavFirst, L"navFirst", S::CmdNavFirst, L"Home"},
    {Command::NavLast, L"navLast", S::CmdNavLast, L"End"},
    {Command::SelectAll, L"selectAll", S::CmdSelectAll, L"Ctrl+A"},
    {Command::ClearSelection, L"clearSelection", S::CmdClearSelection, L"Ctrl+Shift+A"},
    // --- Vista previa ---
    {Command::QuickLook, L"quickLook", S::CmdQuickLook, L"Space"},
    {Command::NextPage, L"nextPage", S::CmdNextPage, L"PageDown, Right"},
    {Command::PrevPage, L"prevPage", S::CmdPrevPage, L"PageUp, Left"},
    {Command::ZoomIn, L"zoomIn", S::CmdZoomIn, L"Ctrl+Plus"},
    {Command::ZoomOut, L"zoomOut", S::CmdZoomOut, L"Ctrl+Minus"},
    {Command::ZoomReset, L"zoomReset", S::CmdZoomReset, L"Ctrl+0"},
    {Command::TogglePane, L"togglePane", S::CmdTogglePane, L"Ctrl+B"},
    {Command::ToggleSheet, L"toggleSheet", S::CmdToggleSheet, L"F7"},
    // --- Buscar, impresoras y servidor ---
    {Command::Find, L"find", S::CmdFind, L"Ctrl+F"},
    {Command::History, L"history", S::CmdHistory, L"Ctrl+H"},
    {Command::ChoosePrinter, L"choosePrinter", S::CmdChoosePrinter, L"Ctrl+E"},
    {Command::NextPrinter, L"nextPrinter", S::CmdNextPrinter, L"Ctrl+Tab"},
    {Command::PrevPrinter, L"prevPrinter", S::CmdPrevPrinter, L"Ctrl+Shift+Tab"},
    {Command::SortCycle, L"sortCycle", S::CmdSortCycle, L"F6"},
    {Command::SortReverse, L"sortReverse", S::CmdSortReverse, L"Shift+F6"},
    {Command::Connect, L"connect", S::CmdConnect, L"Ctrl+O"},
    {Command::Refresh, L"refresh", S::CmdRefresh, L"F5"},
    // --- General ---
    {Command::ToggleTheme, L"toggleTheme", S::CmdToggleTheme, L"Ctrl+D"},
    {Command::Menu, L"menu", S::CmdMenu, L"F10"},
    {Command::Help, L"help", S::CmdHelp, L"F1"},
};
static_assert(sizeof(kCommandTable) / sizeof(kCommandTable[0]) == kCommandCount,
              "kCommandTable debe tener una entrada por Command");
}  // namespace

const CommandInfo& GetCommandInfo(Command command) { return kCommandTable[static_cast<int>(command)]; }

bool CommandFromId(const wchar_t* id, Command& out) {
    for (const auto& info : kCommandTable) {
        if (std::wcscmp(info.id, id) == 0) {
            out = info.command;
            return true;
        }
    }
    return false;
}

}  // namespace cola::input
