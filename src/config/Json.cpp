// Implementación del JSON mínimo (ver Json.h): parser recursivo descendente,
// serializador y lectura/escritura de archivos.

#include "config/Json.h"

#include <windows.h>

#include <cmath>
#include <cstdio>

#include "util/Strings.h"

namespace cola::config {

namespace {

// Valores vacíos compartidos que devuelven los accesores de solo lectura
// cuando el tipo no coincide o la clave no existe (se devuelve una referencia,
// así que tienen que vivir todo el programa).
const JsonValue kNull;
const JsonValue::Array kEmptyArray;
const JsonValue::Object kEmptyObject;

// Parser recursivo descendente sobre el texto UTF-8. Cada Parse* empieza en
// el primer carácter de su valor y deja pos_ justo después de él. Devuelven
// false ante cualquier error de sintaxis (sin recuperación).
class Parser {
public:
    // Guarda una referencia: el texto debe vivir mientras dure el parseo.
    explicit Parser(const std::string& text) : s_(text) {}

    // Parsea un único valor que ocupe todo el texto (salvo espacios).
    // `out` solo se asigna si todo es válido.
    bool Run(JsonValue& out, std::wstring* error) {
        SkipWs();
        if (!ParseValue(out, 0)) {
            if (error) *error = util::Format(L"JSON inválido cerca de la posición %zu", pos_);
            return false;
        }
        SkipWs();
        if (pos_ != s_.size()) {
            if (error) *error = util::Format(L"Datos extra en la posición %zu", pos_);
            return false;
        }
        return true;
    }

private:
    // Salta los cuatro espacios en blanco que admite JSON.
    void SkipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    // Si en pos_ empieza `literal` (true/false/null) lo consume y da true.
    // compare() tolera que el texto restante sea más corto.
    bool Match(const char* literal) {
        size_t len = std::char_traits<char>::length(literal);
        if (s_.compare(pos_, len, literal) != 0) return false;
        pos_ += len;
        return true;
    }

    // Decide el tipo de valor por su primer carácter. `depth` limita el
    // anidamiento a 64 niveles para que un archivo dañado o malicioso
    // ("[[[[...") no desborde la pila con la recursión.
    bool ParseValue(JsonValue& out, int depth) {
        if (depth > 64 || pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return ParseObject(out, depth);
        if (c == '[') return ParseArray(out, depth);
        if (c == '"') {
            std::wstring str;
            if (!ParseString(str)) return false;
            out = JsonValue(std::move(str));
            return true;
        }
        if (Match("true")) { out = JsonValue(true); return true; }
        if (Match("false")) { out = JsonValue(false); return true; }
        if (Match("null")) { out = JsonValue(); return true; }
        // Cualquier otra cosa tiene que ser un número.
        return ParseNumber(out);
    }

    // Número. Es más permisivo que el estándar (acepta "+1", "01" o ".5"):
    // toma la secuencia más larga de caracteres numéricos y la valida con
    // strtod, que debe consumirla entera. Lo importante es aceptar todo JSON
    // válido y rechazar basura, no validar al pie de la letra.
    bool ParseNumber(JsonValue& out) {
        size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) ++pos_;
        while (pos_ < s_.size() && (isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.' ||
                                    s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '-' || s_[pos_] == '+'))
            ++pos_;
        if (start == pos_) return false;
        std::string num = s_.substr(start, pos_ - start);
        char* end = nullptr;
        // strtod usa la configuración regional "C" del proceso (punto
        // decimal), que la app no cambia.
        double v = strtod(num.c_str(), &end);
        if (!end || *end != '\0') return false;
        out = JsonValue(v);
        return true;
    }

    // Agrega un punto de código Unicode (de un escape \uXXXX) codificado en
    // UTF-8: 1 a 4 bytes según su valor.
    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    // Lee exactamente 4 dígitos hexadecimales (el XXXX de \uXXXX).
    bool ParseHex4(unsigned& value) {
        if (pos_ + 4 > s_.size()) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    // Cadena entre comillas. Se acumula en UTF-8 (los bytes del archivo se
    // copian tal cual y los escapes se decodifican) y al cerrar se convierte
    // una sola vez a UTF-16.
    bool ParseString(std::wstring& out) {
        if (s_[pos_] != '"') return false;
        ++pos_;
        std::string utf8;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') {
                out = util::FromUtf8(utf8);
                return true;
            }
            // Carácter normal (incluidos los bytes de secuencias multibyte,
            // que nunca valen '"' ni barra invertida).
            if (c != '\\') {
                utf8 += c;
                continue;
            }
            // Secuencia de escape: barra invertida + un carácter.
            if (pos_ >= s_.size()) return false;
            char e = s_[pos_++];
            switch (e) {
                case '"': utf8 += '"'; break;
                case '\\': utf8 += '\\'; break;
                case '/': utf8 += '/'; break;
                case 'b': utf8 += '\b'; break;
                case 'f': utf8 += '\f'; break;
                case 'n': utf8 += '\n'; break;
                case 'r': utf8 += '\r'; break;
                case 't': utf8 += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!ParseHex4(cp)) return false;
                    // Un sustituto alto (D800-DBFF) seguido de otro \uXXXX
                    // forma un par UTF-16: se combinan en un solo punto de
                    // código > 0xFFFF (emojis, etc.). No se valida que el
                    // segundo sea un sustituto bajo; un sustituto suelto se
                    // codifica tal cual y FromUtf8 lo cambia por U+FFFD.
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= s_.size() && s_[pos_] == '\\' &&
                        s_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        unsigned low = 0;
                        if (!ParseHex4(low)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(utf8, cp);
                    break;
                }
                // Escape desconocido: JSON inválido.
                default: return false;
            }
        }
        // Se llegó al final del texto sin la comilla de cierre.
        return false;
    }

    // Arreglo: '[' (valor (',' valor)*)? ']'. No admite coma final.
    bool ParseArray(JsonValue& out, int depth) {
        ++pos_;  // '['
        JsonValue::Array arr;
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            out = JsonValue(std::move(arr));
            return true;
        }
        for (;;) {
            SkipWs();
            JsonValue item;
            if (!ParseValue(item, depth + 1)) return false;
            arr.push_back(std::move(item));
            SkipWs();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; break; }
            return false;
        }
        out = JsonValue(std::move(arr));
        return true;
    }

    // Objeto: '{' (cadena ':' valor (',' ...)*)? '}'. Las claves deben ser
    // cadenas; una clave repetida se queda con el último valor.
    bool ParseObject(JsonValue& out, int depth) {
        ++pos_;  // '{'
        JsonValue::Object obj;
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            out = JsonValue(std::move(obj));
            return true;
        }
        for (;;) {
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') return false;
            std::wstring key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_;
            SkipWs();
            JsonValue value;
            if (!ParseValue(value, depth + 1)) return false;
            obj[key] = std::move(value);
            SkipWs();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; break; }
            return false;
        }
        out = JsonValue(std::move(obj));
        return true;
    }

    // Texto completo y posición actual (en bytes) dentro de él.
    const std::string& s_;
    size_t pos_ = 0;
};

// Escribe una cadena JSON entre comillas. Se escapan las comillas, la barra
// invertida y los caracteres de control (con su forma corta o \u00XX); todo
// lo demás, incluidos acentos y ñ, va como UTF-8 literal, así config.json e
// historial.json se leen bien a simple vista.
void EscapeString(std::string& out, const std::wstring& value) {
    out += '"';
    for (char c : util::ToUtf8(value)) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

// Salto de línea + indentación del nivel `depth` (nada en modo compacto).
void NewLine(std::string& out, int indent, int depth) {
    if (indent <= 0) return;
    out += '\n';
    out.append(static_cast<size_t>(indent * depth), ' ');
}

}  // namespace

const JsonValue::Array& JsonValue::AsArray() const { return IsArray() ? *array_ : kEmptyArray; }
const JsonValue::Object& JsonValue::AsObject() const { return IsObject() ? *object_ : kEmptyObject; }

// Copia al escribir: si otro JsonValue comparte el mismo arreglo
// (use_count > 1), se hace una copia propia antes de devolverlo para que la
// modificación no se vea en la otra copia. Si no es arreglo, se reemplaza el
// valor entero por un arreglo vacío (se pierde lo anterior).
JsonValue::Array& JsonValue::MutableArray() {
    if (!IsArray()) {
        *this = JsonValue(Array{});
    } else if (array_.use_count() > 1) {
        array_ = std::make_shared<Array>(*array_);
    }
    return *array_;
}

// Igual que MutableArray, para objetos.
JsonValue::Object& JsonValue::MutableObject() {
    if (!IsObject()) {
        *this = JsonValue(Object{});
    } else if (object_.use_count() > 1) {
        object_ = std::make_shared<Object>(*object_);
    }
    return *object_;
}

const JsonValue& JsonValue::operator[](const std::wstring& key) const {
    if (!IsObject()) return kNull;
    auto it = object_->find(key);
    return it == object_->end() ? kNull : it->second;
}

// Versión modificable: pasa por MutableObject (copia al escribir) y el
// operator[] de std::map, que crea la clave con Null si no existe.
JsonValue& JsonValue::operator[](const std::wstring& key) { return MutableObject()[key]; }

bool JsonValue::Has(const std::wstring& key) const { return IsObject() && object_->count(key) > 0; }

std::string JsonValue::Serialize(int indent) const {
    std::string out;
    SerializeTo(out, indent, 0);
    if (indent > 0) out += '\n';
    return out;
}

void JsonValue::SerializeTo(std::string& out, int indent, int depth) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            // Enteros (lo más común: ids, tamaños, horas Unix) sin decimales
            // ni exponente; el resto con 17 cifras significativas, que
            // alcanzan para recuperar el double exacto al volver a leerlo.
            // NaN/infinito no existen en JSON; con %.17g saldrían como
            // "nan"/"inf" (no se espera que aparezcan).
            char buf[64];
            if (std::isfinite(number_) && number_ == std::floor(number_) && std::fabs(number_) < 1e15)
                snprintf(buf, sizeof(buf), "%.0f", number_);
            else
                snprintf(buf, sizeof(buf), "%.17g", number_);
            out += buf;
            break;
        }
        case Type::String: EscapeString(out, string_); break;
        case Type::Array: {
            // Cada elemento en su propia línea, un nivel más adentro; el
            // cierre vuelve al nivel actual. Un arreglo vacío queda "[]".
            out += '[';
            bool first = true;
            for (const auto& item : *array_) {
                if (!first) out += ',';
                first = false;
                NewLine(out, indent, depth + 1);
                item.SerializeTo(out, indent, depth + 1);
            }
            if (!array_->empty()) NewLine(out, indent, depth);
            out += ']';
            break;
        }
        case Type::Object: {
            // Igual que el arreglo; las claves salen en orden alfabético
            // (orden de std::map).
            out += '{';
            bool first = true;
            for (const auto& [key, value] : *object_) {
                if (!first) out += ',';
                first = false;
                NewLine(out, indent, depth + 1);
                EscapeString(out, key);
                out += indent > 0 ? ": " : ":";
                value.SerializeTo(out, indent, depth + 1);
            }
            if (!object_->empty()) NewLine(out, indent, depth);
            out += '}';
            break;
        }
    }
}

bool JsonValue::Parse(const std::string& text, JsonValue& out, std::wstring* error) {
    // Ignora BOM UTF-8 si existe.
    // (Se vuelve a llamar con el texto sin esos 3 bytes.)
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        return Parse(text.substr(3), out, error);
    }
    Parser parser(text);
    return parser.Run(out, error);
}

bool LoadJsonFile(const std::wstring& path, JsonValue& out, std::wstring* error) {
    // Se comparte solo la lectura: si otro proceso lo está escribiendo en ese
    // momento la apertura falla (y History::Load reintenta, D52) en vez de
    // leer un archivo a medias.
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) *error = util::Win32ErrorMessage(GetLastError());
        return false;
    }
    // Se lee de una vez todo el archivo (config e historial miden poco).
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    std::string text(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    // Un archivo vacío no se lee (ReadFile con 0 bytes); luego falla el parseo.
    BOOL ok = text.empty() || ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) {
        if (error) *error = util::Win32ErrorMessage(GetLastError());
        return false;
    }
    // Por si se leyó menos de lo que decía el tamaño.
    text.resize(read);
    // Bloc de notas y PowerShell 5.1 (-Encoding UTF8) guardan con BOM: se ignora.
    // (D52: antes un archivo con BOM se leía como vacío y se sobrescribía.)
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
    return JsonValue::Parse(text, out, error);
}

bool SaveJsonFile(const std::wstring& path, const JsonValue& value) {
    std::string text = value.Serialize(2);
    // Escritura atómica: primero a "<path>.tmp" (sin compartir), luego se
    // reemplaza el destino con un solo MoveFileEx.
    std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    // Fuerza los datos a disco antes del cambio de nombre: si se va la luz
    // justo después, no queda un archivo con nombre correcto pero vacío.
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || written != text.size()) {
        // Escritura fallida: se borra el temporal y el original no se toca.
        DeleteFileW(temp.c_str());
        return false;
    }
    // REPLACE_EXISTING: sobrescribe el original. WRITE_THROUGH: no vuelve
    // hasta que el cambio esté en disco.
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

}  // namespace cola::config
