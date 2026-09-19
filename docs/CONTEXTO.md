# Contexto del proyecto — Cola de Impresión (LAIMI)

> Documento **vivo**: se actualiza con cada cambio relevante. Es la fuente de
> contexto para cualquier IA o persona que continúe el trabajo. El modelo de
> trabajo está en [GUIA_IA.md](GUIA_IA.md); las tareas en [TODO.md](../TODO.md).

**Última actualización:** 2026-09-19

---

## 1. Propósito y entorno

- **Qué es**: aplicación de escritorio para Windows que reemplaza la ventana de cola de impresión nativa, con vista previa de documentos, control por teclado e interfaz minimalista con modo claro/oscuro.
- **Dónde se usa**: en el **LAIMI** del **Instituto Tecnológico de Costa Rica (TEC)**, para que el personal controle las colas de impresión que se manejan en un **servidor de impresión** (los estudiantes imprimen desde las PCs del laboratorio a impresoras compartidas en ese servidor).
- **No** se usa en la PC del desarrollador: allí solo se compila y prueba.
- **Usuarios**: operadores/administradores del laboratorio. Necesitan ver trabajos de todos los estudiantes, cancelarlos, pausarlos, reordenarlos, reenviarlos a otra impresora y ver qué están imprimiendo.

## 2. Decisiones técnicas (y por qué)

| # | Decisión | Motivo |
|---|---|---|
| D1 | **C++20 + Win32 API**, sin frameworks de UI | Pedido explícito del usuario; máximo control, ejecutable autónomo pequeño (~1,4 MB), sin runtime. |
| D2 | **CMake**, compilable con **MinGW-w64 (WinLibs)** y MSVC | En la PC de desarrollo no hay Visual Studio; se usa WinLibs portable en `D:\Programacion\Herramientas\winlibs`. Enlazado estático (`-static`). |
| D3 | **Capa de gestión sobre el spooler**, no reemplazo del servicio | Se usa la API pública de winspool (`EnumPrinters`, `EnumJobs`, `SetJob`, `SetPrinter`...). El spooler de Windows sigue haciendo el trabajo; la app solo lo controla. |
| D4 | **Conexión a servidor remoto** (`\\SERVIDOR`) o equipo local | La app puede correr en la PC del operador (conectándose al servidor) o en el propio servidor. Lista de servidores recientes en el menú. |
| D5 | **Tiempo real con `FindFirstPrinterChangeNotification`** por impresora + **sondeo de respaldo** (3 s por defecto) | Las notificaciones remotas pueden fallar por firewall/RPC; el sondeo garantiza que la lista siempre se actualice. La barra de estado indica "En vivo" o "Actualización por sondeo". |
| D6 | **Vista previa desde el archivo de spool** del servidor | Los archivos originales están en las PCs de los estudiantes y no son accesibles. El spool se lee por `\\SRV\C$\...\spool\PRINTERS` o `\\SRV\ADMIN$\...` (requiere ser administrador del servidor) o por una carpeta alternativa configurable (`spoolDirOverrides`). |
| D7 | **EMF nativo; PS/PDF/PCL/XPS con Ghostscript/GhostPCL/GhostXPS opcionales** | Con *Client Side Rendering* (por defecto en impresoras compartidas) el servidor recibe RAW ya renderizado (PCL/PostScript). Windows no trae intérpretes para eso, y el rasterizador XPS de Windows solo está disponible dentro del pipeline de drivers. Ghostscript se usa como proceso externo (no se enlaza: evita problemas de licencia AGPL) y se autodetecta. |
| D8 | **UI con controles propios pintados con GDI + GDI+** | Controlar colores en modo oscuro (los controles comunes no lo soportan bien) y lograr un diseño minimalista. Solo el cuadro de búsqueda es un EDIT nativo. |
| D9 | **Modo oscuro**: barra de título DWM (API documentada) + menús/scrollbars vía `uxtheme` ordinales 133/135/136 (no documentado, cargado dinámicamente y opcional) | Es lo que usan Explorer, Notepad++, etc. Si la API no existe, simplemente no se aplica. |
| D10 | **JSON propio** (`config/Json`) | Evitar dependencias; el formato de configuración es pequeño. |
| D11 | **Configuración en `%APPDATA%\ColaDeImpresion`** o **modo portable** (`portable.txt` junto al .exe) | Por operador; el modo portable sirve para ejecutar desde una carpeta compartida o USB en el servidor. |
| D12 | **Reenvío a otra impresora**: EMF → reproducción vectorial GDI; mismo driver → copia RAW exacta; otro driver → rasterizar a 300 DPI con Ghostscript/GhostPCL; si no hay forma, pregunta si enviar RAW igual | No existe una API del spooler para mover un trabajo. El original se pausa durante el reenvío y se cancela al terminar bien (se reanuda si falla). |
| D13 | **Historial local** (`historial.json`) generado comparando instantáneas (`JobTracker`) | El spooler no guarda historial por defecto. Se clasifica el resultado (impreso / cancelado / falló / retirado) con heurísticas probadas en `tests/`. |
| D14 | No se reemplaza el ícono de bandeja del spooler | Windows no permite reemplazarlo; la app agrega su propio ícono (modo silencioso + notificaciones). |
| D15 | **Ejecución `asInvoker`** + opción "Reiniciar como administrador" | Muchas funciones (leer spool, reordenar, pausar impresora, trabajos de otros usuarios) requieren permisos; se piden solo cuando hacen falta. |
| D16 | **Idioma**: UI en español por defecto, inglés disponible (X-macro en `StringTable.inl`) | Público del TEC; el cambio es inmediato desde el menú. |

## 3. Arquitectura

### 3.1 Módulos (`src/`)

| Módulo | Archivos clave | Responsabilidad |
|---|---|---|
| `util` | `Strings`, `Format`, `Log`, `WorkQueue`, `ComPtr` | UTF-8/16, formato de tamaños y fechas, log a archivo con rotación, hilo de trabajo con cola, puntero COM. |
| `i18n` | `Strings.h`, `StringTable.inl` | Todos los textos de UI (es/en). |
| `config` | `Json`, `Settings`, `History`, `Paths`, `Autostart` | Configuración persistente, historial + estadísticas, rutas (normal/portable), clave Run. |
| `core` | `PrintTypes`, `Spooler`, `JobStatus`, `QueueMonitor`, `JobTracker` | Tipos del dominio, envoltorio de winspool, mapeo de estados, monitor en tiempo real (hilo propio), detección de eventos entre instantáneas. |
| `preview` | `SpoolFile`, `EmfSpool`, `ZipReader`, `ImageLoader`, `ExternalRenderer`, `PreviewBuilder`, `PreviewService`, `Reprint`, `PreviewTypes` | Ubicar/leer el spool (incl. pooling `FPnnnnn`), detectar formato (EMF/XPS/PDF/PS/PCL/PCL XL/imagen/texto, PJL), extraer páginas EMF, contar páginas XPS, WIC, miniaturas del Shell, Ghostscript, caché LRU asíncrona, impresión GDI/RAW para reenvío. |
| `input` | `Commands`, `Shortcuts` | Enum de comandos con id estable, atajos configurables (parseo/formato "Ctrl+Shift+P"). |
| `ui` | `Window`, `Style`, `Theme`, `Gfx`, `DarkMode`, `HeaderBar`, `JobListView`, `PreviewPane`, `StatusBar`, `HelpOverlay`, `TrayIcon` | Controles propios, paletas claro/oscuro, DPI, dibujo, bandeja del sistema. |
| `app` | `Application`, `MainWindow`, `JobActions`, `RowBuilder` | Arranque (instancia única, COM, GDI+), bucle de mensajes con atajos, ventana principal que orquesta todo, acciones en segundo plano, filas y textos. |

Otros: `tests/` (pruebas unitarias, framework mínimo propio), `tools/testjob.cpp`
(genera trabajos de prueba), `res/` (ícono, manifiesto, versión), `installer/`
(Inno Setup), `scripts/` (build, generación del ícono).

### 3.2 Flujo de datos

```
QueueMonitor (hilo)  ──QueueSnapshot──▶ MainWindow::OnSnapshot
   EnumPrinters/EnumJobs                 ├─ JobTracker.Update → eventos → historial + notificaciones
   notificaciones + sondeo               ├─ RowBuilder → JobListView.SetRows
                                         └─ UpdatePreview → PreviewService.Request
PreviewService (hilo) ──clave lista──▶ MainWindow → PreviewPane.ShowDocument
JobActionRunner (hilo) ──ActionResult──▶ MainWindow::OnActionResult (mensajes, refresco)
```

Todos los resultados de hilos llegan por `PostMessage` (`WM_APP+1..3`) con un
puntero que el receptor libera.

### 3.3 Archivos de datos del usuario

- `config.json`: tema, idioma, servidores, impresora activa, atajos personalizados, `spoolDirOverrides`, sondeo, notificaciones, ventana, rutas de Ghostscript, DPI/páginas de vista previa.
- `historial.json`: últimos N trabajos (por defecto 2000).
- `app.log`: log con rotación a 2 MB.

## 4. Estado actual

**Versión 0.1.0** — funcional de punta a punta en local. Verificado en la PC de
desarrollo (Windows 11, sin permisos de administrador) con trabajos reales en
"Microsoft Print to PDF": lista en tiempo real, pausar/reanudar/cancelar,
confirmaciones, filtro, vista previa EMF con navegación, vista rápida, tema
claro/oscuro, panel de atajos con reasignación persistente, historial, manejo
de servidor inaccesible. 34 pruebas unitarias en verde (Debug y Release).

**Pendiente de verificar en el LAIMI** (no se puede en la PC de desarrollo):
conexión al servidor real, lectura del spool por `C$`/`ADMIN$` como
administrador, trabajos de otros usuarios, reordenamiento, pausa de impresora,
formatos RAW reales (PCL/PS) con Ghostscript, reenvío entre impresoras físicas,
impresoras desconectadas. Ver `docs/PRUEBAS.md`.

## 5. Limitaciones conocidas

- Sin permisos de administración en el servidor no hay vista previa del spool (se muestra un mensaje explicativo) ni se pueden gestionar trabajos ajenos.
- Trabajos PCL/PostScript/PDF/XPS necesitan Ghostscript/GhostPCL/GhostXPS para verse; sin ellos se muestra el formato y cómo habilitarlo (XPS/PDF intentan la miniatura de Windows).
- El reenvío rasterizado (otro driver) imprime imágenes a 300 DPI: calidad buena pero no vectorial; el nuevo trabajo queda a nombre del operador, no del estudiante.
- `SetJob` para reordenar requiere "Administrar documentos"; algunos drivers ignoran cambios de posición de trabajos que ya se están imprimiendo.
- El tamaño de trabajo usa `JOB_INFO_2.Size` (32 bits).
- La miniatura/primera página del Shell depende de los proveedores instalados (Office, lector de PDF).
- Las notificaciones usan globos del área de notificación (no toasts con acciones).

## 6. Preguntas abiertas para el LAIMI

- ~~Nombre del servidor de impresión~~ → **`\\vtec-laimiprint`** (es el que se usa en el Explorador para agregar las impresoras compartidas).
- ¿Los operadores son administradores de `vtec-laimiprint`? (para leer el spool y gestionar trabajos ajenos).
- ¿Se prefiere ejecutar la app en el servidor o en la PC del operador?
- ¿Se desea instalar Ghostscript/GhostPCL en el equipo del operador? (licencia AGPL, uso como programa externo).
- ¿Hace falta autenticación o registro de qué operador canceló cada trabajo?

## 7. Bitácora

| Fecha | Cambio |
|---|---|
| 2026-09-19 | Inicio del proyecto. Toolchain WinLibs (GCC 16.2, CMake 4.4) en `D:\Programacion\Herramientas\winlibs`. Implementación completa de las secciones 0–6 y 8 del TODO. |
| 2026-09-19 | El usuario aclara que la app es para el LAIMI (TEC) y controla colas de un **servidor de impresión** → soporte de servidores remotos, permisos y vista previa desde el spool del servidor. |
| 2026-09-19 | Descubierto *spool file pooling* (`FPnnnnn.SPL`): búsqueda por `.SHD`. Herramienta `cola_testjob` con spool EMF simulado. |
| 2026-09-19 | Script de build, instalador Inno Setup, documentación (README, MANUAL, PRUEBAS, GUIA_IA, CONTEXTO). |
| 2026-09-19 | Confirmado el servidor de impresión del LAIMI: `\\vtec-laimiprint`. |
| 2026-09-19 | `scripts/get-renderers.ps1`: Ghostscript + GhostPCL + GhostXPS 10.08.0 (Artifex) + runtime VC++ en `third_party/renderers` (no versionado), incluidos por `build.ps1 -Package`. Verificada la vista previa real de PCL (owl.pcl), PostScript (tiger.eps) y XPS (tiger.xps). Paquete `dist\` ≈ 63 MB. |
| 2026-09-19 | `build.ps1 -Package` arma `dist\` lista para USB: modo portable, lanzador `Abrir vtec-laimiprint.bat`, instrucciones de prueba, PRUEBAS.md y `herramientas\cola_testjob.exe`. |
