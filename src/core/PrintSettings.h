#pragma once
// Configuración de impresión solicitada para un trabajo, leída del DEVMODE del
// propio trabajo (JOB_INFO_2.pDevMode), no de los valores predeterminados de
// la impresora.
//
// Cada campo solo se marca como válido si dmFields lo declara Y el campo cabe
// dentro de dmSize (un DEVMODE de una versión antigua o truncado puede traer
// el bit puesto sin el campo). Los valores son los públicos de wingdi.h; las
// opciones privadas del controlador (dmDriverExtra) no se interpretan.
//
// Ojo (D23, GUIA_IA §6): el DEVMODE de JOB_INFO_2 no siempre es lo aplicado. En
// EMF manda el EMRI_DEVMODE del spool (Word lo cambia con ResetDC tras
// StartDoc) y en los trabajos RAW por Client Side Rendering el del servidor
// suele ser el predeterminado de la cola (D29). Este módulo solo interpreta un
// DEVMODE; decidir cuál vale es tarea de EffectiveDevMode / PageAccounting.
// También se usa para el DEVMODE predeterminado de la cola
// (PrinterInfo::defaults), solo para comparar.

#include <windows.h>

#include <cstdint>
#include <string>

namespace cola::core {

// Valor que puede no estar disponible.
// Distingue "no declarado/no válido" de un valor real (p. ej. copies = 0 no
// es lo mismo que "el DEVMODE no dice las copias").
template <typename T>
struct Field {
    T value{};           // valor leído (solo significativo si valid)
    bool valid = false;  // el DEVMODE lo declara, cabe en dmSize y está en rango

    // Asigna el valor y lo marca como válido.
    void Set(T v) {
        value = v;
        valid = true;
    }
};

// Opciones públicas de un DEVMODE ya validadas (resultado de ParseDevMode).
struct PrintSettings {
    bool present = false;            // el trabajo tenía DEVMODE
    std::uint32_t fields = 0;        // dmFields tal como vino
    std::uint16_t size = 0;          // dmSize
    std::uint16_t driverExtra = 0;   // bytes privados del controlador (no se interpretan)
    std::uint16_t specVersion = 0;   // dmSpecVersion (versión de la estructura)
    std::uint16_t driverVersion = 0;  // dmDriverVersion (versión propia del controlador)
    std::wstring deviceName;         // dmDeviceName (nombre del dispositivo/controlador)

    Field<short> paperSize;      // DMPAPER_*
    Field<std::wstring> formName;  // dmFormName ("Carta", "A4"...): nombre del formulario
    Field<short> paperWidth;     // décimas de milímetro
    Field<short> paperLength;    // décimas de milímetro
    Field<short> orientation;    // DMORIENT_PORTRAIT (1) / DMORIENT_LANDSCAPE (2)
    Field<short> duplex;         // 1 = una cara, 2 = borde largo, 3 = borde corto
    // dmCopies (>= 1). Puede no ser lo que se imprime: la aplicación puede
    // repetir las páginas ella misma (D24); ver PageAccounting.
    Field<short> copies;
    Field<short> collate;        // DMCOLLATE_FALSE (0) / DMCOLLATE_TRUE (1)
    Field<short> color;          // DMCOLOR_MONOCHROME (1) / DMCOLOR_COLOR (2)
    Field<short> defaultSource;  // DMBIN_*
    Field<short> printQuality;   // DMRES_* (negativo) o ppp
    Field<short> yResolution;    // resolución vertical en ppp (si difiere de printQuality)
    Field<short> scale;          // porcentaje
    Field<std::uint32_t> nup;    // DMNUP_SYSTEM (1) / DMNUP_ONEUP (2): quién compone, no cuántas
    Field<std::uint32_t> mediaType;  // DMMEDIA_*
};

// Lee un DEVMODE. 'available' = bytes accesibles desde dm (0 = confiar en dmSize).
// Con 'available' se protege la lectura de un DEVMODE que viene de datos
// externos (p. ej. el EMRI_DEVMODE de un spool EMF): nunca se lee más allá.
// Devuelve present = false si dm es nulo o demasiado corto para tener cabecera.
PrintSettings ParseDevMode(const DEVMODEW* dm, size_t available = 0);

// Tamaños estándar conocidos (DMPAPER_*), en décimas de milímetro, en retrato.
struct PaperInfo {
    short code;             // DMPAPER_* (0 = sin código estándar, solo por medidas)
    const wchar_t* nameEs;  // nombre corto ("Carta", "A4"...)
    const wchar_t* nameEn;  // nombre en inglés (UI en inglés)
    short width;            // ancho en retrato, décimas de mm
    short length;           // alto en retrato, décimas de mm
};
// Papel estándar con ese código DMPAPER_*, o nullptr si no está en la tabla
// (los códigos >= 256 son propios de cada controlador y nunca están).
const PaperInfo* FindPaper(short code);
// Tamaño estándar con esas medidas (±2 mm, en cualquier orientación) o nullptr.
const PaperInfo* FindPaperBySize(short width, short length);

// Tamaño efectivo del papel (décimas de mm, en retrato) y si es personalizado.
// dmPaperWidth/dmPaperLength, si son válidos, prevalecen sobre dmPaperSize
// (así lo define la documentación de DEVMODE).
struct PaperSize {
    bool known = false;   // se conocen las medidas (width/length válidos)
    bool custom = false;  // no coincide con el papel estándar declarado (o es DMPAPER_USER)
    short width = 0;      // décimas de mm
    short length = 0;     // décimas de mm
};
// Calcula el papel efectivo de un DEVMODE ya interpretado (ver PaperSize).
PaperSize EffectivePaper(const PrintSettings& s);

// true si las opciones visibles (papel, orientación, doble cara, copias,
// color, intercalado, bandeja) coinciden con las de otro DEVMODE. Se usa para
// avisar cuando el trabajo trae exactamente los valores predeterminados de la
// cola (puede que el usuario no haya cambiado nada, o que el DEVMODE no
// refleje lo que el controlador aplicó en el cliente).
bool SameVisibleSettings(const PrintSettings& a, const PrintSettings& b);

}  // namespace cola::core
