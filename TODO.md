# TODO — Reemplazo de Cola de Impresión de Windows

App de escritorio para gestionar la cola de impresión con vista previa de documentos, control por atajos de teclado e interfaz minimalista con modo claro/oscuro.
**Destino:** LAIMI — Instituto Tecnológico de Costa Rica, para controlar las colas de un **servidor de impresión**.

> Leyenda: `[x]` hecho · `[ ]` pendiente · `(LAIMI)` requiere probarse en el laboratorio.
> Al terminar una tarea, marcarla aquí y registrar el cambio en [docs/CONTEXTO.md](docs/CONTEXTO.md). Modelo de trabajo: [docs/GUIA_IA.md](docs/GUIA_IA.md).

---

## 0. Investigación y decisiones técnicas

- [x] Definir stack tecnológico → **C++20 con Win32 API** (decisión D1 en CONTEXTO.md)
  - [x] ~~C# / .NET (WPF o WinUI 3)~~ — descartado
  - [x] ~~Electron / Tauri + Node~~ — descartado
  - [x] **C++ con Win32 API** — elegido; CMake + MinGW-w64 (WinLibs) o MSVC
- [x] Investigar la **Print Spooler API de Windows** (`OpenPrinter`, `EnumPrinters`, `EnumJobs`, `GetJob`, `SetJob`, `SetPrinter`, `FindFirstPrinterChangeNotification`) → `src/core/Spooler`, `QueueMonitor`
- [x] ~~Investigar `System.Printing`~~ — no aplica (no se usa .NET)
- [x] Confirmar permisos necesarios → "Administrar documentos" para trabajos ajenos y reordenar; administrador del servidor para leer el spool; la app corre `asInvoker` con opción "Reiniciar como administrador" (D15)
- [x] Decidir si reemplaza la cola nativa o es una capa de gestión → **capa de gestión** sobre el spooler (D3), con soporte de **servidor remoto** (D4)
- [x] Investigar vista previa sin abrir el programa original → desde el **archivo de spool**: EMF nativo; PS/PDF/PCL/XPS con Ghostscript/GhostPCL/GhostXPS externos; miniaturas del Shell como respaldo (D6, D7)

---

## 1. Núcleo de gestión de la cola de impresión

- [x] Listar impresoras instaladas en el sistema (locales, conexiones y de un servidor remoto)
- [x] Listar trabajos activos en la cola por impresora (y vista de todas las impresoras)
- [x] Obtener metadatos de cada trabajo:
  - [x] Nombre del documento
  - [x] Tamaño del archivo
  - [x] Cantidad de páginas (spooler + conteo desde el spool EMF/XPS)
  - [x] Estado (esperando, recibiendo, imprimiendo, pausado, error, sin conexión, cancelando, impreso)
  - [x] Usuario que envió el trabajo (y equipo de origen)
- [x] Implementar acciones sobre trabajos:
  - [x] Pausar
  - [x] Reanudar
  - [x] Cancelar / eliminar (uno, varios, o todos los de un usuario)
  - [x] Reordenar prioridad en la cola (posición con `SetJob` nivel 1)
  - [x] Reenviar a otra impresora (EMF vectorial, RAW si es el mismo driver, rasterizado con Ghostscript si no)
  - [x] Reiniciar trabajo
- [x] Detectar en tiempo real cuando entra un nuevo trabajo (notificaciones del spooler + sondeo de respaldo)
- [x] Manejar errores comunes (impresora offline, papel atascado, sin papel, sin tinta/tóner, puerta abierta, servidor inaccesible, acceso denegado)
- [x] Acciones sobre la impresora: pausar, reanudar, vaciar cola
- [x] **Retención automática**: todo trabajo nuevo queda en pausa ("Retenido") hasta que un operador lo libere (`Ctrl+Shift+H` para activar/desactivar; activada por defecto)
- [x] Al iniciar / conectar: pausar las impresoras y retener los trabajos que ya estaban en cola (opción del menú, activada por defecto)
- [x] Atajos: `Ctrl+P` alterna pausa del trabajo, `Ctrl+Shift+P` o `F9` alternan la impresora, `Ctrl+Alt+P` o `F8` el trabajo y su impresora (verificados con impresoras reales)
- [x] **Imprimir y volver a pausar** (`Ctrl+Enter` o `F11`): reanuda los trabajos seleccionados y su impresora, y vuelve a pausar cada impresora sola cuando todos sus trabajos liberados se enviaron (`core/PrintRelease`)
- [ ] (LAIMI) Verificar `F11` con impresoras reales en pausa y permiso "Administrar esta impresora" (en la PC de desarrollo no se pudo pausar la impresora local sin ser administrador)
- [x] **Cola completa**: se enumeran también las colas locales del servidor y las conexiones a ese servidor (antes `PRINTER_ENUM_NAME` solo devolvía las compartidas), el sondeo de respaldo ya no se pospone cuando llegan notificaciones seguidas, y si el spooler reporta más trabajos de los que se pudieron enumerar se reintenta y se avisa en la barra de estado
- [ ] (LAIMI) Verificar la retención con trabajos de estudiantes en `vtec-laimiprint` (requiere "Administrar documentos")
- [x] Trabajos enviados desde otras PCs que no aparecían hasta que llegaba otro documento: la lista se consulta por el nombre DNS completo del servidor, que evita la caché de la conexión (0.3.2; `PRINTER_OPTION_NO_CACHE` no bastaba)
- [ ] (LAIMI) Confirmar con trabajos reales que ya no faltan trabajos (en `app.log` aparece "los trabajos se consultan en \\…estudiantes.tec.cr\…" al iniciar y "la caché del cliente muestra N trabajo(s) y el servidor M" cuando la corrección actúa)
- [x] Hojas en blanco / impresora que se reinicia con documentos grandes enviados con la app abierta: la retención ya no pausa la copia local de Client Side Rendering (`RENDERING_LOCALLY`) y los datos solo se leen de trabajos en pausa o impresos (0.4.9, D39)
- [ ] (LAIMI) Confirmar con "50 pag.pdf" desde una PC del laboratorio que llega con ~70,9 MB, queda retenido y se imprime bien al liberarlo con la 0.4.9
- [x] Las acciones del operador (pausar, reanudar, cancelar, reiniciar, mover, reenviar y `F11`) tampoco tocan la copia local de CSR: se omiten con un aviso en la barra de estado (0.5.11, D50). Antes solo la retención automática la evitaba
- [ ] (LAIMI) Con un PDF grande que todavía se envía desde esta PC, intentar pausar/cancelar su copia local y confirmar el aviso y que el documento se imprime completo
- [x] La lista sin la caché del cliente se reintenta si el DNS no respondía al iniciar (p. ej. con Windows): antes esa impresora usaba la caché toda la sesión (0.5.11, D51)
- [x] Varias copias de la app conectadas al servidor se repartían los datos de cada trabajo (lecturas incompletas → "RAW" y doble cara de la cola): se detecta, se reintenta y no se analiza lo incompleto; Epson sin datos leídos = "—" en Doble cara (0.5.9, D48)
- [ ] (LAIMI) Confirmar con dos copias abiertas y un PDF de varios MB a una cara que ambas muestran "Una cara" y que se imprime bien
- [ ] Retención sin depender de la app abierta (p. ej. servicio de Windows en el servidor) — evaluar si hace falta
- [ ] (LAIMI) Verificar reordenamiento y reenvío con impresoras físicas del laboratorio

---

## 2. Vista previa de documentos

- [x] Generar preview del documento durante la impresión (desde el spool)
- [x] Soporte inicial de formatos:
  - [x] PDF (Ghostscript; o miniatura del Shell)
  - [x] Imágenes (JPG, PNG, BMP, GIF, TIFF multipágina vía WIC)
  - [x] Documentos de Office (DOCX, XLSX) — se ven a través del spool que genera el driver (EMF/XPS/PCL/PS); miniatura del Shell si el archivo original es accesible
  - [x] EMF (spool nativo de Windows, vectorial)
  - [x] PostScript, PCL 5, PCL XL (con Ghostscript/GhostPCL) y XPS (GhostXPS / miniatura)
- [x] Mostrar cantidad total de páginas del documento
- [x] Mostrar tamaño del archivo en unidades legibles (KB/MB)
- [x] Permitir hacer zoom o pasar páginas dentro de la vista previa (atajos, rueda, arrastre)
- [x] Cachear vistas previas para no regenerarlas en cada refresco (LRU con límite de memoria)
- [x] Localizar el spool con *spool file pooling* (`FPnnnnn.SPL` vía `.SHD`)
- [x] Localización del spool más robusta: rutas locales cuando la app corre en el propio servidor, carpeta propia de cada cola (`SpoolDirectory`), búsqueda en los `.SHD` solo por id cuando el nombre del documento no coincide, y errores de lectura que antes se ignoraban
- [x] Explicar por qué no hay vista previa: el panel y `app.log` listan las carpetas probadas y el motivo de cada fallo (antes siempre decía "el archivo de spool ya no existe")
- [x] Leer los datos del trabajo con la API del spooler (`OpenPrinter("Cola,Job N")` + `ReadPrinter`) en lugar de la carpeta de spool: sin administrador ni `C$`, sin choques con los bloqueos del spooler (verificado en remoto contra `vtec-laimiprint`)
- [x] Detectar ESC/Page / ESC/P-R de Epson (antes se tomaba por PCL) y explicar que no tiene vista previa
- [x] Temporales de vista previa en carpeta privada (DACL solo del usuario), borrados al usar, al salir y al iniciar
- [ ] (LAIMI) Verificar trabajos PCL/PS reales (`LAIMI1_BN`) y la cola de etiquetas
- [ ] Vista previa universal: piloto de "renderizar siempre en el servidor" en una cola (ver `docs/DATOS_DE_IMPRESION.md` §11; requiere administrador del dominio/servidor)
- [ ] Renderizar XPS sin GhostXPS (p. ej. con PDFium/otra biblioteca, o convertir XPS→PDF)
- [ ] Miniaturas pequeñas en la lista de trabajos (opcional)

---

## 2b. Configuración de impresión por trabajo

- [x] Datos de `JOB_INFO_2` completos (ID, documento, usuario, equipo, impresora, estado, fecha, tamaño, TotalPages, PagesPrinted, pDatatype, controlador, procesador, pParameters)
- [x] DEVMODE **del trabajo** validado (`dmFields` + `dmSize`): papel (`dmPaperSize`, `dmFormName`, `dmPaperWidth/Length` en décimas de mm), orientación, doble cara, copias, intercalado, color, origen, calidad, escala, tipo de medio
- [x] DEVMODE efectivo de los trabajos EMF tomado del spool (`EMRI_DEVMODE`): `JOB_INFO_2.pDevMode` puede estar desactualizado (Word)
- [x] Opciones declaradas en los datos: PrintTicket XPS, DSC de PostScript, PJL (documentados) y EJL de Epson (indicio)
- [x] Páginas en los datos / páginas lógicas / copias / caras / hojas previstas con fiabilidad (Fiable / Estimado / No determinable) y motivo; detección de páginas repetidas por la aplicación
- [x] Pestaña "Configuración" (`F7`) en el panel y resumen de una línea en el encabezado
- [x] `cola_jobinspect` (ficha en consola, `--hold`, `--release`, `--csv`) y serie de pruebas de Word (`tools/pruebas`)
- [x] Matriz de pruebas con Word, Acrobat, Chrome, Edge y RAW (`docs/DATOS_DE_IMPRESION.md`)
- [x] Significado del EJL del controlador EPSON AM-C6000 verificado sin papel (`QT` copias, `JI PAGES` hojas, `DX`/`BD`, `PS`, `poE`): copias y hojas de trabajos de otras PCs fiables
- [x] Documentos de 1 página que aparecían con 2 en las Epson: las páginas de los trabajos ESC/Page se cuentan en los datos (`FF`), sin el dorso en blanco que agrega el controlador a doble cara; `TotalPages` del servidor daba 2/2/4 para 1/2/3 páginas (0.5.2; el encabezado del panel de detalles, en la 0.5.3)
- [ ] (LAIMI) Confirmar con trabajos reales de estudiantes (PDF, Word, navegador; 1, 2 y 3 páginas, a una y a doble cara) que la columna Páginas coincide con el documento
- [x] Trabajos ESC/Page de más de 16 MB: las páginas no se contaban y `TotalPages` traía el dorso de relleno (PDF de 11 páginas desde Edge a doble cara = 12); ahora el resto de los datos se recorre sin guardarlo para contarlas (0.5.13, D53)
- [ ] (LAIMI) Confirmar con "Crocheting Elliot 2.pdf" desde Edge, a una y a doble cara, que la columna Páginas dice 11 (y en `app.log` "Páginas ESC/Page del trabajo N contadas en ... (completo)")
- [ ] (LAIMI) Pruebas P6–P8 (Lexmark, etiquetas, operador sin permisos)
- [x] Columna "Páginas" = páginas × copias para todos los trabajos (análisis en segundo plano de sus datos)
- [x] Columnas de papel, orientación, doble cara y origen del papel
- [x] Origen del papel y papel con nombres en vez de códigos: Auto selección / Alimentador de papel / Bandeja 1 / Bandeja 2 y Carta / A3 / 12 × 18 (bandeja leída del ESC/Page `iuE` de las Epson, nombres del controlador con `DeviceCapabilities` para otras impresoras)
- [ ] (LAIMI) Confirmar con trabajos reales de estudiantes que el origen del papel coincide con la bandeja elegida (incluido el alimentador)

---

## 3. Atajos de teclado

- [x] Definir lista inicial de atajos:
  - [x] `Ctrl + P` → Pausar trabajo seleccionado
  - [x] `Ctrl + R` → Reanudar
  - [x] `Delete` → Cancelar trabajo
  - [x] `↑ / ↓` → Navegar entre trabajos de la cola
  - [x] `Ctrl + ↑ / ↓` → Subir/bajar prioridad
  - [x] `Space` → Abrir vista previa rápida
  - [x] `Ctrl + D` → Alternar modo claro/oscuro
  - [x] `Ctrl + F` → Buscar/filtrar trabajos
  - [x] Extras: `Ctrl+T` reenviar, `Ctrl+E`/`Ctrl+Tab` impresora, `Ctrl+O` servidor, `Ctrl+H` historial, `AvPág/RePág` páginas, `Ctrl±/0` zoom, `Ctrl+Shift+P/R` impresora, `Ctrl+A`, `F5`, `F10`, `Ctrl+B`, `Ctrl+I`
- [x] Sistema de atajos configurable (reasignación desde el panel F1, guardado en config.json)
- [x] Mostrar los atajos disponibles en un panel de ayuda (`?` o `F1`)
- [x] Elegir y reasignar atajos también con el mouse (clic para elegir; segundo clic, doble clic o clic en la tecla para reasignar)
- [x] Panel de atajos: la barra lateral y la rueda del mouse desplazan bien la lista (antes la lista volvía sola a la fila elegida, la rueda fina/touchpad no movía nada y la barra era difícil de agarrar)
- [x] Panel de atajos agrupado por temas y con barra de desplazamiento (todos los comandos, incluidos los de pausa de impresora, se pueden reasignar ahí)
- [x] Selección múltiple arrastrando con el botón izquierdo (rectángulo como en el Explorador) y poder quedarse sin selección (`Esc`, `Ctrl+Shift+A` o clic en el vacío)
- [x] `F7`: alternar vista previa / configuración de impresión del trabajo
- [x] `Ctrl+Enter` / `F11`: imprimir los seleccionados y volver a pausar la impresora al terminar
- [x] `F6` / `Shift+F6`: cambiar e invertir el orden de la cola; `Ctrl+Shift+F`: fijar/quitar la impresora del acceso rápido; `Ctrl+1`…`Ctrl+9`: impresoras del acceso rápido (teclas fijas)

---

## 4. Interfaz de usuario (minimalista)

- [x] Definir estructura general: lista de trabajos + panel lateral de vista previa (redimensionable, ocultable, vista rápida a pantalla completa)
- [x] Diseñar layout simple: nombre, páginas, tamaño, estado, usuario/equipo/hora
- [x] Barra de estado con resumen (ej. "3 trabajos · 1 imprimiendo · 2 en pausa")
- [x] Selector de impresora activa (y de servidor)
- [x] **Barra de acceso rápido a impresoras**: el operador fija las impresoras que quiere arriba (botón `+`), cada una con su cantidad de trabajos y su estado; clic para filtrar, `Ctrl+1`…`Ctrl+9`, clic derecho para pausar/reanudar/vaciar
- [x] **Ordenar la cola** por equipo, hora de envío, usuario, documento, estado o tamaño (además del orden real de impresión), con orden inverso; se guarda entre sesiones
- [x] **Lista en forma de tabla** como la cola de Windows: clic en el encabezado para ordenar, otro clic para invertir (también páginas, impresora, papel, orientación, doble cara y origen)
- [x] Panel de detalles (vista previa y configuración) oculto al iniciar, con botón en la cabecera
- [x] Cambiar el ancho de las columnas con el mouse como en la cola de Windows (arrastrar el borde, doble clic = ajustar al contenido, barra horizontal si no caben, anchos guardados y "Restablecer el ancho de las columnas")
- [x] Mover las columnas arrastrando su título como en la cola de Windows (línea que marca dónde quedan, desplazamiento automático en los bordes); el orden predeterminado es el de siempre y el de cada operador se guarda en su `config.json` (`columnOrder`) y se conserva al abrir; "Restablecer el orden de las columnas"
- [x] Ninguna columna desaparece al achicar la ventana o abrir el panel de detalles (antes se ocultaba Enviado, entre otras): aparece la barra horizontal
- [x] La cabecera (búsqueda, botones de pausa e íconos) y la barra de impresoras se redibujan mientras se cambia el tamaño de la ventana (antes se encimaban hasta soltar el borde)
- [x] En el historial con la ventana al mínimo, "Exportar a Excel" se montaba sobre el botón del panel de detalles: el selector de impresora se achica para dejar lugar a los botones (0.5.11)
- [ ] Verificar en la app la cabecera del historial con la ventana al mínimo (100 % y 150 % de escala)
- [x] Menú: "Conectar al servidor del LAIMI (\\vtec-laimiprint)", marcado mientras está conectado (0.5.11)
- [x] Total de páginas de la selección en la barra inferior ("Selección: 3 trabajo(s) · 11 páginas a imprimir", suma de la columna Páginas, con los "—" aparte); también en el historial (0.5.1)
- [x] Columna **Presentación** (Cuaderno / Libreta / No aplica, por orientación y borde de giro), a la derecha de Doble cara en la cola, el historial y el Excel ("Por presentación") (0.5.14, D55)
- [x] Columna **Carnet** a la derecha de Usuario (cola, historial y Excel; búsqueda y orden), leída del Active Directory en solo lectura (`employeeID`, 0.5.17, D56)
- [ ] (LAIMI) Confirmar con trabajos reales de estudiantes que la columna Carnet muestra el carnet correcto (y revisar `app.log` si siempre dice "—")
- [x] Botón *Conectar al servidor del LAIMI* configurable sin compilar: `servidor-laimi.txt` junto al .exe; `Abrir vtec-laimiprint.bat` usa `--laimi` (0.5.14, D54)
- [x] Cambiar ese servidor desde la app: menú *Cambiar servidor del LAIMI…*, se escribe en el cuadro de arriba y reemplaza al que muestra la opción (0.5.15, D54)
- [ ] (LAIMI) Probar *Cambiar servidor del LAIMI…* con un usuario sin permisos de administrador sobre la carpeta de la app (debe quedar solo para ese usuario y avisarlo)
- [ ] Verificar en la app el arrastre de columnas (cola, vista "Todas" con Impresora, historial y con barra horizontal); no se abrió la app en la PC personal porque al iniciar pausa las impresoras
- [x] Botones junto a la búsqueda para pausar / reanudar los trabajos seleccionados y la impresora (texto según lo que harán; solo íconos en ventanas angostas)
- [x] Ventana "Acerca de" con los autores principales (botón "!" en un círculo a la derecha del de claro/oscuro y menú *Acerca de…*) (0.5.7); sin correos y con descripción corta (0.5.8)
- [x] Indicadores visuales claros de estado (colores + íconos por estado)
- [x] Evitar menús anidados; priorizar acciones directas y atajos (un único menú plano)
- [x] Soporte de DPI alto (Per-Monitor V2)

---

## 5. Modo claro / oscuro

- [x] Definir paleta de colores para ambos modos (`src/ui/Theme.cpp`)
- [x] Detectar automáticamente el tema del sistema operativo al iniciar (y seguir sus cambios en vivo)
- [x] Permitir alternar manualmente (atajo + botón en UI + menú)
- [x] Guardar la preferencia del usuario entre sesiones
- [x] Verificar contraste y legibilidad en ambos modos (prueba automática WCAG AA)
- [x] Barra de título, menús y barras de desplazamiento oscuros

---

## 6. Persistencia y configuración

- [x] Guardar preferencias del usuario (tema, idioma, atajos, impresora y servidor, ventana, notificaciones...)
- [x] Guardar historial reciente de trabajos impresos (con resultado y estadísticas)
- [x] El historial guarda y muestra las mismas columnas que la cola (#, páginas × copias, papel, orientación, doble cara, origen, enviado) además del resultado y la hora en que terminó
- [ ] (LAIMI) Verificar con trabajos reales que el historial conserva papel, doble cara, origen y páginas × copias de trabajos de otras PCs
- [x] Páginas × copias se perdían al liberar el trabajo (2 págs. × 2 copias pasaba al historial como 2): la caché de datos analizados ya no depende del estado ni del tamaño que informa el spooler al imprimir, y el historial guarda el mayor tamaño y páginas vistos (0.5.1)
- [ ] (LAIMI) Confirmar con un trabajo de 2 páginas × 2 copias y uno de 4 páginas a doble cara de una PC del laboratorio que el historial dice 4, igual que la cola
- [x] Exportar el historial a Excel (`.xlsx` propio, sin dependencias): hoja "Historial" con todas las columnas en el orden del operador y hoja "Resumen" (totales, páginas por papel, impresora, doble cara, orientación, origen, usuario, equipo, día y hora); botón en el historial, `Ctrl+Shift+E` y menú (0.5.1)
- [x] Cada tabla del resumen de Excel en su propia hoja ("Por resultado", "Por papel", "Por impresora", "Por usuario", "Por equipo", "Por día"...), con título y encabezados fijos, para no bajar entre tablas; "Resumen" queda con los datos generales (0.5.4)
- [x] Hojas del resumen vacías: los trabajos liberados (`F11`, `Ctrl+R`) que se imprimían entre dos refrescos quedaban "Retirado" y la copia local de CSR se agregaba como otro registro; ahora quedan "Impreso", sin duplicados, y una tabla sin datos dice por qué (0.5.5)
- [ ] (LAIMI) Confirmar con trabajos reales liberados con `F11` que el historial dice "Impreso" una sola vez y que el Excel trae las tablas con datos
- [x] Trabajos que se perdían del historial si su impresora dejaba de responder un momento (y avisos repetidos de "nuevo trabajo" al volver): se siguen recordando hasta que la impresora responda (0.5.11, D52)
- [x] Al volver de un corte, el resultado se decide con el estado de la impresora de antes del corte: el primero de una cola operativa que salió durante el corte queda "Impreso", no "Retirado" (0.5.12, D52)
- [x] `historial.json` bloqueado un momento al iniciar (antivirus, otra copia guardándolo) ya no se trata como dañado: se reintenta la lectura; antes no se guardaba nada en toda la sesión (0.5.12, D52)
- [x] `historial.json` dañado o con BOM UTF-8: el BOM se ignora y un archivo ilegible se copia a `historial.json.danado` antes de guardar encima; si ni eso se puede, no se reemplaza (0.5.11, D52)
- [x] Elegir al exportar: todo el historial o solo un rango de fechas (Desde/Hasta entre los días con registros, con el total del rango); el resumen y el nombre del archivo indican el período (0.5.6)
- [ ] Probar en la app el diálogo de exportación con el mouse (lista de días, cambio de opción) y con un historial largo (muchos días en la lista)
- [ ] Probar en la app el diálogo "Guardar como" de la exportación y abrir el archivo desde el aviso (el libro se verificó abriéndolo en Excel; el diálogo no se abrió para no robar el foco)
- [x] Archivo de configuración local (JSON) — `%APPDATA%\ColaDeImpresion` o modo portable
- [x] `config.json` por usuario de Windows en `%LOCALAPPDATA%\ColaDeImpresion` también en modo portable (PCs institucionales compartidas); la primera vez se copia la configuración anterior; `--config RUTA` para desarrollo (0.5.10, D49)
- [ ] (LAIMI) Confirmar con dos usuarios en la misma PC que cada uno conserva sus preferencias

---

## 7. Pruebas

- [x] Pruebas unitarias automáticas (`cola_tests`, 99 pruebas)
- [x] Herramientas para generar e inspeccionar trabajos de prueba (`tools/testjob.cpp`, `tools/jobinspect.cpp`)
- [ ] (LAIMI) Probar con múltiples impresoras simultáneas
- [ ] (LAIMI) Probar con trabajos grandes (muchas páginas / archivos pesados)
- [ ] (LAIMI) Probar comportamiento cuando la impresora se desconecta a mitad de un trabajo
- [ ] (LAIMI) Probar todos los atajos de teclado (principales ya verificados en la PC de desarrollo; falta la lista completa con teclado español)
- [x] Probar cambio de tema en tiempo real sin reiniciar la app (verificado en PC de desarrollo)

- [x] Revisión de código de la 0.5.17: fallos, corrección propuesta y cómo comprobarla en [docs/FALLOS_DETECTADOS.md](docs/FALLOS_DETECTADOS.md)
- [ ] Corregir los fallos F1–F7 de `docs/FALLOS_DETECTADOS.md` (memoria liberada en el menú contextual, servidor del LAIMI sobrescrito, "Cancelado" falso en el historial, reenvío parcial que borra el original, nombre del documento abierto como ruta, eventos falsos al fallar la consulta directa, reconexión que vuelve a retener)
- [ ] Corregir los menores M1–M6 de `docs/FALLOS_DETECTADOS.md`
- [ ] Mejoras X1–X6 de `docs/FALLOS_DETECTADOS.md` (historial compartido entre copias, historial sin descartes silenciosos, confirmar el borrado, inicio con Windows tras actualizar, selección que no salta sola, no rearmar la tabla sin cambios); varias requieren decisión del usuario antes de implementarlas

Plan detallado: [docs/PRUEBAS.md](docs/PRUEBAS.md).

---

## 8. Empaquetado y distribución

- [x] Crear instalador para Windows → script Inno Setup `installer/ColaDeImpresion.iss` (`scripts\build.ps1 -Installer`)
- [ ] Compilar el instalador y probarlo (requiere Inno Setup 6 instalado)
- [x] Paquete portable para probar en el LAIMI desde USB (`scripts\build.ps1 -Package` → `dist\`)
- [x] `-Package` deja `dist\` limpia (sin `app.log`, `historial.json` ni `config.json`) y crea `dist\ColaDeImpresion <versión>.zip` con todo su contenido; rescata los renderizadores del paquete anterior si falta `third_party\renderers`. Se regenera al terminar cada tarea (regla en `AGENTS.md`)
- [x] Incluir Ghostscript / GhostPCL / GhostXPS en el paquete (`scripts\get-renderers.ps1`); vista previa PCL, PS y XPS verificada
- [x] Configurar la app para iniciar con Windows (opcional, desde el menú o el instalador)
- [x] Definir si reemplaza el ícono de la bandeja del sistema del spooler original → **no** (Windows no lo permite); la app tiene su propio ícono (D14)
- [x] Documentación básica de uso (README + docs/MANUAL.md)
- [x] Información de versión completa en el ejecutable (empresa **ChrisCorp**, descripción, copyright, comentarios, versión 0.2.0) y manifiesto con los sistemas soportados, para que Windows no lo trate como un binario anónimo
- [x] `Desbloquear archivos.bat` en el paquete portable: quita la marca de "archivo de otro equipo" (Mark of the Web) que dispara SmartScreen al copiar desde un USB
- [ ] Firmar digitalmente el ejecutable (única forma de eliminar del todo la advertencia de SmartScreen; requiere un certificado de firma de código)

---

## Notas / Ideas futuras

- [x] Notificaciones cuando un trabajo termina o falla (y problemas de impresoras)
- [x] Estadísticas de uso (páginas impresas hoy / últimos 7 días, por usuario)
- [x] Soporte multi-idioma (español / inglés)
- [x] Modo "silencioso" que minimiza la app a la bandeja del sistema
- [x] Exportar historial/estadísticas → a Excel (`.xlsx`) con hoja de resumen (0.5.1)
- [ ] Vista de estadísticas más completa (gráfico por día, ranking por usuario)
- [x] Código comentado a detalle en español: cada .h/.cpp de src/, las pruebas, las herramientas, los scripts (incluido make_icon.py), el .rc, el manifiesto, el instalador, la página de prueba, .gitignore/.gitattributes y CMakeLists.txt explican qué hace cada parte y por qué (solo comentarios, sin cambios de comportamiento)
- [ ] Notificaciones tipo toast de Windows con acciones (cancelar/reanudar)
- [ ] Registro de qué operador realizó cada acción (auditoría)
- [ ] Cuotas o límites de páginas por estudiante (idea)
- [ ] Monitorear varios servidores a la vez
