// Versiones y manifiesto de la actualización automática. Ver UpdateInfo.h.
#include "update/UpdateInfo.h"

#include "config/Json.h"
#include "util/Strings.h"

namespace cola::update {

std::wstring Version::ToString() const { return util::Format(L"%d.%d.%d", major, minor, patch); }

bool ParseVersion(const std::wstring& textIn, Version& out) {
    std::wstring text = util::Trim(textIn);
    if (!text.empty() && (text[0] == L'v' || text[0] == L'V')) text.erase(0, 1);
    if (text.empty()) return false;
    // Hasta cuatro números separados por puntos; cada uno de 1 a 6 cifras.
    int parts[4] = {0, 0, 0, 0};
    int count = 0;
    size_t pos = 0;
    while (true) {
        if (count >= 4) return false;
        size_t start = pos;
        int value = 0;
        while (pos < text.size() && text[pos] >= L'0' && text[pos] <= L'9') {
            value = value * 10 + (text[pos] - L'0');
            ++pos;
            if (pos - start > 6) return false;
        }
        if (pos == start) return false;  // falta el número
        parts[count++] = value;
        if (pos == text.size()) break;
        if (text[pos] != L'.') return false;
        ++pos;
    }
    out = Version{parts[0], parts[1], parts[2]};
    return true;
}

Version CurrentVersion() {
    Version version;
    ParseVersion(COLA_VERSION_STRING, version);
    return version;
}

namespace {

// Nombre de archivo simple: letras ASCII, números, '.', '-' y '_', sin ".."
// y terminado en ".zip". Con esto no puede colarse una ruta, una dirección ni
// parámetros en la URL de descarga.
bool IsSafePackageName(const std::wstring& name) {
    if (name.size() < 5 || name.size() > 100) return false;
    for (wchar_t c : name) {
        bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'.' ||
                  c == L'-' || c == L'_';
        if (!ok) return false;
    }
    if (name.find(L"..") != std::wstring::npos || name[0] == L'.') return false;
    return util::EqualsInsensitive(name.substr(name.size() - 4), L".zip");
}

// 64 hexadecimales -> en minúsculas; "" si no lo es.
std::wstring NormalizeSha256(const std::wstring& text) {
    std::wstring hex = util::ToLower(util::Trim(text));
    if (hex.size() != 64) return L"";
    for (wchar_t c : hex)
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return L"";
    return hex;
}

bool Fail(std::wstring* error, const wchar_t* message) {
    if (error) *error = message;
    return false;
}

}  // namespace

bool ParseManifest(const std::string& json, Manifest& out, std::wstring* error) {
    config::JsonValue root;
    if (!config::JsonValue::Parse(json, root, error)) return false;
    if (!root.IsObject()) return Fail(error, L"el manifiesto no es un objeto JSON");

    Manifest manifest;
    if (!ParseVersion(root[L"version"].AsString(), manifest.version))
        return Fail(error, L"el manifiesto no trae una versión válida");
    manifest.file = root[L"file"].AsString();
    if (!IsSafePackageName(manifest.file)) return Fail(error, L"el nombre del paquete del manifiesto no es válido");
    // Los números JSON son double: exactos hasta 2^53, de sobra para un tamaño.
    double size = root[L"size"].AsNumber(-1);
    if (!(size >= 1 && size <= static_cast<double>(kMaxPackageBytes)))
        return Fail(error, L"el tamaño del paquete del manifiesto no es válido");
    manifest.size = static_cast<std::uint64_t>(size);
    if (static_cast<double>(manifest.size) != size) return Fail(error, L"el tamaño del paquete no es un entero");
    manifest.sha256 = NormalizeSha256(root[L"sha256"].AsString());
    if (manifest.sha256.empty()) return Fail(error, L"la huella SHA-256 del manifiesto no es válida");
    manifest.notes = util::Trim(root[L"notes"].AsString());
    if (manifest.notes.size() > 500) manifest.notes.resize(500);
    out = std::move(manifest);
    return true;
}

std::wstring ManifestUrl() {
    return util::Format(L"https://github.com/%ls/%ls/releases/latest/download/%ls", kRepoOwner, kRepoName,
                        kManifestName);
}

std::wstring PackageUrl(const Manifest& manifest) {
    return util::Format(L"https://github.com/%ls/%ls/releases/download/v%ls/%ls", kRepoOwner, kRepoName,
                        manifest.version.ToString().c_str(), manifest.file.c_str());
}

}  // namespace cola::update
