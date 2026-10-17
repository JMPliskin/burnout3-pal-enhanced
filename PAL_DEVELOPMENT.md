# Burnout 3 PAL Enhanced: estado y continuidad

Fecha de revisión: 2026-10-17. Rama comprobada: `pal-enhanced`.
HEAD comprobado: `b0c12f6ddbd6d9ba323ada93f71a6829568ce8ea`.
Este documento describe evidencia local, no certifica el juego completo.

## Índice

1. Objetivo y repositorio
2. Entorno e inputs locales
3. Configuración y análisis PAL
4. Pipeline y evidencia verificada
5. Clasificador y lotes
6. Ejecutor aislado y límites
7. Commits comprobados
8. Comandos confirmados
9. Pendientes e incertidumbres
10. Retomar una sesión

## 1. Objetivo y repositorio

Objetivo: desarrollar un port nativo Windows de Burnout 3 PAL europeo con soporte español, denominado Burnout 3 PAL Enhanced. El soporte español y la ejecución completa PAL son objetivos pendientes, no resultados demostrados por las regresiones pequeñas.

Remote `origin` comprobado: https://github.com/danielgomesvieira2000/burnout3-pc.git.
El repositorio contiene el motor genérico en `engine/`; las herramientas Python están en `engine/tools/` y `engine/ps2decomp/`, y el runtime C++ en `engine/runtime/`. La configuración PAL independiente está en `games/burnout3-pal/`. La edición NTSC-U está en `games/burnout3/` y usa `SLUS_210.50`.

El README principal describe el port NTSC-U existente. Sus prestaciones y estado no deben atribuirse automáticamente a esta preparación PAL. No se ha demostrado aquí arranque, carrera, renderizado, audio ni localización completa PAL.

## 2. Entorno e inputs locales

Entorno utilizado: Windows, PowerShell, Python 3.12 mediante `py -3.12`, MSVC x64 encontrado por `vswhere` y preparado con `vcvars64.bat`. Las DLL de regresión se compilan con `/std:c++17 /O2 /W3 /bigobj /LD` mediante `cppcheck.build()`.

Identificador del ejecutable europeo: `SLES_525.84`. ISO local esperada: `Burnout3_PAL.iso`. Son nombres e identificadores, no contenidos distribuidos.

`game.json` utiliza `../../../ISOs/Burnout3_PAL.iso`: resuelta desde `games/burnout3-pal/`, queda en el directorio `ISOs/` hermano del repositorio, fuera de este. `Game.path()` y la existencia del archivo se comprobaron en la sesión. Cada nueva máquina debe proporcionar su propia copia legítima y volver a comprobar las rutas.

ELF local: `games/burnout3-pal/extracted/SLES_525.84`. ELF32 little-endian MIPS; entrada `0x00100008`. No se versiona. Las bases, informes, C++ y DLL generados son locales y permanecen bajo directorios ignorados.

## 3. Configuración y análisis PAL

Configuración: [games/burnout3-pal/game.json](games/burnout3-pal/game.json).
Base activa: `build/sles_525_84_clean.json`, con **9384 funciones**, comprobadas directamente. La original `build/sles_525_84.json` tiene 9388.

[clean_database.py](games/burnout3-pal/tools/clean_database.py) excluye exclusivamente las entradas `0x0046A280`, `0x0046A2A0`, `0x0046A480` y `0x00481818`, falsos positivos dentro del contenedor VU. Conserva la base original y los demás campos, incluidas las estadísticas iniciales. Rechaza sobrescribir sus resultados existentes.

Región DMA/VIF VU confirmada estáticamente: `[0x0046A280, 0x00484320)`, 106656 bytes, final exclusivo. Contiene 28 paquetes DMA y 67 órdenes MPG, con 13226 pares VU decodificados. No se presume equivalencia de direcciones con NTSC. `vu_regions` clasifica bytes en imagemap; por sí solo no filtra funciones de derive/lift.

Layout conservado:

| Parámetro | Valor |
|---|---|
| `image_end` | `0x02000000` |
| `scratch` | `0x01ED0000` |
| `scratch_size` | `0x20000` |
| `stack_top` | `0x01F00000` |
| `stack_size` | `0x10000` |

Scratch `[0x01ED0000, 0x01EF0000)` está mapeado en el intérprete y evita PT_LOAD y la pila sintética. Es una reserva exclusiva para pruebas aisladas: el arranque del juego ofrece esa zona al heap. No está garantizada libre durante ejecución completa.

## 4. Pipeline y evidencia verificada

Pipeline de regresión: ELF MIPS → `derive.collect(max_passes=1)` → expresión simbólica → `derive.emit()` → C++ → `cppcheck.build()` → DLL x64 `/O2` → comparación independiente con MIPS.

Los argumentos conservan imágenes u64; el arnés actual devuelve enteros s32. Se comparan retornos y efectos de memoria. Esto no demuestra todos los registros finales, HI/LO ni retornos completos de 64 bits. Los casos son sintéticos y acotados, no una prueba exhaustiva.

**12 funciones históricamente verificadas, 96 casos distintos por función/entrada:**

| Grupo | Direcciones | Casos |
|---|---|---:|
| Registros y ramas | `0x0013B740`, `0x0019B6B0`, `0x0013B6B0`, `0x003E97F0` | 32 |
| Lecturas sintéticas | `0x00121568`, `0x00134330`, `0x001A87C0` | 24 |
| batch-001 | `0x0026C920`, `0x002C34C8`, `0x002C34D8`, `0x003E9140`, `0x00237310` | 40 |

Las repeticiones de las mismas pruebas no aumentan este recuento histórico. Se verificaron negativos, límites, desbordamientos, ranuras de retardo y cero escrituras donde corresponde; las lecturas comprueban dirección, alineación, anchura y signo. Las comparaciones slt/movn incluyen mitades superiores de 64 bits.

Evidencia local, ignorada y no distribuida:

- `games/burnout3-pal/build/cpp-smoke/run-254444450cd743aeb343d5dc1fd7cbe8/results.json`: 32 casos, passed.
- `games/burnout3-pal/build/cpp-memory/run-88cfdf0c16ef42489ce495067913a0b9/results.json`: 24 casos, passed.
- `games/burnout3-pal/build/batches/batch-001/sequential-b7d736896d66479a9b5de6d2b5964495/supervisor.json`: cinco trabajadores, **5/5 funciones, 40/40 casos, 14.406 s**, passed; cero procesos activos tras limpieza. Contiene rutas a informes individuales.

## 5. Clasificador y lotes

[classify_functions.py](games/burnout3-pal/tools/classify_functions.py) inspecciona cuerpos y chunks con el decodificador R5900, sin ejecutar instrucciones. Produce `build/classification/functions.json`, `summary.json` y `batch_candidates.json`; reglas, SHA-256 y etiquetas solapadas. Rechaza sobrescrituras. La procedencia de punteros queda desconocida.

Inventario comprobado: nivel 1: 34; nivel 2: 256; nivel 3: 475; nivel 4: 1661; nivel 5: 6958. Total 9384. Nivel 1 es preselección estática, no equivalencia verificada.

[batch-001.json](games/burnout3-pal/tools/batches/batch-001.json) conserva los 40 casos originales exactos. [batch-002.json](games/burnout3-pal/tools/batches/batch-002.json) selecciona automáticamente diez funciones nuevas, excluyendo las doce verificadas; sus hashes de inputs coincidían con el inventario al prepararse.

Batch-002: `0x00103CC0`, `0x002372E8`, `0x0040B940`, `0x002C30F0`, `0x00400488`, `0x002149B0`, `0x002CECD0`, `0x0019E9D0`, `0x003EA890`, `0x003F6070`.

Tiene **80 casos SOLO validados estáticamente**. No se ejecutaron derivación, MIPS ni DLL para este lote. Cobertura pendiente; no hay oráculos manuales. Informe: `games/burnout3-pal/build/batches/batch-002/validate-0bb1ac734aad48f9932b714af66f7df1/results.json`, estado `validated_only`.

Al revisar esta fecha, batch-002.json es un archivo nuevo sin commit; no asumir que está en HEAD.

## 6. Ejecutor aislado y límites

[run_batch.py](games/burnout3-pal/tools/run_batch.py) conserva `--validate-only`. La ejecución dinámica admite únicamente las cinco funciones canónicas de batch-001, secuencialmente y con validación obligatoria en cada hijo. Rechaza explore, memoria sintética, escrituras, llamadas y VU para este recorrido.

Windows Jobs privados mediante ctypes: `CreateProcessW(CREATE_SUSPENDED)` → `AssignProcessToJobObject` → `ResumeThread`. `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, terminación del Job propio y `QueryInformationJobObject` para cero procesos activos. Sin atributos privados de subprocess, taskkill ni elevación. Registra APIs, errores Win32, PID, timeout y limpieza por separado.

Límites actuales: 60 s por trabajador y 600 s por lote, efectivos; **timeout independiente de compilación NO implementado**. La compilación queda dentro del plazo del trabajador/lote. La limpieza tiene esperas adicionales acotadas y puede extender el tiempo total más allá del instante del timeout.

Pruebas sintéticas locales de Jobs: salida normal 0.203 s; timeout individual de 2 s completado en 2.016 s; timeout global 2 s con límite individual 10 s completado en 2.031 s. Padre/hijo/nieto terminados sin elevación; cero activos y PID propios ausentes. La primera auditoría global tuvo un KeyError de formato al buscar timeout_scope agregado; se verificaron los PID después y se corrigió el agregado estáticamente antes de ejecutar batch-001.

Referencias: `build/batches/batch-001/job-audit-720da6feb3d64430b90ffbe133b3a73a/` y `sequential-762ba4413eed49ae8ff539cd6276e91d/supervisor.json`.

La ruta anterior taskkill falló dentro del sandbox; no confundir esa prueba fallida con la posterior validación de Jobs. Estados de fallo nunca son éxito. Rechazos, discrepancias, compilación y timeout individual permiten continuar; errores estructurales detienen el lote y conservan informes parciales.

## 7. Commits comprobados

| Commit | Cambio |
|---|---|
| `8e4c6adb4049072aa73a8ffbe2b69c5e8f7ecbe6` | Configuración PAL, depuración y regresión nativa inicial |
| `2a14a1024b426d365832dda0440f1673277fc999` | Regresión de lecturas de memoria |
| `5d768ca27e21d822d4fd8ce1012b403d138c8232` | Clasificador y validador de manifiestos |
| `b0c12f6ddbd6d9ba323ada93f71a6829568ce8ea` | Ejecutor aislado nativo |

Son commits locales comprobados mediante git log. No se ha hecho push durante este trabajo; no se presupone disponibilidad remota de estos commits.

## 8. Comandos confirmados

Desde la raíz, PowerShell. Los comandos dinámicos se documentan para uso futuro autorizado; redactar esta guía no los ejecuta.

```powershell
git branch --show-current
git status --short
git log -4 --oneline
$env:PYTHONDONTWRITEBYTECODE = '1'
py -3.12 games/burnout3-pal/tools/run_batch.py --manifest games/burnout3-pal/tools/batches/batch-001.json --validate-only
py -3.12 games/burnout3-pal/tools/run_batch.py --manifest games/burnout3-pal/tools/batches/batch-002.json --validate-only
```

Ejecutados anteriormente con éxito, crean artefactos nuevos:

```powershell
py -3.12 games/burnout3-pal/tools/test_native_cpp.py
py -3.12 games/burnout3-pal/tools/test_native_memory.py
py -3.12 games/burnout3-pal/tools/run_batch.py --manifest games/burnout3-pal/tools/batches/batch-001.json
```

También comprobados: `clean_database.py` y `classify_functions.py`; no repetir en sus directorios de salida existentes: rechazan sobrescrituras. No usar derive.py --limit como selección de funciones: solo limita filas del resumen. No ejecutar derive.main() para lotes aislados.

## 9. Pendientes e incertidumbres

- Revisar y versionar batch-002 cuando se autorice; ampliar runner explícitamente antes de ejecutarlo.
- Añadir contratos de cobertura de ramas/movz al esquema; actualmente esas obligaciones se anotan como pendientes en procedencia, no se certifican por validate-only.
- Implementar y probar timeout independiente de compilación.
- Soportar lectura/escritura sintética en runner general sin perder reinicialización, trazas ni aislamiento. La regresión de memoria independiente sí funciona.
- Investigar funciones recuperadas, HI/LO, llamadas, VU y estado global en pasos posteriores.
- Verificar integración PAL y español; aún no es un port PAL completo demostrado.
- Actualizar, con autorización separada, `tools/batches/README.md`: sigue describiendo el MVP antiguo sin ejecución dinámica. El código y los informes anteriores son la evidencia actual.
- Los informes build son locales y pueden faltar en otro checkout; nunca inventar evidencias para sustituirlos. Comprobar hashes tras cambios de inputs.

## 10. Retomar una sesión

Leer este documento y AGENTS.md; comprobar rama, HEAD y estado sin descartar cambios. Inspeccionar game.json y existencia de inputs propios, sin copiarlos a Git. Leer informes y distinguir passed de validated_only. Revisar alcance actual del runner y limitaciones antes de proponer el siguiente paso. Empezar por auditoría estática o validate-only; ejecutar solo el lote autorizado. Conservar fallos y artefactos, no reparar automáticamente ni ampliar alcance. No hacer commits o push sin autorización explícita.
