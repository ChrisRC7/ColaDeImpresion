# Manual de uso — Cola de Impresión (LAIMI)

## 1. Primer inicio

1. Ejecutar `ColaDeImpresion.exe`. Aparece la lista de trabajos de **este equipo**.
2. Para controlar el servidor del laboratorio: `Ctrl+O` (o menú `⋯` → *Conectar a servidor…*), escribir el nombre, p. ej. `\\SERVIDOR-LAIMI`, y `Enter`. El servidor queda en la lista de **servidores recientes** del menú.
3. El botón de la izquierda (o `Ctrl+E`) elige la impresora; **Todas las impresoras** muestra la cola completa del servidor con el nombre de la impresora en cada trabajo.

La barra inferior resume la cola (trabajos, imprimiendo, en pausa, con error),
el servidor, problemas de impresoras y si la actualización es **en vivo** o por
**sondeo**.

## 2. Trabajos

Cada fila muestra: estado (ícono y etiqueta de color), documento, usuario,
equipo, hora, impresora, páginas y tamaño.

| Acción | Teclado | Mouse |
|---|---|---|
| Seleccionar | `↑` `↓` `Inicio` `Fin` | clic |
| Selección múltiple | `Shift+↑/↓`, `Ctrl+A` | `Ctrl`+clic, `Shift`+clic |
| Pausar / reanudar | `Ctrl+P` / `Ctrl+R` | clic derecho |
| Reiniciar | `Ctrl+I` | clic derecho |
| Cancelar | `Supr` (pide confirmación) | clic derecho |
| Cancelar todos los de un usuario | — | clic derecho → *Cancelar todos los trabajos de…* |
| Subir / bajar en la cola | `Ctrl+↑` / `Ctrl+↓` | clic derecho |
| Reenviar a otra impresora | `Ctrl+T` | clic derecho |

**Reenviar**: se pausa el original, se envía a la impresora elegida y, si todo
sale bien, se elimina el original. Si ambas impresoras usan el mismo driver se
copian los datos exactos; si no, se convierten (EMF directamente; PCL/PS/PDF con
Ghostscript). Si no hay forma de convertir, la app pregunta si enviar los datos
tal cual (solo funciona si ambas impresoras entienden el mismo lenguaje).

## 2.1 Retención automática (trabajos nuevos en pausa)

Está **activada por defecto**. Cada trabajo que llega a la cola se pausa en el
mismo instante en que el spooler avisa y queda con la etiqueta violeta
**Retenido** hasta que un operador lo libere con **Reanudar** (`Ctrl+R`,
clic derecho, o `Ctrl+A` + `Ctrl+R` para liberar todos). Un trabajo liberado no
se vuelve a retener. Llega una notificación por cada trabajo retenido, salvo que
las notificaciones estén en *ninguna*.

- Activar/desactivar: menú `⋯` → *Retener (pausar) todos los trabajos nuevos* o `Ctrl+Shift+H`. La barra inferior muestra "Retención activa".
- Los trabajos que ya estaban en la cola al abrir la app no se tocan; solo los que llegan después.
- **La app tiene que estar abierta** (puede estar en la bandeja) para retener: conviene activar *Iniciar con Windows* en el equipo del operador.
- Para retener trabajos de otros usuarios hace falta el permiso "Administrar documentos"; si falla, la barra muestra "No se pudieron retener N trabajos".
- Es muy rápido (se salta la espera de notificaciones agrupadas), pero un trabajo diminuto podría empezar a imprimirse antes: en ese caso igual se pausa. Para que eso no pase nunca, configure las impresoras del servidor con *Iniciar la impresión después de que la última página se haya enviado a la cola*.

## 3. Impresoras

Menú `⋯`: *Pausar impresora* (`Ctrl+Shift+P`), *Reanudar impresora*
(`Ctrl+Shift+R`), *Vaciar cola de la impresora…*. Actúan sobre la impresora
elegida o, en la vista de todas, sobre la del trabajo seleccionado.

Los problemas (sin papel, atasco, puerta abierta, sin tóner, fuera de línea…)
aparecen en el botón de impresora, en la barra de estado y como notificación.

## 4. Vista previa

- Panel derecho: datos del trabajo y la página. `Ctrl+B` lo muestra/oculta; se puede cambiar su ancho arrastrando el separador.
- `Espacio` o `Enter`: **vista rápida** a pantalla completa (`Esc` para volver).
- `AvPág`/`→` y `RePág`/`←`: cambiar de página. Rueda del mouse: páginas.
- `Ctrl+rueda`, `Ctrl++`, `Ctrl+-`: zoom; `Ctrl+0`: ajustar. Con zoom, arrastrar para mover; doble clic alterna 100 %/200 %.

De dónde sale la vista previa:

| Datos del trabajo | Qué se necesita |
|---|---|
| EMF (drivers GDI, impresión en el servidor) | Nada extra |
| PostScript / PDF | Ghostscript |
| PCL 5 / PCL XL (PCL 6) | GhostPCL |
| XPS (drivers v4) | GhostXPS (o, sin él, miniatura de la primera página si Windows tiene visor XPS) |

Siempre hace falta poder **leer la carpeta de spool del servidor** (ser
administrador del servidor o configurar `spoolDirOverrides`). Si no, el panel lo
explica. Las vistas previas se guardan en caché mientras la app está abierta.

## 5. Buscar y filtrar

`Ctrl+F` y escribir: filtra por documento, usuario, equipo, impresora o estado.
Varias palabras = todas deben coincidir (`jperez pdf`). `Esc` limpia el filtro.
En la barra de estado se ve "Filtro: … (n de m)".

## 6. Historial y estadísticas

`Ctrl+H` (o el botón del reloj) muestra los trabajos que salieron de la cola con
su resultado: **Impreso**, **Cancelado**, **Falló** o **Retirado** (se fue sin
que se viera imprimir). La barra de estado muestra páginas impresas hoy y en los
últimos 7 días. Menú → *Borrar historial* lo vacía.

## 7. Apariencia e idioma

- `Ctrl+D` o el botón sol/luna: alterna claro/oscuro. En el menú: *Tema: seguir al sistema / claro / oscuro*.
- Menú → *Language: English / Idioma: Español*.

## 8. Atajos de teclado

`F1` (o `?`) abre la lista. Para cambiar uno: elegirlo con `↑`/`↓` o con un
**clic**; luego `Enter`, **otro clic**, **doble clic** o clic sobre la tecla de la
derecha, y presionar la nueva combinación. `Supr` lo deja sin asignar, `R` restaura todos
los predeterminados, `Esc` cierra. Si la combinación ya era de otro comando, se
le quita a ese y se avisa.

## 9. Bandeja del sistema y notificaciones

- Cerrar la ventana la **oculta en la bandeja** (desactivable en el menú: *Cerrar a la bandeja del sistema*). Clic en el ícono para volver; clic derecho → *Salir*.
- *Iniciar con Windows* arranca la app minimizada al iniciar sesión.
- *Notificaciones*: ninguna / solo errores (predeterminado) / todas (incluye trabajos nuevos e impresos).

## 10. Configuración avanzada (`config.json`)

Menú → *Abrir carpeta de configuración*. Con la app cerrada se puede editar:

```json
{
  "spoolDirOverrides": { "\\\\SERVIDOR-LAIMI": "\\\\SERVIDOR-LAIMI\\spool$" },
  "renderers": { "ghostscript": "C:\\Program Files\\gs\\gs10.05.1\\bin\\gswin64c.exe",
                 "ghostpcl": "", "ghostxps": "" },
  "pollIntervalMs": 3000,
  "previewDpi": 110,
  "previewMaxPages": 30,
  "historyLimit": 2000,
  "confirmCancel": true
}
```

- `spoolDirOverrides`: carpeta desde donde leer el spool de cada servidor (clave `""` = este equipo). Útil si se comparte la carpeta `PRINTERS` del servidor con permisos de lectura para los operadores.
- `pollIntervalMs`: intervalo del sondeo de respaldo (500–60000 ms).
- Línea de comandos: `--server NOMBRE` (conectar al iniciar), `--tray` (iniciar oculta).

Registro de diagnóstico: `app.log` en la misma carpeta.

## 11. Problemas frecuentes

| Síntoma | Causa / solución |
|---|---|
| "Sin permisos de administración…" en la barra | El usuario no tiene "Administrar documentos" en esas impresoras. Pedir el permiso o *Reiniciar como administrador*. |
| "No se pudo leer el archivo de spool: acceso denegado" | Hace falta ser administrador del servidor o configurar `spoolDirOverrides`. |
| "Los datos están en PCL / PostScript…" | Instalar Ghostscript/GhostPCL (ver README). |
| "Actualización por sondeo" | El servidor no envía notificaciones (firewall/RPC). Funciona igual, con unos segundos de retraso. |
| "No se pudo conectar con el servidor" | Nombre incorrecto, servidor apagado o firewall bloqueando "Compartir archivos e impresoras". Se reintenta automáticamente. |
