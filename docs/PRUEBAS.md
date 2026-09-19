# Plan de pruebas

## 1. Pruebas automáticas

`cola_tests.exe` (se ejecuta con `scripts\build.ps1`) cubre la lógica sin UI:
JSON, ajustes, historial y estadísticas, formato, mapeo de estados del spooler,
seguimiento de trabajos (eventos y resultado), atajos (parseo, reasignación,
duplicados), contraste WCAG AA de ambas paletas, filtro de filas, extracción de
páginas EMF de un spool sintético, detección de formatos y PJL, conteo de
páginas XPS, búsqueda en `.SHD`, conversión a `C$`, ajuste de página.

## 2. Pruebas manuales ya realizadas (PC de desarrollo, 2026-09-19)

Windows 11, usuario sin permisos de administrador, impresora "Microsoft Print to
PDF", trabajos generados con `cola_testjob` y spool EMF simulado.

| Prueba | Resultado |
|---|---|
| Inicio en modo oscuro siguiendo al sistema | OK |
| Trabajos nuevos aparecen solos ("En vivo") | OK |
| Navegación ↑/↓, AvPág (página 3 de 5) | OK |
| Reanudar con `Ctrl+R` (cambio confirmado con `Get-PrintJob`) | OK |
| Cancelar con confirmación (atajo reasignado a `Ctrl+K`) | OK |
| Reordenar sin permisos → mensaje de acceso denegado | OK (comportamiento esperado) |
| Filtro `Ctrl+F` ("1 de 3") | OK |
| Vista rápida `Espacio` / `Esc` | OK |
| Tema claro con `Ctrl+D` en caliente | OK |
| Panel F1 y reasignación persistida en `config.json` | OK |
| Historial: cancelado vs. retirado | OK |
| Servidor inexistente: mensaje, UI responsiva, reintentos | OK |
| Ejecutable Release autónomo (1,4 MB, ~21 MB RAM) | OK |
| Retención automática: 3 trabajos nuevos quedan "Paused, Spooling" sin imprimirse | OK |
| Liberar un retenido: se imprime y no se vuelve a pausar | OK |
| Panel de atajos: clic selecciona, segundo clic reasigna | OK |
| Vista previa PCL / PostScript / XPS con los renderizadores incluidos | OK |

## 3. Pruebas pendientes en el LAIMI (sección 7 del TODO)

Ejecutar con un usuario **administrador del servidor** y otro **sin** permisos.
Anotar resultado, fecha y versión en la tabla final.

### 3.1 Conexión y permisos
- [ ] Conectar a `\\SERVIDOR` desde la PC del operador; aparecen todas las impresoras compartidas.
- [ ] Ejecutar la app en el propio servidor (servidor local).
- [ ] Barra de estado "En vivo" (si dice "sondeo", revisar firewall: "Compartir archivos e impresoras").
- [ ] Usuario sin permisos: aviso en la barra; puede ver pero no cancelar trabajos ajenos (mensaje claro).

### 3.1b Retención automática
- [ ] Con la retención activa ("Retención activa" en la barra), un estudiante imprime: el trabajo aparece como **Retenido** y la impresora no saca nada.
- [ ] `Ctrl+R` sobre el trabajo: se imprime; no vuelve a quedar retenido.
- [ ] Varios estudiantes imprimen a la vez: todos quedan retenidos.
- [ ] Documento de 1 página muy pequeño: ¿alcanza a salir algo antes de pausarse? (si pasa, configurar la impresora en el servidor para iniciar tras la última página).
- [ ] `Ctrl+Shift+H` desactiva la retención: los trabajos nuevos se imprimen normalmente.

### 3.2 Múltiples impresoras simultáneas
- [ ] Estudiantes imprimen a 3+ impresoras a la vez; la vista "Todas" las muestra ordenadas por impresora y posición.
- [ ] `Ctrl+Tab` alterna impresoras; el filtro por impresora funciona.

### 3.3 Trabajos grandes
- [ ] PDF de 200+ páginas / 100+ MB: la lista sigue fluida, la vista previa muestra las primeras 30 páginas con el aviso "Se muestran solo las primeras…".
- [ ] Varios trabajos grandes seguidos: memoria estable (caché limitada a ~384 MB).

### 3.4 Desconexión a mitad de trabajo
- [ ] Desconectar el cable/apagar una impresora mientras imprime: estado "Error"/"Sin conexión", aviso en barra y notificación.
- [ ] Reconectar: el estado se recupera solo.
- [ ] Apagar el servidor / cortar la red: "No se pudo conectar", sin congelarse; al volver, reconecta.

### 3.5 Acciones reales
- [ ] Pausar / reanudar / reiniciar / cancelar trabajos de estudiantes.
- [ ] Subir / bajar posición (`Ctrl+↑/↓`) con permisos de administración.
- [ ] Pausar / reanudar / vaciar impresora.
- [ ] Reenviar: mismo modelo de impresora (RAW), distinto modelo con Ghostscript, EMF.
- [ ] Cancelar todos los trabajos de un usuario.

### 3.6 Vista previa con datos reales
- [ ] Trabajo EMF (driver GDI con renderizado en el servidor).
- [ ] Trabajo PCL / PS (Client Side Rendering) con GhostPCL / Ghostscript instalados.
- [ ] Trabajo XPS (driver v4).
- [ ] Spool con pooling (`FPnnnnn.SPL`) localizado correctamente.

### 3.7 Atajos y tema
- [ ] Todos los atajos de la tabla del MANUAL (con teclado español: `Ctrl++`, `?`).
- [ ] Cambio de tema del sistema con la app abierta (modo "seguir al sistema").
- [ ] Monitor con 150 % / 200 % de escala y cambio entre monitores con distinto DPI.

### 3.8 Instalación
- [ ] Instalador: instala, crea accesos, opción "Iniciar con Windows", desinstala limpio.
- [ ] Modo portable (`portable.txt`).

| Fecha | Versión | Prueba | Resultado | Notas |
|---|---|---|---|---|
| | | | | |
