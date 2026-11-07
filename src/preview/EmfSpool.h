#pragma once
// Extracción de páginas de un archivo de spool EMF (formato EMFSPOOL).
//
// En lugar de depender de cada tipo de registro EMRI_*, se buscan los
// metarchivos EMF completos (EMR_HEADER con firma " EMF" + nBytes) dentro del
// flujo. Cada EMF encontrado es una página. Es robusto frente a versiones del
// formato y a registros desconocidos (fuentes incrustadas, DEVMODE, etc.).
//
// Contexto: un trabajo EMF ("NT EMF 1.00x") llega al servidor cuando el
// renderizado se hace allí (colas locales, drivers v3 sin Client Side
// Rendering). Es el único formato con vista previa nativa y vectorial (D7):
// cada página EMF se reproduce con PlayEnhMetaFile. El DEVMODE efectivo del
// trabajo también viaja dentro del spool (D23), ver ReadEmfSpoolDevMode.
//
// Todo es lógica pura sobre un búfer en memoria: sin estado global, se puede
// llamar desde cualquier hilo (se usa en hilos de fondo) y tiene pruebas.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cola::preview {

// Devuelve los bytes de cada página EMF encontrada (como máximo maxPages).
// fingerprints (opcional) recibe una huella de TODAS las páginas, también las
// que no se devuelven: páginas idénticas tienen la misma huella (sirve para
// detectar copias hechas por la aplicación repitiendo páginas).
// data/size: datos del trabajo (EMFSPOOL o EMF suelto; pueden estar
// recortados: una página cortada al final simplemente no se cuenta).
// maxPages: cuántas páginas copiar como máximo (cada una se copia entera).
// totalFound (opcional): recibe cuántas páginas completas hay en total.
// Devuelve un vector por página con los bytes de su EMF (desde EMR_HEADER
// hasta EMR_EOF), listo para SetEnhMetaFileBits.
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
// Recorre los registros EMFSPOOL (no los EMF de dentro) buscando EMRI_DEVMODE.
// Si los datos no empiezan con el encabezado EMFSPOOL devuelve todo vacío.
// Ver D23: el DEVMODE de la primera página manda sobre JOB_INFO_2.pDevMode.
EmfSpoolDevMode ReadEmfSpoolDevMode(const std::uint8_t* data, size_t size);

// Huella FNV-1a de 64 bits.
// (Se combina con el tamaño al final para separar búferes que solo difieren
// en largo.) No es criptográfica: solo sirve para comparar páginas.
std::uint64_t Fingerprint(const std::uint8_t* data, size_t size);

}  // namespace cola::preview
