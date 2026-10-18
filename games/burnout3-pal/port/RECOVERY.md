# Recuperar el build PAL funcional

Estado comunicado por el usuario: carrera completada, perfil guardado y cargado,
Español/60 Hz, audio, VU compilado y salto manual de FMV con Start funcionales.
No equivale a verificar todas las funciones ni todos los modos del juego.

## Artefactos locales necesarios

No están en Git y deben conservarse en disco. Rutas relativas al repositorio:

- Captura original: `games/burnout3-pal/build/vu-captures/run-1d27d7abf9a5/vu_programs.txt`.
- SHA-256: `deff4afda9b0d277532c222ff8e56919d5829743a850122c0ef1d10cf98d1a82`.
- 97 programas: 24 VU0, 73 VU1; 17.008 pares. La captura no se mueve ni modifica.
- Build VU de referencia: `games/burnout3-pal/build/native-relocated/run-180cb57a8a54/`.
  Contiene copia del manifiesto, 61 unidades/objetos VU, `work.json`,
  `build.json` y `differential3.json` (582 estados sintéticos, cero diferencias).
- Build con SkipMovie: `games/burnout3-pal/build/native-relocated/run-75fc2363eedf/`.
- Árbol original MSVC/Ninja y librerías host:
  `games/burnout3-pal/build/native-bootstrap/run-15c1a6f3206b/`.
- Fuentes EE publicadas:
  `games/burnout3-pal/build/bulk/run-6785cb70ebf8/published/lifted/`.
- ELF, base y disco propios del usuario: rutas resueltas mediante `game.json`.
- Tarjeta persistente: `games/burnout3-pal/memcards/manual-playtest/`.

Conservar también las dependencias referenciadas por los hashes de `work.json`:
un checkout de Git por sí solo no contiene el juego ni basta para reconstruirlo.
No limpiar build ni editar los informes históricos. Las rutas absolutas de esos
informes describen esta máquina; migrar de equipo exige regenerar configuración.

## Reconstrucción incremental sin recompilar EE/VU

Desde la raíz, con Python 3.12, MSVC y Ninja disponibles:

```powershell
py -3.12 -B games/burnout3-pal/tools/relocate_native.py --reuse-build games/burnout3-pal/build/native-relocated/run-180cb57a8a54/build.json
```

Valida el build de referencia y los objetos VU contra su evidencia diferencial,
crea un directorio exclusivo, compila únicamente los overrides PAL actuales,
reenlaza y ejecuta los tests del host con TEMP exclusivo. Esta receta ya produjo
el build con SkipMovie; la auditoría de recuperación volvió a preparar y validar
sus dos comandos y todos los hashes, sin ejecutar otra compilación.
`--reuse-build` requiere el build VU con evidencia diferencial directa indicado;
no admite indistintamente cualquier build hijo.

## Entrada del generador VU, si fuera necesario regenerar sus objetos

No ejecutado durante la recuperación. La ruta completa del adaptador conserva
las 9.383 fuentes EE publicadas, los descubrimientos y la extensión del trabajador:

```powershell
py -3.12 -B games/burnout3-pal/tools/relocate_native.py --base-build games/burnout3-pal/build/native-bootstrap/run-15c1a6f3206b --discoveries games/burnout3-pal/build/native-probes/run-2b949b096dfe/discovered.txt --discoveries games/burnout3-pal/build/autotest/run-ab60e1786872/probe/discovered.txt --complete-function 00242A50:00242D50 --vu-manifest games/burnout3-pal/build/vu-captures/run-1d27d7abf9a5/vu_programs.txt
```

El adaptador copia/pinea el manifiesto en el nuevo `build/vu_programs.txt` y
`port.generate()` llama al `vurecomp` original. Los bloques y el registro VU se
compilan y enlazan, secuencialmente. No es una regeneración masiva EE, pero sí
vuelve a emitir la extensión libmc2 y los descubrimientos acotados. Los programas
ausentes o modificados conservan el fallback interpretado. No se usa VU-fast.

## Lanzamiento y regreso a la referencia

- `Jugar Burnout 3 PAL FMV.bat`: binario con SkipMovie fijado.
- `Jugar Burnout 3 PAL VU compilado.bat`: referencia VU anterior fijada.
- `Jugar Burnout 3 PAL anterior.bat`: referencia previa sin VU compilado.
- `Jugar Burnout 3 PAL.bat`: selecciona el último build confirmado.
- `--comprobar`: valida rutas/evidencia sin iniciar el juego.

## Motor actualizado experimental

Las recetas anteriores recuperan la referencia estable v0.9.0. No reutilizar
sus objetos VU/runtime con el motor nuevo d7e6e023: cambian ABI/helpers VU.
La rama integration/upstream-20261018 usa tools/sync_native.py y un árbol CMake
nuevo; referencia las fuentes preservadas y regenera VU desde la captura original.
Procedimiento/resultados: docs/pal/UPSTREAM_INTEGRATION.md desde la raíz.
Build experimental compilado y tests pasados, pero captura negra pendiente de
aislar: no sustituir launchers ni ejecutable estables todavía.

No se versionan microcódigo, fuentes recompiladas, binarios, perfiles ni informes.
Consultar [PERFORMANCE_AUDIT.md](PERFORMANCE_AUDIT.md) para la medición pendiente.
