#pragma once
// Comandos de la aplicación. Toda acción disparable por teclado, menú o botón
// es un Command; MainWindow::Execute(Command) los despacha.
// Para agregar un comando: añadirlo al enum, a kCommandTable (Commands.cpp)
// con su id estable (se guarda en config.json), su texto y su atajo por defecto.

#include "i18n/Strings.h"

namespace cola::input {

// El orden define cómo se listan en el panel de atajos (F1): agrupados por
// tema, empezando por lo que más se usa.
enum class Command {
    // Trabajos
    Pause,
    Resume,
    TogglePauseBoth,
    PrintAndRepause,
    Cancel,
    Restart,
    MoveUp,
    MoveDown,
    Redirect,
    // Impresora
    PausePrinter,
    ResumePrinter,
    ToggleAutoHold,
    PinPrinter,
    // Selección y navegación
    NavUp,
    NavDown,
    NavFirst,
    NavLast,
    SelectAll,
    ClearSelection,
    // Vista previa
    QuickLook,
    NextPage,
    PrevPage,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    TogglePane,
    ToggleSheet,
    // Buscar, impresoras y servidor
    Find,
    History,
    ChoosePrinter,
    NextPrinter,
    PrevPrinter,
    SortCycle,
    SortReverse,
    Connect,
    Refresh,
    // General
    ToggleTheme,
    Menu,
    Help,
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
