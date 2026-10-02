#pragma once
// Ficha "Configuración" de un trabajo: datos de JOB_INFO_2, configuración
// solicitada (DEVMODE del trabajo, contrastada con lo que declaran los datos)
// y la separación páginas / copias / caras / hojas, cada valor con su origen y
// su fiabilidad. Lógica pura (sin ventanas) para poder probarla; la usan el
// panel de vista previa y la herramienta cola_jobinspect.

#include <string>
#include <vector>

#include "core/PageAccounting.h"
#include "core/PrintTypes.h"
#include "preview/PreviewTypes.h"
#include "ui/PreviewPane.h"

namespace cola::app {

// doc puede ser nullptr (los datos del trabajo todavía no se leyeron).
// printer puede ser nullptr (no se compara con los predeterminados de la cola).
std::vector<ui::InfoRow> BuildJobSheet(const core::JobInfo& job, const core::PrinterInfo* printer,
                                       const preview::PreviewDocument* doc);

// Resumen de una línea para el encabezado del panel:
// "Carta · Vertical · Una cara · 10 copias · 10 caras" ("≈" = estimado).
std::wstring SettingsSummary(const core::JobInfo& job, const preview::PreviewDocument* doc);

// Textos de valores (también para la herramienta de inspección).
std::wstring PaperText(const core::PrintSettings& settings);
std::wstring OrientationText(short orientation);
std::wstring DuplexText(short duplex);
std::wstring ReliabilityText(core::Reliability reliability);
std::wstring ReasonText(core::Reason reason);

}  // namespace cola::app
