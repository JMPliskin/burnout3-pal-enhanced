# Arranque PAL: hallazgos conservados

Evidencias detalladas, tests e informes: [ADAPTATIONS.md](../../games/burnout3-pal/port/ADAPTATIONS.md).
Los hitos manuales proceden del usuario; no certifican equivalencia global.

| Etapa | Contrato / resultado confirmado |
|---|---|
| Entrada EE | SLES_525.84, entrada 0x00100008. Se conservan 9.383 traducciones EE, sin stubs para completar el inventario. |
| KernelStore | PAL 0x001156C0: escribe a1 en a0 mediante Write32 del host, preservando hooks y errores. Reutiliza el contrato USA acreditado. |
| Código copiado | 0x004854E0 → 0x80076000; mecanismo original de descubrimiento, bytes contrastados. |
| Extensión TLB | 816 bytes 0x004846A8–0x004849D8 → 0x80075000–0x80075330. Solo resolver 0x80075000–0x80075038 integrado; no se inventan tlbwr/tlbwi/tlbp/tlbr. |
| Logitech | RPC 0x046D046D, procedimiento 12: versión 0x010B1B00 en +4. Procedimiento 1: ausencia de dispositivo. |
| Sincronización | 0x1000F000 es INTC_STAT, no D_STAT. VBLANK y DMA GIF progresaban. PAL 0x00102E08 no es sceGsSyncPath: no renombrar ni forzar bits. |
| Audio | RWA.IRX original en VM IOP; handshake SIF tipo 9, versión 0x115 observada en memoria. Imports ausentes conservan trampas. |
| Recursos | GTFSCDVD.IRX, servidor 0x00475453, peticiones 1/3/4 y transferencias EE. PrgData.bin/fixups coherentes; desaparece 0x0C620034 sin corregir punteros manualmente. |
| Frontend | Fase 9 superada naturalmente; fase 23/0x17 completada. Más tiempo reveló frontend, sin acreditar un bloqueo de audio. |
| Español | down@1200-1211, down@1300-1311, cross@1500-1511. Cursor 0x01E89EB0+0x48: índice 2; idioma 5 en 0x004E2E98; GlobalSp.bin/HeadSp.bin. |
| Memory card | 0x00242AC8: entrada de tabla de saltos. Extensión 0x00242A50–0x00242D50 y siete CardOr conservan trabajador/cola/polling. Sin --memcard: handlers originales; con tarjeta: ps2host::mc2 persistente. |
| Vídeo PAL | Pantalla española 50/60 Hz tras idioma/tarjeta. 50 Hz inicialmente. Elegir 60 Hz lleva a segunda confirmación con No por defecto: elegir Sí. No forzar RAM ni omitir pantallas. |
| Perfil/carrera | Usuario confirma guardado y recarga, carrera libre, coche, tutorial, conducción, colisiones, takedowns y audio. |
| FMV | SkipMovie PAL 0x00131410 solicita parada +0x4A por flanco Start; no escribe terminada +0x49, no salta loops, ejecuta actualización original. |

## Interpretación de diagnósticos

El backend software avanzaba aproximadamente 9,4 VBLANK/s frente a 63,8 con GPU
en el diagnóstico de frontend: lentitud no equivalía a bloqueo. La pérdida de
textos/banderas en algunas capturas no se tomó como prueba única de transición.
El contador de funciones mide cobertura de ejecución, no corrección.

Se capturaron 97 programas VU durante juego PAL y se compilaron posteriormente;
una sesión de carrera registró cobertura VU1 >99,99 %. Frame Pacing On mejoró
la suavidad observada, sin demostrar mayor velocidad de simulación. GPU/GS y
readbacks requieren medición por escena; los porcentajes solapados no se suman.

El experimento GPU Thread aislado previo alcanzó 50/60 Hz On/Off: 2.047 funciones,
idioma 5 y tres capturas idénticas. Terminó por límite previsto; no valida carrera
ni sustituye las pruebas de esta integración completa del motor.

Conservar el fallo histórico host 0xC0000409 y su minidump: otra ejecución con
TEMP exclusivo pasó, pero no quedó demostrada la causa.
