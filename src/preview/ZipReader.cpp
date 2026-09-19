#include "preview/ZipReader.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace cola::preview {

namespace {
std::uint32_t U32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint16_t U16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

constexpr std::uint32_t kEndOfCentralDir = 0x06054b50;
constexpr std::uint32_t kCentralDirEntry = 0x02014b50;
constexpr std::uint32_t kLocalHeader = 0x04034b50;

// Recorre los encabezados locales (para ZIPs truncados o sin directorio central,
// p. ej. un spool XPS que todavía se está escribiendo).
std::vector<std::string> ListLocalHeaders(const std::uint8_t* data, size_t size) {
    std::vector<std::string> names;
    size_t pos = 0;
    while (pos + 30 <= size && U32(data + pos) == kLocalHeader) {
        std::uint16_t flags = U16(data + pos + 6);
        std::uint32_t compressed = U32(data + pos + 18);
        std::uint16_t nameLen = U16(data + pos + 26);
        std::uint16_t extraLen = U16(data + pos + 28);
        if (pos + 30 + nameLen > size) break;
        names.emplace_back(reinterpret_cast<const char*>(data + pos + 30), nameLen);
        size_t next = pos + 30 + nameLen + extraLen + compressed;
        if (flags & 0x08) {
            // Tamaño desconocido (data descriptor): busca el siguiente encabezado local.
            size_t search = pos + 30 + nameLen + extraLen;
            next = size;
            for (size_t i = search; i + 4 <= size; ++i) {
                if (U32(data + i) == kLocalHeader || U32(data + i) == kCentralDirEntry) {
                    next = i;
                    break;
                }
            }
        }
        if (next <= pos) break;
        pos = next;
    }
    return names;
}
}  // namespace

std::vector<std::string> ListZipEntries(const std::uint8_t* data, size_t size) {
    std::vector<std::string> names;
    if (size < 22) return names;
    // Busca el registro "End of central directory" desde el final (comentario máx. 64 KB).
    size_t minPos = size > 22 + 65535 ? size - 22 - 65535 : 0;
    size_t eocd = std::string::npos;
    for (size_t i = size - 22 + 1; i-- > minPos;) {
        if (U32(data + i) == kEndOfCentralDir) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) return ListLocalHeaders(data, size);

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

}  // namespace cola::preview
