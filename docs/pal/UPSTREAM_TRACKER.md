# Burnout 3 PAL Enhanced — Upstream Tracker

Revisión: 2026-10-20. Inventario inicial del mantenedor contrastado selectivamente
con releases, gitlinks, código y auditorías. **No es un diff exhaustivo ni una
lista de cambios autorizados.** Véanse [matriz](UPSTREAM_PAL_MATRIX.md),
[roadmap](ROADMAP.md) y [registro de correcciones](PLANNING_CHANGELOG.md).
El [catálogo externo](EXTERNAL_ENGINE_RESEARCH.md) consolida mecanismos, evidencias,
riesgos y puertas de validación; sus estados no significan implementación PAL.

## Repositorios y referencias fijas

- PAL: [JMPliskin/burnout3-pal-enhanced](https://github.com/JMPliskin/burnout3-pal-enhanced).
- Juego USA: [danielgomesvieira2000/burnout3-pc](https://github.com/danielgomesvieira2000/burnout3-pc).
- Motor: [danielgomesvieira2000/ps2-port-engine](https://github.com/danielgomesvieira2000/ps2-port-engine).
- PAL integrado: `434a86c9a4bb8e9392fea5dea99e6967770aec18`, motor
  `d7e6e023f261d9f95ad097fcd063ec95e40ca465`. Rama principal remota observada:
  `3ad44e482c201097333dc79a452bfb6b8b4bc648`.

No confundir versión del juego, tag del motor, gitlink del juego y HEAD de master.
Las fechas siguientes son `published_at` UTC de las releases, no fecha de commit.
Tags y gitlinks se resolvieron mediante API de GitHub; no se descargaron assets.

## Releases USA verificadas

| Tag | Fecha UTC | Commit completo del juego | Gitlink engine completo | Resumen anunciado |
| --- | --- | --- | --- | --- |
| v0.1.0-alpha | 2026-10-10 | c367dd57f6a00ed85cef0fef4afadc25c0234df0 | 8f4120af84c79919f5383686d5830ec66f987010 | Port jugable |
| v0.2.0-alpha | 2026-10-11 | 06a5a27bf1315963679a5b9e3b0abef5f999dba1 | 3ca006518ce07c5ae191cb5e0a3260560bda1de2 | HD, F12, HUD |
| v0.3.0-alpha | 2026-10-13 | f6c5eb96aaebbe72b0bbd2a026a57c55c5720af0 | 1b70ef531a1235e159df24bd1e874b41921047ae | Opciones PC, AA, controles |
| v0.4.0-alpha | 2026-10-15 | c16f8a6f874d6e7a5f34eb45fc6d3e3a70882171 | 645bbf394abf449414c39d975578b6e090eee4ac | Audio de motor, transferencias, F1/F9 |
| v0.5.0-alpha | 2026-10-18 | 46a7a9159b17d6b8872d656e8d1669ec65d0acad | ea7bedbf1a960c28442c2fc482e1c70cc527ade2 | Splitscreen, LAN, mods, clang, GPU Thread |
| v0.5.1-alpha | 2026-10-19 | a420bb7ddcb8333962658307e930940bdc95d141 | d5e4fd46c04f0513e398bb24ef21b04c0b092aab | VBLANK, input, carga y pacing |
| v0.5.2-alpha | 2026-10-20 | 1fd1494b608bc0e1a130aed9e7e0ce65cf60d9aa | 7999f4f8728a9d60f8310d9a759f6f33a81ddf17 | MP3, gráficos, fog/glow, PGO/LTO |
| v0.5.3-alpha | 2026-10-20 | 7ef7836f569dad528f419cb12d0444a0ec6d9a5e | 01b98438d75c3958aae2a85698677d9a29842fa0 | VU1 y trabajo GS/GPU |
| v0.6.0-alpha | 2026-10-20 | 444b541b9919055af3ee97787e84de7c2ceed98f | 721c73cd00e21aa87349ad5ae092277dcd3b69fa | Presentación interpolada hasta 240 Hz |

[Historial público](https://github.com/danielgomesvieira2000/burnout3-pc/releases).
Los motores anunciados en el inventario original —v0.7.0, v0.11.0, v0.12.0,
v0.13.0 y v0.13.2 para USA 0.4/0.5/0.5.1/0.5.2/0.5.3— se conservan como
etiquetas anunciadas; la identidad usada para comparar es el gitlink de la tabla.

## Convenciones de seguimiento

Cada entrada conserva prioridad original, origen, estado USA/PAL, riesgos y prueba.
Las decisiones actuales son **DEFER: estudiar, sin adopción autorizada**. No indican
que se haya descartado la función ni que esté aprobada para integrar.
La implementación PAL se separa en base pública, integración limpia y experimentos.
La referencia de código/commit y las dependencias concretas están en la matriz.

### GPU, coherencia y vídeo

| ID / prioridad | Origen y estado USA | Estado PAL contrastado | Acción, pruebas, riesgos y relación |
| --- | --- | --- | --- |
| UP-001 / P0 | USA 0.4, transferencias de David Petersson; motor 645bbf39 | **Presencia base confirmada por ascendencia**, efecto PAL no homologado; CLUT/decode sí ejercitados | Auditar a172049 y rutas reales; no volver a integrar lo ya heredado ni prometer porcentajes USA. PAL-010/013/016 |
| UP-002 / P0 | USA 0.5.1, motor 1d8d428; opt-in juego a420bb7 | API/activación ausentes en d7/PAL | Secuencia copia→blank→present e input en PAL50/60; no es una corrección causal de readbacks demostrada. PAL-011/012 |
| UP-003 / P1 | USA 0.5.2, fog/glow nativo en motor; opciones en graphics.cpp del juego | Rutas nuevas ausentes en d7; no validadas PAL | Separar Shuffle/ZIntoColour de los overrides de opciones; conservar escrituras consumidas después. PAL-015/016 |
| UP-004 / P1 | USA 0.5.3, listas de texturas, estado de draw, copia 16 bits | Cambios nuevos ausentes en d7; ya existían optimizaciones anteriores | Revisar 072299a/531800d/01b9843 con ABI y coherencia; A/B sin suprimir alias PAL. PAL-016/023 |
| UP-005 / P2 | Motor 676388874920c93c35df886382f94a6669d1d62d: vistas ≥1024 a escala nativa | Ausente d7 | Medir dimensiones y glow relevante; no es arreglo de descargas CPU. PAL-015 |
| UP-006 / P1 | Hallazgo PAL; no arreglo upstream verificado. CSM1 GPU, CSM2 CPU también en 721 | Misma limitación; frecuencia y cobertura PAL desconocidas | TEXCLUT y bloques reales, tests paleta; no culpar CSM2 sin medir. PAL-014 |
| UP-007 / P1 | PCSX2 cce45b1, heurística CLUT y candidato solapado | No es un arreglo de presentación de este motor; ya existe Containing en otros usos | Vigencia/cobertura y productores; no aceptar solapamiento como equivalencia. PAL-011 |

### VU, compilación, profiling y pipelines

| ID / prioridad | Origen y estado USA | Estado PAL contrastado | Acción, pruebas y riesgos |
| --- | --- | --- | --- |
| UP-010 / P1 | VU compilado/Fast y cambios adicionales 0.5.3 | **Parcialmente presente**: mejoras anteriores a d7 y Fast disponible; Fast no activado por defecto PAL; nuevas 0.5.3 ausentes | Separar pares compilados, Fast y Native Blocks; flags entre programas y geometría. PAL-021 |
| UP-011 / P1 | clang-cl automático, 2f3c1ce; USA releases posteriores | Selector nuevo ausente d7; build medido MSVC | Toolchain/ABI/FP/reproducibilidad; no inferir ganancia. PAL-022 |
| UP-012 / P1 | PGO y ThinLTO, USA 0.5.2/0.5.3 | Flujo nuevo ausente d7; no perfil PAL demostrado | Perfiles PAL sin contaminar benchmark y sin mezclar objetos. PAL-022 |
| UP-013 / P2 | PR motor #1 abierta, head observado 0b12cd1488ed86050260315774237414a11cd911 | No hay tools/vunative.py en los árboles d7/721 inspeccionados | No confundir optimizaciones de bloques compilados ya upstream con el pipeline Native Blocks de la PR; revisión de equivalencia pendiente. PAL-021 |
| UP-014 / P3 | Frame skip propuesto en PR #1 | No homologado PAL | No ocultar readbacks ni equiparar omitir imágenes a renderizado preciso |
| UP-015 / P2 | Pipeline cache, especialización y hints ya anteriores a d7 | **API/cache presentes**; registro de hints específicos PAL ausente | Caché fría/caliente y primer uso separados de coste sostenido. PAL-023; E-PERF |

### Cadencia, gráficos e interfaz

| ID / prioridad | Origen y estado USA | Estado PAL contrastado | Acción, pruebas y riesgos |
| --- | --- | --- | --- |
| UP-020 / P2 | USA 0.6, high_fps.cpp; simulación anunciada 60 pasos/s e imágenes interpoladas | Ausente adaptación PAL | Mapear transformaciones y temporización; no asumir 60 pasos para PAL50. PAL-040/041 |
| UP-021 / P2 | USA 0.6, reducción automática 120/60 por imágenes tardías | Ausente adaptación PAL | Distinguir imagen mostrada, paso simulado y recuperación. PAL-041 |
| UP-030 / P2 | F10 editor HUD, anunciado en README 0.6 | No validado ni integrado como editor PAL | No confundir el editor con los anclajes del HUD widescreen ya aceptados. PAL-031 |
| UP-031 / P2 | FXAA/SMAA/SSAA2, motor y UI USA | Backend presente; F9 FXAA/SMAA **validado en experimento separado**; SSAA no homologado | 2x/4x, Off, persistencia, postproceso y coste; no recrear shaders. PAL-030/030-SSAA |
| UP-032 / P2 | graphics.cpp USA 0.5.2: bloom, blur, sombras, LOD, partículas | Opciones avanzadas no adaptadas; no invalida soporte gráfico base | ORIGINAL obligatorio; mapear datos PAL antes de hooks. PAL-032 |
| UP-033 / P2 | Reasignación/prompts/ratón, USA | Infraestructura de motor parcial; funciones del juego sin adaptar/validar | Mapas PAL, persistencia, idiomas y dispositivos. PAL-034 |
| UP-034 / P3 | DualSense, USA | Pendiente de validar PAL | Vibración/gatillos, USB/Bluetooth; no inferir soporte completo por SDL. PAL-034 |
| UP-035 / P3 | Mods/developer menu; expansión Crash Breaker USA 0.6 | No adaptados de manera general | Funciones/datos PAL verificados, pruebas por mod. PAL-035 |

### Audio, multijugador y hardware

| ID / prioridad | Origen y estado USA | Estado PAL contrastado | Acción, pruebas y riesgos |
| --- | --- | --- | --- |
| UP-040 / P2 | custom_songs.cpp, USA 0.5.2 | MP3 EA TRAX no adaptado | Playlists, nombres, idiomas y avance; no implica Spotify. PAL-050 |
| UP-041 / P2 | Sonido motor 0.4; SPU2 b9373efe posterior a d7 | Audio funcional; cambios base 0.4 heredados; mejora posterior ausente | Comparar coche/voces/música/FMV; no declarar idéntico todo el motor de audio. PAL-052 |
| UP-042 / P2 | Splitscreen USA 0.5 | No validado PAL | Separar juego original/infraestructura compartida de correcciones nativas para dos jugadores. PAL-060 |
| UP-043 / P3 | LAN USA 0.5 | Parte inet/IOP ya heredada; lobby y hooks USA no homologados PAL | Red local y protocolo; sin requisito de servidores EA. PAL-061 |
| UP-044 / P3 | SSE4 fallback USA 0.5 | Infraestructura anterior a d7, pero player/build PAL exige AVX2 | No marcar ausente del motor ni soportado por este EXE sin pruebas. PAL-024 |

## Referencias externas conservadas

| ID | Fuente primaria | Uso y límite |
| --- | --- | --- |
| EXT-001 | [PCSX2 PR 9336](https://github.com/PCSX2/pcsx2/pull/9336) | Bloom/CLUT y reinterpretación; no copiar hacks sin evidencia PAL |
| EXT-002 | [PCSX2 PR 8671](https://github.com/PCSX2/pcsx2/pull/8671) | Cielo, alturas 128/256, caso PAL50; no es mapa regional universal |
| EXT-003 | [PCSX2 cce45b1](https://github.com/PCSX2/pcsx2/commit/cce45b1f2ce36374e2b7ce90b14018f5a333695b) | Solapamiento CLUT, dirty/cobertura; no presentación directa automática |
| EXT-004 | [burnout3-symbols, 979bd3191be08a41868b38c5f969bd80b2cd3e9f](https://github.com/u0rhs/burnout3-symbols/tree/979bd3191be08a41868b38c5f969bd80b2cd3e9f) | PAL-010A estático concluido: 40 entradas, 13 parejas previas reexaminadas, cero nuevas; utilidad causal pendiente. PAL review/SLES_52585 separados del retail SLES_52584; [ER-10](EXTERNAL_ENGINE_RESEARCH.md#er-10--burnout3-symbols-y-pal-010a) |
| EXT-005 | [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs), [PS2 Web Recomp](https://github.com/snuri00/ps2-web-recomp), [Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp) | Arquitecturas/dependencias/replay; no inicia una migración |
| EXT-006 | [BT3-Recomp: native render seam](https://github.com/z3xox/BT3-Recomp/blob/main/docs/NATIVE-RENDER-SEAM.md) | Referencia histórica main sin SHA fijado; contrato GS/backends y presentación no equivalen a nuestro host. [ER-06](EXTERNAL_ENGINE_RESEARCH.md#er-06--bt3-recomp-seam-de-renderizado-y-referencia) |
| EXT-007 | [rac1-native-game: setup](https://github.com/daino-selvatico/rac1-native-game/blob/main/docs/setup.md) | Estado histórico PAL60 frente a PAL50; mapping binario no certifica vídeo. [ER-09](EXTERNAL_ENGINE_RESEARCH.md#er-09--rac1-native-game-región-binaria-y-frecuencia) |
| EXT-008 | [Takedawn, 5a00fc10f9548a189e9043c5a55975863ad49175](https://github.com/xenophobentx/takedawn/tree/5a00fc10f9548a189e9043c5a55975863ad49175) | Referencia conceptual Xbox de timing/cámaras/simulación; sin importar direcciones o parches. [ER-11](EXTERNAL_ENGINE_RESEARCH.md#er-11--takedawn-simulación-cámaras-e-imagen-visible) |

PR de motor de referencia: [#1](https://github.com/danielgomesvieira2000/ps2-port-engine/pull/1).
Su estado abierto no significa que todos sus cambios sigan sin integrar:
GPU Thread, sampler y otras partes ya están en d7. Estado de Native Blocks y
frame skip se mantiene separado; no resolverlo por el título de la PR.

## Procedimiento de nuevas entradas

Registrar ID, fecha de detección, repositorio, SHA completo/tag, archivos,
resumen, dependencias, implementación USA (anunciada/verificada/no verificada),
estado PAL, riesgos PAL50/60, pruebas y evidencia local fechada. Añadir decisión
ADOPT/ADAPT/DEFER/REJECT y motivo. No borrar ni sustituir decisiones silenciosamente.

Revisar juego y gitlink del motor por separado; no asumir que master conserva el
estado de una release. No descargar assets, hacer fetch o integrar automáticamente.
Si faltan objetos locales, distinguir la lectura HTTP fijada de una comparación
Git local. La matriz actual usa ambas fuentes y declara su alcance.

## Prioridad y preservación

**PAL-010 sigue siendo el próximo trabajo funcional.** No se adopta ninguna
optimización en esta revisión. Mantener las auditorías GPU, counters, presentation
trace y GS Replay originales; referencias en la matriz, no copias de logs privados.
Las mejoras USA no cierran las hipótesis PAL por sí mismas.
