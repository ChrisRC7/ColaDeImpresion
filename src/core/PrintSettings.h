#pragma once
// Configuración de impresión solicitada para un trabajo, leída del DEVMODE del
// propio trabajo (JOB_INFO_2.pDevMode), no de los valores predeterminados de
// la impresora.
//
// Cada campo solo se marca como válido si dmFields lo declara Y el campo cabe
// dentro de dmSize (un DEVMODE de una versión antigua o truncado puede traer
// el bit puesto sin el campo). Los valores son los públicos de wingdi.h; las
// opciones privadas del controlador (dmDriverExtra) no se interpretan.

#include <windows.h>

#include <cstdint>
#include <string>

namespace cola::core {

// Valor que puede no estar disponible.
template <typename T>
struct Field {
    T value{};
    bool valid = false;

    void Set(T v) {
        value = v;
        valid = true;
    }
};

struct PrintSettings {
    bool present = false;            // el trabajo tenía DEVMODE
    std::uint32_t fields = 0;        // dmFields tal como vino
    std::uint16_t size = 0;          // dmSize
    std::uint16_t driverExtra = 0;   // bytes privados del controlador (no se interpretan)
    std::uint16_t specVersion = 0;
    std::uint16_t driverVersion = 0;
    std::wstring deviceName;

    Field<short> paperSize;      // DMPAPER_*
    Field<std::wstring> formName;
    Field<short> paperWidth;     // décimas de milímetro
    Field<short> paperLength;    // décimas de milímetro
    Field<short> orientation;    // DMORIENT_PORTRAIT (1) / DMORIENT_LANDSCAPE (2)
    Field<short> duplex;         // 1 = una cara, 2 = borde largo, 3 = borde corto
    Field<short> copies;
    Field<short> collate;        // DMCOLLATE_FALSE (0) / DMCOLLATE_TRUE (1)
    Field<short> color;          // DMCOLOR_MONOCHROME (1) / DMCOLOR_COLOR (2)
    Field<short> defaultSource;  // DMBIN_*
    Field<short> printQuality;   // DMRES_* (negativo) o ppp
    Field<short> yResolution;
    Field<short> scale;          // porcentaje
    Field<std::uint32_t> nup;    // DMNUP_SYSTEM (1) / DMNUP_ONEUP (2): quién compone, no cuántas
    Field<std::uint32_t> mediaType;  // DMMEDIA_*
};

// Lee un DEVMODE. 'available' = bytes accesibles desde dm (0 = confiar en dmSize).
PrintSettings ParseDevMode(const DEVMODEW* dm, size_t available = 0);

// Tamaños estándar conocidos (DMPAPER_*), en décimas de milímetro, en retrato.
struct PaperInfo {
    short code;
    const wchar_t* nameEs;  // nombre corto ("Carta", "A4"...)
    const wchar_t* nameEn;
    short width;
    short length;
};
const PaperInfo* FindPaper(short code);
// Tamaño estándar con esas medidas (±2 mm, en cualquier orientación) o nullptr.
const PaperInfo* FindPaperBySize(short width, short length);

// Tamaño efectivo del papel (décimas de mm, en retrato) y si es personalizado.
// dmPaperWidth/dmPaperLength, si son válidos, prevalecen sobre dmPaperSize
// (así lo define la documentación de DEVMODE).
struct PaperSize {
    bool known = false;
    bool custom = false;
    short width = 0;
    short length = 0;
};
PaperSize EffectivePaper(const PrintSettings& s);

// true si las opciones visibles (papel, orientación, doble cara, copias,
// color, intercalado, bandeja) coinciden con las de otro DEVMODE. Se usa para
// avisar cuando el trabajo trae exactamente los valores predeterminados de la
// cola (puede que el usuario no haya cambiado nada, o que el DEVMODE no
// refleje lo que el controlador aplicó en el cliente).
bool SameVisibleSettings(const PrintSettings& a, const PrintSettings& b);

}  // namespace cola::core
