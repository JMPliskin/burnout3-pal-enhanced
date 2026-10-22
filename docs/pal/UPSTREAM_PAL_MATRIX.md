# USA 0.6.0 frente a PAL — matriz de implementación y evidencia

Revisión: 2026-10-20. Auditoría documental y estática selectiva; sin compilación,
ejecución del juego, cambios del motor, fetch o integración. No constituye una
prueba de equivalencia ni una recomendación de actualización global.

## Identidades y método reproducible

- USA juego: tag `v0.6.0-alpha`, commit `444b541b9919055af3ee97787e84de7c2ceed98f`.
- USA engine gitlink: `721c73cd00e21aa87349ad5ae092277dcd3b69fa`.
- PAL desarrollo publicado: `434a86c9a4bb8e9392fea5dea99e6967770aec18`.
- PAL motor base: `d7e6e023f261d9f95ad097fcd063ec95e40ca465`.
- Rama principal del fork: `3ad44e482c201097333dc79a452bfb6b8b4bc648` al consultar.
- Experimento AA/HD: `8c1fa26f803c776d16b0b034043a77bc58a251a3` más cambios locales;
  las mediciones de ese árbol no describen automáticamente la integración limpia.

1. Consultar HEAD remotos con `git ls-remote`; leer gitlinks con `git ls-tree`.
2. Comparar objetos locales mediante `git show`, `git grep REV -- PATH`,
   `git log` y `git merge-base --is-ancestor`, sin checkout ni escritura en fuentes.
3. Para 0.6, no disponible como objeto local, resolver tag y árbol mediante API:
   `repos/danielgomesvieira2000/burnout3-pc/git/ref/tags/v0.6.0-alpha` y
   `git/trees/<SHA>`. Leer archivos raw fijados al SHA, no a master.
4. Consultar el compare API del motor `d7...721`, y commits individuales para
   verificar archivos/patches. La API devuelve **42 commits ahead, 0 behind**.
   No es un fetch ni introduce esos objetos en los repositorios locales.
5. Contrastar activación PAL en overrides/CMake y auditorías, no solo símbolos del motor.
6. Conservar el material raw/API únicamente en diagnóstico privado; publicar aquí
   referencias y conclusiones, no código original, dumps, binarios ni logs.

Esta revisión no verifica el HEAD actual de master como baseline: compara el
tag solicitado. No ha probado los ejecutables USA 0.6 ni nuevas funciones PAL.

## Matriz principal

**Clasificaciones:** validado (solo alcance indicado); experimental; presente pero
desactivado; presente sin validación PAL; ausente en baseline/adaptación. La
ausencia de un hook del port no demuestra que el juego PS2 original carezca de la
función. Las referencias UP enlazan al [tracker](UPSTREAM_TRACKER.md).

| Función / ID | USA 0.6 o motor fijado | PAL integración limpia d7 | Experimento / prueba existente | Evidencia, dependencia y siguiente comprobación |
| --- | --- | --- | --- | --- |
| Inicio Windows, ISO/perfil | Port USA propio | **Validado** en alpha PAL: selector/persistencia/perfil/carrera | Alpha instrumentada, no mismo EXE que clean widescreen | E-STATUS; preservar LocalAppData y revisión de distribución |
| Hor+ 3D/HUD | widescreen.cpp del juego | **Validado** en integración, 14+27 tests y aceptación visual | También referencia instrumentada anterior | E-WIDE; no modificar FOV/HUD; PAL50 y escenas especiales no exhaustivos |
| Transferencias 0.4 / UP-001 | a172049 anterior a 645bbf39 | **Presentes por ascendencia** d7; código genérico shader/swizzle/GS | No hay A/B PAL de ese commit aislado | No atribuir readbacks a una pérdida de esa optimización; investigar ruta causal |
| CLUT GPU CSM1 / UP-006 | LoadClut; todavía ruta GPU | **Presente y ejercitada** | 41.974 cargas en sesión histórica; otros totales en counters | E-PERF/E-COUNTERS; no extrapolar conteos entre sesiones |
| CLUT CSM2 / UP-006 | LoadClut devuelve ruta CPU para csm2 | **Presente CPU, no GPU optimizado** | Uso regional/frecuencia no atribuidos | gs.cpp TEXCLUT y bridge LoadClut; no afirmar que sea origen de las descargas |
| Decode GPU | Callback/bridge | **Presente y ejercitado** en sesiones | 59.580 operaciones en sesión histórica | E-PERF; no confundir decode con readback al host |
| Target/fallback / UP-002 | PresentGpuPicture en 721 sigue exact key + stale + DisplayTarget, luego Picture | d7 conserva ese esquema; el patch PAL-010 añade gate regional opt-in | C regional aceptada manualmente a 2×/3×/4× en EXE experimental SHA-256 `612719b36ad1103d71342b4d01b4da10a0d64640007f99115cbfd9beadf6940b` | No reabrir fallbacks sin regresión reproducible; no hay solución general acreditada para otros juegos |
| SetPresentAtBlankEnd / UP-002 | API motor + opt-in USA; 1d8d428/a420bb7 | **Ausente** API d7 y opt-in PAL | No ensayado | Spans host/GS/threads/IOP y juego; medir copia a display y PAL50/60 antes de adaptar |
| Latencia/cargas / UP-002 | Cambios de scheduling en 1d8d428, no solo una llamada | **Ausentes cambios posteriores** | No comparación PAL de latencia | No copiar temporización NTSC sin sus dependencias; no equivale a menos readbacks |
| Shuffle fog / UP-003 | ChannelShuffle/Shuffle, casos geométricos validados por código | **Ausente esta ruta nueva** | No replay PAL que la homologue | 8edd32e y extensiones 154eb00; formato/alpha/Z y condiciones de fallback |
| ZIntoColour glow / UP-003 | Dibuja piezas de vista depth sobre su imagen color compatible | **Ausente esta ruta nueva** | No validado PAL | 154eb00 + caché 964dcc6; preservar productores/consumidores y mapping de páginas |
| Opciones gráficos / UP-032 | graphics.cpp, BloomRender/RadialBlur y datos del juego | **Ausente adaptación avanzada** | Blur Original como condición de pruebas, no todo PC SETTINGS | No confundir con Shuffle/ZIntoColour del motor; mapear variables y funciones PAL |
| Writebacks por columnas / UP-003/004 | c549898, bridge/renderer/shader/wrapper | **Ausente cambio nuevo** | No prueba PAL | ABI y coherencia de rangos; writeback interno GPU no es necesariamente descarga CPU |
| Vistas ≥1024 nativas / UP-005 | 6763888, ScaleOf | **Ausente** | Dimensiones relevantes PAL pendientes | Puede cambiar coste/imagen glow; no presenta un arreglo causal MemoryRead |
| Texturas por páginas / UP-004 | 072299a, solo referencias potencialmente válidas | **Ausente cambio nuevo** | No coste PAL atribuible | Invalidación/caché y alias antes de adoptar |
| Estado draw / UP-004 | 531800d, aviso de estado repetido al renderer | **Ausente interfaz nueva** | Ya existen fast paths más antiguos en d7 | API/worker; no portar un único caller con ABI anterior |
| Copia GS 16-bit / UP-004 | 01b9843, 16 píxeles por iteración | **Ausente variante nueva** | d7 ya posee optimizaciones de copia anteriores | Tests PSM, límites y alineación; no confundir CPU memcpy con readback Vulkan |
| VU compilado / UP-010 | vurecomp/runtime | **Presente**, coverage por sesión | >99,99% y sesiones 100% compiladas | E-PERF; coverage no acredita equivalencia numérica ni flags |
| VU Fast / UP-010 | SetVuFastDefault(true) USA | **Presente pero desactivado por defecto PAL**; --vu-fast existe | Comparativas retenidas mantienen Fast Off | No activar ahora; tests Exact/Fast y transiciones entre programas |
| Mejoras VU previas / UP-010 | 1700899/2f8b085 anteriores a d7 | **Presentes por ascendencia** | No se miden aquí aisladamente | No marcar todo VU optimizado como ausente |
| Mejoras VU 0.5.3 / UP-010 | 109addf / 7ac256f, flags/operandos y generación | **Ausentes cambios nuevos** | EE/VU conservados sin regenerar | Generador y runtime deben corresponder; no reutilizar objetos incompatibles |
| VU Native Blocks PR / UP-013 | PR #1 abierta, distinta del trabajo upstream de bloques compilados | **No integrado/homologado** | Sin vunative.py en árboles d7/721 examinados | Ausencia del archivo no prueba identidad semántica de todos los mecanismos; diff de PR pendiente |
| GPU Thread / PAL-023 | Trabajador ya anterior a d7 | **Presente**; no certificado On en clean widescreen | Alpha On aceptado; experimento On/Off estable en replay; juego A/B no causal | UP-015/E-GPU; HD On extremo y freeze histórico siguen abiertos |
| Pipeline hints / UP-015 | Registro USA SetGpuPipelineHints | **API presente, lista/registro PAL ausente** | Cache y especialización sí existen; shader frío no aislado | Medir cold/warm por separado; no confundir ausencia de hints con ausencia de cache |
| clang-cl / UP-011 | Selector automático 2f3c1ce | **Ausente selector nuevo**; MSVC en EXE medido | MSVC 14.51.36231, /O2 /Ob2, AVX2 | E-PERF; toolchain/ABI/FP y workload equivalentes |
| PGO/ThinLTO / UP-012 | port.py y build posteriores | **Ausente flujo nuevo**, perfil PAL no acreditado | No comparativa realizada | e10d7d3/b868513; perfiles PAL y reconstrucción coherente requeridos |
| SSAA / UP-031 | Resolve/downsample y selector USA SSAA2 | **Backend presente sin validación PAL** | No validado como FXAA/SMAA | No equiparar escala interna 2x con SSAA; leer E-GRAPHICS |
| FXAA/SMAA F9 / UP-031 | Backend compartido y panel USA | Backend presente; **selector experimental no integrado** | F9/persistencia/2x/4x/On aceptados; commit privado 8c1fa26 | E-AA; no crear filtros nuevos ni declararlo presente en alpha |
| HD / PAL-033 | Cargador compartido + juego/ruta USA | Base loader presente; cambios exedir/catalog privados no integrados | HD333 con matches y mala calidad/rendimiento | E-HD/E-TEXTURES; hashes compatibles no garantizan sustitución visible correcta |
| F10 HUD editor / UP-030 | Anunciado README; no auditoría completa aquí | **No validado como editor PAL** | Anclajes widescreen sí aceptados, mecanismo distinto | No extrapolar del HUD funcional a editor/32:9 |
| High FPS / UP-020/021 | high_fps.cpp + blank/display APIs nuevas | **Ausente adaptación** | Ninguna prueba PAL | Juego/renderer/motor; física original PAL50/60 y múltiples cámaras |
| MP3 EA TRAX / UP-040 | custom_songs.cpp y API música | **Ausente adaptación PAL** | No tests PAL | Cadenas/listas/idiomas; no equivale a streaming externo |
| Audio coche / UP-041 | Base 0.4 + SPU2 posterior | Base heredada, audio PAL funcional; **fix posterior ausente** | No comparación vehículo por vehículo | 2286d72 histórico, b9373efe posterior; voces/FMVs/SPU2 |
| PC SETTINGS/controles/mods / UP-032/033/035 | Código del juego USA | **Sin adaptación completa** | Controles básicos y F9 existentes no validan los extras | Direcciones/textos/contextos PAL y pruebas por función |
| DualSense / UP-034 | Anunciado; no auditoría de dispositivos aquí | **No validado** | No resultados PAL de gatillos aportados | No inferirlo por presencia de SDL |
| Splitscreen / UP-042 | Correcciones juego/motor USA | **No validado** | Sin prueba dos jugadores PAL homologada | Original PS2/infraestructura no equivalen a adaptación del port |
| LAN / UP-043 | inet motor + lobby/hooks USA | **Parcial motor heredado, juego no adaptado/validado** | No prueba LAN PAL | No activar sockets o hooks regionales automáticamente |
| SSE4 / UP-044 | Infraestructura del motor y segundo EXE USA | Motor tiene soporte previo; **player PAL AVX2** | No build SSE4 PAL probado | No confundir posibilidad de compilación con compatibilidad del paquete |
| GS Replay / PAL-002 | Infraestructura compartida | **Útil parcialmente**, no integrada como suite pública | 12 procesos, 13 tests sintéticos; muestras estables, diferencias software/GPU | E-REPLAY; no reproduce PresentGpuPicture; repeat no restaura memoria inicial |

## Hallazgos GPU que no se cierran por USA 0.6

1. d7 ya desciende de 645bbf39; a172049 no está perdido. La rama causal sigue abierta.
2. CLUT GPU y decode se ejercitan. La rama CSM2 continúa CPU incluso en el archivo
   721 inspeccionado. Esto no prueba que CSM2 genere las descargas observadas.
3. PresentGpuPicture 721 todavía busca `(fbp,fbw,psm,false)` exacto, rechaza stale,
   intenta DisplayTarget y conserva Picture como fallback. Una mejora de timing
   no demuestra desaparición de esa ruta ni redundancia de fences.
4. Shuffle y ZIntoColour evitan recorridos internos por memoria en operaciones
   concretas, con condiciones de formato/geométricas y fallback. No autorizan
   ignorar stale, ownership o datos de píxeles visibles.
5. Los 560 KiB siguen siendo correlación agregada. Mantener separados MemoryRead,
   volumen, fences retirados y espera; falta consumidor/ID/rango por operación.
6. No comparar benchmarks USA anunciados con sesiones PAL mixtas ni sumar esperas
   solapadas como tiempo secuencial perdido.

## Commits relevantes y dependencias

Enlaces fijados al repositorio del **motor**, salvo las activaciones del juego.
Los cambios posteriores a d7 están ausentes de la base PAL por revisión; esto no
implica que la misma semántica no tenga otra implementación previa.

| Commit | Cambio y archivos/componentes | Estado PAL / riesgos para una adaptación aislada |
| --- | --- | --- |
| [a172049d8e764574e15b553306e3fd03dbbf74fe](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/a172049d8e764574e15b553306e3fd03dbbf74fe) | Shaders GS/address/swizzle, batching, Words; transferencias 0.4 | Ancestro confirmado de d7; no duplicar |
| [6966dfc3df9c4ffdcb6916a47c54e33963967884](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/6966dfc3df9c4ffdcb6916a47c54e33963967884) | CLUT GPU, referencia de auditoría | Presente; cobertura/CSM2 aún por medir |
| [1d8d42823ec39204a461167ac489b5024b038db8](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/1d8d42823ec39204a461167ac489b5024b038db8) | run.h, devices, gpu_bridge, gs_thread, threads; blank/input/scheduling | Ausente d7; depende de API y opt-in juego, pruebas PAL50/60 |
| [8edd32e199e0a5ad6662d9cbd9e8ca0442c90cac](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/8edd32e199e0a5ad6662d9cbd9e8ca0442c90cac) | shuffle.comp, API/renderer/threaded_renderer y bridge; ChannelShuffle | Ausente; shader, build y ABI conjuntos; comprobar alfa y límites |
| [154eb00fba57e9d091756d88ea031841b1a8e45e](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/154eb00fba57e9d091756d88ea031841b1a8e45e) | Bridge: ZIntoColour y offsets de páginas | Ausente; alias/formato/ownership, depende de rutas previas |
| [964dcc6c1746c4fc5c9d54076815fcb95f2a5dfb](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/964dcc6c1746c4fc5c9d54076815fcb95f2a5dfb) | Caché de piezas ZIntoColour | Ausente; no trasladar sin implementación que cachea |
| [c54989855429033018220842222026fdb721e1d1](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/c54989855429033018220842222026fdb721e1d1) | swizzle/API/renderer/worker/bridge, columnas de páginas | Ausente; fronteras/formatos, no confundir writebacks GPU con CPU |
| [676388874920c93c35df886382f94a6669d1d62d](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/676388874920c93c35df886382f94a6669d1d62d) | ScaleOf, vistas anchas a escala nativa | Ausente; validar memoria/glow/imagen PAL antes de prueba |
| [072299a4025eef95cc37934e600029bc4701f5db](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/072299a4025eef95cc37934e600029bc4701f5db) | gpu_bridge, listas de texturas válidas por páginas | Ausente; invalidación y alias |
| [531800df495d33b468ca5a8acbd67179b057c795](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/531800df495d33b468ca5a8acbd67179b057c795) | GsDrawState/API y worker, estado de draw repetido | Ausente; ABI/caché de estado deben mantenerse coherentes |
| [01b98438d75c3958aae2a85698677d9a29842fa0](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/01b98438d75c3958aae2a85698677d9a29842fa0) | gs_memory.cpp, copia 16-bit vectorizada | Ausente; extremos, stride, PSM y alineación |
| [109addf8c17adee4f31f4dc780a10f7a09a7b13c](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/109addf8c17adee4f31f4dc780a10f7a09a7b13c) | vu_core.h, FMAC/operandos clean | Ausente; numérica y objetos coherentes |
| [7ac256f8a352d64afccd2de664874649f4762df9](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/7ac256f8a352d64afccd2de664874649f4762df9) | vu_core/vu.cpp/vurecomp, MAC frente STATUS | Ausente; requiere runtime/generación concordantes y tests de flags |
| [2f3c1ce0f225fe78b07e7108c12ad71a436af49d](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/2f3c1ce0f225fe78b07e7108c12ad71a436af49d) | port.py, CMake y compatibilidad clang-cl | Ausente; ABI, FP y reconstrucción aislada |
| [e10d7d3ad4baab60056cc0474b8d2bbdc874e30c](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/e10d7d3ad4baab60056cc0474b8d2bbdc874e30c) | port.py/host, PGO | Ausente; workload de entrenamiento PAL propio |
| [b86851311dd12facbef48c8cdb58040066d29a73](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/b86851311dd12facbef48c8cdb58040066d29a73) | ThinLTO clang/port.py | Ausente; selección/configuración posterior también relevante |
| [bf49b6af6ed761a9b93583497e588c0eef951ceb](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/bf49b6af6ed761a9b93583497e588c0eef951ceb) | BlankRate/pacing de cola a tasa de pantalla | Ausente; dependencia de high_fps, no mejora independiente de todos los casos |

Activación USA de blank-end: [a420bb7ddcb8333962658307e930940bdc95d141](https://github.com/danielgomesvieira2000/burnout3-pc/commit/a420bb7ddcb8333962658307e930940bdc95d141).
En [overrides.cpp de 0.6](https://github.com/danielgomesvieira2000/burnout3-pc/blob/444b541b9919055af3ee97787e84de7c2ceed98f/games/burnout3/port/overrides.cpp#L1146),
RegisterGameOverrides activa VuFast, desactiva pacing por defecto, registra
blank-end y hints. PAL no reproduce esas decisiones automáticamente; su banco
mantiene Frame Pacing On. No cambiar defaults por copiar esa función.

Archivos de juego independientes:
[graphics.cpp](https://github.com/danielgomesvieira2000/burnout3-pc/blob/444b541b9919055af3ee97787e84de7c2ceed98f/games/burnout3/port/graphics.cpp),
[high_fps.cpp](https://github.com/danielgomesvieira2000/burnout3-pc/blob/444b541b9919055af3ee97787e84de7c2ceed98f/games/burnout3/port/high_fps.cpp).
No trasladar sus direcciones SLUS ni sus supuestos de cadencia a SLES.

## Evidencias locales

Los originales permanecen **privados**, bajo el directorio de diagnósticos del
workspace del mantenedor. Los siguientes son identificadores de expediente, no
enlaces a archivos distribuidos. No copiar capturas/logs/código generado a Git.

| ID | Expediente o documento | Qué demuestra / qué no |
| --- | --- | --- |
| E-GIT | ls-remote del fork y ls-tree de la integración, revisión 2026-10-20 | HEAD/gitlink y ascendencia; no validación de runtime |
| E-STATUS | [STATUS](STATUS.md), [continuidad](../../PAL_DEVELOPMENT.md) | Aceptación manual alpha y límites por build |
| E-WIDE | [WIDESCREEN_3D](WIDESCREEN_3D.md), [WIDESCREEN_HUD](WIDESCREEN_HUD.md) | 14+27 pruebas y aceptación visual; no todas las escenas/modos |
| E-MAPPING | [USA_PAL_ADDRESS_MAPPING](USA_PAL_ADDRESS_MAPPING.md) | Identidades, candidatos y límites de offsets |
| E-AA | Commit experimental 8c1fa26 y [GRAPHICS_PERFORMANCE](GRAPHICS_PERFORMANCE.md) | Selector privado y aceptación del mantenedor; no integrado |
| E-GRAPHICS | graphics-aa-audit-20261019T202700, REPORT.md | Pipelines y revisión histórica; no master actual |
| E-PERF | upstream-performance-preAB-20261019T190126Z, REPORT.md | CLUT/decode activos, Fast Off, compilador y hints |
| E-GPU | gpu-ab-exhaustive-20261019T194428Z, REPORT.md | A/B exploratorio; sin ventana controlada de carrera |
| E-COUNTERS | gpu-counters-off-on-20261019T200644Z, REPORT.md | Descargas read/fences/560 KiB; no consumidor causal |
| E-DOSSIER | gpu-dossier-audit-20261019T222222Z-3c259b, REPORT.md y VALIDATION_PLAN.md | Hipótesis y criterios; no parche aprobado |
| E-TRACE | presentation-trace-readiness-20261019T205125Z-c21060 | Muestreo/ring y campos insuficientes para atribución completa |
| E-REPLAY | gs-replay-automation, REPORT.md, RUN_TOMORROW.md y results | 12 procesos/13 tests; útil parcialmente, sin fallback de presentación |
| E-HD | hd333-diagnosis-20261019T164735Z-0d8f9c y [HD_TEXTURE_PACK](HD_TEXTURE_PACK.md) | Sustituciones reales y prueba insatisfactoria, no pack homologado |
| E-TEXTURES | texture-triple-20261019T153917Z, texture-history-20261019T162845Z-a7bafd; [TEXTURE_COMPARISON](TEXTURE_COMPARISON.md) | 333 claves directas y candidatos separados de reemplazo válido |

Expediente de esta revisión: `planning-upstream-20261020` (privado), con respuestas
API, fuentes fijadas y verificaciones. Sus archivos no forman parte del commit
documental. Las auditorías originales se conservan, incluidas sus conclusiones
históricas; este documento no reescribe sus revisiones o métricas.

## Prioridades y límites de la decisión

P0 permanece **PAL-010**. Después, medir PAL-011/012/013 para decidir entre
timing, búsqueda/cobertura, CSM2 o lecturas legítimas. Las rutas nativas fog/glow
merecen investigación P1 con replay, pero no desplazan esa prioridad.

No se recomienda adoptar ahora 42 commits en bloque, activar VU Fast, regenerar
VU por optimizaciones nuevas, cambiar compilador, omitir frames, eliminar fences,
ignorar stale ni migrar de backend. Todo ello exigiría autorización y pruebas
separadas. No se ha creado ningún benchmark, compilación o nueva sesión aquí.
