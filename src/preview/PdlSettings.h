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
//
// Actualización (D29, 0.3.1): con el controlador EPSON AM-C6000 el
// significado de las claves EJL se verificó generando datos "a archivo":
// QT = copias, PS = papel, DX/BD = doble cara y borde, JI PAGES = hojas
// físicas, y en los comandos ESC/Page "GS n poE" = orientación y
// "GS n;m iuE" = bandeja (D34). Con ese controlador los datos EJL se marcan
// como documentados (settingsDocumented) y mandan sobre el DEVMODE del
// servidor; con cualquier otro siguen siendo solo indicio (D25).
//   - DSC de PostScript (Adobe DSC 3.0 y funciones PPD): %%Pages,
//     %%Orientation, *PageSize, *NumCopies, *Duplex, *Collate.
//
// También cuenta las páginas de un trabajo ESC/Page recorriendo sus comandos
// (D46), en una pasada (CountEscPagePages) o por bloques (EscPageCounter, D53).
//
// Todo es lógica pura sin estado global: se puede llamar desde cualquier hilo
// (en la práctica, desde los hilos de fondo de JobFacts y PreviewBuilder) y
// tiene pruebas en tests/.

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
// complete: true si 'data' son TODOS los datos del trabajo (solo entonces se
// leen los comandos del final y se acepta un "%%Pages: (atend)").
// Escribe en facts: settingsSource, settingsDocumented, verifiedDriver,
// rawSettings (texto crudo de las opciones), copies, collate, duplex,
// paperName/Width/Length, orientation, color, sourceBin, pages, pageSource,
// pagesFromAllData, sheetsCounter y spoolerCountsBlankBack, según el formato.
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
    // Cómo terminó el recorrido de los comandos (ver arriba).
    enum class End { Complete, Truncated, Invalid };
    End end = End::Invalid;
    std::uint32_t pages = 0;            // cantidad de FF (fin de página) encontrados
    bool lastPageBlank = false;         // la última página no traía comandos con datos
    std::vector<std::uint8_t> trailer;  // EJL del final (hasta 16 KB) si end == Complete
};
// Conteo de una sola vez sobre datos ya en memoria (envoltorio de
// EscPageCounter). data puede ser nullptr (devuelve Invalid).
EscPageCount CountEscPagePages(const std::uint8_t* data, size_t size);

// El mismo conteo, por partes: recibe los datos en bloques sucesivos (tal como
// llegan de ReadPrinter) sin guardarlos, así se cuentan las páginas de trabajos
// más grandes que lo que se lee para analizar (D53: 42 MB de Edge con 11 páginas).
// Máquina de estados: primero acumula el encabezado EJL hasta encontrar la
// línea "@EJL EN LA=ESC/PAGE..."; después recorre comandos GS, saltando los
// datos binarios que declaran (pueden ocupar muchos bloques), cuenta los FF y
// termina al ver el "ESC 01 @EJL" del final. Un comando cortado entre dos
// bloques se guarda en head_ y se completa con el bloque siguiente.
// No es seguro para varios hilos a la vez (un objeto por lectura); en la
// práctica lo usa el callback de ReadJobSpool en un hilo de fondo.
class EscPageCounter {
public:
    // Agrega el siguiente bloque de datos, en orden. Tras terminar (Complete),
    // los bloques siguientes solo se guardan en el trailer (hasta 16 KB).
    void Feed(const std::uint8_t* data, size_t size);
    // Truncated mientras no llegue al EJL del final.
    // (Invalid si nunca se encontró el encabezado EJL de ESC/Page.)
    EscPageCount Result() const;
    // true si ya se sabe que los datos no son ESC/Page válido: el llamador
    // puede dejar de leer.
    bool Failed() const { return done_ && count_.end == EscPageCount::End::Invalid; }
    // Total de bytes recibidos por Feed (para comprobar que no faltan, D48).
    std::uint64_t BytesFed() const { return fed_; }

private:
    // Valor especial de Parse: el comando sigue en el bloque siguiente.
    static constexpr size_t kNeedMore = static_cast<size_t>(-1);
    // Recorre comandos del cuerpo (después del encabezado EJL).
    void FeedBody(const std::uint8_t* data, size_t size);
    // Un comando (o FF) al comienzo de p: bytes que ocupa, kNeedMore si no
    // alcanzan, o 0 con done_ si se terminó (EJL del final o algo desconocido).
    size_t Parse(const std::uint8_t* p, size_t avail);
    // Marca el final del recorrido con el resultado dado.
    void Finish(EscPageCount::End end);
    // Agrega bytes al trailer, sin pasar de 16 KB (kTrailerWindow).
    void KeepTrailer(const std::uint8_t* data, size_t size);

    bool header_ = true;             // todavía se busca "@EJL EN LA=ESC/PAGE"
    bool done_ = false;              // se terminó (Complete o Invalid); ya no se cuenta
    bool pageHasData_ = false;       // la página actual trajo algún comando con datos (imagen)
    std::vector<std::uint8_t> head_; // encabezado EJL o comando partido entre dos bloques
    std::uint64_t skip_ = 0;         // datos de un comando que faltan saltar
    std::uint64_t fed_ = 0;          // bytes recibidos en total
    EscPageCount count_;             // resultado parcial
};

}  // namespace cola::preview
