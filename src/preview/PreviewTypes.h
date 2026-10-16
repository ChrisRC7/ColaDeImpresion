#pragma once
// Modelo de una vista previa: un documento con N páginas renderizables.
// Las páginas no retienen objetos GDI (los crean al dibujar), así que se pueden
// generar en un hilo de fondo, cachear y dibujar en el hilo de la UI o en un
// DC de impresora (reenvío a otra impresora).

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/PageAccounting.h"

namespace cola::preview {

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
class EmfPage final : public PreviewPage {
public:
    explicit EmfPage(std::vector<std::uint8_t> bytes);
    SIZE SizeHimetric() const override { return size_; }
    void Render(HDC dc, const RECT& target) const override;
    size_t MemoryBytes() const override { return bytes_.size(); }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
    SIZE size_{21590, 27940};  // Carta por defecto
};

// Página rasterizada (imagen, miniatura o render externo). Píxeles BGRA 32 bpp.
class BitmapPage final : public PreviewPage {
public:
    BitmapPage(int width, int height, std::vector<std::uint32_t> pixels, double dpi = 96.0);
    SIZE SizeHimetric() const override;
    void Render(HDC dc, const RECT& target) const override;
    size_t MemoryBytes() const override { return pixels_.size() * sizeof(std::uint32_t); }
    int width() const { return width_; }
    int height() const { return height_; }

    // Crea una página a partir de un HBITMAP (no toma posesión del handle).
    static std::shared_ptr<BitmapPage> FromHBitmap(HBITMAP bitmap, double dpi = 96.0);

private:
    int width_;
    int height_;
    double dpi_;
    std::vector<std::uint32_t> pixels_;
};

using PagePtr = std::shared_ptr<const PreviewPage>;

// Formato de los datos del trabajo, detectado a partir de su contenido (no de
// pDatatype: un trabajo de un driver v4 dice "RAW" y contiene XPS).
// EscPage / EscPr: lenguajes propios de Epson (ESC/Page, ESC/Page-Color,
// ESC/P-R) que generan los drivers de las impresoras del LAIMI; no existe un
// intérprete libre para ellos, así que no tienen vista previa.
enum class DataFormat { Unknown, EmfSpool, Xps, Pdf, PostScript, Pcl, PclXl, Image, Text, EscPage, EscPr };

const wchar_t* DataFormatName(DataFormat format);

struct PreviewDocument {
    std::vector<PagePtr> pages;
    DataFormat format = DataFormat::Unknown;
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

    size_t MemoryBytes() const;
    bool HasPages() const { return !pages.empty(); }
};

using DocumentPtr = std::shared_ptr<const PreviewDocument>;

// Calcula el rectángulo que ocupa una página centrada en 'bounds' conservando aspecto.
RECT FitPage(SIZE pageHimetric, const RECT& bounds);

}  // namespace cola::preview
