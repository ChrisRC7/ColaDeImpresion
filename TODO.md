# TODO — Reemplazo de Cola de Impresión de Windows

App de escritorio para gestionar la cola de impresión con vista previa de documentos, control por atajos de teclado e interfaz minimalista con modo claro/oscuro.

---

## 0. Investigación y decisiones técnicas

- [ ] Definir stack tecnológico (opciones típicas para este caso):
  - [ ] **C# / .NET (WPF o WinUI 3)** — acceso nativo más directo a la API de impresión de Windows
  - [ ] **Electron / Tauri + Node** — más rápido de prototipar UI, pero requiere bindings nativos para el spooler
  - [ ] **C++ con Win32 API** — máximo control, mayor curva de desarrollo
- [ ] Investigar la **Print Spooler API de Windows** (`winspool.drv`, funciones `EnumJobs`, `SetJob`, `OpenPrinter`, `GetJob`)
- [ ] Investigar `System.Printing` (si se usa .NET) para manipular trabajos de impresión
- [ ] Confirmar permisos necesarios (la app probablemente necesite ejecutarse con privilegios para pausar/eliminar trabajos de otros usuarios)
- [ ] Decidir si la app reemplaza completamente la cola nativa o actúa como una capa de gestión sobre ella
- [ ] Investigar cómo generar vista previa de distintos formatos (PDF, DOCX, imágenes, XPS) sin abrir el programa original

---

## 1. Núcleo de gestión de la cola de impresión

- [ ] Listar impresoras instaladas en el sistema
- [ ] Listar trabajos activos en la cola por impresora
- [ ] Obtener metadatos de cada trabajo:
  - [ ] Nombre del documento
  - [ ] Tamaño del archivo
  - [ ] Cantidad de páginas
  - [ ] Estado (esperando, imprimiendo, pausado, error)
  - [ ] Usuario que envió el trabajo
- [ ] Implementar acciones sobre trabajos:
  - [ ] Pausar
  - [ ] Reanudar
  - [ ] Cancelar / eliminar
  - [ ] Reordenar prioridad en la cola
  - [ ] Reenviar a otra impresora
- [ ] Detectar en tiempo real cuando entra un nuevo trabajo (evento o polling)
- [ ] Manejar errores comunes (impresora offline, papel atascado, sin tinta)

---

## 2. Vista previa de documentos

- [ ] Generar miniatura/preview del documento antes o durante la impresión
- [ ] Soporte inicial de formatos:
  - [ ] PDF
  - [ ] Imágenes (JPG, PNG)
  - [ ] Documentos de Office (DOCX, XLSX) — evaluar si se renderiza vía librería o se usa el motor de impresión virtual (XPS)
- [ ] Mostrar cantidad total de páginas del documento
- [ ] Mostrar tamaño del archivo en unidades legibles (KB/MB)
- [ ] Permitir hacer zoom o pasar páginas dentro de la vista previa
- [ ] Cachear miniaturas para no regenerarlas en cada refresco de la lista

---

## 3. Atajos de teclado

- [ ] Definir lista inicial de atajos (ejemplos, ajustar según UX):
  - [ ] `Ctrl + P` → Pausar trabajo seleccionado
  - [ ] `Ctrl + R` → Reanudar
  - [ ] `Delete` → Cancelar trabajo
  - [ ] `↑ / ↓` → Navegar entre trabajos de la cola
  - [ ] `Ctrl + ↑ / ↓` → Subir/bajar prioridad
  - [ ] `Space` → Abrir vista previa rápida
  - [ ] `Ctrl + D` → Alternar modo claro/oscuro
  - [ ] `Ctrl + F` → Buscar/filtrar trabajos
- [ ] Sistema de atajos configurable (permitir que el usuario los reasigne)
- [ ] Mostrar los atajos disponibles en un panel de ayuda (`?` o `F1`)

---

## 4. Interfaz de usuario (minimalista)

- [ ] Definir estructura general: lista de trabajos + panel lateral de vista previa
- [ ] Diseñar layout simple: pocos elementos visibles, foco en la información esencial (nombre, páginas, tamaño, estado)
- [ ] Barra de estado con resumen (ej. "3 trabajos en cola, 1 imprimiendo")
- [ ] Selector de impresora activa
- [ ] Indicadores visuales claros de estado (colores/íconos por estado del trabajo)
- [ ] Evitar menús anidados; priorizar acciones directas y atajos

---

## 5. Modo claro / oscuro

- [ ] Definir paleta de colores para ambos modos
- [ ] Detectar automáticamente el tema del sistema operativo al iniciar
- [ ] Permitir alternar manualmente (atajo + botón en UI)
- [ ] Guardar la preferencia del usuario entre sesiones
- [ ] Verificar contraste y legibilidad en ambos modos (accesibilidad)

---

## 6. Persistencia y configuración

- [ ] Guardar preferencias del usuario (tema, atajos personalizados, impresora por defecto)
- [ ] Guardar historial reciente de trabajos impresos (opcional)
- [ ] Archivo de configuración local (JSON o similar)

---

## 7. Pruebas

- [ ] Probar con múltiples impresoras simultáneas
- [ ] Probar con trabajos grandes (muchas páginas / archivos pesados)
- [ ] Probar comportamiento cuando la impresora se desconecta a mitad de un trabajo
- [ ] Probar todos los atajos de teclado
- [ ] Probar cambio de tema en tiempo real sin reiniciar la app

---

## 8. Empaquetado y distribución

- [ ] Crear instalador para Windows (MSIX, Inno Setup o similar)
- [ ] Configurar la app para iniciar con Windows (opcional, configurable por el usuario)
- [ ] Definir si reemplaza el ícono de la bandeja del sistema (system tray) del spooler original
- [ ] Documentación básica de uso (README)

---

## Notas / Ideas futuras

- [ ] Notificaciones cuando un trabajo termina o falla
- [ ] Estadísticas de uso (páginas impresas por día/semana)
- [ ] Soporte multi-idioma
- [ ] Modo "silencioso" que minimiza la app a la bandeja del sistema
