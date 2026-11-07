#pragma once
// Implementación JSON mínima (sin dependencias externas) para configuración e
// historial. Soporta: null, bool, número (double), cadena, arreglo y objeto.
// Las cadenas se guardan en UTF-16 (std::wstring) y el texto serializado es UTF-8.
// Decisión D10: el formato de configuración es pequeño y no justifica una
// biblioteca externa.
//
// Detalles a tener en cuenta:
//  - Los objetos son std::map: las claves salen ordenadas alfabéticamente al
//    serializar (no se conserva el orden original) y una clave repetida en el
//    texto se queda con el último valor.
//  - Los números son double: los enteros son exactos hasta 2^53, suficiente
//    para horas Unix, tamaños y contadores.
//  - Un JsonValue NO es seguro para usar desde varios hilos a la vez sin
//    sincronización (las copias comparten datos, ver más abajo).

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cola::config {

// Valor JSON de cualquier tipo. Valor por defecto: Null.
class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::wstring, JsonValue>;

    // Constructores implícitos a propósito, para escribir de forma natural:
    //   obj[L"theme"] = L"dark";  obj[L"pollMs"] = 3000;  obj[L"x"] = nullptr;
    // Todos los enteros se guardan como double.
    JsonValue() = default;
    JsonValue(std::nullptr_t) {}
    JsonValue(bool v) : type_(Type::Bool), bool_(v) {}
    JsonValue(int v) : type_(Type::Number), number_(v) {}
    JsonValue(long long v) : type_(Type::Number), number_(static_cast<double>(v)) {}
    JsonValue(unsigned long v) : type_(Type::Number), number_(static_cast<double>(v)) {}
    JsonValue(double v) : type_(Type::Number), number_(v) {}
    JsonValue(const wchar_t* v) : type_(Type::String), string_(v) {}
    JsonValue(std::wstring v) : type_(Type::String), string_(std::move(v)) {}
    JsonValue(Array v) : type_(Type::Array), array_(std::make_shared<Array>(std::move(v))) {}
    JsonValue(Object v) : type_(Type::Object), object_(std::make_shared<Object>(std::move(v))) {}

    // Arreglo u objeto vacíos (equivalen a [] y {}).
    static JsonValue MakeArray() { return JsonValue(Array{}); }
    static JsonValue MakeObject() { return JsonValue(Object{}); }

    // Consulta del tipo.
    Type type() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    // Accesores tolerantes: devuelven el valor por defecto si el tipo no coincide.
    // Así un config.json editado a mano con un tipo equivocado no rompe la
    // carga: ese ajuste simplemente toma su valor predeterminado. AsInt y
    // AsInt64 truncan los decimales (no redondean).
    bool AsBool(bool def = false) const { return IsBool() ? bool_ : def; }
    double AsNumber(double def = 0) const { return IsNumber() ? number_ : def; }
    int AsInt(int def = 0) const { return IsNumber() ? static_cast<int>(number_) : def; }
    long long AsInt64(long long def = 0) const { return IsNumber() ? static_cast<long long>(number_) : def; }
    std::wstring AsString(const std::wstring& def = L"") const { return IsString() ? string_ : def; }

    // Lectura de arreglo/objeto: si el tipo no coincide devuelven uno vacío
    // compartido (nunca fallan), así se puede recorrer sin comprobar antes.
    const Array& AsArray() const;
    const Object& AsObject() const;
    // Acceso para modificar. Si el contenido está compartido con otra copia,
    // primero se duplica (copia al escribir) para no alterar la otra.
    Array& MutableArray();    // convierte a arreglo si no lo es
    Object& MutableObject();  // convierte a objeto si no lo es

    // Objeto: acceso por clave (devuelve Null si no existe).
    // También devuelve Null si este valor no es un objeto.
    const JsonValue& operator[](const std::wstring& key) const;
    JsonValue& operator[](const std::wstring& key);  // crea la clave
                                                     // (y convierte a objeto si no lo es)
    // true si es un objeto y tiene la clave (aunque su valor sea null).
    bool Has(const std::wstring& key) const;

    // Agrega un elemento al final (convierte a arreglo si no lo es).
    void Push(JsonValue v) { MutableArray().push_back(std::move(v)); }

    // Serializa a UTF-8. indent > 0 produce salida legible.
    // indent = espacios por nivel; con 0 sale todo en una línea. Con
    // indentación se agrega un salto de línea final. Los caracteres no ASCII
    // se escriben tal cual en UTF-8 (no como \u).
    std::string Serialize(int indent = 2) const;

    // Parsea texto UTF-8. Devuelve false (y un mensaje) si el JSON es inválido.
    // Ignora un BOM UTF-8 inicial. Profundidad máxima de anidamiento: 64
    // (protege la pila ante archivos dañados). Si falla, `out` no se modifica.
    static bool Parse(const std::string& text, JsonValue& out, std::wstring* error = nullptr);

private:
    // Serialización recursiva; depth = nivel de anidamiento actual (para la
    // indentación).
    void SerializeTo(std::string& out, int indent, int depth) const;

    // Solo es válido el campo que corresponde a type_; los demás quedan en
    // su valor por defecto.
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::wstring string_;
    // shared_ptr para que JsonValue sea barato de copiar; se hace copia al escribir.
    // (El historial tiene miles de registros: copiar el árbol entero en cada
    // asignación sería caro.)
    std::shared_ptr<Array> array_;
    std::shared_ptr<Object> object_;
};

// Lectura/escritura de archivos JSON (escritura atómica vía archivo temporal).
// LoadJsonFile: lee el archivo completo (UTF-8, con o sin BOM) y lo parsea.
// false si no se puede abrir/leer o el JSON es inválido; `error` (opcional)
// recibe el motivo legible. Se abre compartiendo solo lectura.
bool LoadJsonFile(const std::wstring& path, JsonValue& out, std::wstring* error = nullptr);
// SaveJsonFile: serializa con indentación 2 en "<path>.tmp" y luego lo mueve
// sobre el destino. Si la app se cierra o falla a mitad, el archivo original
// queda intacto (nunca a medias). false si algo falla (sin mensaje).
bool SaveJsonFile(const std::wstring& path, const JsonValue& value);

}  // namespace cola::config
