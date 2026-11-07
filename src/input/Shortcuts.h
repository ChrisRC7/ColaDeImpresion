#pragma once
// Atajos de teclado configurables: combinación de teclas <-> comando.
// Se serializan como texto legible ("Ctrl+Shift+P") en config.json.
//
// Flujo: el bucle de mensajes (app/Application) convierte cada WM_KEYDOWN en
// un KeyChord con FromKeyEvent, busca el comando con ShortcutMap::Find y, si
// hay uno, MainWindow::Execute lo ejecuta. Cada comando puede tener varias
// combinaciones, y una combinación pertenece a un solo comando.

#include <windows.h>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "input/Commands.h"

namespace cola::input {

// Una combinación de teclas: una tecla virtual (VK_*) más modificadores.
// No distingue Ctrl/Shift/Alt izquierdo y derecho. La tecla Windows no se usa.
struct KeyChord {
    UINT vk = 0;          // código de tecla virtual (normalizado); 0 = inválida
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool operator==(const KeyChord& o) const {
        return vk == o.vk && ctrl == o.ctrl && shift == o.shift && alt == o.alt;
    }
    // true si tiene tecla (una combinación vacía no es un atajo).
    bool IsValid() const { return vk != 0; }

    // Texto canónico "Ctrl+Shift+Alt+Tecla" (modificadores siempre en ese
    // orden, nombres en inglés); "" si no es válida. Es lo que se guarda en
    // config.json y lo que muestra el panel de ayuda.
    std::wstring ToString() const;
    // Interpreta un texto como "Ctrl+Shift+P", "ctrl + p", "Ctrl++" o
    // "Mayús+Supr" (acepta algunos nombres en español). nullopt si algún
    // modificador o la tecla no se reconocen.
    static std::optional<KeyChord> Parse(const std::wstring& text);

    // Combinación a partir de un WM_KEYDOWN (lee el estado de Ctrl/Shift/Alt).
    // GetKeyState da el estado en el momento en que se generó el mensaje que
    // se está procesando, no el actual: correcto aunque el usuario ya haya
    // soltado los modificadores. Debe llamarse desde el hilo de la UI.
    static KeyChord FromKeyEvent(UINT vk);
};

// Unifica teclas equivalentes (numpad + / -, dígitos del teclado numérico).
// Así "Ctrl+Plus" funciona con ambas teclas "+" y Ctrl+0 con ambos "0".
UINT NormalizeVirtualKey(UINT vk);

// Parsea una lista "PageDown, Right".
// Las partes inválidas se ignoran y las repetidas se quitan (el orden se
// conserva).
std::vector<KeyChord> ParseChordList(const std::wstring& text);
// Inversa de ParseChordList: "PageDown, Right".
std::wstring FormatChordList(const std::vector<KeyChord>& chords);

// Asignación comando -> combinaciones, con búsqueda inversa por combinación.
// La usa solo el hilo de la UI.
class ShortcutMap {
public:
    ShortcutMap();  // carga los predeterminados

    // Vuelve a los atajos de kCommandTable (descarta los personalizados).
    void ResetDefaults();
    // Aplica los atajos guardados por el usuario (id de comando -> lista de combinaciones).
    // Ids desconocidos y combinaciones inválidas se ignoran. Una combinación
    // asignada aquí se le quita a cualquier otro comando que la tuviera por
    // defecto. Un texto vacío deja el comando sin atajo.
    void ApplyOverrides(const std::map<std::wstring, std::wstring>& overrides);
    // Solo los que difieren de los predeterminados (para guardar).
    // Incluye también los comandos que perdieron su atajo predeterminado
    // porque otro se lo quitó (con lista vacía o la que les quedó).
    std::map<std::wstring, std::wstring> Overrides() const;

    // Comando asignado a la combinación, o nullopt. Recorre todos los
    // comandos (~40): suficiente para una pulsación de tecla.
    std::optional<Command> Find(const KeyChord& chord) const;
    // Combinaciones actuales del comando (puede estar vacía).
    const std::vector<KeyChord>& Get(Command command) const;

    // Asigna 'chord' como único atajo del comando. Si otro comando lo tenía,
    // se lo quita y devuelve ese comando (para avisar al usuario).
    std::optional<Command> Assign(Command command, const KeyChord& chord);
    // Deja el comando sin ningún atajo (sigue disponible por menú/botones).
    void Clear(Command command);

private:
    // Combinaciones de cada comando, indexadas por static_cast<int>(Command).
    std::array<std::vector<KeyChord>, kCommandCount> bindings_;
};

// Teclas que deben llegar al cuadro de texto cuando tiene el foco (no son atajos).
// Mientras se escribe en la búsqueda, una letra o Ctrl+C deben editar el
// texto y no disparar un comando; en cambio Arriba/Abajo, Enter, Esc, Tab y
// las F siguen funcionando como atajos. Nada con Alt se considera edición.
bool IsTextEditingChord(const KeyChord& chord);

}  // namespace cola::input
