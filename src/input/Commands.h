#pragma once
// Comandos de la aplicación. Toda acción disparable por teclado, menú o botón
// es un Command; MainWindow::Execute(Command) los despacha.
// Para agregar un comando: añadirlo al enum, a kCommandTable (Commands.cpp)
// con su id estable (se guarda en config.json), su texto y su atajo por defecto.

#include "i18n/Strings.h"

namespace cola::input {

// El orden define cómo se listan en el panel de atajos (F1): agrupados por
// tema, empezando por lo que más se usa.
// El valor numérico de cada comando se usa como índice (en kCommandTable y en
// ShortcutMap), así que los valores van de 0 a Count_ - 1 sin huecos. Lo que
// se guarda en config.json es el id de texto, no el número: reordenar el enum
// no rompe las configuraciones guardadas.
enum class Command {
    // Trabajos (actúan sobre los trabajos seleccionados)
    Pause,            // pausar; si todos están en pausa, reanudar (Ctrl+P alterna, D18)
    Resume,           // reanudar
    TogglePauseBoth,  // alternar el trabajo y su impresora a la vez (D18)
    PrintAndRepause,  // imprimir y volver a pausar la impresora al terminar (D35)
    Cancel,           // cancelar (con confirmación si confirmCancel)
    Restart,          // reiniciar el trabajo desde el principio
    MoveUp,           // subir prioridad en el spooler (reordenar la cola real)
    MoveDown,         // bajar prioridad en el spooler
    Redirect,         // reenviar a otra impresora (D12)
    // Impresora
    PausePrinter,     // alternar la pausa de la impresora (todas en la vista "Todas")
    ResumePrinter,    // reanudar la impresora
    ToggleAutoHold,   // activar/desactivar la retención automática (D17)
    PinPrinter,       // fijar/quitar la impresora de la barra de acceso rápido (D19)
    // Selección y navegación
    NavUp,
    NavDown,
    NavFirst,
    NavLast,
    SelectAll,
    ClearSelection,
    // Vista previa
    QuickLook,        // mostrar la vista previa del trabajo seleccionado
    NextPage,
    PrevPage,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    TogglePane,       // mostrar/ocultar el panel de detalles (D33)
    ToggleSheet,      // alternar vista previa / ficha "Configuración" del trabajo
    // Buscar, impresoras y servidor
    Find,             // enfocar el cuadro de búsqueda
    History,          // alternar entre la cola y el historial
    ExportHistory,    // exportar el historial a Excel (D43)
    ChoosePrinter,    // elegir la impresora activa
    NextPrinter,
    PrevPrinter,
    SortCycle,        // pasar al siguiente criterio de orden (D20)
    SortReverse,      // invertir el orden
    Connect,          // conectar a otro servidor
    Refresh,          // forzar una actualización de la cola
    // General
    ToggleTheme,      // alternar modo claro/oscuro
    Menu,             // abrir el menú principal
    Help,             // panel de atajos
    Count_            // no es un comando: cantidad de comandos
};

// Cantidad de comandos (tamaño de las tablas indexadas por Command).
constexpr int kCommandCount = static_cast<int>(Command::Count_);

// Datos fijos de un comando.
struct CommandInfo {
    Command command;
    const wchar_t* id;          // identificador estable para config.json
    i18n::S label;              // texto para el panel de ayuda
    const wchar_t* defaultKeys; // combinaciones separadas por coma
                                // (formato de KeyChord::Parse, p. ej. "Ctrl+P, F9")
};

// Información de un comando (acceso directo por índice). `command` debe ser
// un comando real, no Count_.
const CommandInfo& GetCommandInfo(Command command);
// Busca un comando por su id (comparación exacta, distingue mayúsculas).
// false si no existe (p. ej. un id de otra versión en config.json).
bool CommandFromId(const wchar_t* id, Command& out);

}  // namespace cola::input
