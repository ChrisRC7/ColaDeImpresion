#include "input/Commands.h"

#include <cwchar>

namespace cola::input {

using i18n::S;

namespace {
// El orden debe coincidir con el enum Command.
const CommandInfo kCommandTable[] = {
    {Command::Pause, L"pause", S::CmdPause, L"Ctrl+P"},
    {Command::Resume, L"resume", S::CmdResume, L"Ctrl+R"},
    {Command::Cancel, L"cancel", S::CmdCancel, L"Delete"},
    {Command::Restart, L"restart", S::CmdRestart, L"Ctrl+I"},
    {Command::NavUp, L"navUp", S::CmdNavUp, L"Up"},
    {Command::NavDown, L"navDown", S::CmdNavDown, L"Down"},
    {Command::NavFirst, L"navFirst", S::CmdNavFirst, L"Home"},
    {Command::NavLast, L"navLast", S::CmdNavLast, L"End"},
    {Command::MoveUp, L"moveUp", S::CmdMoveUp, L"Ctrl+Up"},
    {Command::MoveDown, L"moveDown", S::CmdMoveDown, L"Ctrl+Down"},
    {Command::QuickLook, L"quickLook", S::CmdQuickLook, L"Space"},
    {Command::ToggleTheme, L"toggleTheme", S::CmdToggleTheme, L"Ctrl+D"},
    {Command::Find, L"find", S::CmdFind, L"Ctrl+F"},
    {Command::Help, L"help", S::CmdHelp, L"F1"},
    {Command::Refresh, L"refresh", S::CmdRefresh, L"F5"},
    {Command::Redirect, L"redirect", S::CmdRedirect, L"Ctrl+T"},
    {Command::NextPrinter, L"nextPrinter", S::CmdNextPrinter, L"Ctrl+Tab"},
    {Command::PrevPrinter, L"prevPrinter", S::CmdPrevPrinter, L"Ctrl+Shift+Tab"},
    {Command::ChoosePrinter, L"choosePrinter", S::CmdChoosePrinter, L"Ctrl+E"},
    {Command::Connect, L"connect", S::CmdConnect, L"Ctrl+O"},
    {Command::History, L"history", S::CmdHistory, L"Ctrl+H"},
    {Command::NextPage, L"nextPage", S::CmdNextPage, L"PageDown, Right"},
    {Command::PrevPage, L"prevPage", S::CmdPrevPage, L"PageUp, Left"},
    {Command::ZoomIn, L"zoomIn", S::CmdZoomIn, L"Ctrl+Plus"},
    {Command::ZoomOut, L"zoomOut", S::CmdZoomOut, L"Ctrl+Minus"},
    {Command::ZoomReset, L"zoomReset", S::CmdZoomReset, L"Ctrl+0"},
    {Command::PausePrinter, L"pausePrinter", S::CmdPausePrinter, L"Ctrl+Shift+P"},
    {Command::ResumePrinter, L"resumePrinter", S::CmdResumePrinter, L"Ctrl+Shift+R"},
    {Command::SelectAll, L"selectAll", S::CmdSelectAll, L"Ctrl+A"},
    {Command::Menu, L"menu", S::CmdMenu, L"F10"},
    {Command::TogglePane, L"togglePane", S::CmdTogglePane, L"Ctrl+B"},
    {Command::ToggleAutoHold, L"toggleAutoHold", S::CmdToggleAutoHold, L"Ctrl+Shift+H"},
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
