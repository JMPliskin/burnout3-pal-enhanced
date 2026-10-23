# Planning changelog

## 2026-10-23 - Consolidate documentation on the published README baseline

- Reconcile the handoff against PAL a2e524e, preserving published installation/
  licensing changes and engine bfad7255. Retain roadmap 0.1.19 and task states.
- Repair obsolete branch URLs with permanent SHAs; retain historical provenance.
- Validate document links, formatting, roadmap preservation and the complete diff;
  no builds, gameplay or new functional claims.


## 2026-10-23 - Final native-menu and PAL-070 handoff refinement

- Roadmap schema 0.1.18 -> 0.1.19; no game-version or task-state changes.
- Reused the preceding read-only source/ELF review: PAL has five original main
  entries; target seven while retaining ONLINE and Multiplayer. Recorded external
  arrays of at least 56 entry + 28 help bytes, alignment, lifetime, six-row USA
  layout limits and callback requirements. No USA ONLINE donor memory reuse.
- Required selected-PAL-language UTF-16 for new native labels/dialogues and a
  separate pause-menu Quit to Desktop with confirmation and save-safe shutdown.
  All native additions remain unimplemented.
- Recorded PAL language init 0x00437740 and selection 0x004374C0; guest language=3
  selects Spanish resources/internal ID 5. First milestone is persistent language
  preference with original selection available; video preferences/skipping later.
  Host argument transport and full selector/loading contracts remain unvalidated.
- Identified the clean 2aac9350 / bfad7255 checkout from the previous baseline
  review. Added only missing context-efficiency instructions; existing incremental
  policy retained. No repeated audits, Git commands, builds, gameplay or commits.
- Only existing root documents updated. Other worktrees and the earlier
  planning-sync.patch remain untouched; that patch predates these refinements.

## 2026-10-23 - Session handoff and roadmap consolidation

- Roadmap schema 0.1.17 -> 0.1.18; game version remains 0.2.0-alpha. Current
  technical documentation and instructions use English. Original historical
  entries below are retained verbatim for provenance, not as current instructions.
- Recorded verified 22 October source publication: engine bfad7255, PAL main
  2aac9350, annotated PAL tag 830b9cb3. A fresh HTTPS recursive clone resolved the
  exact fork gitlink. No GitHub Release or binary upload was performed by Codex;
  later maintainer activity is not checked here. Binary-rights review stays separate.
- Corrected the current baseline versus the old main checkout 13f4295 and stale
  tracking ref e400b619. No branches/submodules were changed to hide that mismatch.
- Added session handoff and architectural evidence, boot, regional-difference,
  validation/build and current-status entry points. Preserved exact incremental
  policy and translated durable instructions without removing existing safeguards.
- Preserved completed PAL-010, USA-001/002, camera/HUD, F9-A/B, controls, bounded
  audio recovery and fullscreen milestones. Closed duplicate pending AA/widescreen
  wording; kept broader compatibility and VU Fast gaps explicit. 0x00242AC8 is a
  recovered memory-card worker switch case, not an unresolved locale initializer.
- Added scoped future PAL startup preferences, standalone soundtrack editing,
  exploratory Spotify, original texture extraction/catalog/mapping, staged vehicle
  import hypotheses and separate native main/pause-menu PC Settings/Quit phases.
  Existing task IDs are retained; no feature implementation or new investigation.
- Source/report inspection and documentation checks only. No build, game, replay,
  benchmark, EE/VU regeneration, staging, commit, push or release operation.
  Backups and local check evidence stay in the exclusive ignored session-handoff build.
- Root AGENTS's preexisting incremental policy and unrelated user files are preserved.
  Root roadmap/changelog are consolidated. Windows denied writes to the planning
  worktree; its files and unrelated edits remain intact. A synchronization patch
  is prepared in the ignored handoff build; no permissions were changed.

## Retained historical entries through 22 October 2026

The dated records below preserve their original wording and language. Later
acceptance/publication above supersedes old pending/next-action statements; it
does not rewrite their test results or authorize repeating their investigations.

## 2026-10-22 — Preparación local de v0.2.0-alpha

- Roadmap 0.1.16 → 0.1.17 (esquema documental; versión del juego 0.2.0-alpha).
  Candidata aislada `release/v0.2.0-alpha-candidate`: merge normal de la integración
  aceptada con `pal-enhanced` remoto e400b619, preservando la plantilla de incidencias.
  Motor fijado en bfad7255; las ramas principales locales y remotas no se publican.
- README, notas de release y guía del ZIP actualizados con funciones aceptadas,
  requisitos, persistencia y limitaciones. Se conserva el roadmap maestro completo
  y el historial previo de este changelog al incorporarlos a la candidata; no se
  descartan cambios anteriores del worktree documental.
- Compilación de release con versión Windows/log/título 0.2.0-alpha, sin trazas
  temporales ni símbolos de diagnóstico en el enlace. Reutilización verificada
  de objetos EE/VU inmutables; no regeneración ni reconstrucción limpia ficticia.
- ZIP: `Burnout3-PAL-Enhanced-0.2.0-alpha-windows-x64.zip`, sin ISO, assets extraídos,
  tarjetas, INI personal, logs ni cachés. Notices/licencias, arquitectura y hashes
  comprobados. Inicio/selector y F9/AA del EXE extraído aceptados por el mantenedor;
  SMAA conservado y segundo arranque hasta frame 180 con INI intacto.
- EXE SHA-256 `a7122e8718fb2459a0bd231784d8fce8930485eb43985cbe2814c0d60a2dc341`.
  ZIP SHA-256 `bf8cdc0c2c8836f63bb4e6642bf1883a415b1d541d168d2ea90b69c6558b9a71`.
- PAL-074 sigue BLOCKED para publicación: el mantenedor confirma que no dispone
  de autorización/revisión documentada para distribuir el programa reconstruido.
  Se completa toda la preparación local, incluido tag local, sin push ni subida.
  La publicación manual en GitHub tampoco elimina esa revisión ni la autorización.
- Sin nuevas campañas PAL-010, GS16, VU, audio o benchmarks. Las incidencias y
  funciones pendientes conservan su alcance anterior.


## 2026-10-22 — Consolidación local de F9-A/F9-B aceptados

- Roadmap 0.1.15 → 0.1.16. USA-001, USA-002 y F9-A aceptados; no se atribuyen
  porcentajes de mejora de rendimiento que no estén demostrados.
- F9-B aceptado manualmente sobre EXE SHA-256
  `12772164da2b71557d61e3097ec2fc18c946020cc505a7993886181f7870b2e4`:
  GPU Thread con reinicio y persistencia, CLI, FPS/F1/F9, AA OFF/FXAA/SMAA en
  caliente, pipelines preparados, overlay y controles L3+R3/F9 separados de F12.
- Recuperación de audio aceptada en las situaciones probadas de captura GS y
  movimiento de ventana; no certifica cualquier interrupción. Fullscreen inicial
  en perfiles nuevos aceptado, respetando preferencias previas y CLI.
- Motor local `bfad7255f17f7789a8bbb0a59a6e3a003535c548`, rama
  `integration/pc-settings-accepted-engine`, descendiente de USA-002 `e966221`.
  Integración PAL funcional `0d0839406aa79e8bce77b86ec3e413ca87642029`, rama
  `integration/pc-settings-accepted`, con gitlink exacto al motor y fork JMPliskin.
- Se excluyen trazas temporales de transición, VU Fast, binarios y diagnósticos.
  Se conservan tests pertinentes y snapshot de cola de solo lectura; sus contadores
  de dispositivo usan el flag de audio existente. No se altera el flujo funcional
  aceptado. Validación limitada a ascendencia, diffs, índice e integridad;
  sin recompilar ni repetir pruebas aceptadas.
- VU Fast permanece pendiente. Skybox 3× sigue MINOR / no bloqueante. Sin push,
  merge, rebase, cambios de release o actualización de `pal-enhanced`.
- El roadmap y el historial previo del changelog siguen sin una base documental
  versionada en el worktree de planificación. Esta consolidación versiona solo
  esta entrada nueva; el contenido previo permanece íntegro y sin stagear allí.


## 2026-10-22 — F9-B: controles, recuperación experimental de audio y fullscreen

- Roadmap 0.1.14 → 0.1.15. La reproducción del mantenedor y las trazas confirman
  la captura GS accidental con L3+R3 (7,56/6,71 s); el menú por teclado no muestra
  ese defecto. Se retira solo el binding de captura del mando, manteniendo F12,
  flanco del menú y AA inmediato. `PAL-070B` sigue `TESTING`.
- `PAL-011-AUDIO` pasa a `TESTING`: capturas y arrastre dejan el mismo patrón de
  underrun y cola excesiva. Recuperación experimental acotada tras huecos de
  producción, aplicada por el consumidor sin vaciar WASAPI ni alterar simulación.
  3.000 operaciones normales idénticas a la base; pausa sintética con WASAPI real
  recupera la cola a 30 ms a los 30 ms y 42 ms al final. Falta escucha/reproducción
  real de captura y arrastre; no se declara corregida la incidencia.
- `PAL-070B-FS`, `TESTING`: PAL fullscreen por defecto en perfiles nuevos; respeta
  ventana guardada y CLI. 29 comprobaciones de menú, 134 nativas SDL/Vulkan y un
  arranque limitado pasan. EXE `12772164da2b71557d61e3097ec2fc18c946020cc505a7993886181f7870b2e4`.
- Expediente privado `pal-f9-b-controls-audio-fullscreen-20261022`; fuentes y
  configuraciones aisladas, sin commits, publicación, gitlinks ni EE/VU regenerados.

## 2026-10-22 — F9-B: cambio AA en caliente y diagnóstico acotado

- Roadmap 0.1.13 → 0.1.14. `PAL-070B` sigue `TESTING`: se parte del F9-B
  original, conservando OFF/FXAA/SMAA en caliente. La alternativa con reinicio
  permanece aislada; no es el comportamiento de esta nueva compilación.
- Las funciones relevantes de USA son iguales a la base PAL. Una medición
  nativa localiza ~40/51 ms de creación inicial de pipelines, pero no reproduce
  las pausas históricas de ~2 s. Se prepara ese trabajo al arrancar, se conserva
  la reutilización y las esperas al sustituir recursos usados, y se reutiliza la
  corrección del mutex del overlay. 39 comprobaciones y pruebas Vulkan dirigidas
  pasan, incluida comprobación limitada con wrapper GPU Thread ON.
- EXE `267450fcf5a55ee1b44d595d53d7689e9764b00fe03f19cf408dce4c8637ca9c`;
  observación optativa de transiciones/cola de audio con reloj común. Sin cambios
  al algoritmo de audio; PAL-011-AUDIO abierto. Pendiente una ronda manual marcada,
  sin atribuir cada pausa antigua ni declarar resuelta la desincronización.

## 2026-10-22 — F9-B: AA diferido y bloqueo del overlay acotado

- Roadmap 0.1.12 → 0.1.13. El mantenedor confirma diferencias visuales y
  persistencia, pero detecta pausas de hasta 2,6 s y desfase de audio: F9-B
  conserva `TESTING`, sin aceptación de sus transiciones.
- Corrección aislada: AA guarda la preferencia para el siguiente arranque,
  mostrando método efectivo y `Restart required`; el mutex del overlay ya no
  cubre llamadas Vulkan. Tres unidades recompiladas, 31 comprobaciones del menú
  y tres procesos Vulkan pasan, incluida espera de driver controlada. EXE
  `9d43b7e8afa2e13e4d17549047c756fb7a7248152185798d77f68941cf204bb5`.
- Sin atribución individual de las pausas antiguas ni cambios al audio:
  se reutiliza PAL-011-AUDIO. Pendiente prueba manual de 2–3 minutos del nuevo
  EXE; sin commits, push, cambios de gitlink ni nuevas funciones.

## 2026-10-22 — F9-A aceptado; F9-B con AA y evaluación VU acotada

- Roadmap 0.1.11 → 0.1.12. `PAL-070A`, `DONE`: el mantenedor confirma GPU
  Thread, persistencia, bloqueos CLI y FPS/F1 del EXE F9-A; no se repite su batería.
- `PAL-070B`, `TESTING`: se adapta únicamente la fila OFF/FXAA/SMAA del
  experimento existente a F9-A. Compilación incremental de pc_menu, 12 casos
  Vulkan y bloqueo CLI pasan; arranque hasta frame 180 con limpieza y sin timeout.
  EXE independiente SHA-256 `7d4a011581dcdecffe1309ec7acddeb9567c5b7d867f6698e5554381ef996deb`;
  pendiente aceptación manual breve. Sin shaders ni predicado regional nuevos.
- `PAL-070B-VU`, `RESEARCH`: comparación Exact/Fast existente superada con
  bloques PAL, excluyendo MAC/STATUS por contrato. Falta acreditar las transiciones
  entre programas; Fast no se ofrece en F9. Exact permanece activo, sin regenerar
  EE/VU, commits, push, cambios de gitlink ni modificación del experimento AA original.

## 2026-10-22 — F9-A: opciones de PC y persistencia

- Roadmap 0.1.10 → 0.1.11. `PAL-070A`, `TESTING`: GPU Thread muestra estado
  efectivo y cambio pendiente de reinicio; FPS conserva F1. Los valores CLI/env
  quedan identificados y fijados durante la ejecución sin contaminar el INI.
  Guardado con sustitución de archivo y recuperación ante fallo.
- Sobre PAL `d370c12` y USA-002 `e966221`, compilación incremental de cuatro
  unidades, 35 comprobaciones, reapertura y pruebas cortas con ventana/renderer
  reales OFF/ON superadas. EXE y perfil separados; aceptación manual del menú
  pendiente. Sin nuevos benchmarks, carreras, GS Replay, commits ni gitlinks.
- `PAL-070B`, `RESEARCH`: antialiasing y decisión sobre VU Fast quedan para la
  siguiente fase. PAL-010 y las incidencias independientes conservan su estado.

## 2026-10-22 — USA-001 aceptada y USA-002 preparada

- Roadmap 0.1.9 → 0.1.10. `PAL-016A / USA-001` pasa a `DONE` en su alcance
  experimental: el usuario acepta la carrera sin regresiones importantes y
  cadencia próxima a 60 FPS; beneficio de rendimiento todavía sin cuantificar.
  Pruebas anteriores reutilizadas. Commit local `2fcc03da44700346f459b0f0d676ca0b68de657c`.
- Nueva `PAL-016B / USA-002`, `TESTING`: PSMCT16 agrupado con certificación
  del layout e ISA condicionada; otros formatos y compilaciones base conservan
  la ruta escalar. Commit local `e966221255dec0d846a7059e1b5d02d26fb46bb6`, hijo de USA-001.
  1.504 casos por variante, buffers con páginas de guarda, build incremental
  y replay A/B idéntico en imágenes/memoria. Mejora sintética medida; una pareja
  de replay no acredita velocidad en carrera. EXE separado y BAT A/B preparados;
  pendiente una única carrera corta. Sin push, cambios de gitlink o releases.

## 2026-10-22 — PAL-016A / USA-001: índice de texturas por páginas GS

- Roadmap 0.1.8 → 0.1.9. Nueva subtarea `PAL-016A`, `TESTING`: adaptación
  aislada de `072299a` sobre motor `720583c` y PAL `d370c12`. Retira texturas
  invalidadas del índice y las reinserta al validarlas, incluidas las rutas
  locales de dibujo GPU y reemplazos. Predicado y presentación PAL-010 intactos.
- Compilación incremental y tests focalizados pasan. Cuatro ejecuciones cortas
  de una captura PAL a 2×/GPU Thread OFF producen imágenes y memoria GS
  idénticas; tiempos 12,05/11,65 ms base y 11,36/11,64 ms experimental por pasada.
  Diferencia pequeña frente a la dispersión: mejora en carreras no acreditada.
  EXE separado y BAT A/B preparados; aceptación manual y decisión de adopción
  pendientes. Sin commits, push ni cambios a ramas aceptadas o releases.

## 2026-10-22 — Publicación de las ramas PAL-010

- Roadmap 0.1.7 → 0.1.8. Publicada primero la rama
  [experimental/pal010-regional-engine](https://github.com/JMPliskin/ps2-port-engine/tree/experimental/pal010-regional-engine)
  en `720583cfa885b3bbc25a14302d0b4e0a779433bf`; después,
  [integration/pal010-regional-accepted](https://github.com/JMPliskin/burnout3-pal-enhanced/tree/d370c12d2451322136cb175da10c77878871b795)
  en `d370c12d2451322136cb175da10c77878871b795`.
- `d370c12` modifica solo `.gitmodules` para que una clonación PAL obtenga el
  submódulo del fork; el gitlink sigue en `720583c`. Git verificó ambas ramas
  remotas y confirmó sin cambios `master` del motor y `pal-enhanced` del juego.
  No se publicaron tags ni releases, ni se repitieron compilaciones o pruebas gráficas.

## 2026-10-22 — Reconciliación de la base del motor PAL-010

- Roadmap 0.1.6 → 0.1.7. `28057a1a` es ancestro de `d7e6e023`: 35 commits,
  un merge y 20 commits en primer padre. La base aceptada ya contenía los 35;
  no se importaron cambios posteriores ni se reabrieron los fallbacks.
- Se crearon únicamente commits locales: motor `720583cfa885b3bbc25a14302d0b4e0a779433bf`
  en `experimental/pal010-regional-engine` y PAL `43a28f8e0f3b208bf596dfd9d25f780eb2124a53`
  en `integration/pal010-regional-accepted`. El segundo fija el gitlink del primero.
  El gitlink de `pal-enhanced` sigue en `28057a1a`.
- El fork propio del motor no está configurado ni accesible con la referencia
  examinada. Publicar exige primero un remoto accesible que contenga el commit
  del motor; no hubo push, merge, rebase, actualización de la release ni nuevas
  pruebas de renderer. Se reutilizaron la compilación y aceptación PAL-010.

## 2026-10-22 — Cierre de PAL-010 por aceptación manual

- Roadmap 0.1.5 → 0.1.6. PAL-010 pasa de `TESTING` a `DONE` **solo para la
  integración experimental**. El usuario aceptó el EXE consolidado SHA-256
  `612719b36ad1103d71342b4d01b4da10a0d64640007f99115cbfd9beadf6940b`
  a 2× con pacing ON/OFF y a 3×/4× con pacing ON. Las cuatro sesiones privadas
  terminaron con código 0. Siguen vigentes el test sintético, la compilación
  incremental y GS Replay corto, cada uno con su límite de alcance.
- La solución C evita el fallback causado por stale global cuando la región
  visible es válida; conserva las comprobaciones de coherencia y el fallback
  640×512. No depende de Frame Pacing. Es base gráfica experimental, no release.
- Incidencias separadas: skybox 3× **MINOR/no bloqueante**; present puntual de
  375,6 ms a 2× pacing OFF, sin causa atribuida; GPU busy ~94 % en un intervalo
  4× de carga alta; PAL-011-AUDIO diagnosticado y sin corrección. Ninguna reabre
  la investigación de fallbacks.
- Se revisó la preparación Git del motor y del puerto PAL. No se hizo staging,
  commit, push, merge, rebase ni cambio del gitlink. Las mejoras USA se tratarán
  por mecanismos acotados y evidencia incremental.

## 2026-10-22 — PAL-010: integración regional experimental

- Roadmap 0.1.4 → 0.1.5. Diagnóstico de fallbacks concluido; C regional
  extraído en un worktree aislado sobre motor `d7e6e023`, sin trazas A/B/C ni
  cambios al gitlink o ejecutables anteriores. Nuevo EXE enlazado con objetos
  limpios reutilizados; predicado sintético y GS Replay corto pasan. La
  aceptación visual de ese EXE queda pendiente, por lo que PAL-010 es `TESTING`.
- Frame Pacing ON/OFF y escalas 2×/3×/4× proceden de evidencia experimental
  anterior. El nuevo EXE requiere la ronda manual breve. El selector 640×512
  mantiene el fallback original; skybox 3× y saturación 4× siguen pendientes.
- PAL-011-AUDIO pasa a `READY`: causa temporal diagnosticada en expediente
  separado; corrección funcional pendiente. Se añadió al `AGENTS.md` principal
  la política permanente de validación incremental y reutilización de evidencia.
- Sin commit, push, merge, rebase, actualización upstream ni cambio de release.

## 2026-10-21 — PAL-011-AUDIO: arrastre de ventana

- Roadmap 0.1.3 → 0.1.4. Se registra como `TESTING` el defecto de audio reproducido
  por el mantenedor tras arrastrar la ventana, con prioridad P1 para diagnóstico
  acotado. La maximización sin arrastre no lo reprodujo; los logs conservados
  muestran pausas de presentación y VBLANK perdidos, pero carecen de ocupación
  temporal de la cola de audio.
- El ID `PAL-011` ya designaba la investigación de búsqueda y validez de targets
  GPU. Para no alterar IDs históricos, la solicitud nueva se conserva como
  `PAL-011-AUDIO`; no se cierra ni renumera PAL-011 ni PAL-010.
- El expediente privado `pal-window-drag-20261021` contiene instrumentación
  opt-in, EXE aislado y pruebas sintéticas. No demuestra aún la causa del desfase
  ni habilita una corrección de audio. No se incorporó código funcional al roadmap
  ni se cambiaron otras prioridades o estados.

## 2026-10-20 — PAL-001B: investigación externa consolidada

- Creado EXTERNAL_ENGINE_RESEARCH.md a partir del dossier GPU, PAL-010A y GS Replay,
  sin reabrir auditorías generales ni importar sus archivos originales al Git.
  Incluye PCSX2, Tenkaichi3Decomp, BT3-Recomp, PS2 Web Recomp, paraLLEl-GS,
  rac1-native-game, burnout3-symbols y Takedawn; enlaza además CLUT/blank-end/replay.
- Cada mecanismo distingue problema, implementación externa, arquitectura,
  aplicabilidad, evidencia, pruebas, riesgos e IDs PAL. Las fuentes históricas
  main/master sin SHA se identifican como tales: no se inventó una revisión.
- Roadmap pasa de 0.1.1 a 0.1.2. Se añaden PAL-001B y PAL-010A como subtareas
  TESTING, prioridad P0 heredada, sin renumerar ni modificar tareas existentes.
  En PAL-010A, el análisis estático está concluido; TESTING conserva la utilidad
  causal pendiente. No significa reabrir las comparaciones binarias ya realizadas.
- EXT-004 incorpora el resultado de símbolos y su revisión fijada. Se conservan
  EXT-001–005 y se añaden EXT-006–008 para BT3-Recomp, rac1 y Takedawn.
- No se cierra la hipótesis de fallback ni se atribuyen los 560 KiB a un framebuffer
  por tamaño. Solapamiento no implica vigencia; CSM2 no queda identificado como
  causa; GS Replay no reproduce PresentGpuPicture; otro renderer no garantiza
  eliminar readbacks de presentación. No se autoriza saltar sincronizaciones.
- PAL-010 sigue READY y es el próximo trabajo funcional, pendiente de autorización.
  No hay cambio estratégico de prioridades, implementación ni actualización del motor.

Alcance de esta sesión: exclusivamente este catálogo, ROADMAP.md,
UPSTREAM_TRACKER.md y este changelog. Los cambios documentales anteriores se
conservan pendientes; sin staging, commit, push, compilación o ejecución del juego.
Verificación: enlaces relativos y anclas correctos; 18 enlaces externos del catálogo
respondieron HTTP 200. Es una comprobación de acceso, no una nueva certificación
de los árboles main/master. `git diff --check` y la comprobación equivalente del
catálogo nuevo pasaron; no se copiaron recursos ni informes privados al repositorio.

## 2026-10-20 — Revisión documental inicial

Base: textos del mantenedor ROADMAP 0.1 y UPSTREAM_TRACKER del 20/10/2026.
Documentación normalizada y resumida, conservando todos los IDs, prioridades,
referencias históricas y objetivos pendientes. No es una autorización funcional.

Worktree documental nuevo, rama `docs/pal-roadmap-20261020`, basado en
`434a86c9a4bb8e9392fea5dea99e6967770aec18`. Se eligió la integración publicada
para conservar la documentación del widescreen validado; no se fusionó con la
rama principal. El ROADMAP de esa base era un archivo vacío ya versionado:
esta tarea lo completa. No había tracker permanente en esa base.

| Corrección | Antes / alcance original | Resultado y evidencia |
| --- | --- | --- |
| Identidades completas | Commits USA/engine abreviados y motores anunciados | Se resolvieron los nueve tags y sus gitlinks por API. 0.6 = 444b541b9919055af3ee97787e84de7c2ceed98f, engine 721c73cd00e21aa87349ad5ae092277dcd3b69fa. No se confunde con master |
| Publicado frente a integrado | Podía leerse widescreen como parte de pal-enhanced | ls-remote confirma principal 3ad44e4 e integración 434a86c separadas; alpha permanece intacta |
| PAL-001 | READY, documentos aún no permanentes | TESTING: documentación preparada sin commit, pendiente revisión. No DONE por mero texto creado |
| PAL-030 y PAL-033 | Estados compuestos TESTING/RESEARCH | Estado único TESTING para experimentos; PAL-030-SSAA y PAL-033-MAP RESEARCH preservan lo no validado. Misma prioridad P2; no se elimina alcance |
| UP-001 | Integración/efecto de transferencias USA 0.4 no verificados | Presencia base confirmada: 645bbf39 y a172049 son ancestros de d7. Efecto/ganancia PAL sigue sin prueba aislada |
| UP-010 | VU optimizado PAL por verificar | Separar VU compilado, mejoras anteriores ya heredadas, Fast disponible pero no activado por defecto y cambios 0.5.3 ausentes. No modificar modo ni regenerar programas |
| UP-015 | Pipeline hints/shaders PAL genéricamente no verificados | Cache/especialización/API ya en d7; lo ausente es lista/registro de hints específicos PAL. Auditoría E-PERF |
| UP-041 y UP-044 | Audio de motor/SSE4 pendientes | Base de audio e infraestructura SSE4 heredadas; mejoras de audio posteriores y soporte del player SSE4 no demostrados. El EXE PAL medido exige AVX2 |
| Fog/glow | Una función anunciada como renderizado nativo | Separar rutas nuevas del motor Shuffle/ZIntoColour, sus condiciones/dependencias, de graphics.cpp y direcciones del juego. No validar PAL por anuncio USA |
| Presentación/readbacks | Riesgo de asumir que cambios recientes resuelven el problema | 721 conserva exact lookup/stale/fallback; blank-end cambia timing. No prueba consumidor causal ni lectura redundante |
| PR #1 | Estado histórico experimental/pending | API consultada: abierta/no merged, head 0b12cd1488ed86050260315774237414a11cd911. Partes ya integradas no se clasifican por el estado global de la PR |
| GS Replay | Infraestructura validada en 12 procesos y 13 pruebas | Se conserva el recuento con su alcance: dos muestras cortas, independencia por proceso, diferencias software/GPU y falta de dump PCSX2 Software real. Decisión B; no fallback de presentación |

### Decisiones que permanecen

- PAL-010 es el próximo trabajo funcional. No ha aparecido evidencia suficiente
  para cambiar el orden ni se pide adoptar una optimización ahora.
- No cambiar las prioridades originales P0–P4. Las subdivisiones documentales
  evitan estados compuestos, no añaden autorización o compromisos de calendario.
- Mantener PAL50 como cobertura explícita pendiente cuando no se ha probado.
- Conservar hipótesis/contadores/historial GPU y replay: ninguna investigación
  se cierra porque upstream tenga una función de nombre parecido.
- ADOPT/ADAPT requieren decisión del mantenedor; las entradas quedan DEFER
  para implementación, disponibles para investigación posterior autorizada.

### Comprobaciones de esta revisión

Lecturas de Git local, ls-remote y HTTP/API fijada al tag. No fetch ni actualización
de submódulo. Fuentes remotas consultadas en caché privada, no incorporadas al Git.
No se compilaron ni ejecutaron juego/tests funcionales: solo se verifican textos,
referencias, estados y diff. Los resultados históricos se atribuyen a sus informes.

La documentación preparada se revisará antes de cualquier commit/push. No modifica
Release, código, hooks, tarjetas, ISO, EE/VU ni experimentos anteriores.
## 2026-10-21 — PAL-010 Fase 6, actualización experimental

`ROADMAP.md` pasa de 0.1.2 a 0.1.3. PAL-010 cambia de READY a IN_PROGRESS
porque existen instrumentación causal, sesiones reales y el experimento C en
curso; no se declara DONE. Se corrige la afirmación obsoleta de que los readbacks
grandes carecen totalmente de consumidor atribuido: las trazas PAL60 muestran
su relación con el fallback observado, pero no certifican aún la equivalencia
gráfica de C ni una corrección general. Se añaden únicamente los resultados
privados 2×/3×/4×, el tratamiento 640×512 del selector, la prueba ON/OFF aún
pendiente de validación manual y las incidencias separadas de audio/skybox.
Prioridades e IDs existentes no cambian; GPU Thread, VU Fast, AA, mejoras USA
0.5.2/0.5.3 y 120 FPS siguen con el alcance anterior. Evidencia local:
expedientes PAL-010 `abc-scale-20261021`, `pal-visual-timing-20261021` y
`phase6-20261021` (el último en preparación). No se integró código ni se hizo
commit o push en este worktree.
