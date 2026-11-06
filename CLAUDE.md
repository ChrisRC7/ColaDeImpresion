Lee y sigue [AGENTS.md](AGENTS.md): contiene el orden de lectura (docs/CONTEXTO.md,
TODO.md, docs/GUIA_IA.md) y las reglas del proyecto. En particular: nunca
agregues `Co-Authored-By` en los commits y actualiza TODO.md y docs/CONTEXTO.md
al terminar cada tarea.

**Active Directory: solo lectura.** Al trabajar con el Active Directory (el
directorio del dominio, p. ej. para leer el carnet de un estudiante) solo se
hacen operaciones de lectura: conectar/autenticarse y buscar. Nunca se crea,
modifica, renombra, mueve ni borra nada (ni desde el código de la app ni desde
comandos o scripts de prueba).
