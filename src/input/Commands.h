#pragma once
// Comandos de la aplicación. Toda acción disparable por teclado, menú o botón
// es un Command; MainWindow::Execute(Command) los despacha.
// Para agregar un comando: añadirlo al enum, a kCommandTable (Commands.cpp)
// con su id estable (se guarda en config.json), su texto y su atajo por defecto.

#include "i18n/Strings.h"

namespace cola::input {

enum class Command {
    Pause,
    Resume,
    Cancel,
    Restart,
    NavUp,
    NavDown,
    NavFirst,
    NavLast,
    MoveUp,
    MoveDown,
    QuickLook,
    ToggleTheme,
    Find,
    Help,
    Refresh,
    Redirect,
    NextPrinter,
    PrevPrinter,
    ChoosePrinter,
    Connect,
    History,
    NextPage,
    PrevPage,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    PausePrinter,
    ResumePrinter,
    SelectAll,
    Menu,
    TogglePane,
    ToggleAutoHold,
    TogglePauseBoth,
    ClearSelection,
    Count_
};

constexpr int kCommandCount = static_cast<int>(Command::Count_);

struct CommandInfo {
    Command command;
    const wchar_t* id;          // identificador estable para config.json
    i18n::S label;              // texto para el panel de ayuda
    const wchar_t* defaultKeys; // combinaciones separadas por coma
};

const CommandInfo& GetCommandInfo(Command command);
bool CommandFromId(const wchar_t* id, Command& out);

}  // namespace cola::input
