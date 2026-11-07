// Implementación de los atajos de teclado: nombres de teclas, parseo y
// formato de combinaciones, y la tabla comando -> combinaciones (ver Shortcuts.h).

#include "input/Shortcuts.h"

#include <algorithm>

#include "util/Strings.h"

namespace cola::input {

namespace {

// Par tecla virtual <-> nombre de texto.
struct KeyName {
    UINT vk;
    const wchar_t* name;
};

// Nombres canónicos (en inglés) de las teclas que no son letra, dígito ni F1-F24.
// Son los que se escriben al guardar y se muestran en la ayuda.
const KeyName kKeyNames[] = {
    {VK_UP, L"Up"},           {VK_DOWN, L"Down"},        {VK_LEFT, L"Left"},        {VK_RIGHT, L"Right"},
    {VK_HOME, L"Home"},       {VK_END, L"End"},          {VK_PRIOR, L"PageUp"},     {VK_NEXT, L"PageDown"},
    {VK_DELETE, L"Delete"},   {VK_INSERT, L"Insert"},    {VK_BACK, L"Backspace"},   {VK_RETURN, L"Enter"},
    {VK_SPACE, L"Space"},     {VK_TAB, L"Tab"},          {VK_ESCAPE, L"Escape"},    {VK_OEM_PLUS, L"Plus"},
    {VK_OEM_MINUS, L"Minus"}, {VK_APPS, L"Apps"},        {VK_OEM_COMMA, L"Comma"},  {VK_OEM_PERIOD, L"Period"},
    {VK_PAUSE, L"Pause"},     {VK_SNAPSHOT, L"PrintScreen"},
};

// Alias aceptados al parsear.
// (Abreviaturas comunes y nombres en español, para quien edite config.json a
// mano. Nunca se escriben: al guardar se usa el nombre canónico.)
const KeyName kKeyAliases[] = {
    {VK_DELETE, L"Del"}, {VK_DELETE, L"Supr"}, {VK_PRIOR, L"PgUp"}, {VK_NEXT, L"PgDn"},
    {VK_ESCAPE, L"Esc"}, {VK_RETURN, L"Return"}, {VK_OEM_PLUS, L"+"}, {VK_OEM_MINUS, L"-"},
    {VK_UP, L"Arriba"}, {VK_DOWN, L"Abajo"}, {VK_LEFT, L"Izquierda"}, {VK_RIGHT, L"Derecha"},
    {VK_SPACE, L"Espacio"}, {VK_HOME, L"Inicio"}, {VK_END, L"Fin"},
};

// Nombre de una tecla virtual. En Windows el código VK de las letras y los
// dígitos es su propio carácter ASCII en mayúscula ('A' = 0x41), por eso se
// devuelve tal cual. Una tecla sin nombre conocido sale como "VK<número>",
// que VkFromName también entiende (así cualquier tecla se puede guardar).
std::wstring VkName(UINT vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return util::Format(L"F%u", vk - VK_F1 + 1);
    for (const auto& k : kKeyNames)
        if (k.vk == vk) return k.name;
    return util::Format(L"VK%u", vk);
}

// Inversa de VkName (sin distinguir mayúsculas). 0 si no se reconoce.
UINT VkFromName(const std::wstring& name) {
    if (name.empty()) return 0;
    // Un solo carácter: letra o dígito (las letras se pasan a mayúscula
    // porque ese es su código VK).
    if (name.size() == 1) {
        wchar_t c = static_cast<wchar_t>(towupper(name[0]));
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return c;
    }
    // "F1".."F24" (como mucho 3 caracteres). _wtoi da 0 si no hay número,
    // así "F" o "Fx" no coinciden aquí y siguen buscándose como nombre.
    if ((name[0] == L'F' || name[0] == L'f') && name.size() <= 3) {
        int n = _wtoi(name.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + static_cast<UINT>(n) - 1;
    }
    // Nombres canónicos y luego alias.
    for (const auto& k : kKeyNames)
        if (util::EqualsInsensitive(name, k.name)) return k.vk;
    for (const auto& k : kKeyAliases)
        if (util::EqualsInsensitive(name, k.name)) return k.vk;
    // Forma numérica "VK123" (la que produce VkName para teclas sin nombre).
    if (name.size() > 2 && (name.rfind(L"VK", 0) == 0 || name.rfind(L"vk", 0) == 0)) return static_cast<UINT>(_wtoi(name.c_str() + 2));
    return 0;
}

}  // namespace

UINT NormalizeVirtualKey(UINT vk) {
    // "+" y "-" del teclado numérico -> las del teclado principal.
    if (vk == VK_ADD) return VK_OEM_PLUS;
    if (vk == VK_SUBTRACT) return VK_OEM_MINUS;
    // Dígitos del teclado numérico (con Bloq Num) -> '0'..'9'.
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return '0' + (vk - VK_NUMPAD0);
    return vk;
}

// Siempre en el orden Ctrl, Shift, Alt para que el texto sea canónico y dos
// combinaciones iguales se escriban igual.
std::wstring KeyChord::ToString() const {
    if (!IsValid()) return L"";
    std::wstring s;
    if (ctrl) s += L"Ctrl+";
    if (shift) s += L"Shift+";
    if (alt) s += L"Alt+";
    return s + VkName(vk);
}

std::optional<KeyChord> KeyChord::Parse(const std::wstring& textIn) {
    std::wstring text = util::Trim(textIn);
    if (text.empty()) return std::nullopt;
    KeyChord chord;
    // "Ctrl++" : el último '+' es la tecla.
    // Se parte el texto en "+" a mano (no con Split) porque "+" es a la vez
    // separador y una tecla válida. Un "+" que aparece justo donde empezaría
    // una parte (dos "+" seguidos, o al principio) se toma como la tecla "+";
    // si lo sigue otro "+", ese segundo es su separador y se salta.
    // Ej.: "Ctrl+Shift+P" -> {Ctrl, Shift, P}; "Ctrl++" -> {Ctrl, +}.
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start < text.size()) {
        size_t plus = text.find(L'+', start);
        if (plus == start) {  // '+' literal como tecla
            parts.push_back(L"+");
            start = plus + 1;
            if (start < text.size() && text[start] == L'+') ++start;
            continue;
        }
        if (plus == std::wstring::npos) plus = text.size();
        // Trim permite espacios alrededor de "+" ("Ctrl + P").
        parts.push_back(util::Trim(text.substr(start, plus - start)));
        start = plus + 1;
    }
    if (parts.empty()) return std::nullopt;
    // Todas las partes menos la última son modificadores; cualquier otra
    // palabra invalida la combinación entera.
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const auto& m = parts[i];
        if (util::EqualsInsensitive(m, L"Ctrl") || util::EqualsInsensitive(m, L"Control")) chord.ctrl = true;
        else if (util::EqualsInsensitive(m, L"Shift") || util::EqualsInsensitive(m, L"Mayús")) chord.shift = true;
        else if (util::EqualsInsensitive(m, L"Alt")) chord.alt = true;
        else return std::nullopt;
    }
    // La última parte es la tecla.
    chord.vk = VkFromName(parts.back());
    if (!chord.vk) return std::nullopt;
    return chord;
}

KeyChord KeyChord::FromKeyEvent(UINT vk) {
    KeyChord chord;
    chord.vk = NormalizeVirtualKey(vk);
    // Bit alto (0x8000) = tecla presionada. VK_MENU es Alt.
    chord.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    chord.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    chord.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    return chord;
}

std::vector<KeyChord> ParseChordList(const std::wstring& text) {
    std::vector<KeyChord> chords;
    // Las combinaciones se separan con coma (por eso la tecla coma se escribe
    // "Comma" y no ",").
    for (const auto& part : util::Split(text, L',')) {
        if (auto chord = KeyChord::Parse(part)) {
            if (std::find(chords.begin(), chords.end(), *chord) == chords.end()) chords.push_back(*chord);
        }
    }
    return chords;
}

std::wstring FormatChordList(const std::vector<KeyChord>& chords) {
    std::wstring s;
    for (const auto& c : chords) {
        if (!s.empty()) s += L", ";
        s += c.ToString();
    }
    return s;
}

ShortcutMap::ShortcutMap() { ResetDefaults(); }

void ShortcutMap::ResetDefaults() {
    // Cada comando recibe las combinaciones de su defaultKeys en kCommandTable.
    for (int i = 0; i < kCommandCount; ++i)
        bindings_[static_cast<size_t>(i)] = ParseChordList(GetCommandInfo(static_cast<Command>(i)).defaultKeys);
}

void ShortcutMap::ApplyOverrides(const std::map<std::wstring, std::wstring>& overrides) {
    for (const auto& [id, keys] : overrides) {
        Command command;
        // Id de un comando que ya no existe (config de otra versión): se ignora.
        if (!CommandFromId(id.c_str(), command)) continue;
        std::vector<KeyChord> chords = ParseChordList(keys);
        // Quita esas combinaciones de otros comandos para evitar duplicados.
        // (Incluye al propio comando; enseguida se reemplaza su lista entera.)
        for (const auto& chord : chords)
            for (auto& list : bindings_) list.erase(std::remove(list.begin(), list.end(), chord), list.end());
        bindings_[static_cast<size_t>(command)] = std::move(chords);
    }
}

std::map<std::wstring, std::wstring> ShortcutMap::Overrides() const {
    std::map<std::wstring, std::wstring> result;
    // Se compara cada lista con la predeterminada (mismo orden incluido); solo
    // las distintas se guardan. Así, si cambian los predeterminados en una
    // versión nueva, los operadores que no tocaron un atajo reciben el nuevo.
    for (int i = 0; i < kCommandCount; ++i) {
        const auto& info = GetCommandInfo(static_cast<Command>(i));
        if (!(bindings_[static_cast<size_t>(i)] == ParseChordList(info.defaultKeys)))
            result[info.id] = FormatChordList(bindings_[static_cast<size_t>(i)]);
    }
    return result;
}

std::optional<Command> ShortcutMap::Find(const KeyChord& chord) const {
    for (int i = 0; i < kCommandCount; ++i) {
        const auto& list = bindings_[static_cast<size_t>(i)];
        if (std::find(list.begin(), list.end(), chord) != list.end()) return static_cast<Command>(i);
    }
    return std::nullopt;
}

const std::vector<KeyChord>& ShortcutMap::Get(Command command) const {
    return bindings_[static_cast<size_t>(command)];
}

std::optional<Command> ShortcutMap::Assign(Command command, const KeyChord& chord) {
    std::optional<Command> previous;
    // Se quita la combinación de cualquier otro comando (como mucho uno la
    // tiene, porque se mantiene la regla "una combinación, un comando").
    for (int i = 0; i < kCommandCount; ++i) {
        if (static_cast<Command>(i) == command) continue;
        auto& list = bindings_[static_cast<size_t>(i)];
        auto it = std::find(list.begin(), list.end(), chord);
        if (it != list.end()) {
            list.erase(it);
            previous = static_cast<Command>(i);
        }
    }
    // Reemplaza todas las combinaciones anteriores del comando por esta sola.
    bindings_[static_cast<size_t>(command)] = {chord};
    return previous;
}

void ShortcutMap::Clear(Command command) { bindings_[static_cast<size_t>(command)].clear(); }

bool IsTextEditingChord(const KeyChord& c) {
    // Alt+... nunca edita texto (son atajos o menús).
    if (c.alt) return false;
    if (!c.ctrl) {
        // Sin Ctrl: todo es escritura salvo navegación vertical, Esc, Enter y teclas de función.
        if (c.vk == VK_UP || c.vk == VK_DOWN || c.vk == VK_PRIOR || c.vk == VK_NEXT || c.vk == VK_ESCAPE ||
            c.vk == VK_RETURN || c.vk == VK_TAB || (c.vk >= VK_F1 && c.vk <= VK_F24))
            return false;
        return true;
    }
    // Con Ctrl: atajos estándar de edición de texto.
    // (Seleccionar todo, copiar, pegar, cortar, deshacer, rehacer, moverse y
    // borrar por palabras, ir al principio/final.) Por eso Ctrl+A selecciona
    // el texto de la búsqueda y no todos los trabajos mientras tiene el foco.
    switch (c.vk) {
        case 'A': case 'C': case 'V': case 'X': case 'Z': case 'Y':
        case VK_LEFT: case VK_RIGHT: case VK_BACK: case VK_DELETE: case VK_HOME: case VK_END:
            return true;
        default: return false;
    }
}

}  // namespace cola::input
