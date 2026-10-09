#pragma once
// Separa las magnitudes de un trabajo que suelen confundirse:
//   - páginas en los datos  : lo que la aplicación envió (JOB_INFO_2.TotalPages
//                             o el conteo de páginas EMF/XPS de los datos)
//   - páginas lógicas       : las de UNA copia del documento
//   - copias solicitadas
//   - caras físicas previstas y hojas físicas previstas
//
// Reglas (ver docs/DATOS_DE_IMPRESION.md, con las pruebas que las respaldan):
//   - Nunca se multiplica TotalPages × dmCopies a ciegas. TotalPages cuenta las
//     páginas que la aplicación generó: si la aplicación hizo las copias ella
//     misma (repitiendo páginas) ya están incluidas y dmCopies vale 1; si las
//     hace el controlador o el procesador de impresión, TotalPages es de una
//     sola copia y dmCopies dice cuántas.
//   - Cada valor lleva su fiabilidad: Fiable, Estimado (con motivo) o No
//     determinable (con motivo). Un cálculo hereda la peor fiabilidad de sus
//     datos de entrada.
//   - PagesPrinted no es un contador de hojas: solo se muestra como progreso.
//
// Lógica pura (sin textos ni llamadas al sistema): la interfaz traduce los
// motivos en app/JobSheet.

#include <cstdint>
#include <string>
#include <vector>

#include "core/PrintSettings.h"
#include "core/PrintTypes.h"

namespace cola::core {

// Familia del contenido del trabajo (decide quién aplica las copias).
enum class DataKind {
    Unknown,
    Emf,  // EMF del spooler: el procesador de impresión del servidor reproduce las páginas
    Xps,  // paquete XPS (drivers v4 / XPSDrv): manda el PrintTicket
    Pdl,  // lenguaje de impresora ya generado (PCL, PostScript, ESC/Page...): RAW
};

// Lo que se pudo averiguar leyendo los datos del trabajo (lo llena preview/).
struct ContentFacts {
    bool analyzed = false;  // se leyeron y analizaron los datos
    bool complete = false;  // se leyeron completos (no se cortaron por tamaño)
    DataKind kind = DataKind::Unknown;
    std::wstring language;  // "EMF", "XPS", "ESC/Page", "PCL"... (para mostrar)

    // EscPageFormFeeds: fines de página (FF) contados en los comandos ESC/Page.
    enum class PageSource { None, EmfRecords, XpsFixedPages, PostScriptDsc, EscPageFormFeeds };
    PageSource pageSource = PageSource::None;
    std::uint32_t pages = 0;
    // La secuencia de páginas se repite de forma exacta con este período
    // (p. ej. 1 página repetida 10 veces => 1): indicio de copias hechas por la
    // aplicación. 0 = no se detectó repetición o no se pudo comprobar.
    std::uint32_t repeatPeriod = 0;
    bool repeatChecked = false;  // se compararon las páginas (EMF/XPS completos)

    // Opciones declaradas dentro de los datos.
    enum class SettingsSource { None, Pjl, Ejl, PrintTicket, PostScriptDsc };
    SettingsSource settingsSource = SettingsSource::None;
    // false = sintaxis sin documentación pública (EJL de Epson): solo indicio.
    bool settingsDocumented = true;
    Field<std::uint32_t> copies;
    Field<short> duplex;       // mismos valores que dmDuplex
    Field<short> orientation;  // mismos valores que dmOrientation
    Field<short> color;        // mismos valores que dmColor
    Field<short> collate;      // mismos valores que dmCollate
    // Bandeja, como el código DMBIN equivalente del controlador (ESC/Page: "GS n;m iuE").
    Field<short> sourceBin;
    Field<std::uint32_t> pagesPerSheet;
    Field<std::wstring> paperName;  // nombre tal como viene en los datos
    Field<short> paperWidth;        // décimas de mm
    Field<short> paperLength;
    std::wstring rawSettings;       // opciones crudas (para mostrarlas tal cual)
    // Hojas físicas que declaró el controlador en los datos (EJL "JI PAGES"):
    // ya incluyen copias y doble cara.
    Field<std::uint32_t> sheetsCounter;
    // Controlador con el que se verificó la sintaxis EJL (vacío = solo indicio).
    std::wstring verifiedDriver;
    // ESC/Page del controlador EPSON AM-C6000 con los datos cortados por tamaño:
    // no se pudieron contar las páginas y, a doble cara, TotalPages cuenta el
    // dorso en blanco que completa la última hoja (medido: 1 -> 2, 2 -> 2, 3 -> 4).
    bool spoolerCountsBlankBack = false;

    // EMF: DEVMODE que viaja dentro del spool (registro EMRI_DEVMODE vigente para
    // la primera página). Es el que aplica el procesador de impresión; puede no
    // coincidir con JOB_INFO_2.pDevMode (la aplicación lo cambió tras StartDoc).
    PrintSettings spoolDevMode;
    bool spoolDevModeChanges = false;  // otro DEVMODE distinto a mitad del trabajo
};

enum class Reliability { Reliable, Estimated, Unknown };

// De dónde sale un valor.
enum class Basis {
    None,
    SpoolerTotalPages,  // JOB_INFO_2.TotalPages
    ContentPages,       // páginas contadas en los datos (EMF / XPS)
    ContentRepeat,      // período de repetición detectado en los datos
    DevMode,            // DEVMODE del trabajo (JOB_INFO_2.pDevMode)
    SpoolDevMode,       // DEVMODE dentro del spool EMF (EMRI_DEVMODE)
    DevModeAndContent,  // DEVMODE y datos coinciden
    Content,            // solo los datos (PJL / PrintTicket)
    AssumedDefault,     // supuesto (p. ej. 1 copia, una cara)
    SinglePage,         // 1 página lógica: 1 cara por copia sea cual sea el N-up
    DriverSheets,       // hojas físicas declaradas por el controlador (EJL "JI PAGES")
    Calculated,         // cálculo a partir de otras magnitudes
};

// Por qué un valor no es fiable o no se pudo determinar.
enum class Reason {
    None,
    NoDevMode,          // el trabajo no trae DEVMODE
    FieldNotDeclared,   // dmFields no declara el campo
    StillSpooling,      // el trabajo todavía se está recibiendo
    PagesUnknown,       // el spooler no informa páginas (0) y los datos no se pudieron contar
    PossibleAppCopies,  // no se pudo comprobar si la aplicación repitió páginas para hacer copias
    RepeatDetected,     // los datos repiten la misma secuencia de páginas
    RawUnverified,      // RAW: el controlador ya aplicó las opciones en los datos y no se pudieron confirmar
    XpsTicketUnread,    // XPS: no se pudo leer el PrintTicket que manda sobre el DEVMODE
    Conflict,           // el DEVMODE y los datos no coinciden
    UndocumentedHint,   // solo hay un indicio en una sintaxis no documentada (EJL)
    HintAgrees,         // el DEVMODE coincide con un indicio no documentado (EJL): sigue sin confirmarse
    DataNotRead,        // EMF sin leer los datos: JOB_INFO_2.pDevMode puede no ser el que se aplica
    NupPrivate,         // las páginas por cara son una opción privada del controlador
    KindUnknown,        // no se sabe en qué formato están los datos
    DependsOnInputs,    // cálculo con alguna entrada estimada o no determinable
    PagesMismatch,      // TotalPages y el conteo de los datos no coinciden
    BlankBackCounted,   // TotalPages par puede incluir el dorso en blanco que agrega el controlador Epson
};

struct Quantity {
    std::uint32_t value = 0;
    Reliability reliability = Reliability::Unknown;
    Basis basis = Basis::None;
    Reason reason = Reason::None;

    bool Known() const { return reliability != Reliability::Unknown; }
};

struct PageAccounting {
    DataKind kind = DataKind::Unknown;
    Quantity dataPages;     // páginas en los datos (incluye las repetidas por la aplicación)
    Quantity logicalPages;  // páginas de una copia
    Quantity copies;        // copias solicitadas (totales)
    Quantity appCopies;     // repeticiones detectadas en los datos (1 si no hay)
    Quantity deviceCopies;  // copias que aplica el controlador / la impresora (sin las repeticiones)
    // Páginas a imprimir: páginas en los datos x copias del controlador. Es lo que
    // se ve en la lista ("3 págs. x 3 copias = 9"); no depende de las páginas por
    // cara ni de la doble cara (eso es caras y hojas).
    Quantity printPages;
    Quantity pagesPerSide;  // N-up
    Quantity duplex;        // 1 = una cara, 2 = borde largo, 3 = borde corto
    Quantity sides;         // caras físicas previstas (todas las copias)
    Quantity sheets;        // hojas físicas previstas (todas las copias)
    // Datos útiles para explicar el resultado.
    std::uint32_t spoolerTotalPages = 0;
    std::uint32_t pagesPrinted = 0;
    bool copiesConflict = false;  // DEVMODE y datos declaran copias distintas
    bool duplexConflict = false;
    bool pagesMismatch = false;   // TotalPages != páginas contadas en los datos
    bool sheetsMismatch = false;  // el contador de hojas del controlador no coincide con el cálculo
    std::uint32_t calculatedSheets = 0;  // hojas según el cálculo (para explicar una diferencia)
    // Se usó el DEVMODE del spool EMF en lugar de JOB_INFO_2.pDevMode.
    bool devModeFromSpool = false;
    // JOB_INFO_2.pDevMode no coincide con el DEVMODE efectivo del spool.
    bool jobDevModeStale = false;
};

// DEVMODE que realmente se aplica: el del spool EMF si los datos lo traen; si
// no, JOB_INFO_2.pDevMode. fromSpool indica cuál se eligió.
const PrintSettings& EffectiveDevMode(const JobInfo& job, const ContentFacts* facts, bool* fromSpool = nullptr);

// Familia del contenido según el trabajo (cuando no se pudieron leer los datos).
DataKind KindFromJob(const JobInfo& job);

// Calcula todas las magnitudes. facts puede ser nullptr (datos no leídos).
PageAccounting ComputePageAccounting(const JobInfo& job, const ContentFacts* facts);

// Fiabilidad de una opción (papel, orientación, color...) según de dónde salga:
// en EMF el DEVMODE del trabajo es lo que reproduce el servidor; en XPS manda
// el PrintTicket; en RAW el controlador ya aplicó las opciones en el cliente y
// el DEVMODE del servidor solo es fiable si los datos lo confirman.
struct SettingAssessment {
    Reliability reliability = Reliability::Unknown;
    Basis basis = Basis::None;
    Reason reason = Reason::None;
};
// dataRead = se leyeron los datos del trabajo (en EMF, sin ellos el DEVMODE de
// JOB_INFO_2 puede estar desactualizado y solo se considera estimado).
SettingAssessment AssessSetting(DataKind kind, bool inDevMode, bool inContent, bool contentDocumented, bool agree,
                                bool dataRead = true);

// Peor de dos fiabilidades (Unknown < Estimated < Reliable).
Reliability Worst(Reliability a, Reliability b);

// Detecta si una secuencia de huellas de página es la misma subsecuencia
// repetida (p. ej. A B A B A B => 2). Devuelve 0 si no hay repetición.
std::uint32_t RepeatPeriod(const std::vector<std::uint64_t>& pageHashes);

}  // namespace cola::core
