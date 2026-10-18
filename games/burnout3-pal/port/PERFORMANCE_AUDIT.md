# Referencia de rendimiento PAL

## Integración experimental de upstream

Motor d7e6e023 en integration/upstream-20261018: build y pruebas VU/host/Vulkan
pasados, sin medición de carrera. Captura frontend2400 negra frente a referencia
visible: validar captura/renderizado antes de atribuir mejoras de rendimiento.
Frame Pacing On conservado. Los números siguientes siguen perteneciendo al motor
estable v0.9.0; no trasladarlos al nuevo. Ver docs/pal/UPSTREAM_INTEGRATION.md
desde raíz y build/pal-upstream-ef97de2beb del worktree experimental.

## Medición histórica estable

Sesión manual `build/manual-play/fmv-skip-0d77722ceaef`: ejecutable con SkipMovie,
Vulkan RTX 3070 Ti Laptop, salida 0. Captura de configuración al inicio y archivo
actual coinciden: Frame Pacing On, V-Sync Off, resolución 4x, 16:9, ventana,
antialiasing Off. Sin virtual-time, GS-inline ni VIF1-sync.
No se midió cuándo el usuario cambió ajustes dentro de otras sesiones.

151,4 segundos de sesión: VU1 28,0 %, VU0 3,0 %, GPU bookkeeping 5,1 %, GPU
drawing CPU 0,5 %, sincronización GPU/memoria 42,3 %, espera GS 54,5 %.
Los tiempos se solapan: no sumarlos ni atribuirlos exclusivamente a carrera.
GPU busy es una medición separada de GPU: 48,8 s. Esperas por submissions: 48,2 s;
10.402 readbacks de memoria acumulan esos 48,2 s. Transferencias reportadas:
2.912.720 KB a CPU, 3.099.010 KB desde CPU. No demuestra que todos los readbacks
sean redundantes ni que procedan de presentación; faltan causas por escena.
VU1: 6.229.461.329 pares compilados de 6.229.754.163; intérprete residual intacto.

## Reutilización NTSC-U comprobada por código local

| Mecanismo | Situación PAL / evidencia |
|---|---|
| Presentación directa de targets GPU, sin readback por frame normal | Compartido: `gpu_bridge.cpp` SetGsFrameHandler/PresentGpuPicture. Capturas/fallbacks aún pueden leer targets. |
| VIF1/VU1 en trabajador y GS en hilo | Compartido; `host.cpp` activa ambos por defecto. Los launchers no fuerzan ejecución síncrona. |
| PSMT8H de targets/CLUT en GPU | Compartido: `gpu_bridge.cpp` tex_format=4, muestreo de targets; no hook NTSC. |
| Recarga parcial de filas modificadas y reutilización del estado de dibujo | Compartido: `gpu_bridge.cpp` stale_row0/stale_row1 y renderer; no copiar otro motor. |
| Acceso RAM inline, caché Dispatch, DMA por puntero, VU compilado | Compartido; PAL enlaza objetos host y VU ya validados. |
| Frame pacing, V-Sync, resolución y panel F9 | Compartido; el usuario confirma cambios funcionales. Pacing regula presentación, no prueba mayor velocidad de simulación. |
| WaitsForInterrupt + AddIdleLoop de RenderWare | Específico NTSC-U: función 001F2408/bucle001F24E0. PAL no los registra. Equivalente/coste PAL pendientes: no asumir direcciones ni que explique readbacks. |
| PostEffects / BLUR Off | Específico NTSC-U: 002FA9A0. PAL no registra ese hook. Desactivar efectos cambia la imagen; no es una optimización transparente. |
| FastLoading y menú PC SETTINGS dentro del juego | Específicos NTSC-U, ausentes PAL; no son correcciones demostradas del rendimiento de carrera. |

FINDINGS contiene perfiles de distintas revisiones; su afirmación antigua de
que VU domina el frame no sustituye la medición PAL actual. Las mejoras gráficas
posteriores ya presentes en el motor tampoco deben proponerse por segunda vez.

## Medición corta preparada, no ejecutada

```powershell
& '.\Jugar Burnout 3 PAL FMV.bat' --medir
```

Reutiliza el binario y la tarjeta actuales; añade solo `--frame-times 120` y
`--setting frame_pacing=on`. Conserva resolución, V-Sync, aspecto, audio y ventana.
No combinar con `--pacing`: ese modo antiguo de comparación fija otros ajustes.
No recompila, no envía botones, no activa todos los profilers.

Elegir manualmente la misma carrera/coche, saltar tutorial, observar un tramo
de 20–30 segundos sin completar otra carrera y cerrar normalmente. Anotar qué
tramo corresponde a carrera y no mezclarlo con menús/carga. El launcher imprime
la carpeta exclusiva con launch.json/stdout.log/stderr.log.

Las líneas `[frames]` separan VBLANK/s, transferencias VIF1/s, uso VIF1/GS,
esperas EE, GPU busy y presents/s con intervalo/jitter. VIF1/s es un indicador
de trabajo 3D, no una prueba universal de fotogramas únicos visibles. No hay
medición directa de latencia de entrada ni del paso de física; no inventarlas.
Si no basta para localizar las lecturas, usar después el sampler GS existente
(`--sample-gs`, `--sample-from N`) sobre una ventana acreditada de carrera, o
el timeline existente para percentiles de presentación; no activarlos todos.

## Único ajuste candidato para la siguiente fase

Probar resolución interna 2x frente a la referencia 4x, en el mismo tramo,
manteniendo Frame Pacing On y todos los demás ajustes. 2x usa una cuarta parte
de los píxeles de 4x; es un intercambio de calidad por coste, no una corrección
del motor ni una mejora de FPS demostrada. No aplicado en esta auditoría.
La medición A/B debe decidir si reduce tiempo GPU/readbacks y estabiliza presents
antes de tocar sincronización o introducir nuevos overrides.
