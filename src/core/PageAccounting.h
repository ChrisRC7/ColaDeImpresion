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
//
// Decisiones: D24 (fiabilidad de cada magnitud), D25 (opciones declaradas en
// los datos: PrintTicket/DSC/PJL confirman; EJL solo es indicio), D29 (EJL
// verificado con el AM-C6000 manda sobre el DEVMODE del servidor), D31 (la
// columna Páginas = páginas × copias), D46/D53 (páginas ESC/Page contadas en
// los datos, dorso en blanco). Flujo: preview/ analiza los datos y llena un
// ContentFacts; ComputePageAccounting combina eso con el JobInfo y devuelve un
// PageAccounting que usan la tabla (RowBuilder), la ficha y el historial.

#include <cstdint>
#include <string>
#include <vector>

#include "core/PrintSettings.h"
#include "core/PrintTypes.h"

namespace cola::core {

// Familia del contenido del trabajo (decide quién aplica las copias).
enum class DataKind {
    Unknown,  // no se sabe (sin datos leídos y tipo de datos no reconocible)
    Emf,  // EMF del spooler: el procesador de impresión del servidor reproduce las páginas
    Xps,  // paquete XPS (drivers v4 / XPSDrv): manda el PrintTicket
    Pdl,  // lenguaje de impresora ya generado (PCL, PostScript, ESC/Page...): RAW
};

// Lo que se pudo averiguar leyendo los datos del trabajo (lo llena preview/).
// preview::JobFactsService lo calcula una vez por trabajo (D31) y guarda solo
// esta estructura, no los datos. Los campos Field<> son inválidos cuando los
// datos no los declaran.
struct ContentFacts {
    bool analyzed = false;  // se leyeron y analizaron los datos
    bool complete = false;  // se leyeron completos (no se cortaron por tamaño)
    DataKind kind = DataKind::Unknown;  // familia detectada por contenido (no por pDatatype)
    std::wstring language;  // "EMF", "XPS", "ESC/Page", "PCL"... (para mostrar)

    // De dónde sale el conteo de páginas de los datos:
    // EmfRecords: páginas EMF del spool; XpsFixedPages: FixedPage del paquete;
    // PostScriptDsc: comentarios %%Pages del DSC;
    // EscPageFormFeeds: fines de página (FF) contados en los comandos ESC/Page.
    enum class PageSource { None, EmfRecords, XpsFixedPages, PostScriptDsc, EscPageFormFeeds };
    PageSource pageSource = PageSource::None;
    std::uint32_t pages = 0;  // páginas contadas en los datos (0 = no se contaron)
    // Las páginas se contaron recorriendo todos los datos aunque solo se guardó
    // el principio (complete = false): ESC/Page de más de 16 MB.
    bool pagesFromAllData = false;
    // La secuencia de páginas se repite de forma exacta con este período
    // (p. ej. 1 página repetida 10 veces => 1): indicio de copias hechas por la
    // aplicación. 0 = no se detectó repetición o no se pudo comprobar.
    std::uint32_t repeatPeriod = 0;
    bool repeatChecked = false;  // se compararon las páginas (EMF/XPS completos)

    // Opciones declaradas dentro de los datos.
    // Pjl: @PJL SET (HP y compatibles); Ejl: lenguaje de trabajos de Epson;
    // PrintTicket: XML del paquete XPS; PostScriptDsc: %%BeginFeature/DSC.
    enum class SettingsSource { None, Pjl, Ejl, PrintTicket, PostScriptDsc };
    SettingsSource settingsSource = SettingsSource::None;
    // false = sintaxis sin documentación pública (EJL de Epson): solo indicio.
    // (Con un controlador verificado, ver verifiedDriver, se considera documentado: D29.)
    bool settingsDocumented = true;
    Field<std::uint32_t> copies;  // copias declaradas en los datos (PJL COPIES/QTY, EJL QT...)
    Field<short> duplex;       // mismos valores que dmDuplex
    Field<short> orientation;  // mismos valores que dmOrientation
    Field<short> color;        // mismos valores que dmColor
    Field<short> collate;      // mismos valores que dmCollate
    // Bandeja, como el código DMBIN equivalente del controlador (ESC/Page: "GS n;m iuE").
    Field<short> sourceBin;
    Field<std::uint32_t> pagesPerSheet;  // páginas por cara (N-up) declaradas en los datos
    Field<std::wstring> paperName;  // nombre tal como viene en los datos
    Field<short> paperWidth;        // décimas de mm
    Field<short> paperLength;       // décimas de mm
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

// Fiabilidad de un valor (D24): Reliable = confirmado por una fuente que manda;
// Estimated = valor probable, con un Reason que explica la duda;
// Unknown = no determinable (también con su Reason). La UI muestra "—" o una
// marca de estimado según esto.
enum class Reliability { Reliable, Estimated, Unknown };

// De dónde sale un valor.
enum class Basis {
    None,               // sin fuente (valor desconocido o trivial)
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
// Cada valor se traduce a un texto explicativo en app/JobSheet.
enum class Reason {
    None,               // sin motivo: el valor es fiable
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

// Una magnitud con su procedencia: valor, fiabilidad, de dónde sale y, si no
// es fiable, por qué. value solo tiene sentido si Known().
struct Quantity {
    std::uint32_t value = 0;
    Reliability reliability = Reliability::Unknown;
    Basis basis = Basis::None;
    Reason reason = Reason::None;

    // true si hay un valor (fiable o estimado).
    bool Known() const { return reliability != Reliability::Unknown; }
};

// Resultado completo del cálculo de páginas/copias/caras/hojas de un trabajo.
struct PageAccounting {
    DataKind kind = DataKind::Unknown;  // familia usada para decidir quién aplica las copias
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
    std::uint32_t spoolerTotalPages = 0;  // JOB_INFO_2.TotalPages tal cual
    std::uint32_t pagesPrinted = 0;       // JOB_INFO_2.PagesPrinted (solo progreso)
    bool copiesConflict = false;  // DEVMODE y datos declaran copias distintas
    bool duplexConflict = false;  // DEVMODE y datos declaran doble cara distinta
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
// Se deduce de pDatatype ("NT EMF...", "XPS...", "RAW", "TEXT") y del
// procesador de impresión (MS_XPS_PROC). Es solo una aproximación: un driver
// v4 declara RAW y envía OpenXPS.
DataKind KindFromJob(const JobInfo& job);

// Calcula todas las magnitudes. facts puede ser nullptr (datos no leídos).
// Un facts con analyzed = false se trata igual que nullptr. Pasos: páginas en
// los datos, páginas lógicas y repeticiones, copias, N-up, doble cara, caras
// y hojas; al final las hojas que declaró el controlador (EJL) si las hay.
PageAccounting ComputePageAccounting(const JobInfo& job, const ContentFacts* facts);

// Fiabilidad de una opción (papel, orientación, color...) según de dónde salga:
// en EMF el DEVMODE del trabajo es lo que reproduce el servidor; en XPS manda
// el PrintTicket; en RAW el controlador ya aplicó las opciones en el cliente y
// el DEVMODE del servidor solo es fiable si los datos lo confirman.
// (Como Quantity pero sin valor: el valor lo elige quien llama.)
struct SettingAssessment {
    Reliability reliability = Reliability::Unknown;
    Basis basis = Basis::None;
    Reason reason = Reason::None;
};
// Parámetros:
//  kind:              familia de los datos (decide cuánto vale el DEVMODE solo).
//  inDevMode:         el DEVMODE efectivo declara la opción.
//  inContent:         los datos declaran la opción.
//  contentDocumented: la sintaxis de los datos está documentada/verificada.
//  agree:             DEVMODE y datos dicen lo mismo (si están ambos).
// dataRead = se leyeron los datos del trabajo (en EMF, sin ellos el DEVMODE de
// JOB_INFO_2 puede estar desactualizado y solo se considera estimado).
SettingAssessment AssessSetting(DataKind kind, bool inDevMode, bool inContent, bool contentDocumented, bool agree,
                                bool dataRead = true);

// Peor de dos fiabilidades (Unknown < Estimated < Reliable).
Reliability Worst(Reliability a, Reliability b);

// Detecta si una secuencia de huellas de página es la misma subsecuencia
// repetida (p. ej. A B A B A B => 2). Devuelve 0 si no hay repetición.
// Devuelve el período más corto que divide exactamente la secuencia y se
// repite al menos dos veces; con menos de 2 páginas siempre es 0. Las huellas
// las calcula preview/ sobre el contenido de cada página EMF/XPS (D24).
std::uint32_t RepeatPeriod(const std::vector<std::uint64_t>& pageHashes);

}  // namespace cola::core
