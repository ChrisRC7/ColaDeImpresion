#pragma once
// Lector mínimo del directorio central de un ZIP (solo nombres de entradas).
// Se usa para contar las páginas de un paquete XPS (entradas *.fpage) sin
// necesidad de descomprimir nada.
//
// Es el respaldo de XpsPackage cuando el XPS Object Model no abre el paquete
// (datos recortados o un ZIP aún incompleto). No descomprime: solo lee
// encabezados del ZIP (formato PKWARE APPNOTE). No soporta ZIP64 (los
// spools XPS de más de 4 GB o 65535 entradas no se dan en la práctica).
// Funciones puras sobre un búfer: cualquier hilo; con pruebas en tests/.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cola::preview {

// Nombres de las entradas del ZIP (UTF-8 tal como vienen). Vacío si no es un ZIP válido.
// Usa el directorio central si está; si no (ZIP recortado), recorre los
// encabezados locales desde el principio.
std::vector<std::string> ListZipEntries(const std::uint8_t* data, size_t size);

// Cuenta las páginas de un XPS (entradas que terminan en ".fpage").
// Una página partida en piezas ("X.fpage/[0].piece", "[1].piece"...) cuenta
// una vez. No distingue mayúsculas.
std::uint32_t CountXpsPages(const std::uint8_t* data, size_t size);

// Huella de cada página de un XPS (CRC-32 y tamaño sin comprimir del .fpage,
// tomados del directorio central), en el orden Documents/N/Pages/M. Vacío si
// el paquete no tiene directorio central o usa páginas partidas en piezas.
// Sirve para detectar páginas idénticas repetidas por la aplicación.
std::vector<std::uint64_t> XpsPageFingerprints(const std::uint8_t* data, size_t size);

}  // namespace cola::preview
