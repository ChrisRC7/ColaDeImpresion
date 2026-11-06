# Instrucciones para agentes de IA

Antes de hacer cualquier cambio, lee en este orden:

1. [docs/CONTEXTO.md](docs/CONTEXTO.md) — contexto vivo del proyecto.
2. [TODO.md](TODO.md) — tareas hechas y pendientes.
3. [docs/GUIA_IA.md](docs/GUIA_IA.md) — modelo de trabajo, reglas, recetas y trampas conocidas.

Reglas clave (detalle en la guía):

- **Nunca** agregues `Co-Authored-By` ni firmas de IA en commits o PRs.
- Al terminar cada tarea actualiza `TODO.md` y `docs/CONTEXTO.md` (incluida la bitácora).
- C++20 + Win32 API, código modular por capas; comentarios y documentación en español.
- **Active Directory: solo lectura.** Solo conectar y buscar (p. ej. el carnet,
  `core/Directory`); nunca crear, modificar, mover ni borrar nada, tampoco desde
  scripts o comandos de prueba.
- `cola_tests.exe` en verde y compilación sin warnings antes de cada commit.
- **Al terminar cada tarea que cambie la app, regenerar el paquete** sin que el
  usuario lo pida: `.\scripts\build.ps1 -Package` (Release + pruebas). Deja
  `dist\` lista para usar con solo ejecutar `ColaDeImpresion.exe` (modo portable,
  renderizadores, documentación, herramientas), **limpia** (sin `app.log`,
  `historial.json` ni `config.json`), y `dist\ColaDeImpresion <versión>.zip` con
  todo su contenido. Antes, subir la versión (CMakeLists.txt, res/app.rc,
  installer/ColaDeImpresion.iss) y agregar las novedades al LEEME que arma el
  script. Detalle en la guía (§3 y §9).
