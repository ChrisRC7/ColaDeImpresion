#include "config/Json.h"

#include <windows.h>

#include <cmath>
#include <cstdio>

#include "util/Strings.h"

namespace cola::config {

namespace {

const JsonValue kNull;
const JsonValue::Array kEmptyArray;
const JsonValue::Object kEmptyObject;

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

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
    void SkipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    bool Match(const char* literal) {
        size_t len = std::char_traits<char>::length(literal);
        if (s_.compare(pos_, len, literal) != 0) return false;
        pos_ += len;
        return true;
    }

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
        return ParseNumber(out);
    }

    bool ParseNumber(JsonValue& out) {
        size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) ++pos_;
        while (pos_ < s_.size() && (isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.' ||
                                    s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '-' || s_[pos_] == '+'))
            ++pos_;
        if (start == pos_) return false;
        std::string num = s_.substr(start, pos_ - start);
        char* end = nullptr;
        double v = strtod(num.c_str(), &end);
        if (!end || *end != '\0') return false;
        out = JsonValue(v);
        return true;
    }

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
            if (c != '\\') {
                utf8 += c;
                continue;
            }
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
                default: return false;
            }
        }
        return false;
    }

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

    const std::string& s_;
    size_t pos_ = 0;
};

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

void NewLine(std::string& out, int indent, int depth) {
    if (indent <= 0) return;
    out += '\n';
    out.append(static_cast<size_t>(indent * depth), ' ');
}

}  // namespace

const JsonValue::Array& JsonValue::AsArray() const { return IsArray() ? *array_ : kEmptyArray; }
const JsonValue::Object& JsonValue::AsObject() const { return IsObject() ? *object_ : kEmptyObject; }

JsonValue::Array& JsonValue::MutableArray() {
    if (!IsArray()) {
        *this = JsonValue(Array{});
    } else if (array_.use_count() > 1) {
        array_ = std::make_shared<Array>(*array_);
    }
    return *array_;
}

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
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        return Parse(text.substr(3), out, error);
    }
    Parser parser(text);
    return parser.Run(out, error);
}

bool LoadJsonFile(const std::wstring& path, JsonValue& out, std::wstring* error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) *error = util::Win32ErrorMessage(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    std::string text(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = text.empty() || ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) {
        if (error) *error = util::Win32ErrorMessage(GetLastError());
        return false;
    }
    text.resize(read);
    // Bloc de notas y PowerShell 5.1 (-Encoding UTF8) guardan con BOM: se ignora.
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
    return JsonValue::Parse(text, out, error);
}

bool SaveJsonFile(const std::wstring& path, const JsonValue& value) {
    std::string text = value.Serialize(2);
    std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || written != text.size()) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

}  // namespace cola::config
