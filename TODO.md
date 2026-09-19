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
- [x] Atajos: `Ctrl+P` alterna pausa del trabajo, `Ctrl+Shift+P` alterna la impresora, `Ctrl+Alt+P` ambos a la vez
- [ ] (LAIMI) Verificar la retención con trabajos de estudiantes en `vtec-laimiprint` (requiere "Administrar documentos")
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
- [ ] (LAIMI) Verificar lectura del spool por `\\SERVIDOR\C$` / `ADMIN$` y con trabajos PCL/PS reales
- [ ] Renderizar XPS sin GhostXPS (p. ej. con PDFium/otra biblioteca, o convertir XPS→PDF)
- [ ] Miniaturas pequeñas en la lista de trabajos (opcional)

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
- [x] Selección múltiple arrastrando con el botón izquierdo (rectángulo como en el Explorador) y poder quedarse sin selección (`Esc`, `Ctrl+Shift+A` o clic en el vacío)

---

## 4. Interfaz de usuario (minimalista)

- [x] Definir estructura general: lista de trabajos + panel lateral de vista previa (redimensionable, ocultable, vista rápida a pantalla completa)
- [x] Diseñar layout simple: nombre, páginas, tamaño, estado, usuario/equipo/hora
- [x] Barra de estado con resumen (ej. "3 trabajos · 1 imprimiendo · 2 en pausa")
- [x] Selector de impresora activa (y de servidor)
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
- [x] Archivo de configuración local (JSON) — `%APPDATA%\ColaDeImpresion` o modo portable

---

## 7. Pruebas

- [x] Pruebas unitarias automáticas (`cola_tests`, 36 pruebas)
- [x] Herramienta para generar trabajos de prueba (`tools/testjob.cpp`)
- [ ] (LAIMI) Probar con múltiples impresoras simultáneas
- [ ] (LAIMI) Probar con trabajos grandes (muchas páginas / archivos pesados)
- [ ] (LAIMI) Probar comportamiento cuando la impresora se desconecta a mitad de un trabajo
- [ ] (LAIMI) Probar todos los atajos de teclado (principales ya verificados en la PC de desarrollo; falta la lista completa con teclado español)
- [x] Probar cambio de tema en tiempo real sin reiniciar la app (verificado en PC de desarrollo)

Plan detallado: [docs/PRUEBAS.md](docs/PRUEBAS.md).

---

## 8. Empaquetado y distribución

- [x] Crear instalador para Windows → script Inno Setup `installer/ColaDeImpresion.iss` (`scripts\build.ps1 -Installer`)
- [ ] Compilar el instalador y probarlo (requiere Inno Setup 6 instalado)
- [x] Paquete portable para probar en el LAIMI desde USB (`scripts\build.ps1 -Package` → `dist\`)
- [x] Incluir Ghostscript / GhostPCL / GhostXPS en el paquete (`scripts\get-renderers.ps1`); vista previa PCL, PS y XPS verificada
- [x] Configurar la app para iniciar con Windows (opcional, desde el menú o el instalador)
- [x] Definir si reemplaza el ícono de la bandeja del sistema del spooler original → **no** (Windows no lo permite); la app tiene su propio ícono (D14)
- [x] Documentación básica de uso (README + docs/MANUAL.md)
- [ ] Firmar digitalmente el ejecutable (opcional, evita advertencias de SmartScreen)

---

## Notas / Ideas futuras

- [x] Notificaciones cuando un trabajo termina o falla (y problemas de impresoras)
- [x] Estadísticas de uso (páginas impresas hoy / últimos 7 días, por usuario)
- [x] Soporte multi-idioma (español / inglés)
- [x] Modo "silencioso" que minimiza la app a la bandeja del sistema
- [ ] Exportar historial/estadísticas a CSV
- [ ] Vista de estadísticas más completa (gráfico por día, ranking por usuario)
- [ ] Notificaciones tipo toast de Windows con acciones (cancelar/reanudar)
- [ ] Registro de qué operador realizó cada acción (auditoría)
- [ ] Cuotas o límites de páginas por estudiante (idea)
- [ ] Monitorear varios servidores a la vez
