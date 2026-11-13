#pragma once
// SHA-256 con la API de criptografía de Windows (CNG, bcrypt.dll), sin
// dependencias externas.
//
// Lo usa la actualización automática (D58) para comprobar que el paquete
// descargado es exactamente el que se publicó (su huella va en el manifiesto
// actualizacion.json). Función pura respecto del estado de la app: cualquier
// hilo.

#include <string>
#include <string_view>

namespace cola::util {

// Huella SHA-256 de `data` como 64 dígitos hexadecimales en minúsculas.
// Cadena vacía si la API de Windows falla (no debería ocurrir).
std::wstring Sha256Hex(std::string_view data);

}  // namespace cola::util
