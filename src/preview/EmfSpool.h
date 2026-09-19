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
std::vector<std::vector<std::uint8_t>> ExtractEmfPages(const std::uint8_t* data, size_t size, size_t maxPages,
                                                       size_t* totalFound = nullptr);

}  // namespace cola::preview
