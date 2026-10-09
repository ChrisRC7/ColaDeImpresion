#pragma once
// Opciones de trabajo que el controlador escribe al principio (y al final) de
// los datos RAW, antes del lenguaje de página:
//   - PJL (HP y compatibles, sintaxis documentada en el "PJL Technical
//     Reference Manual"): COPIES, QTY, DUPLEX, BINDING, PAPER, ORIENTATION,
//     RENDERMODE, PLANESINUSE.
//   - EJL (Epson, "ESC 01 @EJL"): la sintaxis no tiene documentación pública.
//     Solo se interpretan claves cuyo significado es inequívoco en los datos
//     observados (PS = papel, DX = doble cara, BD = borde de encuadernación,
//     JI PAGES = páginas que el controlador generó) y se marcan como indicio.
//     Las claves numéricas cuyo significado no está confirmado (CO, QT...) se
//     muestran crudas y no se usan para calcular copias.
// No se copian a rawSettings los campos con datos personales (USER, MACHINE,
// DOCUMENT): ya se muestran desde JOB_INFO_2.

#include <cstddef>
#include <cstdint>

#include "core/PageAccounting.h"

namespace cola::preview {

// Completa facts con lo que declaren los encabezados PJL/EJL. 'data' son los
// datos del trabajo; si no están completos (truncated) no se busca el final.
void ParsePdlSettings(const std::uint8_t* data, size_t size, bool complete, core::ContentFacts& facts);

// Páginas de un trabajo ESC/Page (Epson) contadas recorriendo sus comandos:
// "GS <parámetros><nombre>", los que llevan datos ("...{X") declaran su largo en
// el primer parámetro, y cada página termina con un FF fuera de los comandos.
// Verificado con el controlador EPSON AM-C6000 (1, 2 y 3 páginas a archivo y en
// el servidor). Complete = se llegó al final EJL; Truncated = los datos se
// acabaron antes (pages es un mínimo); Invalid = algo que no es ESC/Page.
// lastPageBlank: la última página no trae ninguna imagen; a doble cara con un
// número impar de páginas el controlador agrega así el dorso en blanco de la
// última hoja (3 páginas => 4 FF), que no es una página del documento.
struct EscPageCount {
    enum class End { Complete, Truncated, Invalid };
    End end = End::Invalid;
    std::uint32_t pages = 0;
    bool lastPageBlank = false;
};
EscPageCount CountEscPagePages(const std::uint8_t* data, size_t size);

}  // namespace cola::preview
