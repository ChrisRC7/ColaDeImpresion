#pragma once
// Descompresor DEFLATE (RFC 1951) mínimo, sin dependencias externas.
//
// Lo usa la actualización automática (update/UpdatePackage, D58) para extraer
// las entradas comprimidas (método 8) del paquete .zip que arma build.ps1 con
// .NET. Solo descomprime el flujo "crudo" de DEFLATE, que es lo que guarda un
// ZIP (sin las cabeceras de zlib ni de gzip).
//
// Es una función pura sobre búferes: cualquier hilo, con pruebas en tests/.
// Prioriza la claridad sobre la velocidad (decodifica los códigos de Huffman
// bit a bit): unos pocos MB tardan décimas de segundo, suficiente para un
// paquete que se instala en segundo plano.

#include <cstddef>
#include <cstdint>
#include <string>

namespace cola::util {

// Descomprime `size` bytes de `data` y deja el resultado en `out` (se vacía
// antes). `expectedSize` es el tamaño sin comprimir que declara el ZIP: sirve
// para reservar memoria y como tope (un flujo que produzca más se rechaza, así
// un archivo dañado o malicioso no puede agotar la memoria).
// Devuelve false si los datos están dañados, cortados o producen un tamaño
// distinto de expectedSize; en ese caso `out` queda con contenido sin valor.
bool Inflate(const std::uint8_t* data, size_t size, size_t expectedSize, std::string& out);

}  // namespace cola::util
