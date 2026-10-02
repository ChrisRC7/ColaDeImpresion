# Contexto del proyecto — Cola de Impresión (LAIMI)

> Documento **vivo**: se actualiza con cada cambio relevante. Es la fuente de
> contexto para cualquier IA o persona que continúe el trabajo. El modelo de
> trabajo está en [GUIA_IA.md](GUIA_IA.md); las tareas en [TODO.md](../TODO.md).

**Última actualización:** 2026-10-02

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
| D17 | **Retención automática** (activada por defecto): el `QueueMonitor` pausa cada trabajo nuevo apenas lo detecta (`core/AutoHold`), saltándose el agrupamiento de notificaciones si llega `PRINTER_CHANGE_ADD_JOB`. Cada trabajo se evalúa una sola vez (si un operador lo libera no se re-pausa); los existentes al conectar no se tocan. Los retenidos se marcan `heldByApp` y se muestran como "Retenido". | Pedido del usuario: nada debe imprimirse sin que un operador lo libere. Windows no tiene una opción de "retener todo" por trabajo (pausar la impresora frena la cola entera). Limitación: solo funciona con la app abierta; un trabajo muy pequeño podría empezar a imprimirse antes (se pausa igual). |
| D18 | **Al iniciar/conectar se pausa todo**: se pausan las impresoras del servidor (`SetPrinter` + `PRINTER_CONTROL_PAUSE`) y se retienen los trabajos ya encolados. El resultado de esa pausa automática se informa solo en la barra de estado (sin diálogos) porque puede fallar por permisos. Atajos: `Ctrl+P` alterna el trabajo, `Ctrl+Shift+P`/`F9` la impresora (todas en la vista "Todas"), `Ctrl+Alt+P`/`F8` el trabajo y su impresora. El alternar de impresora usa el criterio "si hay alguna en pausa, reanudar; si ninguna, pausar" para que siga sirviendo aunque alguna impresora no se pueda controlar. Los errores de acciones sobre impresoras se muestran solo en la barra de estado (un modal bloqueaba el siguiente atajo). | Pedido del usuario: al abrir la app nada debe poder imprimirse. Pausar la impresora exige "Administrar esta impresora" (más estricto que "Administrar documentos"), por eso la retención de trabajos es la defensa principal y la pausa de impresora es complementaria. |
| D19 | **Acceso rápido a impresoras**: barra de chips bajo la cabecera con las impresoras que el operador fija (`favoritePrinters` en config.json), con cuenta de trabajos y estado; `Ctrl+1`…`Ctrl+9` como teclas fijas (no reasignables) | En el LAIMI hay varias impresoras y el operador trabaja casi siempre con dos o tres; abrir el menú para cambiar de cola era lento. Las teclas numéricas son fijas porque su significado depende de la posición en la barra, no de un comando concreto. |
| D20 | **Orden de la cola configurable** (`SortBy` en `app/RowBuilder`): orden real de impresión (predeterminado), equipo, hora, usuario, documento, estado o tamaño, con orden inverso; a igualdad de criterio se conserva el orden de impresión | Pedido del usuario: ubicar rápido de qué equipo o de qué hora es un trabajo. Se mantiene como una *vista*: no cambia la prioridad real en el spooler (eso sigue siendo `Ctrl+↑/↓`). |
| D21 | **Diagnóstico visible en vez de mensajes genéricos**: la búsqueda del spool devuelve las carpetas probadas y el error de cada una (`SpoolData::diagnostics`), y el monitor compara `PRINTER_INFO_2.cJobs` con los trabajos enumerados y publica la diferencia | En el servidor del LAIMI no hay depurador: "el archivo de spool ya no existe" y una cola incompleta no daban ninguna pista. Ahora el panel y `app.log` dicen dónde se buscó y qué falló, y la barra de estado avisa si faltan trabajos. |
| D22 | **Datos del trabajo por la API del spooler**: `OpenPrinter("Cola,Job N", JOB_READ)` + `ReadPrinter` (`core::spooler::ReadJobData`); el archivo de spool queda como respaldo | Usa el permiso ya delegado ("Administrar documentos" incluye `JOB_ACCESS_READ`), funciona en remoto sin `C$` ni administrador del servidor y evita los bloqueos del spooler sobre `.SHD` (error *"being used by another process"* en el servidor). Verificado contra `vtec-laimiprint` el 2026-10-02. No se lee un trabajo que todavía se recibe (`ReadPrinter` esperaría). |
| D23 | **DEVMODE efectivo en EMF = el del spool** (`EMRI_DEVMODE` vigente para la primera página; va entre `EMRI_METAFILE_DATA` y `EMRI_*_METAFILE_EXT`) | Word cambia el DEVMODE con `ResetDC` tras `StartDoc`: `JOB_INFO_2.pDevMode` mostraba las copias del trabajo anterior. La salida PostScript real confirmó que se aplica el del spool. La ficha muestra la diferencia. |
| D24 | **Páginas / copias / caras / hojas con fiabilidad** (`core/PageAccounting`): nunca `TotalPages × dmCopies` a ciegas; cada valor es Fiable, Estimado o No determinable con su motivo; la repetición de páginas hecha por la aplicación se detecta comparando huellas de páginas EMF/XPS | Pedido del LAIMI. Las pruebas con Word mostraron los tres casos (copias en el DEVMODE, dentro del spool, o páginas repetidas). |
| D25 | **Opciones declaradas en los datos**: PrintTicket XPS (XPS Object Model de Windows, `IXpsOMObjectFactory1`, porque los drivers v4 generan OpenXPS), DSC de PostScript y PJL se consideran documentados y confirman; EJL de Epson es solo **indicio** (sin documentación pública) | Son lo que la impresora obedece en RAW; no se interpreta lo que no está documentado (`CO`/`QT` del EJL se muestran crudos). |
| D26 | **ESC/Page como formato propio** (antes se detectaba como PCL) y "vista previa no disponible" con el motivo | Las colas Epson del LAIMI reciben ESC/Page-Color por Client Side Rendering; no hay intérprete libre. La configuración sí se muestra. Para una vista previa universal se propone renderizar en el servidor (EMF), ver `DATOS_DE_IMPRESION.md` §11. |
| D27 | **Temporales privados**: carpeta por proceso con DACL protegida solo para el usuario actual, borrado inmediato y limpieza al iniciar y al salir (`preview/TempFiles`) | Las copias para Ghostscript y las miniaturas pueden contener documentos confidenciales de estudiantes. |
| D28 | **Lista de trabajos sin la caché del cliente**: las impresoras de un servidor se consultan además abriendo la cola con el **nombre DNS completo** del servidor (`spooler::CacheBypassName`), y esa lista manda; de la caché de la conexión solo se agregan los trabajos que esta PC todavía procesa (`ReconcileJobLists`, `JOB_STATUS_RENDERING_LOCALLY`) | Los trabajos enviados desde otras PCs a veces no aparecían hasta que llegaba otro: por el nombre de la conexión Windows responde desde la caché de Client Side Rendering (0,6 ms, sin ir al servidor), incluso con `PRINTER_OPTION_NO_CACHE` (intento de la 0.3.1, insuficiente). Por el nombre completo la consulta va al servidor (17 ms). Las acciones y la retención siguen usando el nombre de la conexión. |
| D29 | **EJL verificado con el controlador EPSON AM-C6000**: `QT` = copias, `JI PAGES` = hojas físicas, `DX`/`BD`, `PS` y `GS n poE` (orientación); los datos documentados o verificados **mandan** sobre el DEVMODE del servidor | En los trabajos que llegan por CSR el DEVMODE del servidor es el predeterminado de la cola. Verificado generando los datos del controlador a archivo (sin papel). |
| D30 | **Lista en forma de tabla** con encabezado clicable (`ui::ListColumn`, `JobColumns`/`HistoryColumns`, `NextSort`): un clic ordena por la columna y otro invierte; columnas de papel, orientación, doble cara y origen; prioridades de ocultamiento para ventanas angostas | Pedido del usuario: ordenar "como en la cola de impresión de Windows", por usuario y por equipo. |
| D31 | **"Páginas" = páginas × copias** (`PageAccounting::printPages`), con los datos de TODOS los trabajos analizados en segundo plano (`preview::JobFactsService`: lee hasta 16 MB por trabajo con la API, una vez, y guarda solo los `ContentFacts`) | Las copias de los trabajos de otras PCs solo están en sus datos (EJL). En EMF, sin leer los datos, el DEVMODE de `JOB_INFO_2` puede estar desactualizado: se muestra como estimado. |
| D32 | **Lecturas `ReadPrinter` serializadas** en el proceso (y reintento si llegan 0 bytes de un trabajo con datos) | Comprobado: dos lecturas simultáneas del mismo trabajo hacen que la segunda reciba 0 bytes (la vista previa salía "vacía" mientras el análisis de la lista leía el mismo trabajo). |
| D33 | **Panel de detalles oculto al iniciar**, con botón en la cabecera (además de `Ctrl+B` / `F7`) | Pedido del usuario: más espacio para la tabla. |
| D16 | **Idioma**: UI en español por defecto, inglés disponible (X-macro en `StringTable.inl`) | Público del TEC; el cambio es inmediato desde el menú. |

## 3. Arquitectura

### 3.1 Módulos (`src/`)

| Módulo | Archivos clave | Responsabilidad |
|---|---|---|
| `util` | `Strings`, `Format`, `Log`, `WorkQueue`, `ComPtr` | UTF-8/16, formato de tamaños y fechas, log a archivo con rotación, hilo de trabajo con cola, puntero COM. |
| `i18n` | `Strings.h`, `StringTable.inl` | Todos los textos de UI (es/en). |
| `config` | `Json`, `Settings`, `History`, `Paths`, `Autostart` | Configuración persistente, historial + estadísticas, rutas (normal/portable), clave Run. |
| `core` | `PrintTypes`, `Spooler`, `JobStatus`, `QueueMonitor`, `JobTracker`, `AutoHold`, `PrintSettings`, `PageAccounting` | Tipos del dominio, envoltorio de winspool (incluye `IsLocalServer`, la carpeta de spool por cola y `ReadJobData`), DEVMODE validado del trabajo, cálculo de páginas/copias/caras/hojas con fiabilidad, mapeo de estados, monitor en tiempo real (hilo propio, sondeo con vencimiento propio y detección de trabajos no enumerados), detección de eventos entre instantáneas, retención automática de trabajos nuevos. |
| `preview` | `SpoolFile`, `EmfSpool`, `ZipReader`, `ImageLoader`, `ExternalRenderer`, `PreviewBuilder`, `PreviewService`, `Reprint`, `PreviewTypes`, `PdlSettings`, `XpsPackage`, `TempFiles` | Leer los datos del trabajo (API del spooler; archivo con pooling `FPnnnnn` como respaldo), detectar formato (EMF/XPS/PDF/PS/PCL/PCL XL/ESC/Page/imagen/texto, PJL/EJL), extraer páginas EMF y su `EMRI_DEVMODE`, opciones PJL/EJL/DSC, PrintTicket XPS, huellas de páginas, temporales privados, WIC, miniaturas del Shell, Ghostscript, caché LRU asíncrona, impresión GDI/RAW para reenvío. |
| `input` | `Commands`, `Shortcuts` | Enum de comandos con id estable, atajos configurables (parseo/formato "Ctrl+Shift+P"). |
| `ui` | `Window`, `Style`, `Theme`, `Gfx`, `DarkMode`, `HeaderBar`, `PrinterBar`, `JobListView`, `PreviewPane`, `StatusBar`, `HelpOverlay`, `TrayIcon` | Controles propios, paletas claro/oscuro, DPI, dibujo, bandeja del sistema. `PrinterBar` es la barra de acceso rápido a impresoras. |
| `app` | `Application`, `MainWindow`, `JobActions`, `RowBuilder`, `JobSheet` | Arranque (instancia única, COM, GDI+), bucle de mensajes con atajos, ventana principal que orquesta todo, acciones en segundo plano, filas y textos, ficha "Configuración" del trabajo (origen y fiabilidad de cada dato). |

Otros: `tests/` (pruebas unitarias, framework mínimo propio), `tools/testjob.cpp`
(genera trabajos de prueba), `tools/jobinspect.cpp` (`cola_jobinspect`: ficha de
cada trabajo en consola, vigilancia `--hold` y matriz CSV), `tools/pruebas/`
(serie de Word por COM y página HTML de prueba), `res/` (ícono, manifiesto, versión), `installer/`
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

- `config.json`: tema, idioma, servidores, impresora activa, impresoras fijadas (`favoritePrinters`, `showPrinterBar`), orden de la cola (`sortBy`, `sortDescending`), atajos personalizados, `spoolDirOverrides`, sondeo, notificaciones, ventana, rutas de Ghostscript, DPI/páginas de vista previa.
- `historial.json`: últimos N trabajos (por defecto 2000).
- `app.log`: log con rotación a 2 MB.

## 4. Estado actual

**Versión 0.3.0** — configuración de impresión y vista previa por trabajo
(pestaña `F7`), lectura de datos por la API del spooler, DEVMODE efectivo del
spool EMF, PrintTicket/DSC/PJL/EJL, separación páginas/copias/caras/hojas con
fiabilidad y temporales privados. 62 pruebas unitarias en verde (Debug y
Release), sin warnings. Verificado en LAIMI1-CTRL1 con Word, Acrobat, Chrome,
Edge y datos RAW (14/14 predicciones de caras coinciden con la salida real) y
con la lectura remota de un trabajo real en `vtec-laimiprint` sin ser
administrador. Toolchain en este equipo: WinLibs GCC 16.2 en
`C:\Users\chrodriguez\Desktop\Programacion\Herramientas\winlibs` (`build.ps1` la
encuentra sola).

**Pendiente de verificar en el LAIMI**: pruebas P1–P8 de
`docs/DATOS_DE_IMPRESION.md` §12 (qué refleja el DEVMODE del servidor en los
trabajos RAW de las Epson: copias, doble cara, papel; significado de `CO`/`QT`
del EJL; formato de `LAIMI1_BN` y de la cola de etiquetas), además de lo que ya
estaba pendiente: reordenamiento, pausa de impresora, reenvío entre impresoras
físicas e impresoras desconectadas. Ver `docs/PRUEBAS.md`.

## 5. Limitaciones conocidas

- Sin "Administrar documentos" en la cola no se pueden leer los datos de trabajos ajenos (ni la vista previa ni las opciones declaradas en los datos) ni gestionarlos; el panel lo explica.
- Los trabajos ESC/Page de las Epson del LAIMI no tienen vista previa (las bandas raster usan una compresión propietaria de Epson); su configuración (copias, doble cara, papel, orientación, hojas) sí es fiable con el controlador EPSON AM-C6000.
- Las páginas por cara (N-up) no son determinables en EMF/RAW (opción privada del controlador): con varias páginas lógicas las caras quedan como estimadas.
- La vista previa EMF no usa las fuentes incrustadas en el spool: si falta una fuente en el equipo del operador, Windows la sustituye.
- El ejecutable no está firmado digitalmente: SmartScreen puede advertir la primera vez (la información de versión completa y `Desbloquear archivos.bat` reducen el problema, pero solo un certificado lo elimina).
- El ordenamiento de la cola es una vista: no cambia la prioridad real del spooler.
- El acceso rápido guarda nombres completos de impresora, así que las impresoras fijadas de un servidor no aparecen al conectarse a otro (es intencional).
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
| 2026-09-19 | Panel de atajos (F1): comandos reordenados por grupos (trabajos, impresora, selección, vista previa, servidor, general) y barra de desplazamiento visible y arrastrable, para que los atajos nuevos se vean y se puedan reasignar. Verificado reasignando "Pausar / reanudar impresora" con clic y restaurando con R. |
| 2026-09-19 | Pausa total al iniciar (D18), atajo combinado `Ctrl+Alt+P`, selección por arrastre tipo Explorador y posibilidad de no tener nada seleccionado (`Esc` / `Ctrl+Shift+A` / clic en el vacío). Verificado con trabajos reales: los encolados antes de abrir la app quedan retenidos. 38 pruebas. |
| 2026-09-19 | **Retención automática** de trabajos nuevos (D17, `Ctrl+Shift+H`, activada por defecto): verificada con trabajos reales (quedan "Paused, Spooling" sin llegar a imprimirse; al liberarlos no se re-pausan). Panel de atajos: selección y reasignación con clic/doble clic. 36 pruebas. Publicado en GitHub (`origin`). |
| 2026-09-19 | `build.ps1 -Package` arma `dist\` lista para USB: modo portable, lanzador `Abrir vtec-laimiprint.bat`, instrucciones de prueba, PRUEBAS.md y `herramientas\cola_testjob.exe`. |
| 2026-09-22 | **Acceso rápido a impresoras** (D19, `ui/PrinterBar` + `BuildQuickAccess`) y **orden de la cola** por equipo/hora/usuario/documento/estado/tamaño (D20, `F6`/`Shift+F6`). Verificado en la app con dos impresoras fijadas y trabajos reales. |
| 2026-09-22 | **Trabajos que faltaban respecto a la cola de Windows**: el sondeo de respaldo pasa a tener vencimiento propio (antes cada notificación lo reiniciaba y una impresora "ruidosa" dejaba a las demás sin refrescar); la enumeración de impresoras de un servidor suma sus colas locales y las conexiones a ese servidor (`PRINTER_ENUM_NAME` solo devuelve las compartidas); si `cJobs` supera lo enumerado se reintenta y se avisa en la barra de estado. |
| 2026-09-22 | **Vista previa en el servidor** (D21): rutas locales si la app corre en el propio servidor (`IsLocalServer`), carpeta de spool propia de cada cola, segunda pasada en los `.SHD` solo por id, errores de lectura ya no se ignoran y el panel/`app.log` muestran dónde se buscó y por qué falló. |
| 2026-09-22 | Versión 0.2.0. Información de versión completa del ejecutable con empresa **ChrisCorp** (descripción, copyright, comentarios, bloques es/en) y manifiesto con Windows 7–11/Server; `Desbloquear archivos.bat` en el paquete portable. |
| 2026-10-02 | 0.4.2: valores y títulos centrados desde la columna Estado; "Enviado" con fecha y hora (dd/mm/aaaa hh:mm). |
| 2026-10-02 | 0.4.1: la tabla ya no dibuja el ícono de estado a la izquierda (repetía la columna Estado). |
| 2026-10-02 | 0.4.0: lista en forma de tabla con orden por columnas (D30), columna Páginas = páginas × copias para todos los trabajos (D31), columnas de papel, orientación, doble cara y origen, panel de detalles oculto al iniciar con botón (D33) y lecturas `ReadPrinter` serializadas (D32). 66 pruebas. |
| 2026-10-02 | 0.3.2: el arreglo de la 0.3.1 no bastaba (`PRINTER_OPTION_NO_CACHE` también responde desde la caché de la conexión). La lista de trabajos se consulta por el nombre DNS completo del servidor, que sí va al servidor (D28 actualizada). |
| 2026-10-02 | 0.3.1: trabajos de otras PCs que no aparecían hasta que llegaba otro (D28, caché del cliente) y copias/doble cara/orientación/hojas fiables en las Epson leyendo el EJL verificado (D29). La vista previa ESC/Page sigue sin ser posible (compresión propietaria). 64 pruebas. |
| 2026-10-02 | **Configuración de impresión por trabajo** (D22–D27): pestaña "Configuración" (`F7`) y resumen en el encabezado; datos por `ReadPrinter` (resuelve el *"being used by another process"* del servidor); `EMRI_DEVMODE` como DEVMODE efectivo en EMF; PrintTicket XPS, DSC, PJL y EJL; ESC/Page detectado; páginas/copias/caras/hojas con fiabilidad; temporales privados; `cola_jobinspect`; `--instancia-adicional` para probar junto a la versión de producción. Matriz de pruebas con Word, Acrobat, Chrome, Edge y RAW en `docs/DATOS_DE_IMPRESION.md`. Versión 0.3.0. |
