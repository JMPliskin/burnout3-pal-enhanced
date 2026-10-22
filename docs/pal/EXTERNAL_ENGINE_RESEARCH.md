# Investigación externa de motores y diagnóstico GS

PAL-001B · Consolidación documental: 2026-10-20.
Fuentes: auditorías conservadas E-DOSSIER, E-SYMBOLS y E-REPLAY, sin repetir
investigaciones, ejecutar pruebas nuevas o importar código externo.

Este catálogo relaciona mecanismos externos con el [roadmap](ROADMAP.md) y el
[tracker](UPSTREAM_TRACKER.md). **Ninguno constituye una corrección validada en
PAL ni autoriza instrumentación o una migración de renderer.** La matriz de
implementación USA/PAL permanece en [UPSTREAM_PAL_MATRIX.md](UPSTREAM_PAL_MATRIX.md).

## Estados, procedencia y evidencia

- **REFERENCIA:** método o arquitectura para consultar; no propuesta de integración.
- **CANDIDATO:** mecanismo potencialmente relevante, condicionado a evidencia PAL.
- **PENDIENTE DE VALIDACIÓN:** hay apoyo o uso parcial, pero falta la validación indicada.
- **DESCARTADO:** aplicación concreta rechazada por razones expresas; no se descarta
  automáticamente el proyecto externo ni investigaciones relacionadas.

Estos estados describen investigación externa; no sustituyen los estados de tareas
del roadmap. Los hechos de otro juego, las hipótesis PAL y las mediciones locales
se mantienen separados. Una URL main/master identifica la referencia histórica,
**no una revisión binaria reproducible**: cuando la auditoría no conservó SHA, se
indica y deberá fijarse antes de una comparación o implementación funcional.

Los siguientes expedientes son privados, conservados en el workspace de
diagnósticos del mantenedor. Se publican sus identificadores, no sus contenidos
originales, rutas personales, capturas, memoria GS ni traducciones.

| Evidencia | Expediente / documento | Alcance conservado |
| --- | --- | --- |
| E-DOSSIER | gpu-dossier-audit-20261019T222222Z-3c259b: REPORT, VALIDATION_PLAN, EXTERNAL_REFERENCES, evidence.json | Código y referencias revisadas; causalidad MemoryRead aún no demostrada |
| E-COUNTERS | gpu-counters-off-on-20261019T200644Z | Totales `read`, fences y correlación de 560 KiB; no operaciones individuales atribuidas |
| E-SYMBOLS | pal-symbols-audit-20261020: REPORT, SYMBOLS_PAL_MAPPING, SYMBOLS_MAPPING.csv, GPU_FUNCTION_FLOW, SOURCE_MANIFEST | 40 entradas, 13 parejas previas reexaminadas; contexto estático, no causal |
| E-REPLAY | gs-replay-automation: REPORT, RUN_TOMORROW, results | 12 procesos/13 tests sintéticos; estabilidad parcial; no fallback de presentación |
| E-GRAPHICS | graphics-aa-audit-20261019T202700 y upstream-performance-preAB-20261019T190126Z | Pipelines, CLUT/decode activos, límites de contadores y de las comparaciones |

## Índice de mecanismos

| Entrada | Proyecto / tema | Estado | Roadmap |
| --- | --- | --- | --- |
| ER-01 | PCSX2: targets solapados y dirty | CANDIDATO | PAL-010/011/014 |
| ER-02 | PCSX2: bloom y reutilización depth/color | REFERENCIA | PAL-015/016 |
| ER-03 | PCSX2: cielo, altura y PAL50 | PENDIENTE DE VALIDACIÓN | PAL-015/073 |
| ER-04 | ps2-port-engine: CLUT GPU y CSM2/TEXCLUT | PENDIENTE DE VALIDACIÓN | PAL-010/014 |
| ER-05 | Tenkaichi3Decomp: stale, bisección y DMA | CANDIDATO | PAL-010/011/023 |
| ER-06 | BT3-Recomp: separación de backends y referencia GS | REFERENCIA | PAL-002/017 |
| ER-07 | PS2 Web Recomp: memoria GS y dependencias compute | REFERENCIA | PAL-016/017 |
| ER-08 | paraLLEl-GS: Vulkan compute y replay | REFERENCIA | PAL-002/017 |
| ER-09 | rac1-native-game: región y modo de vídeo | REFERENCIA | PAL-012/071/073 |
| ER-10 | burnout3-symbols: nombres y binarios | PENDIENTE DE VALIDACIÓN | PAL-010A/010 |
| ER-11 | Takedawn: simulación y presentación | REFERENCIA | PAL-012/020/040 |
| ER-12 | PresentGpuPicture y SetPresentAtBlankEnd | CANDIDATO | PAL-010/011/012/013 |
| ER-13 | GS Replay: precisión y límites causales | PENDIENTE DE VALIDACIÓN | PAL-002/003/004 |

## ER-01 — PCSX2: búsqueda de productores solapados

- **Revisión/código:** [cce45b1f2ce36374e2b7ce90b14018f5a333695b](https://github.com/PCSX2/pcsx2/commit/cce45b1f2ce36374e2b7ce90b14018f5a333695b),
  GSTextureCache y PossibleCLUTDraw. Referencias EXT-003/UP-007.
- **Problema original:** la coincidencia exacta omitía un target que podía contener
  datos relevantes para una heurística de dibujo CLUT.
- **Implementado:** FindOverlappingTarget recorre intervalos color/depth mediante
  fin de bloque sin wrap y CheckOverlap; el consumidor comprueba dirty intersectante.
  Encontrar un candidato no certifica todos sus píxeles ni su geometría visible.
- **Diferencia arquitectónica:** es la texture cache de PCSX2 y una heurística CLUT.
  Nuestro bridge ya dispone de Owned/Containing y tracking de páginas para otros
  usos; PresentGpuPicture conserva lookup exacto y stale global del target.
- **Aplicación PAL posible:** enumerar productores alternativos y contrastar cobertura
  física/visible, solo si PAL-010 demuestra una búsqueda fallida con datos vigentes.
- **Evidencia/prueba:** E-DOSSIER confirma la asimetría; falta runtime. Medir key,
  ownership, último escritor, región visible, overlap y dirty por bloque/fila;
  ensayar formatos, FBW, wrap, recortes, PAL50/60 y target parcialmente válido.
- **Riesgo:** presentar un alias incompleto o stale, ignorar swizzle o confundir
  cobertura de memoria con equivalencia de píxeles. Estado **CANDIDATO**.

## ER-02 — PCSX2: bloom de Burnout

- **Revisión/código:** [PR 9336](https://github.com/PCSX2/pcsx2/pull/9336), integrada
  según E-DOSSIER; GSHwHack y sus rutas HLE. El expediente conserva el PR, no un
  SHA único de todo el árbol PCSX2. Referencia EXT-001.
- **Problema original:** cadena de bloom que reutiliza escrituras depth como color,
  buffers por columnas y coordenadas desplazadas, con conversiones intermedias.
- **Implementado:** reconocimiento específico de la operación del juego y sustitución
  o colapso de pases. No es únicamente una optimización genérica de coherencia.
- **Diferencia:** los hacks de título de PCSX2 se apoyan en su renderer/cache;
  ps2-port-engine conserva el flujo GS y sus propios alias/conversiones. No trasladar
  reglas de invalidación, skips o direcciones del caso USA a PAL sin reconstrucción.
- **Aplicación:** interpretar productores/consumidores de bloom y reducir un caso de
  replay. No atribuye por sí sola descargas CPU ni mejora específica del hilo GPU.
- **Prueba/evidencia:** E-DOSSIER/E-GRAPHICS; capturar cadena original PAL, datos
  depth/alpha y dependencias completas; comparar píxeles, máscaras y carga de circuito.
- **Riesgo:** suprimir un pase que produce datos reutilizados, cambiar glow/blur o
  conservar contenido de un circuito anterior. Estado **REFERENCIA**; PAL-015/016.

## ER-03 — PCSX2: cielo y modo PAL50

- **Revisión/código:** [PR 8671](https://github.com/PCSX2/pcsx2/pull/8671) y
  [GSHwHack.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/HW/GSHwHack.cpp),
  GSC_BlackAndBurnoutSky. La consulta master del dossier no tiene SHA fijado.
- **Problema/implementación:** tratar también cielos con altura 128, no solo 256;
  la revisión documenta una operación PAL50 `0x3ba0 → 0x1e80`.
- **Diferencia:** esas cifras identifican una operación/regiones GS del hack, no
  direcciones EE ni una transformación regional universal para el port.
- **Aplicación:** seleccionar una prueba PAL50 de nubes y registrar dimensiones,
  formato y regiones reales antes de proponer una adaptación.
- **Evidencia/pruebas:** E-DOSSIER y EXT-002; falta una secuencia PAL equivalente.
  Comparar PAL50/PAL60, cielos de 128/256, transiciones y circuitos distintos.
- **Riesgo:** arreglar una escena y ocultar nubes o reusar contenido antiguo en otra.
  Estado **PENDIENTE DE VALIDACIÓN**; PAL-015/073.

## ER-04 — CLUT GPU existente y hueco CSM2/TEXCLUT

- **Revisión/código:** [GPU.md en d5e4fd46](https://github.com/danielgomesvieira2000/ps2-port-engine/blob/d5e4fd46c04f0513e398bb24ef21b04c0b092aab/docs/GPU.md),
  código base PAL d7e6e023f261d9f95ad097fcd063ec95e40ca465, LoadClut del bridge y
  Gs::LoadClut. El expediente documental 0.6 inspeccionó además 721c73cd00e21aa87349ad5ae092277dcd3b69fa.
- **Problema/implementación:** paletas cuyo productor está en GPU podían requerir
  reconciliación CPU. El motor implementa carga CSM1 directa GPU; CSM2 continúa
  por CPU en las revisiones inspeccionadas. La memoria GS común ya existe: no es
  una capacidad completamente ausente de nuestra arquitectura.
- **Beneficio externo:** la documentación histórica reporta una reducción de 3134
  a 82 contadores etiquetados readback y de 25 a 0,2 s en su prueba USA. No son
  llamadas MemoryRead certificadas ni un benchmark PAL comparable.
- **Aplicación:** comprobar primero la frecuencia real de CSM2, su consumidor y
  cobertura TEXCLUT; no declarar perdida la optimización CSM1, ejercitada en PAL.
- **Pruebas/evidencia:** E-DOSSIER/E-GRAPHICS; CBP/CBW/COU/COV, CPSM, CSA/CLD y TEXA,
  bloques efectivamente leídos, ownership CPU/GPU, paleta exacta y transiciones.
- **Riesgo:** cobertura incompleta, dependencias de paleta omitidas y color/alfa
  incorrectos. Estado **PENDIENTE DE VALIDACIÓN**, UP-006/PAL-010/014; no hay ruta
  CSM2 GPU nueva aprobada ni atribución causal a los 560 KiB.

## ER-05 — Tenkaichi3Decomp: primer productor/consumidor incorrecto

- **Fuente/revisión:** [docs/port/README.md](https://github.com/z3xox/Tenkaichi3Decomp/blob/main/docs/port/README.md),
  consulta histórica main sin SHA conservado en E-DOSSIER, EXTERNAL_REFERENCES P4.
- **Problema:** un efecto sustituido dejó datos de buffer obsoletos que otro paso
  consumía; también se investigaron listas DMA modificadas tras su entrega.
- **Implementado/documentado:** BT3_GPU_CUT permite localizar el primer punto
  incorrecto de una secuencia; BT3_GS_RACE comprueba cambios entre handoff y consumo.
  Son instrumentos/resultados de ese proyecto, no opciones de nuestro EXE.
- **Diferencia:** otro juego y sus sustituciones nativas tienen productores y vida
  útil distintos. El método puede reutilizarse; las invalidaciones concretas no.
- **Aplicación:** IDs y últimos escritores por región; preservar productores al
  reducir un replay, y comprobar inmutabilidad del trabajo encolado si hay evidencia
  de divergencia Off/On. No afirmar una carrera de datos por tener un hilo GPU.
- **Pruebas/evidencia:** E-DOSSIER/E-SYMBOLS; prefijo que conserve dependencias y
  comparación software/GPU; comprobar entrega, consumo y lifetime de DMA.
- **Riesgo:** cortar un productor necesario, introducir stalls de diagnóstico o
  atribuir a datos GS un simple cambio temporal. Estado **CANDIDATO**; PAL-010/011/023.

## ER-06 — BT3-Recomp: seam de renderizado y referencia

- **Fuente/revisión:** [NATIVE-RENDER-SEAM.md](https://github.com/z3xox/BT3-Recomp/blob/main/docs/NATIVE-RENDER-SEAM.md),
  consulta histórica main, sin SHA fijado en E-DOSSIER P7.
- **Problema/implementación:** separar la entrada GIF común de distintas salidas
  gráficas, conservando GS como referencia durante la cobertura de pases nativos.
  No se acredita aquí que el renderer nativo tenga cobertura completa.
- **Diferencia:** el seam y contrato de sus backends no equivalen a GsRenderer.
  La presentación revisada en el dossier incluía readback/blit: otro backend no asegura
  eliminar descargas al host. La memoria GS unificada es una posibilidad de
  diseño a evaluar, no una propiedad asumida de todas sus salidas.
- **Aplicación:** referencia paralela y pruebas de un mismo stream antes de plantear
  PAL-017; no sustituir ahora la presentación/coherencia por una API diferente.
- **Pruebas/evidencia:** E-DOSSIER; contrato GIF/estado inicial, read/write GS,
  alias color/depth, CLUT, DATE/AFAIL, máscaras, píxeles y presentación nativa.
- **Riesgo:** cobertura parcial, ABI/cachés duplicadas, sincronizaciones adicionales
  y falsa equivalencia entre rasterizado y presentación. Estado **REFERENCIA**.

## ER-07 — PS2 Web Recomp: dependencias y compute

- **Fuente/revisión:** [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp),
  README consultado por E-DOSSIER P6, sin SHA fijado en esa auditoría.
- **Problema/implementación:** GT4 PAL con EE WebAssembly y GS WebGPU compute;
  representación GS en GPU y agrupación/reordenación de uploads y trabajo que se
  declara independiente. Su autor reporta aproximadamente 650→80 dispatches en
  un caso; no es una medición Burnout ni una promesa de mejora equivalente.
- **Diferencia:** navegador/WebGPU, planificador y renderer propios frente a
  Vulkan con targets, bridge y memoria GS ya presentes en ps2-port-engine.
- **Aplicación:** explicitar dependencias antes de adelantar conversiones o cargas,
  solo si PAL-010 identifica esas operaciones como coste relevante.
- **Pruebas/evidencia:** E-DOSSIER; leer/escribir/CLUT completos, barreras y orden
  observables, referencia software y replay con el mismo estado inicial.
- **Riesgo:** paralelizar dependencias ocultas, descartar un productor o medir menos
  dispatches sin medir tiempo/precisión. Estado **REFERENCIA**; PAL-016/017.

## ER-08 — paraLLEl-GS: compute Vulkan y reproducción

- **Fuente/revisión:** [parallel-gs](https://github.com/Arntzen-Software/parallel-gs),
  referencia de arquitectura consultada en E-DOSSIER P7; sin SHA fijado allí.
- **Problema/implementación:** implementar operaciones GS mediante Vulkan compute.
  Su objetivo de precisión no certifica identidad bit a bit universal con PS2.
  Esta consolidación no acredita un adaptador de replay para nuestro port.
- **Diferencia:** no es un renderer nativo Burnout ni un reemplazo pequeño de
  GsRenderer. Formato de replay, estado inicial, memoria y presentación requieren
  un contrato de integración propio; no asumir que lea nuestro GSRP1 directamente.
- **Aplicación:** eventual referencia alternativa o comparación de backend, después
  de atribuir el coste actual. La disponibilidad de compute no resuelve por sí misma
  un consumidor CPU legítimo ni el fallback del host.
- **Pruebas/evidencia:** E-DOSSIER/E-REPLAY; fijar revisión, revisar interfaz/licencias,
  demostrar reproducción del mismo estado/stream y separar descarga de validación.
- **Riesgo:** adaptación arquitectónica, resultados no comparables y coste de doble
  memoria/presentación. Estado **REFERENCIA**, PAL-002/017; sin migración propuesta.

## ER-09 — rac1-native-game: región binaria y frecuencia

- **Fuentes/revisión:** [README](https://github.com/daino-selvatico/rac1-native-game/blob/main/README.md),
  [setup](https://github.com/daino-selvatico/rac1-native-game/blob/main/docs/setup.md),
  consultas históricas main sin SHA fijado, E-DOSSIER P5.
- **Problema/implementación:** adaptar funciones/datos por tablas regionales; el
  setup revisado soportaba PAL60 y declaraba PAL50 sin soporte. Es estado histórico,
  no afirmación del estado actual del proyecto.
- **Diferencia:** otro juego, tablas y runtime. Las entradas individuales de su tabla
  regional no pudieron revisarse en aquella auditoría; no se les atribuye validación.
- **Aplicación:** separar identidad binaria de equivalencia de vídeo. El mapper
  USA/PAL de Burnout no certifica secuencia de campos, geometría o pacing PAL50.
- **Pruebas/evidencia:** E-DOSSIER/E-SYMBOLS; modos y registros reales, escenas/campos
  PAL50/PAL60 y funciones dependientes de temporización.
- **Riesgo:** tratar PAL60 como cobertura de toda la edición europea o introducir
  stand-ins de otra región. Estado **REFERENCIA**; PAL-012/071/073.

## ER-10 — burnout3-symbols y PAL-010A

- **Revisión/fuentes:** [979bd3191be08a41868b38c5f969bd80b2cd3e9f](https://github.com/u0rhs/burnout3-symbols/tree/979bd3191be08a41868b38c5f969bd80b2cd3e9f),
  [nombres retail](https://github.com/u0rhs/burnout3-symbols/blob/979bd3191be08a41868b38c5f969bd80b2cd3e9f/data/burnout3-retail/prototype-names.csv),
  documentación de builds, clases y herramientas. Referencia EXT-004.
- **Problema/implementación:** relacionar nombres de mapas prototipo con retail USA,
  guardando proof; otra tabla usa heurísticas desde SLES_52585. No aporta un mapa
  certificado para nuestro SLES_52584 comercial 1.01. El PAL review del mismo serial
  no es el mismo binario. Nombre, firma y pareja regional son pruebas diferentes.
- **Diferencia:** es metadato, no renderer. El USA local coincide por hashes con el
  retail de la fuente. No se revalidaron aquí las pruebas contra el prototipo.
- **Resultado local:** PAL-010A concluyó la investigación estática: 40 entradas,
  13 parejas PAL **previas** reexaminadas y traducciones localizadas; 7 asociaciones
  CONFIRMED, 6 STRONG, 10 TENTATIVE y 17 UNMAPPED; cero parejas nuevas.
- **Aplicación:** diccionario offline para temporizadores, caché original y etapas
  bloom. PresentDrawBuffer, fog y productores concretos aún requieren certificado
  PAL/paquetes. El original CGtTextureCache no es la caché Vulkan del bridge.
- **Pruebas/evidencia:** E-SYMBOLS; transportar IDs/rangos/path/DMA `from`, resolver
  nombres después y certificar límites/argumentos de cualquier punto EE utilizado.
- **Riesgo:** usar último PC de otro hilo, convertir direcciones por offset o tratar
  fuente de datos DMA como PC. Estado **PENDIENTE DE VALIDACIÓN** causal, PAL-010A/010.

## ER-11 — Takedawn: simulación, cámaras e imagen visible

- **Revisión/fuentes:** `5a00fc10f9548a189e9043c5a55975863ad49175`,
  [present-timing](https://github.com/xenophobentx/takedawn/blob/5a00fc10f9548a189e9043c5a55975863ad49175/docs/present-timing.md),
  [race-clock](https://github.com/xenophobentx/takedawn/blob/5a00fc10f9548a189e9043c5a55975863ad49175/docs/race-clock.md).
- **Problema/implementación:** port **Xbox**, estrategias legacy/auto de presentación
  y análisis del stepper original; opción de reloj de carrera. Sus pruebas separan
  paso de simulación, flip, blank, imagen visible y cambios al entrar/salir de cámaras.
- **Diferencia:** instrucciones/estructuras Xbox, reloj y backend distintos de PS2.
  Ni la tasa interna ni sus direcciones o parches se trasladan directamente.
- **Aplicación:** medir orden y latencia reales, anotar cámara/accidente y distinguir
  simulación de presents. No propone activar su modo de reloj en PAL.
- **Pruebas/evidencia:** E-SYMBOLS; PAL50/60, carrera/accidentes/menús/FMV y timestamps
  de entrada, simulación y presentación, con igual escena y ventana medible.
- **Riesgo:** confundir fluidez de salida con velocidad del juego o importar un
  cambio de física/temporización. Estado **REFERENCIA**; PAL-012/020/040.

## ER-12 — PresentGpuPicture, blank-end y los 560 KiB

- **Revisión/código:** PAL d7e6e023; [1d8d42823ec39204a461167ac489b5024b038db8](https://github.com/danielgomesvieira2000/ps2-port-engine/commit/1d8d42823ec39204a461167ac489b5024b038db8),
  [opt-in USA a420bb7ddcb8333962658307e930940bdc95d141](https://github.com/danielgomesvieira2000/burnout3-pc/commit/a420bb7ddcb8333962658307e930940bdc95d141).
  El tag USA 0.6 fija engine 721c73cd00e21aa87349ad5ae092277dcd3b69fa.
- **Problema/implementación:** upstream cambia scheduling/GS/IOP y ofrece
  SetPresentAtBlankEnd para observar la imagen después de la copia durante blank;
  USA lo activa. No es únicamente un cambio de renderer. PAL d7 no tiene ese opt-in.
- **Diferencia y hecho local:** PresentGpuPicture conserva lookup exacto, stale y
  DisplayTarget antes de Picture incluso en el archivo 721 inspeccionado. Containing
  para texturas no significa que presentar cualquier solapamiento sea seguro.
- **Hipótesis PAL:** una copia, invalidación o consulta en un instante distinto
  podría explicar un fallback; no se ha demostrado. La ruta estática potencial
  Picture→Touch→Sync→Coherent(false)→MemoryRead no es una traza causal observada.
- **Evidencia:** E-COUNTERS clasifica bytes como `read`; los incrementos de 560 KiB
  correlacionan con recargas C 0/10/0A, PSMCT16S. `640×448×2 = 560 KiB`, frente a
  `640×512×2 = 640 KiB`, es compatibilidad de tamaño, no identidad de una descarga.
  El contador etiquetado readback contabiliza fences retirados, no llamadas MemoryRead.
- **Prueba necesaria:** ID causal compartido, consumidor, bloques físicos, bytes,
  último escritor, invalidación/stale, rectángulo visible y registros de vídeo;
  tiempos copia/blank/consulta, PAL50/60 y GPU Off/On. Separar espera efectiva de
  retiro de fence y no sumar tiempos solapados como pérdida secuencial.
- **Riesgo:** mover presentación antes de datos vigentes, mostrar píxeles viejos o
  cambiar input/pacing. Estado **CANDIDATO**; PAL-010/011/012/013 y UP-002.

## ER-13 — GS Replay como banco parcial

- **Revisión/código:** motor d7e6e023, host.cpp GsReplay; `pcsx2dump.py` y captura
  nativa en devices.cpp. [Formato PCSX2 v2.8.2](https://github.com/PCSX2/pcsx2/blob/v2.8.2/pcsx2/GS/GSState.h)
  y convertidor de estado 9 revisados en E-REPLAY.
- **Implementado:** replay de memoria/registros y comandos GS sin juego completo;
  GPU mediante GpuShadowFrame/GpuCaptureScreen. Las repeticiones internas no restauran
  toda la memoria inicial; las pruebas independientes usan nuevos procesos.
- **Diferencia:** no ejecuta PresentGpuPicture ni su fallback. Inicialización,
  exportación de imágenes y reconciliación final añaden transferencias ajenas al
  coste ordinario de la secuencia. No es un save state ni benchmark causal completo.
- **Aplicación/evidencia:** 12 procesos y 13 tests sintéticos; estabilidad por backend
  en muestras cortas, diferencias software/GPU. Validación parcial, no oráculo exacto.
- **Pruebas:** dump PCSX2 Software PAL60 corto, conversión real, mismo estado y
  comparación de píxeles/instante/escala; después efectos y PAL50. Conservar todos
  los productores al reducir la secuencia.
- **Riesgo:** medir descargas de captura, llamar FPS de juego al tiempo del replay,
  ignorar diferencias o confundir repeat con reinicio. Estado **PENDIENTE DE VALIDACIÓN**.

## Aplicaciones descartadas y criterios antes de intervenir

Estado **DESCARTADO** para las siguientes aplicaciones, no para sus fuentes:

- Presentar el primer target solapado sin vigencia, cobertura y formato acreditados.
- Ignorar stale globalmente, quitar fences/Drain o forzar DisplayTarget.
- Copiar skips/hacks específicos, direcciones GS/EE o layouts Xbox/Revenge a PAL.
- Atribuir las descargas a CSM2, framebuffer o glow solo por nombre/tamaño.
- Considerar replay o cambio de backend una prueba de que desaparece el fallback.

Motivo común: no conservan una dependencia demostrada o sustituyen evidencia por
suposición. Las alternativas verificables siguen abiertas: stale acotado a región
visible, alias con cobertura certificada, timing del blank y CLUT con rangos reales.

La siguiente tarea funcional sigue siendo **PAL-010 — READY**, con autorización
separada pendiente. Primero atribuir una lectura real; después elegir una sola
hipótesis y validar precisión/orden, PAL50/PAL60, 4:3/widescreen, HUD, efectos,
menús/FMV y GPU Off/On. No se ha iniciado instrumentación en esta consolidación.
