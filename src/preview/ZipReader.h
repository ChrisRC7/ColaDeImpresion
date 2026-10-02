#pragma once
// Lector mínimo del directorio central de un ZIP (solo nombres de entradas).
// Se usa para contar las páginas de un paquete XPS (entradas *.fpage) sin
// necesidad de descomprimir nada.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cola::preview {

// Nombres de las entradas del ZIP (UTF-8 tal como vienen). Vacío si no es un ZIP válido.
std::vector<std::string> ListZipEntries(const std::uint8_t* data, size_t size);

// Cuenta las páginas de un XPS (entradas que terminan en ".fpage").
std::uint32_t CountXpsPages(const std::uint8_t* data, size_t size);

// Huella de cada página de un XPS (CRC-32 y tamaño sin comprimir del .fpage,
// tomados del directorio central), en el orden Documents/N/Pages/M. Vacío si
// el paquete no tiene directorio central o usa páginas partidas en piezas.
// Sirve para detectar páginas idénticas repetidas por la aplicación.
std::vector<std::uint64_t> XpsPageFingerprints(const std::uint8_t* data, size_t size);

}  // namespace cola::preview
