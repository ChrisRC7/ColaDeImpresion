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
#include <vector>

#include "core/PageAccounting.h"

namespace cola::preview {

struct EscPageCount;

// Completa facts con lo que declaren los encabezados PJL/EJL. 'data' son los
// datos del trabajo; si no están completos (truncated) no se busca el final.
// allData: páginas ESC/Page contadas recorriendo TODOS los datos aunque 'data'
// sea solo el principio (EscPageCounter); si llegó al final, manda sobre el
// conteo de 'data' y su EJL final (trailer) da las hojas.
void ParsePdlSettings(const std::uint8_t* data, size_t size, bool complete, core::ContentFacts& facts,
                      const EscPageCount* allData = nullptr);

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
    std::vector<std::uint8_t> trailer;  // EJL del final (hasta 16 KB) si end == Complete
};
EscPageCount CountEscPagePages(const std::uint8_t* data, size_t size);

// El mismo conteo, por partes: recibe los datos en bloques sucesivos (tal como
// llegan de ReadPrinter) sin guardarlos, así se cuentan las páginas de trabajos
// más grandes que lo que se lee para analizar (D53: 42 MB de Edge con 11 páginas).
class EscPageCounter {
public:
    void Feed(const std::uint8_t* data, size_t size);
    // Truncated mientras no llegue al EJL del final.
    EscPageCount Result() const;
    bool Failed() const { return done_ && count_.end == EscPageCount::End::Invalid; }
    std::uint64_t BytesFed() const { return fed_; }

private:
    static constexpr size_t kNeedMore = static_cast<size_t>(-1);
    void FeedBody(const std::uint8_t* data, size_t size);
    // Un comando (o FF) al comienzo de p: bytes que ocupa, kNeedMore si no
    // alcanzan, o 0 con done_ si se terminó (EJL del final o algo desconocido).
    size_t Parse(const std::uint8_t* p, size_t avail);
    void Finish(EscPageCount::End end);
    void KeepTrailer(const std::uint8_t* data, size_t size);

    bool header_ = true;             // todavía se busca "@EJL EN LA=ESC/PAGE"
    bool done_ = false;
    bool pageHasData_ = false;       // la página actual trajo algún comando con datos (imagen)
    std::vector<std::uint8_t> head_; // encabezado EJL o comando partido entre dos bloques
    std::uint64_t skip_ = 0;         // datos de un comando que faltan saltar
    std::uint64_t fed_ = 0;
    EscPageCount count_;
};

}  // namespace cola::preview
