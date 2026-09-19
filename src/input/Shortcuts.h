#pragma once
// Atajos de teclado configurables: combinación de teclas <-> comando.
// Se serializan como texto legible ("Ctrl+Shift+P") en config.json.

#include <windows.h>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "input/Commands.h"

namespace cola::input {

struct KeyChord {
    UINT vk = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool operator==(const KeyChord& o) const {
        return vk == o.vk && ctrl == o.ctrl && shift == o.shift && alt == o.alt;
    }
    bool IsValid() const { return vk != 0; }

    std::wstring ToString() const;
    static std::optional<KeyChord> Parse(const std::wstring& text);

    // Combinación a partir de un WM_KEYDOWN (lee el estado de Ctrl/Shift/Alt).
    static KeyChord FromKeyEvent(UINT vk);
};

// Unifica teclas equivalentes (numpad + / -, dígitos del teclado numérico).
UINT NormalizeVirtualKey(UINT vk);

// Parsea una lista "PageDown, Right".
std::vector<KeyChord> ParseChordList(const std::wstring& text);
std::wstring FormatChordList(const std::vector<KeyChord>& chords);

class ShortcutMap {
public:
    ShortcutMap();  // carga los predeterminados

    void ResetDefaults();
    // Aplica los atajos guardados por el usuario (id de comando -> lista de combinaciones).
    void ApplyOverrides(const std::map<std::wstring, std::wstring>& overrides);
    // Solo los que difieren de los predeterminados (para guardar).
    std::map<std::wstring, std::wstring> Overrides() const;

    std::optional<Command> Find(const KeyChord& chord) const;
    const std::vector<KeyChord>& Get(Command command) const;

    // Asigna 'chord' como único atajo del comando. Si otro comando lo tenía,
    // se lo quita y devuelve ese comando (para avisar al usuario).
    std::optional<Command> Assign(Command command, const KeyChord& chord);
    void Clear(Command command);

private:
    std::array<std::vector<KeyChord>, kCommandCount> bindings_;
};

// Teclas que deben llegar al cuadro de texto cuando tiene el foco (no son atajos).
bool IsTextEditingChord(const KeyChord& chord);

}  // namespace cola::input
