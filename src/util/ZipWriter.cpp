#include "util/ZipWriter.h"

#include <array>

namespace cola::util {

namespace {
std::array<std::uint32_t, 256> MakeCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    return table;
}

void Put16(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void Put32(std::string& out, std::uint32_t v) {
    Put16(out, v & 0xFFFF);
    Put16(out, v >> 16);
}

// Fecha fija (1/1/2026 00:00, formato MS-DOS): el contenido no depende de cuándo se generó.
constexpr std::uint32_t kDosTime = 0;
constexpr std::uint32_t kDosDate = ((2026 - 1980) << 9) | (1 << 5) | 1;
constexpr std::uint32_t kUtf8Flag = 1u << 11;
}  // namespace

std::uint32_t Crc32(std::string_view data) {
    static const auto table = MakeCrcTable();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : data) crc = table[(crc ^ c) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void ZipWriter::Add(const std::string& name, std::string data) { entries_.push_back({name, std::move(data)}); }

std::string ZipWriter::Finish() const {
    std::string out;
    std::string central;
    for (const auto& e : entries_) {
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        const std::uint32_t crc = Crc32(e.data);
        const auto size = static_cast<std::uint32_t>(e.data.size());
        const auto nameLen = static_cast<std::uint32_t>(e.name.size());

        // Encabezado local.
        Put32(out, 0x04034B50);
        Put16(out, 20);  // versión necesaria
        Put16(out, kUtf8Flag);
        Put16(out, 0);  // stored
        Put16(out, kDosTime);
        Put16(out, kDosDate);
        Put32(out, crc);
        Put32(out, size);
        Put32(out, size);
        Put16(out, nameLen);
        Put16(out, 0);
        out += e.name;
        out += e.data;

        // Entrada del directorio central.
        Put32(central, 0x02014B50);
        Put16(central, 20);  // creado por
        Put16(central, 20);  // versión necesaria
        Put16(central, kUtf8Flag);
        Put16(central, 0);
        Put16(central, kDosTime);
        Put16(central, kDosDate);
        Put32(central, crc);
        Put32(central, size);
        Put32(central, size);
        Put16(central, nameLen);
        Put16(central, 0);  // extra
        Put16(central, 0);  // comentario
        Put16(central, 0);  // disco
        Put16(central, 0);  // atributos internos
        Put32(central, 0);  // atributos externos
        Put32(central, offset);
        central += e.name;
    }
    const auto centralOffset = static_cast<std::uint32_t>(out.size());
    out += central;
    Put32(out, 0x06054B50);
    Put16(out, 0);
    Put16(out, 0);
    Put16(out, static_cast<std::uint32_t>(entries_.size()));
    Put16(out, static_cast<std::uint32_t>(entries_.size()));
    Put32(out, static_cast<std::uint32_t>(central.size()));
    Put32(out, centralOffset);
    Put16(out, 0);
    return out;
}

}  // namespace cola::util
