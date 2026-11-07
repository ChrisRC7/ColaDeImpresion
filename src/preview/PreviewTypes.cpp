// Implementación de las páginas de vista previa (EMF y bitmap), nombres de
// formato y ajuste de página al área de dibujo. Ver PreviewTypes.h.
#include "preview/PreviewTypes.h"

#include <algorithm>
#include <cmath>

namespace cola::preview {

EmfPage::EmfPage(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {
    // rclFrame del ENHMETAHEADER = rectángulo de la imagen en centésimas de
    // mm (HIMETRIC), que es justo el tamaño de la página impresa. Si falta o
    // es inválido queda el tamaño Carta predeterminado.
    if (bytes_.size() >= sizeof(ENHMETAHEADER)) {
        const auto* header = reinterpret_cast<const ENHMETAHEADER*>(bytes_.data());
        LONG w = header->rclFrame.right - header->rclFrame.left;
        LONG h = header->rclFrame.bottom - header->rclFrame.top;
        if (w > 0 && h > 0) size_ = {w, h};
    }
}

// Crea el HENHMETAFILE desde los bytes, lo reproduce estirado a 'target' y
// lo libera. SaveDC/RestoreDC deshacen el recorte y cualquier cambio de
// estado que el propio metarchivo haga en el DC. Las fuentes incrustadas en
// el spool no se reproducen: si falta una fuente, Windows la sustituye.
void EmfPage::Render(HDC dc, const RECT& target) const {
    HENHMETAFILE emf = SetEnhMetaFileBits(static_cast<UINT>(bytes_.size()), bytes_.data());
    if (!emf) return;
    int saved = SaveDC(dc);
    IntersectClipRect(dc, target.left, target.top, target.right, target.bottom);
    PlayEnhMetaFile(dc, emf, &target);
    RestoreDC(dc, saved);
    DeleteEnhMetaFile(emf);
}

BitmapPage::BitmapPage(int width, int height, std::vector<std::uint32_t> pixels, double dpi)
    : width_(width), height_(height), dpi_(dpi > 0 ? dpi : 96.0), pixels_(std::move(pixels)) {}

// píxeles / dpi = pulgadas; x 2540 = centésimas de milímetro.
SIZE BitmapPage::SizeHimetric() const {
    return {static_cast<LONG>(std::lround(width_ / dpi_ * 2540.0)),
            static_cast<LONG>(std::lround(height_ / dpi_ * 2540.0))};
}

void BitmapPage::Render(HDC dc, const RECT& target) const {
    if (pixels_.empty()) return;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width_;
    bmi.bmiHeader.biHeight = -height_;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    // HALFTONE promedia píxeles al reducir (mucho mejor que el modo por
    // defecto, que descarta filas); la documentación de GDI exige fijar el
    // origen del pincel después de activarlo.
    int oldMode = SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, target.left, target.top, target.right - target.left, target.bottom - target.top, 0, 0,
                  width_, height_, pixels_.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    SetStretchBltMode(dc, oldMode);
}

std::shared_ptr<BitmapPage> BitmapPage::FromHBitmap(HBITMAP bitmap, double dpi) {
    BITMAP info{};
    if (!bitmap || !GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight == 0)
        return nullptr;
    const int width = info.bmWidth;
    const int height = std::abs(info.bmHeight);
    std::vector<std::uint32_t> pixels(static_cast<size_t>(width) * height);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // pide las filas de arriba hacia abajo
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    // GetDIBits necesita un DC compatible; el de la pantalla basta (el
    // bitmap no debe estar seleccionado en ningún DC).
    HDC screen = GetDC(nullptr);
    int lines = GetDIBits(screen, bitmap, 0, static_cast<UINT>(height), pixels.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    if (lines == 0) return nullptr;

    // Las miniaturas del Shell traen alfa premultiplicado; componemos sobre blanco.
    // Si ningún píxel usa alfa, el bitmap es opaco y se deja igual.
    bool hasAlpha = std::any_of(pixels.begin(), pixels.end(), [](std::uint32_t px) { return (px >> 24) != 0; });
    for (auto& px : pixels) {
        std::uint32_t a = hasAlpha ? (px >> 24) : 255;
        std::uint32_t inv = 255 - a;
        std::uint32_t b = std::min<std::uint32_t>(255, (px & 0xFF) + inv);
        std::uint32_t g = std::min<std::uint32_t>(255, ((px >> 8) & 0xFF) + inv);
        std::uint32_t r = std::min<std::uint32_t>(255, ((px >> 16) & 0xFF) + inv);
        px = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    return std::make_shared<BitmapPage>(width, height, std::move(pixels), dpi);
}

const wchar_t* DataFormatName(DataFormat format) {
    switch (format) {
        case DataFormat::EmfSpool: return L"EMF";
        case DataFormat::Xps: return L"XPS";
        case DataFormat::Pdf: return L"PDF";
        case DataFormat::PostScript: return L"PostScript";
        case DataFormat::Pcl: return L"PCL";
        case DataFormat::PclXl: return L"PCL XL";
        case DataFormat::Image: return L"Imagen";
        case DataFormat::Text: return L"Texto";
        case DataFormat::EscPage: return L"ESC/Page (Epson)";
        case DataFormat::EscPr: return L"ESC/P-R (Epson)";
        default: return L"RAW";
    }
}

// No cuenta las cadenas ni facts (son pequeños frente a las páginas).
size_t PreviewDocument::MemoryBytes() const {
    size_t total = sizeof(PreviewDocument);
    for (const auto& p : pages) total += p->MemoryBytes();
    return total;
}

// Escala uniforme = la menor de ancho/alto (la página cabe entera) y se
// centra en ambos ejes.
RECT FitPage(SIZE page, const RECT& bounds) {
    const int bw = bounds.right - bounds.left;
    const int bh = bounds.bottom - bounds.top;
    if (page.cx <= 0 || page.cy <= 0 || bw <= 0 || bh <= 0) return bounds;
    double scale = std::min(static_cast<double>(bw) / page.cx, static_cast<double>(bh) / page.cy);
    int w = std::max(1, static_cast<int>(std::lround(page.cx * scale)));
    int h = std::max(1, static_cast<int>(std::lround(page.cy * scale)));
    int x = bounds.left + (bw - w) / 2;
    int y = bounds.top + (bh - h) / 2;
    return {x, y, x + w, y + h};
}

}  // namespace cola::preview
