#pragma once
// Ficha "Configuración" de un trabajo: datos de JOB_INFO_2, configuración
// solicitada (DEVMODE del trabajo, contrastada con lo que declaran los datos)
// y la separación páginas / copias / caras / hojas, cada valor con su origen y
// su fiabilidad. Lógica pura (sin ventanas) para poder probarla; la usan el
// panel de vista previa y la herramienta cola_jobinspect.
//
// Criterio común (D23, D24, D25, D29): el DEVMODE efectivo es el del spool en
// EMF (core::EffectiveDevMode); los valores que declaran los datos con una
// sintaxis documentada o verificada (PJL, PrintTicket, DSC, EJL del AM-C6000)
// mandan sobre el DEVMODE; los indicios no documentados solo se informan. La
// fiabilidad de cada opción la decide core::AssessSetting y la de cada
// cantidad core::ComputePageAccounting; aquí solo se traducen a texto.
// Todo corre en el hilo de la UI (y en el proceso de cola_jobinspect).

#include <string>
#include <vector>

#include "core/PageAccounting.h"
#include "core/PrintTypes.h"
#include "preview/PreviewTypes.h"
#include "ui/PreviewPane.h"

namespace cola::app {

// Filas de la pestaña "Configuración" del panel de detalles: secciones
// "Configuración solicitada", "Páginas, caras y hojas", "Datos y vista
// previa" y, al final, "Trabajo" (JOB_INFO_2). Cada fila lleva valor, origen
// (detalle) e insignia de fiabilidad cuando aplica.
// doc puede ser nullptr (los datos del trabajo todavía no se leyeron).
// printer puede ser nullptr (no se compara con los predeterminados de la cola).
std::vector<ui::InfoRow> BuildJobSheet(const core::JobInfo& job, const core::PrinterInfo* printer,
                                       const preview::PreviewDocument* doc);

// Resumen de una línea para el encabezado del panel:
// "Carta · Vertical · Una cara · 10 copias · 10 págs. a imprimir".
std::wstring SettingsSummary(const core::JobInfo& job, const preview::PreviewDocument* doc);

// Opciones del trabajo en forma corta para las columnas de la lista, con el
// mismo criterio que la ficha (datos documentados o verificados mandan sobre
// el DEVMODE; "—" = no declarado). Sin marcas de estimado: la fiabilidad se
// ve en la pestaña Configuración.
struct ListSettings {
    std::wstring paper;        // "Carta", "A4", "100 × 150 mm"
    std::wstring orientation;  // "Vertical" / "Horizontal"
    std::wstring duplex;       // "Una cara" / "Borde largo" / "Borde corto"
    std::wstring binding;      // "Cuaderno" / "Libreta" / "No aplica" (BindingText)
    std::wstring source;       // origen del papel
};

// Cómo se pasan las hojas impresas a doble cara (D55), según la orientación y
// el borde de giro: vertical + borde largo u horizontal + borde corto = "Cuaderno"
// (se hojea de lado); vertical + borde corto u horizontal + borde largo =
// "Libreta" (se hojea hacia arriba). Una cara = "No aplica".
// orientation: DMORIENT_* o 0 si no se conoce; duplex: DMDUP_* o 0 si no se
// conoce. Sin la doble cara o, a doble cara, sin la orientación: "—".
std::wstring BindingText(short orientation, short duplex);
// Valores cortos de las columnas Papel, Orientación, Doble cara, Presentación
// y Origen para un trabajo (MakeJobRow, MakeHistoryEntry y el orden por esas
// columnas). facts: datos analizados (nullptr o sin analizar = solo DEVMODE).
// printer (puede ser nullptr) aporta los nombres de bandejas y papeles del controlador.
ListSettings ResolveListSettings(const core::JobInfo& job, const core::ContentFacts* facts,
                                 const core::PrinterInfo* printer = nullptr);

// Nombre de una bandeja. En las Epson AM-C6000 del LAIMI: "Auto selección",
// "Bandeja 1", "Bandeja 2" o "Alimentador de papel"; en otras, el que le da el
// controlador (printer->bins) o uno genérico. driver = controlador del trabajo.
// (D34: los códigos >= 256 son privados de cada controlador.)
std::wstring BinName(short bin, const std::wstring& driver, const core::PrinterInfo* printer);
// true si el controlador es el EPSON AM-C6000, cuyo EJL y bandejas se
// verificaron (D29, D34). Compara sin distinguir mayúsculas y acepta
// variantes del nombre que lo contengan.
bool IsVerifiedEpsonDriver(const std::wstring& driver);

// Textos de valores (también para la herramienta de inspección).
// PaperText: nombre largo con medidas ("Carta — 215,9 × 279,4 mm"), o "No
// declarado"; con printer, nombra los papeles propios del controlador.
std::wstring PaperText(const core::PrintSettings& settings, const core::PrinterInfo* printer = nullptr);
// "Vertical" / "Horizontal" (DMORIENT_*; cualquier otro valor = vertical).
std::wstring OrientationText(short orientation);
// "Una cara" / "Doble cara, borde largo" / "Doble cara, borde corto" (DMDUP_*).
std::wstring DuplexText(short duplex);
// Texto de la insignia de fiabilidad: Fiable / Estimado / No determinable.
std::wstring ReliabilityText(core::Reliability reliability);
// Explicación de un motivo de core::Reason ("" para Reason::None).
std::wstring ReasonText(core::Reason reason);

}  // namespace cola::app
