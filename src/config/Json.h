#pragma once
// Implementación JSON mínima (sin dependencias externas) para configuración e
// historial. Soporta: null, bool, número (double), cadena, arreglo y objeto.
// Las cadenas se guardan en UTF-16 (std::wstring) y el texto serializado es UTF-8.

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cola::config {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::wstring, JsonValue>;

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

    static JsonValue MakeArray() { return JsonValue(Array{}); }
    static JsonValue MakeObject() { return JsonValue(Object{}); }

    Type type() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    // Accesores tolerantes: devuelven el valor por defecto si el tipo no coincide.
    bool AsBool(bool def = false) const { return IsBool() ? bool_ : def; }
    double AsNumber(double def = 0) const { return IsNumber() ? number_ : def; }
    int AsInt(int def = 0) const { return IsNumber() ? static_cast<int>(number_) : def; }
    long long AsInt64(long long def = 0) const { return IsNumber() ? static_cast<long long>(number_) : def; }
    std::wstring AsString(const std::wstring& def = L"") const { return IsString() ? string_ : def; }

    const Array& AsArray() const;
    const Object& AsObject() const;
    Array& MutableArray();    // convierte a arreglo si no lo es
    Object& MutableObject();  // convierte a objeto si no lo es

    // Objeto: acceso por clave (devuelve Null si no existe).
    const JsonValue& operator[](const std::wstring& key) const;
    JsonValue& operator[](const std::wstring& key);  // crea la clave
    bool Has(const std::wstring& key) const;

    void Push(JsonValue v) { MutableArray().push_back(std::move(v)); }

    // Serializa a UTF-8. indent > 0 produce salida legible.
    std::string Serialize(int indent = 2) const;

    // Parsea texto UTF-8. Devuelve false (y un mensaje) si el JSON es inválido.
    static bool Parse(const std::string& text, JsonValue& out, std::wstring* error = nullptr);

private:
    void SerializeTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::wstring string_;
    // shared_ptr para que JsonValue sea barato de copiar; se hace copia al escribir.
    std::shared_ptr<Array> array_;
    std::shared_ptr<Object> object_;
};

// Lectura/escritura de archivos JSON (escritura atómica vía archivo temporal).
bool LoadJsonFile(const std::wstring& path, JsonValue& out, std::wstring* error = nullptr);
bool SaveJsonFile(const std::wstring& path, const JsonValue& value);

}  // namespace cola::config
