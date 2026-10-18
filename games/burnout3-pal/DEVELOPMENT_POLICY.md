# Política permanente de desarrollo PAL eficiente

Leer con AGENTS.md raíz, AGENTS.md PAL y PAL_DEVELOPMENT.md. Codex carga las
instrucciones AGENTS.md según el directorio; la referencia desde el raíz hace
visible esta política también cuando se trabaja desde la raíz del repositorio.
El estado ejecutable se comprueba en manifiestos e informes locales: no se infiere
de documentación histórica ni de recuentos de funciones.

## Orden obligatorio ante cada bloqueo

1. AutoTest: leer summary.json y localizar el primer bloqueo reproducible.
2. NTSC-U: usar rg/git grep en games/burnout3/port, docs/FINDINGS.md, config y
   port/runs; consultar port/ADAPTATIONS.md PAL para evitar repetir análisis.
3. Motor: revisar engine/host, runtime, tools y docs/PORTING.md. Reutilizar sus
   mecanismos existentes antes de implementar otro traductor, supervisor o runner.
4. Diagnóstico: acreditar instrucciones, argumentos, estructuras, llamadas,
   retornos y efectos observables. Direcciones/tamaños iguales no prueban identidad.
5. Corrección: mínima, preferiblemente PAL; separar servicios necesarios de mejoras
   PC opcionales. Nunca instrucciones ficticias, no-ops o respuestas falsas.
6. Pruebas: primero unitarias específicas; compilar incrementalmente solo si hay
   cambios que lo justifiquen; después AutoTest QUICK cuando corresponda. Ante
   integridad incorrecta, regresión o fallo inesperado, preservar evidencia y parar.
7. Registro: actualizar ADAPTATIONS.md solo con hallazgos comprobados y enlaces a
   informes. Indicar problema PAL, solución NTSC-U (o ausencia), equivalencias y
   diferencias, corrección, tests, limitaciones y único siguiente bloqueo/acción.

## Recursos y autonomía local

Usar AutoTest-PAL.bat y tools/autotest.py para trabajos autosuficientes con informes.
Leer resúmenes JSON y fragmentos pertinentes; no supervisar continuamente CPU,
RAM o porcentajes ni consultar repetitivamente logs completos. Mantener un máximo
inicial de tres trabajos MSVC/Ninja; el adaptador actual compila secuencialmente.
No confundir reutilización de objetos inmutables con un árbol CMake compatible;
validar hashes, generador, parámetros y rutas antes de reutilizarlo.

No repetir las 9383 traducciones, los 37 lotes ni las 2923 verificaciones pendientes
sin razón concreta y alcance explícito. Estos recuentos describen el estado de
origen, no son límites permanentes. Evitar cadenas de reparación/compilación: un
cambio acotado, pruebas, como máximo una compilación incremental y un QUICK salvo
necesidad técnica justificada. Detenerse ante la siguiente corrección importante.
No usar agentes paralelos para tareas pequeñas, búsquedas externas si basta el
código local, ni instalar dependencias sin necesidad justificada.

## Integridad

Conservar traducciones, ejecutables, logs y resultados previos en directorios
exclusivos bajo build. Nunca promover NOT VERIFIED por compilación o avance del
arranque. La cuenta de funciones ejecutadas mide avance, no equivalencia ni menú.
Mantener supervisión de procesos y timeouts existentes, TEMP exclusivo cuando
corresponda y separación de fallos/evidencia de limpieza.

No modificar NTSC-U, actualizar engine o cambiar runtime compartido sin autorización
posterior específica. No alterar contenidos extraídos ni versionar sus derivados.
No hacer staging, commit ni push sin autorización expresa. La eficiencia reduce
supervisión y trabajo duplicado, no la calidad de validación.

## Sincronización con upstream USA

La estrategia acordada permite integrar el historial USA mediante merge en una
rama y worktree experimentales, usando el motor coherente de su gitlink. No
mezclar arbitrariamente revisiones ni mantener indefinidamente backports aislados.
La autorización de integración no autoriza cambiar el checkout estable, publicar
artefactos, hacer push o activar parches SLUS sobre PAL. Revisar cambios de ABI,
recompilar objetos incompatibles y conservar pruebas regionales. Promover a
estable exige revisión. Procedimiento y alcance: docs/pal/UPSTREAM_INTEGRATION.md
desde la raíz; hallazgos del arranque: docs/pal/BOOT_FLOW.md.

Resource update authorized by the maintainer: subsequent Ninja/CMake builds
may use up to `-j 3`. Reduce automatically to `-j 2` when excessive memory
pressure, intensive paging or allocation errors are detected. Do not restart
an active build to change this limit or launch extra builds just to test it.
Test parallelism and all other restrictions remain unchanged.
