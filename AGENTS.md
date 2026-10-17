# Instrucciones de trabajo: Burnout 3 PAL Enhanced

- Para tareas PAL, leer también `games/burnout3-pal/AGENTS.md` y la política
  persistente `games/burnout3-pal/DEVELOPMENT_POLICY.md`: consultar NTSC-U y
  motor antes de desarrollar soluciones nuevas, y reutilizar AutoTest.

## Contexto y comprobación inicial

- Leer PAL_DEVELOPMENT.md para estado, evidencias y pendientes.
- Comprobar rama `pal-enhanced`, HEAD y `git status` antes de trabajar. Si la rama difiere, informar y pedir indicación; no cambiarla ni descartar trabajo automáticamente.
- Inspeccionar archivos e informes reales. La documentación puede estar desactualizada: señalar discrepancias.

## Alcance y protección

- Trabajar en pasos pequeños y revisables, con selección explícita de funciones y casos.
- Proteger `engine/`, `games/burnout3/` (NTSC-U), game.json, ELF y bases existentes. No modificarlos sin autorización explícita para esas rutas.
- No sobrescribir ejecuciones anteriores; crear directorios exclusivos bajo build. Conservar informes parciales y diagnósticos.
- No ejecutar juego completo, port.py, derivación o compilación fuera del alcance autorizado.

## Contenido y Git

- Nunca versionar ISO, ELF/ejecutables, DLL, binarios, BIOS, recursos o contenido comercial del juego, bases generadas, C++ traducido/reconstruido, informes generados ni datos personales/secretos.
- `build/` y `extracted/` están ignorados. Comprobar exclusiones y el índice; no usar git add -f para artefactos.
- Los scripts propios pueden contener direcciones y datos de prueba sintéticos, pero no cuerpos manuales del juego ni oráculos reconstruidos incorporados.
- Usar rutas portables relativas al script/configuración; no incorporar rutas personales. Las rutas absolutas en diagnósticos locales permanecen bajo build ignorado.
- No ejecutar git add, commit, push ni publicar sin autorización del usuario para la acción. Con autorización, preparar solo las rutas indicadas y revisar el diff del índice. Nunca inferir permiso de push de un permiso de commit local.

## Pruebas y evidencia

- Nunca confundir validación de esquema o inspección estática con equivalencia dinámica.
- No inventar casos originales, resultados, cobertura ni tiempos. Recuperar evidencia o declarar que falta.
- Mantener pruebas independientes y trazables: hashes de inputs/herramientas, casos exactos, argumentos u64, memoria inicial y contratos, informes por función y agregado.
- Comparar MIPS, expresión derivada y DLL por separado cuando corresponda; exigir todos los casos, retornos y efectos de memoria. No aceptar omisiones silenciosas.
- Distinguir retorno s32 del arnés de imágenes u64 de argumentos. No afirmar equivalencia de registros completos, HI/LO o retornos de 64 bits sin comprobación específica.
- Cubrir ramas, selecciones condicionales y delay slots; registrar cobertura no demostrada como pendiente.
- Desactivar mapeo automático de fallos en verificación final. Scratch es exclusivamente sintético para pruebas aisladas, no memoria libre garantizada del juego completo.
- Los fallos no son éxito. Detener ante error estructural; continuar ante fallo individual solo si el procedimiento autorizado lo permite. No corregir motor/código generado automáticamente.

## Aislamiento Windows

- Mantener un Job privado por trabajador, asignado antes de reanudarlo, sin elevar privilegios ni eludir restricciones del sandbox.
- Terminar únicamente procesos/Jobs propios. Verificar limpieza y conservar PID, APIs y GetLastError.
- Timeouts individual y global deben impedir passed; timeout independiente de compilación sigue pendiente hasta evidencia de implementación y prueba.
- Pruebas del supervisor: procesos sintéticos propios, sin juego ni compilación salvo autorización específica.
