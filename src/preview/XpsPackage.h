#pragma once
// Datos de un trabajo XPS (drivers v4 / XPSDrv): páginas y PrintTicket.
//
// En el camino de impresión XPS las opciones efectivas viajan en el
// PrintTicket dentro del paquete (nivel trabajo, documento y página; el más
// específico prevalece), no en el DEVMODE. El paquete se abre con el XPS
// Object Model de Windows (IXpsOMObjectFactory1, Windows 8+), que entiende
// tanto MS XPS como OpenXPS, el formato que generan los drivers v4.

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/PageAccounting.h"

namespace cola::preview {

// Aplica un PrintTicket (XML de Print Schema) sobre facts: copias, papel,
// orientación, doble cara, páginas por cara, color e intercalado. Lo que el
// ticket no declara no se toca, así que se puede llamar en orden trabajo ->
// documento -> página para que el más específico prevalezca.
// Devuelve true si reconoció alguna opción.
bool ApplyPrintTicket(const std::string& xml, core::ContentFacts& facts);

// Abre el paquete XPS con el XPS Object Model y completa facts: páginas del
// primer documento y PrintTickets. Requiere COM inicializado en el hilo.
// Devuelve false si el paquete no se pudo abrir (p. ej. incompleto).
bool AnalyzeXpsPackage(const std::uint8_t* data, size_t size, core::ContentFacts& facts, std::wstring* error = nullptr);

}  // namespace cola::preview
