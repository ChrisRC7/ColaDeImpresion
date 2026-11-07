// Lectura de nombres y huellas de entradas ZIP (paquetes XPS). Ver ZipReader.h.
//
// Estructuras ZIP que se interpretan (little-endian):
//   Encabezado local (30 bytes + nombre + extra + datos), firma "PK\3\4":
//     +6 flags (bit 3 = tamaños en un "data descriptor" posterior)
//     +18 tamaño comprimido  +26 largo del nombre  +28 largo del extra
//   Entrada del directorio central (46 bytes + nombre + extra + comentario),
//   firma "PK\1\2":
//     +16 CRC-32  +24 tamaño sin comprimir  +28 largo del nombre
//     +30 largo del extra  +32 largo del comentario
//   Fin del directorio central (EOCD, 22 bytes + comentario), firma "PK\5\6":
//     +10 total de entradas  +16 posición del directorio central
#include "preview/ZipReader.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace cola::preview {

namespace {
// Lectores little-endian byte a byte (sin alineación ni aliasing).
std::uint32_t U32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint16_t U16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

constexpr std::uint32_t kEndOfCentralDir = 0x06054b50;  // "PK\5\6"
constexpr std::uint32_t kCentralDirEntry = 0x02014b50;  // "PK\1\2"
constexpr std::uint32_t kLocalHeader = 0x04034b50;      // "PK\3\4"

// Recorre los encabezados locales (para ZIPs truncados o sin directorio central,
// p. ej. un spool XPS que todavía se está escribiendo).
// Se detiene en lo primero que no sea un encabezado local (normalmente el
// directorio central o el final de los datos).
std::vector<std::string> ListLocalHeaders(const std::uint8_t* data, size_t size) {
    std::vector<std::string> names;
    size_t pos = 0;
    while (pos + 30 <= size && U32(data + pos) == kLocalHeader) {
        std::uint16_t flags = U16(data + pos + 6);
        std::uint32_t compressed = U32(data + pos + 18);
        std::uint16_t nameLen = U16(data + pos + 26);
        std::uint16_t extraLen = U16(data + pos + 28);
        if (pos + 30 + nameLen > size) break;  // nombre cortado: fin de los datos
        names.emplace_back(reinterpret_cast<const char*>(data + pos + 30), nameLen);
        // Siguiente entrada = tras encabezado + nombre + extra + datos comprimidos.
        size_t next = pos + 30 + nameLen + extraLen + compressed;
        if (flags & 0x08) {
            // Tamaño desconocido (data descriptor): busca el siguiente encabezado local.
            // (Con el bit 3 el campo de tamaño vale 0; el tamaño real va
            // después de los datos.) Se busca la siguiente firma, sabiendo
            // que podría aparecer por azar dentro de datos comprimidos.
            size_t search = pos + 30 + nameLen + extraLen;
            next = size;
            for (size_t i = search; i + 4 <= size; ++i) {
                if (U32(data + i) == kLocalHeader || U32(data + i) == kCentralDirEntry) {
                    next = i;
                    break;
                }
            }
        }
        if (next <= pos) break;  // sin avance (datos corruptos): evita un bucle infinito
        pos = next;
    }
    return names;
}

// Posición del registro "End of central directory" o npos.
size_t FindEndOfCentralDir(const std::uint8_t* data, size_t size) {
    if (size < 22) return std::string::npos;
    // Se busca desde el final (el comentario del ZIP mide como máximo 64 KB).
    // El EOCD mide 22 bytes sin comentario, así que la última posición
    // posible es size - 22; la más lejana, size - 22 - 65535.
    size_t minPos = size > 22 + 65535 ? size - 22 - 65535 : 0;
    for (size_t i = size - 22 + 1; i-- > minPos;)
        if (U32(data + i) == kEndOfCentralDir) return i;
    return std::string::npos;
}

// Copia en minúsculas ASCII (los nombres de partes XPS no distinguen mayúsculas).
std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return text;
}

// true si 'text' termina en 'suffix' (comparación exacta).
bool EndsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}  // namespace

std::vector<std::string> ListZipEntries(const std::uint8_t* data, size_t size) {
    std::vector<std::string> names;
    if (size < 22) return names;
    size_t eocd = FindEndOfCentralDir(data, size);
    if (eocd == std::string::npos) return ListLocalHeaders(data, size);

    // Recorre las entradas del directorio central una tras otra; si algo no
    // cuadra (posición fuera de los datos o firma distinta) se detiene con lo
    // que lleve.
    std::uint16_t entries = U16(data + eocd + 10);
    std::uint32_t cdOffset = U32(data + eocd + 16);
    size_t pos = cdOffset;
    for (std::uint16_t i = 0; i < entries; ++i) {
        if (pos + 46 > size || U32(data + pos) != kCentralDirEntry) break;
        std::uint16_t nameLen = U16(data + pos + 28);
        std::uint16_t extraLen = U16(data + pos + 30);
        std::uint16_t commentLen = U16(data + pos + 32);
        if (pos + 46 + nameLen > size) break;
        names.emplace_back(reinterpret_cast<const char*>(data + pos + 46), nameLen);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    // Un EOCD encontrado por azar (o un directorio dañado): respaldo local.
    if (names.empty()) return ListLocalHeaders(data, size);
    return names;
}

std::uint32_t CountXpsPages(const std::uint8_t* data, size_t size) {
    std::uint32_t pages = 0;
    for (auto name : ListZipEntries(data, size)) {
        std::transform(name.begin(), name.end(), name.begin(),
                       [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        // Las páginas grandes pueden estar partidas en piezas "[0].piece"; contamos solo la primera.
        const std::string ext = ".fpage";
        if (name.size() >= ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0) ++pages;
        else if (name.find(".fpage/[0].piece") != std::string::npos) ++pages;
    }
    return pages;
}

// La huella es (CRC-32 << 32) | tamaño sin comprimir: dos .fpage con el
// mismo contenido la comparten sin tener que descomprimirlos. Aquí no hay
// respaldo por encabezados locales: sin directorio central (datos
// recortados) no se sabe si están todas las páginas.
std::vector<std::uint64_t> XpsPageFingerprints(const std::uint8_t* data, size_t size) {
    struct Page {
        std::wstring name;          // nombre en minúsculas (para el orden natural)
        std::uint64_t fingerprint;  // CRC-32 y tamaño
    };
    std::vector<Page> pages;
    size_t eocd = FindEndOfCentralDir(data, size);
    if (eocd == std::string::npos) return {};
    std::uint16_t entries = U16(data + eocd + 10);
    size_t pos = U32(data + eocd + 16);
    for (std::uint16_t i = 0; i < entries; ++i) {
        if (pos + 46 > size || U32(data + pos) != kCentralDirEntry) return {};
        std::uint32_t crc = U32(data + pos + 16);
        std::uint32_t uncompressed = U32(data + pos + 24);
        std::uint16_t nameLen = U16(data + pos + 28);
        std::uint16_t extraLen = U16(data + pos + 30);
        std::uint16_t commentLen = U16(data + pos + 32);
        if (pos + 46 + nameLen > size) return {};
        std::string name = Lower(std::string(reinterpret_cast<const char*>(data + pos + 46), nameLen));
        if (name.find(".fpage/") != std::string::npos) return {};  // páginas en piezas: no comparables
        // (El nombre se ensancha byte a byte: basta para ordenar rutas ASCII.)
        if (EndsWith(name, ".fpage"))
            pages.push_back({std::wstring(name.begin(), name.end()),
                             (static_cast<std::uint64_t>(crc) << 32) | uncompressed});
        pos += 46 + nameLen + extraLen + commentLen;
    }
    // Orden natural: Documents/1/Pages/2.fpage antes que .../10.fpage.
    std::sort(pages.begin(), pages.end(),
              [](const Page& a, const Page& b) { return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0; });
    std::vector<std::uint64_t> out;
    out.reserve(pages.size());
    for (const auto& p : pages) out.push_back(p.fingerprint);
    return out;
}

}  // namespace cola::preview
