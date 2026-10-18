# Burnout 3 PAL Enhanced: estado comprobado

> Integración experimental: Release, host/Vulkan y diferencial VU pasados.
> Español/RWA/tarjeta conservados; captura frame2400 negra pendiente de aislar.
> No promover todavía a estable. Detalles en UPSTREAM_INTEGRATION.md.

Checkpoint jugable: d47b1328ffb0f301268247ae12ca0fffaa146187.
Documentaci?n posterior del fork: 13f42950282d6167707d6ae1bbf9943f02098248.
Motor estable: 28057a1a1ac2b83eb0982e2ffc0a9b9575213598 (v0.9.0).

- 9.384 funciones identificadas; 9.383 traducciones EE compiladas.
- 6.460 verificadas por lifter; 2.923 NOT VERIFIED. Compilar/jugar no cambia esa clasificaci?n.
- 97 programas VU PAL compilados; cobertura VU1 >99,99 % en la sesi?n de carrera registrada.
- Espa?ol y recursos GlobalSp.bin/HeadSp.bin comprobados.
- Pantallas originales de idioma, memory card y 50/60 Hz alcanzadas.
- Usuario confirma 60 Hz, perfil guardado/recargado, carrera completada, controles, audio y SkipMovie.

El antiguo bloqueo 0x00242AC8 est? resuelto: entrada de tabla de saltos libmc2.
No es el bloqueo actual. No se certifican todos los modos ni equivalencia global.

## Integraci?n actual

Historial USA y motor coherente en rama/worktree experimental. La rama estable,
sus binarios y los datos del usuario permanecen intactos.

- [Integraci?n y pr?ximas sincronizaciones](UPSTREAM_INTEGRATION.md).
- [Hallazgos del arranque](BOOT_FLOW.md).
- [Recuperaci?n local](../../games/burnout3-pal/port/RECOVERY.md).

Rendimiento 3D y compatibilidad general pendientes de validaci?n adicional.
Sin release PAL p?blica certificada; no distribuir recursos ni c?digo derivado del juego.
