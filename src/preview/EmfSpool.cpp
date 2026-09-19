#include "preview/EmfSpool.h"

#include <cstring>

namespace cola::preview {

namespace {
constexpr std::uint32_t kEmrHeader = 1;
constexpr std::uint32_t kEmrEof = 14;
constexpr std::uint32_t kEmfSignature = 0x464D4520;  // " EMF"
constexpr size_t kMinHeaderSize = 88;                // ENHMETAHEADER sin extensiones

std::uint32_t U32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// Valida que en 'offset' empiece un EMF completo; devuelve su tamaño o 0.
size_t EmfAt(const std::uint8_t* data, size_t size, size_t offset) {
    if (size - offset < kMinHeaderSize) return 0;
    const std::uint8_t* p = data + offset;
    if (U32(p) != kEmrHeader || U32(p + 40) != kEmfSignature) return 0;
    std::uint32_t headerSize = U32(p + 4);
    std::uint32_t totalBytes = U32(p + 48);
    if (headerSize < kMinHeaderSize || headerSize > totalBytes) return 0;
    if (totalBytes > size - offset || totalBytes < headerSize + 20) return 0;
    // El último registro debe ser EMR_EOF; su tamaño está al final del registro.
    std::uint32_t eofSize = U32(p + totalBytes - 4);
    if (eofSize < 20 || eofSize > totalBytes) return 0;
    if (U32(p + totalBytes - eofSize) != kEmrEof) return 0;
    return totalBytes;
}
}  // namespace

std::vector<std::vector<std::uint8_t>> ExtractEmfPages(const std::uint8_t* data, size_t size, size_t maxPages,
                                                       size_t* totalFound) {
    std::vector<std::vector<std::uint8_t>> pages;
    size_t found = 0;
    size_t offset = 0;
    while (offset + kMinHeaderSize <= size) {
        size_t emfSize = EmfAt(data, size, offset);
        if (emfSize) {
            ++found;
            if (pages.size() < maxPages) pages.emplace_back(data + offset, data + offset + emfSize);
            offset += emfSize;
            continue;
        }
        offset += 4;  // los registros EMF/EMFSPOOL están alineados a 4 bytes
    }
    if (totalFound) *totalFound = found;
    return pages;
}

}  // namespace cola::preview
