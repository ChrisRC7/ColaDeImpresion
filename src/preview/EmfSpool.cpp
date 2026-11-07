// Lectura de spool EMF: páginas (metarchivos EMF incrustados), DEVMODE
// efectivo (registros EMRI_DEVMODE) y huellas de página. Ver EmfSpool.h.
//
// Estructuras binarias que se interpretan (todo little-endian, alineado a 4):
//   EMFSPOOL (MS-EMFSPOOL): encabezado EMRI_HEADER
//     { dwVersion = 0x00010000, cjSize (largo del encabezado), ... }
//     seguido de registros { ulID (u32), cjSize (u32), datos[cjSize] }.
//   EMF (MS-EMF), primer registro ENHMETAHEADER:
//     +0 iType = 1 (EMR_HEADER)   +4 nSize (largo del registro)
//     +40 dSignature = " EMF"     +48 nBytes (largo de TODO el metarchivo)
//   y el último registro EMR_EOF (iType 14), cuyo último DWORD (nSizeLast)
//   repite el tamaño del propio registro.
#include "preview/EmfSpool.h"

#include <cstring>

namespace cola::preview {

namespace {
constexpr std::uint32_t kEmrHeader = 1;              // EMR_HEADER (iType)
constexpr std::uint32_t kEmrEof = 14;                // EMR_EOF (iType)
constexpr std::uint32_t kEmfSignature = 0x464D4520;  // " EMF"
constexpr size_t kMinHeaderSize = 88;                // ENHMETAHEADER sin extensiones

// DWORD little-endian sin exigir alineación.
std::uint32_t U32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// Valida que en 'offset' empiece un EMF completo; devuelve su tamaño o 0.
// Las comprobaciones evitan falsos positivos (bytes que por azar parecen un
// EMR_HEADER dentro de una fuente incrustada o una imagen) y que una página
// cortada al final de datos recortados se tome como completa.
size_t EmfAt(const std::uint8_t* data, size_t size, size_t offset) {
    if (size - offset < kMinHeaderSize) return 0;
    const std::uint8_t* p = data + offset;
    // Tipo EMR_HEADER y firma " EMF".
    if (U32(p) != kEmrHeader || U32(p + 40) != kEmfSignature) return 0;
    std::uint32_t headerSize = U32(p + 4);   // nSize del EMR_HEADER
    std::uint32_t totalBytes = U32(p + 48);  // nBytes: largo del metarchivo completo
    if (headerSize < kMinHeaderSize || headerSize > totalBytes) return 0;
    // Tiene que caber entero en los datos y dejar sitio al menos para un
    // EMR_EOF (20 bytes) después del encabezado.
    if (totalBytes > size - offset || totalBytes < headerSize + 20) return 0;
    // El último registro debe ser EMR_EOF; su tamaño está al final del registro.
    std::uint32_t eofSize = U32(p + totalBytes - 4);
    if (eofSize < 20 || eofSize > totalBytes) return 0;
    if (U32(p + totalBytes - eofSize) != kEmrEof) return 0;
    return totalBytes;
}
}  // namespace

EmfSpoolDevMode ReadEmfSpoolDevMode(const std::uint8_t* data, size_t size) {
    // MS-EMFSPOOL: encabezado {dwVersion = 0x10000, cjSize, ...} y luego registros
    // {ulID, cjSize, datos[cjSize]}. Tipos de interés:
    constexpr std::uint32_t kDevMode = 3;  // EMRI_DEVMODE
    // Fin de página: EMRI_METAFILE_EXT / EMRI_BW_METAFILE_EXT cierran la página
    // cuyos datos van en EMRI_METAFILE_DATA; el EMRI_DEVMODE que la aplicación
    // registra durante la página queda ENTRE ambos (comprobado con Word: los
    // datos de la página, luego el DEVMODE con las copias, luego el _EXT).
    // Los formatos antiguos (EMRI_METAFILE, *_FORM_METAFILE, EMRI_BW_METAFILE)
    // traen la página completa en un solo registro.
    // Ids: 1 EMRI_METAFILE, 9 EMRI_FORM_METAFILE, 10 EMRI_BW_METAFILE,
    // 11 EMRI_BW_FORM_METAFILE, 13 EMRI_METAFILE_EXT, 14 EMRI_BW_METAFILE_EXT.
    auto endsPage = [](std::uint32_t id) { return id == 13 || id == 14 || id == 1 || id == 9 || id == 10 || id == 11; };
    // ¿Hay en 'pos' un registro EMFSPOOL creíble? Id dentro del rango de
    // tipos conocidos (con margen, hasta 0x30) y largo que cabe en los datos.
    // Si no lo es, el recorrido termina (datos recortados o desconocidos).
    auto plausible = [&](size_t pos) {
        if (pos + 8 > size) return false;
        std::uint32_t id = U32(data + pos), len = U32(data + pos + 4);
        return id >= 1 && id <= 0x30 && len <= size - pos - 8;
    };
    EmfSpoolDevMode out;
    if (size < 16 || U32(data) != 0x00010000u) return out;
    // cjSize del encabezado = posición del primer registro (el encabezado
    // incluye el nombre del documento y del puerto, de largo variable).
    size_t pos = U32(data + 4);
    if (pos < 16 || pos > size) return out;
    bool pageSeen = false;  // ya terminó la primera página
    while (plausible(pos)) {
        const std::uint32_t id = U32(data + pos);
        const std::uint32_t len = U32(data + pos + 4);
        const std::uint8_t* record = data + pos + 8;  // datos tras {ulID, cjSize}
        if (id == kDevMode) {
            // Los datos del registro son un DEVMODEW completo (con su parte
            // privada del controlador).
            ++out.records;
            std::vector<std::uint8_t> devmode(record, record + len);
            if (!pageSeen) out.firstPage = std::move(devmode);  // el último antes de la primera página
            else if (devmode != out.firstPage) out.changesBetweenPages = true;
        } else if (endsPage(id)) {
            pageSeen = true;
        }
        size_t next = pos + 8 + len;
        // Los registros suelen medir múltiplos de 4; si no, se prueba la posición alineada.
        if (!plausible(next) && next % 4 != 0 && plausible((next + 3) & ~static_cast<size_t>(3)))
            next = (next + 3) & ~static_cast<size_t>(3);
        pos = next;
    }
    // Se recorrió hasta el final sin encontrar un registro raro: la
    // estructura es confiable (si no, el DEVMODE encontrado es parcial).
    out.walked = pos >= size;
    return out;
}

// FNV-1a: por cada byte, XOR y luego multiplicar por el primo FNV de 64 bits.
std::uint64_t Fingerprint(const std::uint8_t* data, size_t size) {
    std::uint64_t hash = 14695981039346656037ull;  // offset basis de FNV-1a 64
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash ^ size;
}

std::vector<std::vector<std::uint8_t>> ExtractEmfPages(const std::uint8_t* data, size_t size, size_t maxPages,
                                                       size_t* totalFound, std::vector<std::uint64_t>* fingerprints) {
    if (fingerprints) fingerprints->clear();
    std::vector<std::vector<std::uint8_t>> pages;
    size_t found = 0;
    size_t offset = 0;
    // Barrido lineal: en cada posición alineada a 4 se pregunta si empieza un
    // EMF completo; si sí, se salta entero (sus registros internos no se
    // vuelven a examinar). Así no hace falta entender los registros EMRI.
    while (offset + kMinHeaderSize <= size) {
        size_t emfSize = EmfAt(data, size, offset);
        if (emfSize) {
            ++found;
            if (fingerprints) fingerprints->push_back(Fingerprint(data + offset, emfSize));
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
