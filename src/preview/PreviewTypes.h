#pragma once
// Modelo de una vista previa: un documento con N páginas renderizables.
// Las páginas no retienen objetos GDI (los crean al dibujar), así que se pueden
// generar en un hilo de fondo, cachear y dibujar en el hilo de la UI o en un
// DC de impresora (reenvío a otra impresora).
//
// Las páginas y los documentos son inmutables una vez construidos y se
// comparten con shared_ptr<const ...> (PagePtr, DocumentPtr): el hilo de
// fondo los crea, la caché los guarda y la UI los dibuja sin candados.
// Render() sí usa GDI, así que se llama en el hilo que posee el HDC.

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/PageAccounting.h"

namespace cola::preview {

// Interfaz común de una página de vista previa (vectorial o rasterizada).
class PreviewPage {
public:
    virtual ~PreviewPage() = default;
    // Tamaño físico de la página en centésimas de milímetro (HIMETRIC).
    virtual SIZE SizeHimetric() const = 0;
    // Dibuja la página ocupando exactamente 'target' (el llamador conserva el aspecto).
    virtual void Render(HDC dc, const RECT& target) const = 0;
    // Memoria aproximada que ocupa (para el presupuesto de la caché).
    virtual size_t MemoryBytes() const = 0;
};

// Página vectorial EMF (del spool EMF de Windows).
// Guarda los bytes del metarchivo (no un HENHMETAFILE) para no retener
// objetos GDI; el handle se crea y destruye en cada Render.
class EmfPage final : public PreviewPage {
public:
    // bytes: un EMF completo (EMR_HEADER ... EMR_EOF), ver ExtractEmfPages.
    // El tamaño se toma de rclFrame del encabezado.
    explicit EmfPage(std::vector<std::uint8_t> bytes);
    SIZE SizeHimetric() const override { return size_; }
    // Reproduce el EMF con PlayEnhMetaFile recortado a 'target'.
    void Render(HDC dc, const RECT& target) const override;
    size_t MemoryBytes() const override { return bytes_.size(); }
    // Bytes del EMF (la reimpresión vectorial los reproduce en el DC de la
    // impresora destino, D12).
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;  // metarchivo EMF completo
    SIZE size_{21590, 27940};  // Carta por defecto (215,9 x 279,4 mm en HIMETRIC)
};

// Página rasterizada (imagen, miniatura o render externo). Píxeles BGRA 32 bpp.
class BitmapPage final : public PreviewPage {
public:
    // width x height píxeles, filas de arriba hacia abajo sin relleno
    // (pixels.size() == width * height). dpi define el tamaño físico; un
    // valor <= 0 se toma como 96.
    BitmapPage(int width, int height, std::vector<std::uint32_t> pixels, double dpi = 96.0);
    // Tamaño físico = píxeles / dpi, en HIMETRIC (2540 por pulgada).
    SIZE SizeHimetric() const override;
    // Escala la imagen a 'target' con StretchDIBits en modo HALFTONE.
    void Render(HDC dc, const RECT& target) const override;
    size_t MemoryBytes() const override { return pixels_.size() * sizeof(std::uint32_t); }
    int width() const { return width_; }    // ancho en píxeles
    int height() const { return height_; }  // alto en píxeles

    // Crea una página a partir de un HBITMAP (no toma posesión del handle).
    // Devuelve nullptr si el handle no es válido o GetDIBits falla.
    static std::shared_ptr<BitmapPage> FromHBitmap(HBITMAP bitmap, double dpi = 96.0);

private:
    int width_;                          // píxeles
    int height_;                         // píxeles
    double dpi_;                         // resolución (siempre > 0)
    std::vector<std::uint32_t> pixels_;  // BGRA opaco, de arriba hacia abajo
};

// Puntero compartido a una página inmutable (la caché y la UI la comparten).
using PagePtr = std::shared_ptr<const PreviewPage>;

// Formato de los datos del trabajo, detectado a partir de su contenido (no de
// pDatatype: un trabajo de un driver v4 dice "RAW" y contiene XPS).
// EscPage / EscPr: lenguajes propios de Epson (ESC/Page, ESC/Page-Color,
// ESC/P-R) que generan los drivers de las impresoras del LAIMI; no existe un
// intérprete libre para ellos, así que no tienen vista previa.
// Valores: Unknown (no reconocido), EmfSpool (EMFSPOOL o EMF suelto), Xps
// (XPS u OpenXPS: paquete ZIP), Pdf, PostScript, Pcl (PCL 5), PclXl (PCL 6),
// Image (PNG/JPEG suelto), Text (texto plano), EscPage, EscPr.
enum class DataFormat { Unknown, EmfSpool, Xps, Pdf, PostScript, Pcl, PclXl, Image, Text, EscPage, EscPr };

// Nombre para mostrar del formato (UI y log). No se traduce: son nombres
// técnicos, salvo "Imagen"/"Texto". Unknown se muestra como "RAW".
const wchar_t* DataFormatName(DataFormat format);

// Resultado de construir la vista previa de un trabajo (PreviewBuilder):
// las páginas renderizables más todo lo que se averiguó de los datos.
// Inmutable una vez publicado (DocumentPtr).
struct PreviewDocument {
    std::vector<PagePtr> pages;               // páginas listas para dibujar (pueden ser menos que el total)
    DataFormat format = DataFormat::Unknown;  // lenguaje detectado de los datos
    std::wstring source;    // de dónde salió la vista previa (spool, archivo original, miniatura...)
    std::wstring message;   // explicación si no hay vista previa o es parcial
    std::uint32_t totalPages = 0;  // total real si se conoce (puede ser > pages.size())
    std::uint64_t dataBytes = 0;   // tamaño del spool leído
    bool truncated = false;        // solo se renderizó una parte
    // Los datos llegaron incompletos (otro programa los leía a la vez): la caché
    // la olvida pasado un rato para volver a intentarlo (PreviewService::Find).
    bool readIncomplete = false;
    std::uint64_t builtTick = 0;   // GetTickCount64() al generarla
    // Lo que se averiguó de los datos (páginas, repeticiones, opciones PJL/EJL/PrintTicket).
    core::ContentFacts facts;
    std::wstring dataOrigin;       // cómo se obtuvieron los datos (API del spooler / archivo)

    // Memoria aproximada (estructura + páginas) para el presupuesto de la caché.
    size_t MemoryBytes() const;
    // true si hay al menos una página para mostrar.
    bool HasPages() const { return !pages.empty(); }
};

// Documento compartido e inmutable (caché de PreviewService y UI).
using DocumentPtr = std::shared_ptr<const PreviewDocument>;

// Calcula el rectángulo que ocupa una página centrada en 'bounds' conservando aspecto.
// pageHimetric: tamaño de la página (cualquier unidad, solo importa la
// proporción). Si algún tamaño no es positivo devuelve 'bounds' sin cambios.
// El resultado mide al menos 1x1 píxel.
RECT FitPage(SIZE pageHimetric, const RECT& bounds);

}  // namespace cola::preview
