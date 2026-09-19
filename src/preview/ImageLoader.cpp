#include "preview/ImageLoader.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>

#include "util/ComPtr.h"
#include "util/Strings.h"

namespace cola::preview {

using util::ComPtr;

namespace {

std::vector<PagePtr> DecodeFrames(IWICImagingFactory* factory, IWICBitmapDecoder* decoder, size_t maxPages,
                                  int maxDimension, std::uint32_t* totalFrames) {
    std::vector<PagePtr> pages;
    UINT count = 0;
    if (FAILED(decoder->GetFrameCount(&count))) return pages;
    if (totalFrames) *totalFrames = count;

    for (UINT i = 0; i < count && pages.size() < maxPages; ++i) {
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(i, frame.Put()))) continue;

        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        if (w == 0 || h == 0) continue;
        double dpiX = 96, dpiY = 96;
        frame->GetResolution(&dpiX, &dpiY);
        if (dpiX < 1) dpiX = 96;

        ComPtr<IWICBitmapSource> source;
        if (FAILED(frame->QueryInterface(IID_IWICBitmapSource, source.PutVoid()))) continue;

        // Reduce si excede el máximo.
        double scale = std::min(1.0, static_cast<double>(maxDimension) / std::max(w, h));
        if (scale < 1.0) {
            ComPtr<IWICBitmapScaler> scaler;
            UINT sw = std::max(1u, static_cast<UINT>(std::lround(w * scale)));
            UINT sh = std::max(1u, static_cast<UINT>(std::lround(h * scale)));
            if (SUCCEEDED(factory->CreateBitmapScaler(scaler.Put())) &&
                SUCCEEDED(scaler->Initialize(source.Get(), sw, sh, WICBitmapInterpolationModeFant))) {
                ComPtr<IWICBitmapSource> scaled;
                scaler->QueryInterface(IID_IWICBitmapSource, scaled.PutVoid());
                source = scaled;
                w = sw;
                h = sh;
                dpiX *= scale;
            }
        }

        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(converter.Put()))) continue;
        // BGRA premultiplicado; luego se compone sobre blanco.
        if (FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                         nullptr, 0.0, WICBitmapPaletteTypeCustom)))
            continue;
        std::vector<std::uint32_t> pixels(static_cast<size_t>(w) * h);
        if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size() * 4),
                                         reinterpret_cast<BYTE*>(pixels.data()))))
            continue;
        for (auto& px : pixels) {
            std::uint32_t inv = 255 - (px >> 24);
            std::uint32_t b = std::min<std::uint32_t>(255, (px & 0xFF) + inv);
            std::uint32_t g = std::min<std::uint32_t>(255, ((px >> 8) & 0xFF) + inv);
            std::uint32_t r = std::min<std::uint32_t>(255, ((px >> 16) & 0xFF) + inv);
            px = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
        pages.push_back(std::make_shared<BitmapPage>(static_cast<int>(w), static_cast<int>(h), std::move(pixels), dpiX));
    }
    return pages;
}

ComPtr<IWICImagingFactory> CreateFactory() {
    ComPtr<IWICImagingFactory> factory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                     factory.PutVoid());
    return factory;
}

}  // namespace

std::vector<PagePtr> LoadImageFile(const std::wstring& path, size_t maxPages, int maxDimension,
                                   std::uint32_t* totalFrames) {
    auto factory = CreateFactory();
    if (!factory) return {};
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  decoder.Put())))
        return {};
    return DecodeFrames(factory.Get(), decoder.Get(), maxPages, maxDimension, totalFrames);
}

std::vector<PagePtr> LoadImageMemory(const std::uint8_t* data, size_t size, size_t maxPages, int maxDimension,
                                     std::uint32_t* totalFrames) {
    auto factory = CreateFactory();
    if (!factory) return {};
    ComPtr<IStream> stream;
    *stream.Put() = SHCreateMemStream(data, static_cast<UINT>(size));
    if (!stream) return {};
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.Put())))
        return {};
    return DecodeFrames(factory.Get(), decoder.Get(), maxPages, maxDimension, totalFrames);
}

PagePtr LoadShellThumbnail(const std::wstring& path, int size) {
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_IShellItemImageFactory, factory.PutVoid())))
        return nullptr;
    HBITMAP bitmap = nullptr;
    // THUMBNAILONLY: si no hay miniatura real, no queremos el ícono genérico.
    if (FAILED(factory->GetImage({size, size}, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &bitmap)) || !bitmap)
        return nullptr;
    auto page = BitmapPage::FromHBitmap(bitmap);
    DeleteObject(bitmap);
    return page;
}

bool IsImageExtension(const std::wstring& path) {
    static const wchar_t* exts[] = {L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".tif",
                                    L".tiff", L".jfif", L".webp", L".heic", L".ico"};
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    for (const wchar_t* e : exts)
        if (_wcsicmp(ext, e) == 0) return true;
    return false;
}

}  // namespace cola::preview
