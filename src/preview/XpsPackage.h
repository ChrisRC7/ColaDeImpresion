#pragma once
// Datos de un trabajo XPS (drivers v4 / XPSDrv): páginas y PrintTicket.
//
// En el camino de impresión XPS las opciones efectivas viajan en el
// PrintTicket dentro del paquete (nivel trabajo, documento y página; el más
// específico prevalece), no en el DEVMODE. El paquete se abre con el XPS
// Object Model de Windows (IXpsOMObjectFactory1, Windows 8+), que entiende
// tanto MS XPS como OpenXPS, el formato que generan los drivers v4.
//
// Estructura del paquete que se recorre:
//   FixedDocumentSequence (PrintTicket de nivel trabajo)
//     └ FixedDocument 1..N  (PrintTicket de nivel documento)
//         └ PageContent / FixedPage 1..M (PrintTicket de nivel página)
// Ver D25 (opciones declaradas en los datos: el PrintTicket confirma).
// Si el XPS OM no abre el paquete, PreviewBuilder recurre a ZipReader para
// al menos contar las páginas.
//
// Hilos: AnalyzeXpsPackage usa COM (hilos de fondo con COM inicializado:
// PreviewService y JobFactsService). ApplyPrintTicket es lógica pura.

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
// No es un parser XML completo: recorre las etiquetas con una pila y
// reconoce psf:Feature / psf:Option / psf:ParameterInit / psf:Value por su
// nombre local (ignora los prefijos de espacio de nombres). Si reconoce algo,
// marca la fuente como PrintTicket documentado.
bool ApplyPrintTicket(const std::string& xml, core::ContentFacts& facts);

// Abre el paquete XPS con el XPS Object Model y completa facts: páginas del
// primer documento y PrintTickets. Requiere COM inicializado en el hilo.
// Devuelve false si el paquete no se pudo abrir (p. ej. incompleto).
// (Aclaración: el conteo de páginas suma las de TODOS los documentos del
// paquete; los PrintTickets de documento y de página se toman del primer
// documento y de su primera página.) error (opcional) recibe
// "paso: 0xHRESULT" si falla. El paquete debe medir menos de 4 GB.
bool AnalyzeXpsPackage(const std::uint8_t* data, size_t size, core::ContentFacts& facts, std::wstring* error = nullptr);

}  // namespace cola::preview
