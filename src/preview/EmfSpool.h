#pragma once
// Extracción de páginas de un archivo de spool EMF (formato EMFSPOOL).
//
// En lugar de depender de cada tipo de registro EMRI_*, se buscan los
// metarchivos EMF completos (EMR_HEADER con firma " EMF" + nBytes) dentro del
// flujo. Cada EMF encontrado es una página. Es robusto frente a versiones del
// formato y a registros desconocidos (fuentes incrustadas, DEVMODE, etc.).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cola::preview {

// Devuelve los bytes de cada página EMF encontrada (como máximo maxPages).
// fingerprints (opcional) recibe una huella de TODAS las páginas, también las
// que no se devuelven: páginas idénticas tienen la misma huella (sirve para
// detectar copias hechas por la aplicación repitiendo páginas).
std::vector<std::vector<std::uint8_t>> ExtractEmfPages(const std::uint8_t* data, size_t size, size_t maxPages,
                                                       size_t* totalFound = nullptr,
                                                       std::vector<std::uint64_t>* fingerprints = nullptr);

// DEVMODE efectivo de un spool EMF. Las aplicaciones pueden cambiar la
// configuración con ResetDC después de StartDoc (Word lo hace): ese cambio
// queda como registro EMRI_DEVMODE dentro del spool (formato MS-EMFSPOOL) y
// es el que aplica el procesador de impresión, mientras JOB_INFO_2.pDevMode
// conserva el DEVMODE con el que se abrió el trabajo (comprobado en las
// pruebas con Word: copias y orientación del trabajo anterior).
struct EmfSpoolDevMode {
    std::vector<std::uint8_t> firstPage;  // DEVMODE vigente para la primera página (vacío si no hay)
    std::uint32_t records = 0;            // registros EMRI_DEVMODE encontrados
    bool changesBetweenPages = false;     // hay otro DEVMODE distinto después de la primera página
    bool walked = false;                  // se pudo recorrer la estructura de registros
};
EmfSpoolDevMode ReadEmfSpoolDevMode(const std::uint8_t* data, size_t size);

// Huella FNV-1a de 64 bits.
std::uint64_t Fingerprint(const std::uint8_t* data, size_t size);

}  // namespace cola::preview
