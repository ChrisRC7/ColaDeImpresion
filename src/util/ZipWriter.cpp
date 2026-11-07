// Implementación del escritor ZIP "stored" (ver ZipWriter.h).
//
// Estructura de un ZIP (especificación APPNOTE de PKWARE), en el orden en que
// se escribe aquí:
//   [encabezado local 1][datos 1] ... [encabezado local N][datos N]
//   [directorio central: una entrada por archivo, con el desplazamiento de
//    su encabezado local]
//   [fin del directorio central (EOCD): cuántas entradas y dónde empieza]
// Los lectores (Excel incluido) leen primero el EOCD al final del archivo.
// Todos los enteros van en little-endian.

#include "util/ZipWriter.h"

#include <array>

namespace cola::util {

namespace {
// Tabla de 256 valores para calcular el CRC-32 byte a byte (algoritmo
// clásico con el polinomio reflejado 0xEDB88320).
std::array<std::uint32_t, 256> MakeCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    return table;
}

// Agrega un entero de 16 bits en little-endian (solo se usan los 16 bajos).
void Put16(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

// Agrega un entero de 32 bits en little-endian.
void Put32(std::string& out, std::uint32_t v) {
    Put16(out, v & 0xFFFF);
    Put16(out, v >> 16);
}

// Fecha fija (1/1/2026 00:00, formato MS-DOS): el contenido no depende de cuándo se generó.
// Formato de fecha DOS: bits 15-9 = año desde 1980, 8-5 = mes, 4-0 = día.
constexpr std::uint32_t kDosTime = 0;
constexpr std::uint32_t kDosDate = ((2026 - 1980) << 9) | (1 << 5) | 1;
// Bit 11 de las banderas generales: los nombres están en UTF-8 (sin él los
// lectores asumen la página de códigos 437).
constexpr std::uint32_t kUtf8Flag = 1u << 11;
}  // namespace

std::uint32_t Crc32(std::string_view data) {
    // La tabla se calcula una sola vez (static local, inicialización segura
    // entre hilos en C++11+).
    static const auto table = MakeCrcTable();
    // Valor inicial y XOR final con 0xFFFFFFFF, como exige ZIP.
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : data) crc = table[(crc ^ c) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void ZipWriter::Add(const std::string& name, std::string data) { entries_.push_back({name, std::move(data)}); }

std::string ZipWriter::Finish() const {
    // `out` recibe los encabezados locales y los datos; `central` va armando
    // el directorio central en paralelo y se pega al final.
    std::string out;
    std::string central;
    for (const auto& e : entries_) {
        // Posición de este encabezado local: el directorio central la necesita.
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        const std::uint32_t crc = Crc32(e.data);
        const auto size = static_cast<std::uint32_t>(e.data.size());
        const auto nameLen = static_cast<std::uint32_t>(e.name.size());

        // Encabezado local.
        // Firma "PK\3\4". Versión 2.0 basta para entradas sin compresión.
        // Tamaño comprimido = tamaño real porque el método es 0 ("stored").
        Put32(out, 0x04034B50);
        Put16(out, 20);  // versión necesaria
        Put16(out, kUtf8Flag);
        Put16(out, 0);  // stored
        Put16(out, kDosTime);
        Put16(out, kDosDate);
        Put32(out, crc);
        Put32(out, size);  // tamaño comprimido
        Put32(out, size);  // tamaño sin comprimir
        Put16(out, nameLen);
        Put16(out, 0);  // largo del campo extra (no se usa)
        out += e.name;
        out += e.data;

        // Entrada del directorio central.
        // Firma "PK\1\2". Repite los datos del encabezado local y agrega el
        // desplazamiento donde empieza ese encabezado.
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
    // El directorio central empieza justo después del último dato.
    const auto centralOffset = static_cast<std::uint32_t>(out.size());
    out += central;
    // Fin del directorio central (EOCD), firma "PK\5\6".
    Put32(out, 0x06054B50);
    Put16(out, 0);  // número de este disco
    Put16(out, 0);  // disco donde empieza el directorio central
    Put16(out, static_cast<std::uint32_t>(entries_.size()));  // entradas en este disco
    Put16(out, static_cast<std::uint32_t>(entries_.size()));  // entradas en total
    Put32(out, static_cast<std::uint32_t>(central.size()));   // tamaño del directorio central
    Put32(out, centralOffset);                                // dónde empieza
    Put16(out, 0);  // largo del comentario del ZIP
    return out;
}

}  // namespace cola::util
