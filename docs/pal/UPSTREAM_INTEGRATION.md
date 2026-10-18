# Sincronización USA / PAL

## Primera integración: 2026-10-18

- Base PAL real: `13f42950282d6167707d6ae1bbf9943f02098248`, descendiente del checkpoint jugable `d47b1328ffb0f301268247ae12ca0fffaa146187`.
- Ancestro común USA: `22524a3df8b763451760bdc1f45d458931c08033`.
- USA: `5832c4a35a09b4e2fcb130a4e674ebfd5386cd2f`.
- Motor del gitlink USA: `d7e6e023f261d9f95ad097fcd063ec95e40ca465`.
- Rama: `integration/upstream-20261018`; worktree independiente del estable.
- Único conflicto Git: README raíz. Se conserva identidad PAL y referencia USA.

Se integran 40 commits USA y 35 del motor respecto a la base, mediante merge
con sus padres, no una copia de archivos. El código NTSC-U queda como upstream
lo proporciona; no se enlaza al ejecutable PAL ni se aplican parches SLUS al SLES.

| Grupo | Alcance |
|---|---|
| Compartido actualizado | Hilo renderer GPU, caché/pipelines, transferencias GS, shaders, VU exacto, SPU2, IOP y servicios del host. |
| Conservado PAL | Dispatch europeo, fragmentos descubiertos, extensión libmc2, KernelStore, Logitech, RWA/GTFS, siete handlers mc2 y SkipMovie. |
| Pendiente PAL | PC SETTINGS dentro del juego, controles configurables y prompts, HUD/editor, parches 60 FPS, mods, texturas y LAN. |

F9 y controles/aspecto/resolución compartidos no equivalen a adaptar todos los
hooks USA. Frame Pacing On sigue siendo referencia explícita. Native Blocks,
Frame Skip y clang-cl del PR abierto no forman parte de esta revisión master.

## Reconstrucción experimental

Desde el worktree, con MSVC, CMake, Ninja y Python 3.12:

```powershell
py -3.12 -B games/burnout3-pal/tools/sync_native.py --prepare RUTA_CHECKOUT_ESTABLE --build-root RUTA_BUILDS_PRIVADOS
py -3.12 -B games/burnout3-pal/tools/sync_native.py --execute RUTA_WORK_JSON_DEVUELTA
```

El adaptador verifica artefactos locales, referencia las fuentes EE preservadas
y su unidad libmc2 completada, y copia el despacho/fragmentos ya generados a un
directorio ignorado. No traduce otra vez EE. Regenera VU con el compilador actual
y recompila C++/runtime en un árbol nuevo, sin reutilizar objetos o librerías
antiguos. PS2_PAL_SOURCES_FILE permite seleccionar la unidad completada sin
editar traducciones históricas. Las dependencias reutilizan fuentes de la caché
local; comandos, hashes, logs e informes quedan en build/pal-upstream-*/.

Captura original en el checkout estable:
`games/burnout3-pal/build/vu-captures/run-1d27d7abf9a5/vu_programs.txt`.
97 programas: 24 VU0 y 73 VU1. Nunca incorporar su contenido a Git.

## Próximas sincronizaciones

1. Conservar checkpoint, binario, configuración, captura VU y tarjeta; revisar status.
2. Fetch de upstream/master; revisar commits desde el último merge y su gitlink.
3. Crear rama/worktree desde la integración revisada y hacer merge de upstream.
4. Usar exactamente el motor del gitlink en el checkout experimental.
5. Revisar ABI/generadores; regenerar solo lo indispensable y no mezclar objetos incompatibles.
6. Tests PAL/host, un arranque corto y capturas; carreras/guardado manuales aislados después.
7. Registrar cambios revisados; promover a estable solo tras aprobación.

No hacer pull sobre estable ni sustituir su engine. Más funciones ejecutadas
no demuestran equivalencia. Un timeout controlado no es una ejecución completa.
No publicar los artefactos del usuario.

## Módulos concretos pendientes (no activados en PAL)

| Módulo | Referencia USA | Requisito PAL |
|---|---|---|
| PC SETTINGS | port/overrides.cpp | Identificar objetos, constructores, callbacks y textos PAL; mantener idioma/50–60 Hz. |
| Controles y símbolos | port/controls.cpp, prompts.cpp | Mapear contexto menú/carrera y recursos; conservar Triángulo/Start y mando funcionales. |
| HUD/editor | port/widescreen.cpp | Verificar builders, estructuras y offsets europeos; no sustituir el 16:9 funcional. |
| 60 FPS | port/sixty_fps.cpp | Auditar temporización y funciones PAL; 60 Hz seleccionado no demuestra estos parches. |
| Mods | port/mods.cpp, port/tools/lift_mods.py | Parches SLUS no aplicables automáticamente a SLES; validar instrucciones por variante. |
| Texturas | engine/host/src/texture_pack.cpp y menú USA | El soporte compartido existe; comprobar identificación/recursos PAL sin distribuir contenido del disco. |
| LAN | port/lan.cpp, lan_lobby.cpp, lan_libnet.cpp | Traducir contratos/direcciones y frontend PAL; bibliotecas de red compartidas no bastan. |

Antes de cualquiera de estos módulos, validar manualmente carrera y recarga de
perfil con la base experimental. No encadenar adaptaciones después del primer arranque.

## Integration validation

Local merge: `b1b8858e8d49b149b3bd3a4353dafd4f94d71e10`.
PAL adaptations and documentation remain pending review and commit.

A separate Release tree was built with two jobs. Seventeen PAL tests, host/Vulkan
tests and a 582-state synthetic VU differential passed, including missing/stale
program fallback. This does not validate the complete PAL execution.

The short startup retained Spanish, RWA and memory-card access, but frame 2400
was black where the stable reference displayed the Spanish 50/60 Hz screen.
Rendering versus capture has not been isolated. Do not promote the new engine
or claim functional parity. Manual racing, audio and profile reload validation
remain pending for this integration.

Detailed logs, RAM, screenshots and executable hashes remain in ignored local
storage, outside the public documentation.

`--build-root` is optional and only accepted with `--prepare`. Without it, output
remains under `build/pal-upstream-*`. `--execute` uses paths stored in work.json.
AutoTest still uses its historical build-selection contract and does not directly
consume this integration build format. Never move an already configured CMake/Ninja tree.

External `--build-root` passed nine isolated tests with simulated generation and
execution. No real CMake configuration or compilation using the external root
has been performed; the earlier successful build used the historical location.
