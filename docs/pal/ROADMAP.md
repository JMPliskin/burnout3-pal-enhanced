# Burnout 3 PAL Enhanced — Development Roadmap

Versión 0.1.17 · Revisión documental: 2026-10-22.
Base: roadmap 0.1 del mantenedor; [correcciones trazables](PLANNING_CHANGELOG.md).
Planificación técnica: **no autoriza implementación, compilación, integración ni publicación**.

## Visión y reglas

Port nativo Windows de Burnout 3 PAL **SLES-52584 (1.01)**, conservando idiomas,
selección de vídeo y comportamiento europeo. Precisión y estabilidad preceden al
rendimiento. Reutilizar burnout3-pc/ps2-port-engine; un backend propio sigue
siendo una posibilidad condicionada a evidencia de una limitación estructural.

1. Medir antes de optimizar; validar gráficos y comportamiento después.
2. No suponer equivalencia USA/PAL ni aplicar offsets globales.
3. Separar juego, vídeo regional, renderer y funciones PC.
4. Conservar versiones estables, experimentos, resultados y decisiones.
5. Consultar USA y motor por separado y fijar sus revisiones exactas.
6. No hacer commit, push, cambios de submódulo o ampliaciones funcionales sin autorización.
7. El roadmap registra decisiones; el mantenedor fija prioridades y autoriza cambios.

## Baselines y evidencia

| Referencia | Identidad | Alcance |
| --- | --- | --- |
| Rama pública pal-enhanced | `3ad44e482c201097333dc79a452bfb6b8b4bc648` | HEAD remoto observado el 20/10; no contiene todavía la integración widescreen |
| Integración publicada | `434a86c9a4bb8e9392fea5dea99e6967770aec18` | integration/pal-widescreen-20261019; base de esta documentación |
| Motor de integración | `d7e6e023f261d9f95ad097fcd063ec95e40ca465` | Gitlink limpio, conservado |
| Release protegida | [v0.1.0-alpha](https://github.com/JMPliskin/burnout3-pal-enhanced/releases/tag/v0.1.0-alpha) | No se altera el tag, ZIP ni receta histórica instrumentada |
| Candidata v0.2.0-alpha | Rama local `release/v0.2.0-alpha-candidate`; motor `bfad7255` | Paquete x64 y prueba manual de inicio/F9/AA preparados; publicación bloqueada por revisión de derechos y autorización final; sin modificar la release anterior |
| EXE experimental de diagnóstico | SHA-256 `12d2228832aa9c3b1c35f8ae12b171ec94c966a5f16c014ea133a9010748f08c` | Widescreen con instrumentación local; no confundir con build limpio |
| USA revisado | `444b541b9919055af3ee97787e84de7c2ceed98f` | Tag v0.6.0-alpha; no es una referencia móvil a master |
| Motor de USA 0.6.0 | `721c73cd00e21aa87349ad5ae092277dcd3b69fa` | Gitlink del tag comprobado; no incorporado a PAL |

Las evidencias privadas se identifican sin publicar sus archivos ni rutas personales
en el [índice de la matriz](UPSTREAM_PAL_MATRIX.md#evidencias-locales).
El [tracker](UPSTREAM_TRACKER.md) conserva releases, commits y decisiones upstream.
El [catálogo de investigación externa](EXTERNAL_ENGINE_RESEARCH.md) reúne métodos,
revisiones, límites arquitectónicos y pruebas necesarias antes de cualquier adaptación.

### Estado demostrado y sus límites

- Alpha: inicio Windows/ISO PAL, español, selección original 50/60 Hz, audio/FMV,
  carreras, persistencia ISO y perfil nuevo guardado/recuperado, GPU Thread On.
- Integración: Hor+ 3D, 4:3 original, Fill y HUD principal aceptados; 14 pruebas de
  cámara y 27 de HUD. Aceptación del build limpio con GPU Thread Off y HD Off.
- Experimento separado: F9 Off/FXAA/SMAA y persistencia aceptados a 2x/4x, con
  GPU Thread On. No equivale a estar integrado en la rama pública.
- HD333: sustituciones reales, pero calidad/rendimiento insatisfactorios. No homologado.
- VU compilado: cobertura por sesión cercana o igual a 100%; no prueba equivalencia
  completa ni activación de VU Fast.
- GS Replay: 12 procesos y 13 pruebas sintéticas, estabilidad de muestras; diferencias
  software/GPU pendientes. No reproduce PresentGpuPicture ni su fallback.
- CLUT GPU CSM1 y decode activos. CSM2 usa CPU; su frecuencia/coste PAL no están atribuidos.
- PAL-010 atribuyó en las trazas PAL60 las grandes lecturas de 560 KiB al
  fallback de presentación observado. La integración regional pasó su alcance
  experimental y aceptación manual; esto no certifica todos los efectos,
  configuraciones o equipos. Fences y MemoryRead se contabilizan por separado.
- No está certificada la precisión de todos los efectos en PAL50/PAL60. Conservar
  la selección PAL50 no equivale a validar todas las funciones nuevas en ese modo.

### PAL-010 — integración experimental aceptada (22/10/2026)

- La invalidación global stale provocaba fallbacks innecesarios aun cuando las
  páginas visibles del target seguían válidas. La solución C comprueba región,
  identidad, geometría, PSM, escala, inicialización y conflictos de coherencia;
  presenta directamente solo si el layout PAL60 acreditado es seguro.
- El worktree aislado sobre motor `d7e6e023` contiene la integración limpia en
  `PresentGpuPicture`, activada solo por el puerto PAL y sin selector A/B/C.
  El selector inicial 640×512 conserva el fallback original.
- El usuario aceptó el nuevo EXE a 2× con Frame Pacing ON/OFF y a 3×/4× con
  pacing ON. 2× con pacing ON es la configuración experimental recomendada;
  OFF sigue disponible. Frame Pacing es independiente de C; no cambiaron reloj,
  VBLANK ni simulación. Las cuatro sesiones terminaron con código 0.
- Compilación incremental, predicado sintético y GS Replay corto pasaron. GS
  Replay no prueba `PresentGpuPicture` en carrera; la aceptación manual cubre
  el alcance experimental. EXE SHA-256:
  `612719b36ad1103d71342b4d01b4da10a0d64640007f99115cbfd9beadf6940b`.
  Esto no es una release pública ni certificación general de hardware o PAL50.
- La política permanente de validación incremental está añadida al `AGENTS.md`
  principal; la evidencia anterior y la del nuevo EXE quedan diferenciadas.
- GPU Thread, VU Fast y AA mantienen pendientes sus validaciones e integración
  propias; USA 0.5.2/0.5.3 requiere selección y pruebas PAL. 120 FPS sigue en
  fase posterior. No cambian prioridades ni estados de esas tareas.

### Reconciliación Git del motor para PAL-010 (22/10/2026)

- El motor fijado en `pal-enhanced` (`28057a1a`) es ancestro de la base aceptada
  `d7e6e023`: 35 commits alcanzables, incluido un merge (20 en primer padre).
  Es una ascendencia compatible, no una secuencia lineal de 35 commits. El EXE
  PAL-010 aceptado ya incluía esa base; no se reprodujeron sus commits.
- Rama del motor `experimental/pal010-regional-engine`: commit
  `720583cfa885b3bbc25a14302d0b4e0a779433bf` sobre `d7e6e023`, con la
  implementación regional limpia. Rama PAL `integration/pal010-regional-accepted`:
  commit `43a28f8e0f3b208bf596dfd9d25f780eb2124a53` sobre `434a86c9`, con
  la activación PAL, el test sintético y el gitlink exacto al commit del motor.
- Publicadas en orden: [rama del motor](https://github.com/JMPliskin/ps2-port-engine/tree/experimental/pal010-regional-engine)
  (`720583c`) y [rama PAL](https://github.com/JMPliskin/burnout3-pal-enhanced/tree/integration/pal010-regional-accepted)
  (`d370c12`). El segundo commit cambia solo `.gitmodules` al fork del motor,
  conserva la URL original como upstream y mantiene el gitlink `720583c`.
  Git confirmó ambas referencias remotas. `master` del fork del motor,
  `pal-enhanced`, su gitlink `28057a1a` y la release siguen intactos.
  Se reutiliza la aceptación PAL-010; no hubo nueva compilación ni prueba gráfica.

### Incidencias independientes de PAL-010

- Skybox a 3×: discontinuidad visible, **MINOR / no bloqueante**; corrección
  futura. No altera la aceptación de la presentación regional.
- 2× con Frame Pacing OFF: un intervalo registró un present de 375,6 ms;
  observación puntual de regularidad temporal, sin causa atribuida ni defecto
  gráfico sostenido. La opción permanece disponible.
- 4×: un intervalo registró GPU busy alrededor del 94 % y hay caídas en escenas
  caóticas; limitación dependiente de carga, no regresión regional demostrada.
- PAL-011-AUDIO: arrastrar la ventana interrumpe host/EE, pierde VBLANK y deja
  unos 650–680 ms acumulados en la cola de audio con pacing ON/OFF. Diagnóstico
  concluido; recuperación aceptada manualmente en captura GS y movimiento de
  ventana. La aceptación cubre esos escenarios, no toda interrupción imaginable.

## Estados y contratos de las tareas

Estados permitidos: `DONE`, `TESTING`, `IN_PROGRESS`, `READY`, `RESEARCH`,
`BACKLOG`, `BLOCKED`, `REJECTED`. Un único estado por tarea.
`DONE` exige implementación y pruebas PAL de su alcance, no mera presencia USA.
Para PAL-010 significa integración experimental aceptada, no release final.
`READY` significa investigado para proponer ejecución, no autorizado.

Las tablas conservan ID, prioridad, estado, dependencias, aceptación y referencias.
Referencias `UP-*` y `EXT-*` apuntan al tracker; `E-*` al índice local de la matriz.
Las dependencias propuestas son puertas de evidencia, no nuevos compromisos estratégicos.

## F0 — Infraestructura y seguimiento

| ID | Prioridad | Estado | Trabajo / dependencias | Aceptación | Referencias y evidencia |
| --- | --- | --- | --- | --- | --- |
| PAL-001 | P0 | TESTING | Roadmap, tracker, matriz y lectura en AGENTS; pendiente revisión del mantenedor | Inventario coherente, trazable y actualización en tareas autorizadas | Esta documentación; UPSTREAM_PAL_MATRIX; E-GIT |
| PAL-001B | P0 | TESTING | Consolidación externa preparada; revisión documental pendiente, sin integración funcional | Fuentes/revisiones, límites y pruebas trazables; enlaces y estados consistentes | [Catálogo externo](EXTERNAL_ENGINE_RESEARCH.md); E-DOSSIER/E-SYMBOLS/E-REPLAY |
| PAL-002 | P0 | TESTING | Biblioteca GS; obtener primero PCSX2 Software PAL60 corto; después conducción, bloom, nubes, colisiones, menús; luego PAL50/USA | Estado inicial idéntico entre procesos, conversión real comprobada, diferencias visuales cuantificadas | E-REPLAY; no sustituye diagnóstico de presentación |
| PAL-003 | P2 | RESEARCH | Estudiar F12 multifotograma reversible; depende de límites de PAL-002 | Captura finita, memoria limitada, integridad/replay; sin save state completo | E-REPLAY; UP-015 |
| PAL-004 | P1 | RESEARCH | Unificar regresión GS, juego, HUD, cámara, audio, VU y benchmarks | Informe por revisión y vídeo que separe proceso, simulación, presentación y GPU | PAL-002/020; E-WIDE, E-GPU |

## F1 — GPU, coherencia y precisión

| ID | Prioridad | Estado | Trabajo / dependencias | Aceptación | Referencias y evidencia |
| --- | --- | --- | --- | --- | --- |
| PAL-010 | P0 | DONE | Integración regional C aceptada como base gráfica experimental; fallback 640×512 y pacing independiente | Compilación/test sintético/GS Replay corto y aceptación manual 2× ON/OFF, 3×/4× ON; no equivale a release | UP-001/002/006/007; expediente pal-regional-integration-20261021; EXE SHA-256 arriba |
| PAL-010A | P0 | TESTING | Investigación estática de símbolos concluida; utilidad causal aún no validada. 40 entradas, 13 parejas previas reexaminadas, cero nuevas | Conservar proof de nombres separado de identidad binaria; atribución real requiere PAL-010 | [ER-10](EXTERNAL_ENGINE_RESEARCH.md#er-10--burnout3-symbols-y-pal-010a); expediente pal-symbols-audit-20261020 |
| PAL-011 | P0 | RESEARCH | Exact lookup, stale, ownership, DisplayTarget, fallback, Use/Containing; depende de PAL-010 | Causa reproducible; no aceptar solapamiento como prueba de validez | UP-002/007; E-TRACE, E-DOSSIER |
| PAL-012 | P0 | RESEARCH | SetPresentAtBlankEnd, copia del framebuffer, VBLANK e input; medir antes de adaptar | PAL60/PAL50, menús/FMV/carrera y GPU Off/On; temporización correcta | UP-002/020; E-DOSSIER |
| PAL-013 | P0 | RESEARCH | Rango físico de los 560 KiB, 448/512 líneas PSMCT16S; depende de PAL-010 | Consumidor/región/bytes individuales, sin inferirlos del agregado | UP-001/004; E-COUNTERS |
| PAL-014 | P1 | RESEARCH | CSM2, TEXCLUT, CBP/CBW/COU/COV/CPSM/CSA/TEXA; medir incidencia y cobertura | Tests sintéticos y equivalencia de paleta/regiones; ruta GPU solo si justificada | UP-006; E-DOSSIER |
| PAL-015 | P1 | RESEARCH | Cielo/nubes, bloom/glow/fog, alfa, alias color/depth; conservar ORIGINAL | Replay y referencia gráfica, dependencias y coste por efecto; PAL50 explícito | UP-003/005; EXT-001/002; E-GRAPHICS |
| PAL-016 | P1 | RESEARCH | Transferencias USA 0.4/0.5.2/0.5.3; inventariar lo ya heredado; dirty pages y búsquedas | Un mecanismo por prueba, precisión y beneficio A/B medidos | UP-001/003/004; PAL-010/015 |
| PAL-016A | P1 | DONE | USA-001: ACCEPTED — Experimental optimization; performance gain not yet quantified. Índice de texturas por páginas GS conservado en commit local `2fcc03d` | Pruebas específicas anteriores reutilizadas y carrera aceptada por el usuario sin regresiones importantes, cadencia cercana a 60 FPS; beneficio sostenido sin cuantificar | Expediente privado pal-usa001-texture-page-index-20261022; base `720583c`; EXE `8e469c8f` (SHA-256 completo en expediente) |
| PAL-016B | P1 | DONE | USA-002: transferencias PSMCT16 agrupadas, adaptación aislada de `01b9843`; commit local `e966221` sobre USA-001, aceptada experimentalmente | Pruebas diferenciales y replay previos reutilizados; aceptación visual/manual del mantenedor. Microbenchmark favorable; beneficio sostenido PAL sin cuantificación concluyente | Expediente privado pal-usa002-gs16-20261022; incluido en la línea local PC Settings aceptada |
| PAL-017 | P3 | BACKLOG | Backend propio; estudiar referencias sin iniciar migración | Limitación estructural o mejora significativa demostrada, integración evaluada y autorización | EXT-005; PAL-002/010/016 |

## F2 — EE/VU, hilos y compilación

| ID | Prioridad | Estado | Trabajo / dependencias | Aceptación | Referencias y evidencia |
| --- | --- | --- | --- | --- | --- |
| PAL-020 | P1 | RESEARCH | Baseline juego completo, escenas/configuración iguales; no extrapolar replay | Frame time/CPU/GPU/audio/simulación y sincronización separados | E-GPU/COUNTERS; PAL-004 |
| PAL-021 | P1 | RESEARCH | VU Exact/Fast y mejoras 0.5.3; mantener modo actual | Equivalencia numérica/geometría/secuencias entre programas y velocidad reproducible | UP-010/013; E-PERF |
| PAL-022 | P1 | RESEARCH | clang-cl/LTO/PGO frente MSVC, sin mezclar ABI ni perfiles | Builds equivalentes, perfiles PAL identificados y validación funcional | UP-011/012; E-PERF |
| PAL-023 | P2 | RESEARCH | GPU Thread, colas/buffers, pipeline hints/caché | Off/On con mismas entradas, métricas y coherencia; separar coste de primer uso | UP-015; E-GPU |
| PAL-024 | P3 | BACKLOG | SSE4 sin AVX2, iGPU/dedicada | Matriz de compatibilidad y precisión, sin degradación silenciosa | UP-044; PAL-020/022 |

## F3 — Gráficos, controles y mods

| ID | Prioridad | Estado | Trabajo / dependencias | Aceptación | Referencias y evidencia |
| --- | --- | --- | --- | --- | --- |
| PAL-030 | P2 | TESTING | Off/FXAA/SMAA ya aceptados en experimento; integración pendiente | Selector real, persistencia y regresión con bloom/blur/HUD; separar build público | UP-031; E-AA |
| PAL-030-SSAA | P2 | RESEARCH | SSAA 2x, separado del selector ya probado | Distinguir supersampling, resolución interna, resolve y coste; validar PAL | UP-031; E-GRAPHICS |
| PAL-031 | P2 | TESTING | Preservar Hor+/HUD aceptados; F10, 32:9 y escenas no cubiertas siguen por investigar | No acumulación, anclajes y proporciones; menús/FMV y modos probados separados | UP-030; E-WIDE |
| PAL-032 | P2 | BACKLOG | Bloom, blur, sombras, LOD, fog/glow, partículas | Modo ORIGINAL conservado y cada opción verificada | UP-003/032; PAL-015 |
| PAL-033 | P2 | TESTING | HD333/cargador/caché DDS/PNG; no ampliar paquete ahora | Sustituciones reales, imagen y coste aceptables por región; A/B HD Off/On | UP-031; E-HD |
| PAL-033-MAP | P2 | RESEARCH | Correspondencias regionales e históricas; no convertir sin evidencia | Equivalencias separadas de candidatos, sin inferencia por prefijo | E-TEXTURES; PAL-033 |
| PAL-034 | P2 | BACKLOG | Reasignación, clásico/moderno, prompts, ratón, DualSense/vibración/gatillos; futuro volante/FFB | Dispositivos/menús/carrera y persistencia probados en PAL | UP-033/034; PAL-071 |
| PAL-035 | P3 | BACKLOG | km/h, bocina, tráfico, luces, mirror, Crash Breaker, developer menu | Cada función/dato PAL mapeado y probado; no usar direcciones SLUS | UP-035; E-MAPPING |

## F4 — Alta tasa de imágenes

| ID | Prioridad | Estado | Trabajo / dependencias | Aceptación | Referencias y evidencia |
| --- | --- | --- | --- | --- | --- |
| PAL-040 | P2 | RESEARCH | Simulación desacoplada de presentación; interpolar coche/tráfico/cámara sin acelerar lógica | Física/relojes/entrada correctos en PAL50/PAL60; baseline PAL-020 | UP-020; no copiar 60 pasos/s como supuesto PAL |
| PAL-041 | P2 | BACKLOG | 60/120/DISPLAY, 144/165/240 y reducción automática; contemplar PAL50 | Carrera, choque, Impact Time, menús/FMV/splitscreen, latencia y fallback | UP-020/021; PAL-040 |

## F5 — Audio y música

| ID | Prioridad | Estado | Trabajo / dependencias | Aceptación | Referencias y evidencia |
| --- | --- | --- | --- | --- | --- |
| PAL-050 | P2 | RESEARCH | MP3 locales EA TRAX, nombres y listas Race/Menu/Off sin alterar ISO | Reproducción/avance/persistencia y todos los idiomas afectados | UP-040; PAL-071 |
| PAL-051 | P4 | BACKLOG | Spotify u otros servicios según APIs/licencias; independiente de MP3 | Viabilidad y reglas acreditadas; no bloquear PAL-050 | Sin implementación revisada |
| PAL-052 | P2 | RESEARCH | Motor de coches, SPU2 y crujidos; comparar cambios concretos | Audio de vehículos, voces, música y FMV sin regresión | UP-041; E-PERF |
| PAL-011-AUDIO | P1 | DONE | Recuperación acotada del consumidor tras huecos ≥250 ms, conservando cola objetivo y crossfade; sin alterar simulación ni buffers WASAPI | Aceptación manual de recuperación en captura GS y movimiento de ventana sobre EXE 12772164; pruebas sintéticas/diferenciales previas reutilizadas. Alcance limitado a las interrupciones probadas | Motor local bfad725; expediente privado pal-f9-b-controls-audio-fullscreen-20261022 |

## F6 — Multijugador

| ID | Prioridad | Estado | Trabajo / dependencias | Aceptación | Referencias y evidencia |
| --- | --- | --- | --- | --- | --- |
| PAL-060 | P2 | RESEARCH | Splitscreen USA, HUD/GS/audio/mandos y pacing 30/60 PAL | Dos jugadores, ambos viewports y modos de vídeo verificados | UP-042; PAL-015/020/031 |
| PAL-061 | P3 | RESEARCH | IOP/inet/lobby y funciones PAL; LAN antes de VPN | Protocolo regional y red local probados; sin requisito de servidores EA | UP-043; E-MAPPING |

## F7 — Experiencia, localización y releases

| ID | Prioridad | Estado | Trabajo / dependencias | Aceptación | Referencias y evidencia |
| --- | --- | --- | --- | --- | --- |
| PAL-070 | P2 | BACKLOG | Preferencia español/60 Hz, omitir o mostrar pantallas; F9/PC Settings | Acceso a selección original conservado; no cambiar defaults ahora | PAL-071/072 |
| PAL-070A | P2 | DONE | F9-A aceptado manualmente: GPU Thread, preferencia tras reinicio, bloqueos CLI y FPS/F1 | Aceptación del mantenedor sobre EXE SHA-256 304821eecce85b986cca8bd72e9a5cf1f89c3a18ca4aa189b49b915ee64117d1; pruebas previas reutilizadas | Base PAL d370c12 + motor USA-002 e966221; expediente privado pal-f9-a-20261022 |
| PAL-070B | P2 | DONE | F9-B aceptado manualmente: OFF/FXAA/SMAA en caliente, warm-up, overlay y separación L3+R3/F9 de captura F12; configuración persistente y CLI conservados | EXE SHA-256 12772164da2b71557d61e3097ec2fc18c946020cc505a7993886181f7870b2e4; aceptación del mantenedor y pruebas focalizadas anteriores reutilizadas, sin repetirlas | Motor bfad725; PAL funcional 0d08394; ramas locales integration/pc-settings-accepted-engine e integration/pc-settings-accepted; sin publicación |
| PAL-070B-FS | P2 | DONE | Fullscreen predeterminado solo para perfiles PAL nuevos, incluido arranque sin argumentos; ventana guardada y CLI prevalecen; mismo modo fullscreen del motor | Aceptación manual del mantenedor y pruebas nativas previas de estado inicial, cambio F9, persistencia y CLI | PAL-070B; perfil diagnóstico en ventana conservado |
| PAL-070B-VU | P2 | RESEARCH | F9-B / VU Fast: backend y bloques PAL disponibles; no expuesto en F9 | Comparación Exact/Fast existente pasa excluyendo MAC/STATUS por diseño; falta validar transiciones entre programas que lean flags anteriores. Exact conservado, sin regeneración | PAL-070B; evaluación acotada, sin nueva investigación ni adopción general |
| PAL-071 | P1 | RESEARCH | Todos los idiomas SLES, acentos, HUD, menús y textos nuevos | Cobertura por idioma documentada; español no certifica el resto | E-STATUS; UP-033/040 |
| PAL-072 | P1 | RESEARCH | LocalAppData, perfiles, backups y actualizaciones | No pérdida de datos; instalaciones limpias y migraciones comprobadas por versión | E-STATUS; alpha inicial ya probada |
| PAL-073 | P1 | BACKLOG | Carrera/Road Rage/Crash/World Tour/garaje/pausa/carga/FMV | Matriz PAL50/60, 4:3/wide, GPU Off/On, audio y persistencia | PAL-004/020/071/072 |
| PAL-074 | P2 | BLOCKED | v0.2.0-alpha: candidata local integrada preservando ambas historias; motor bfad7255, versión/README/notas y ZIP x64 preparados | Inicio/selector/F9/AA aceptados sobre el nuevo ZIP; reinicio corto y conservación de INI pasan. Falta revisión documentada de distribución, publicación del motor y autorización final de push/tag/release | PAL-010/016A/016B/070A/070B completados; VU Fast y opciones PAL avanzadas pendientes; alpha anterior intacta |

## Orden de trabajo conservado

1. PAL-010 cerrado en su alcance experimental; seleccionar una mejora USA acotada
   usando la matriz y la evidencia existente, sin actualizar el motor en bloque.
2. PAL-002 sigue en validación como infraestructura auxiliar; no diagnostica el fallback.
3. PAL-011/012/013: mantener su alcance separado; no reabrir el diagnóstico de
   fallbacks de PAL-010 sin evidencia nueva o regresión reproducible.
4. PAL-014/015/016: escoger un mecanismo respaldado por esas mediciones.
5. Resto: prioridades originales conservadas; no adoptar mejoras por ser más recientes.

Las demás prioridades no cambian. Las incidencias independientes anteriores no
reabren el diagnóstico de fallbacks ni bloquean el siguiente ciclo.

## Cierre de tareas y relación con upstream

Antes: leer AGENTS, roadmap, tracker y evidencias; declarar ID, estado, alcance
mínimo y pruebas. Después: registrar fecha, revisiones exactas, pruebas reales,
limitaciones PAL50/60, resultados y bloqueos; actualizar tracker solo si corresponde.
Un resultado no satisfactorio no se etiqueta DONE. Las optimizaciones exigen A/B
y precisión. No actualizar globalmente el motor para adoptar un único cambio.

Las funciones existentes en motor, sus activaciones por juego y la validación PAL
son tres comprobaciones distintas. Registrar ADOPT/ADAPT/DEFER/REJECT como decisión
explicada, sin inferir autorización de integración. No borrar decisiones históricas.
