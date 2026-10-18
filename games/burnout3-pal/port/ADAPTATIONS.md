# PAL: adaptaciones NTSC-U y AutoTest

Auditoría local del motor 28057a1 (v0.9.0). No se ha actualizado el motor ni
modificado games/burnout3. Los registros bajo build son la evidencia de ejecución.
No se ha alcanzado el menú. La documentación histórica PAL aún describe etapas
anteriores; este documento no modifica sus recuentos de regresión.

| Prioridad / adaptación | Origen NTSC-U | PAL / evidencia | Dependencias y pruebas pendientes | Estado |
|---|---|---|---|---|
| P0 KernelStore | port/overrides.cpp, 001156C0 | 001156C0: única escritura sw a1,0(a0), ErrorEPC recibe ra; setters 00115730/40/50 preparan B0001000/1010/1020 en delay slots | generated::Write32, memoria/dispositivos compartidos. No hay ELF/ISO NTSC-U local para comparar bytes; equivalencia del contrato de host, no de todos los registros COP0 | Integrada; seis casos sintéticos y arranque real por los tres setters |
| P0 registro | RegisterGameOverrides, host/src/main.cpp | El host registra despacho, overlays y después overrides antes de Main | API compartida ps2host::Override; no requiere copiar direcciones | Existente |
| P0 TtyWrite | port/overrides.cpp, 0010D3E8 | Candidata 0010D3E8 identificada por argumentos buffer/longitud y llamadas 0010CFE0/0010D010 a 0010C250 (envoltorios DECI2); no basta igualdad de dirección | Confirmar servicio 7C, estado de escritura, retorno y memoria. El host ya trata Deci2Call | Candidata, sin integrar |
| P0 relocalización | engine/tools/port.py, discovered_pairs/functions | 004854E0 -> 80076000; 56 bytes de entrada, bloque copiado 740 hex; dumps idénticos al ELF | relocate_native.py conserva objetos. Nueva copia 004846A8 -> 80075000, tamaño registrado 330 hex, pendiente de delimitar/traducir | Primera integrada, NOT VERIFIED; segunda pendiente |
| P1 RWA | NTSC-U iop_servers.cpp RunModule | PAL versi?n0115, SIF0/1 y handshake9; _start real en18C84, ocho hilos IOP observados | Ocho imports ausentes conservan trampas; ninguno invocado en esta prueba. No acredita audio funcional ni cobertura total | Registro integrado y QUICK avanza1297 funciones; handshake inferido por paso a fase2 |
| P1 GTFSCDVD | iop_servers.cpp, RPC 00475453 | IOP/GTFSCDVD.IRX PAL, 13056 bytes, versión 0101 | iop_vm/cdvdman/disco; comparar requests EE | Pendiente |
| P1 B3ROUTE | iop_servers.cpp, RPC 00150276 | IOP/B3ROUTE.IRX PAL, 8520 bytes, campo versión 0000 | iop_vm, verificar protocolo EE | Pendiente |
| P1 Logitech | iop_servers.cpp, RPC 046D046D, EE lgDevInit 004438E0 | PAL lgDevInit 00443D80: procedimiento 12, buffer 240 hex, versi?n 010B1B00 en +4; Enumerate 00444168 usa procedimiento 1 y retorno en +0 | Registro antes de Main; sin dispositivo devuelve 80000001; procedimientos no acreditados fallan expl?citamente | Integrados 12/1; cinco tests nativos sint?ticos; QUICK supera el bloqueo |
| P1 SDK gráfico | engine/host/src/sdk.cpp | Solo sceGsSyncPath se registra por nombre en esta revisión; config/names.csv PAL ausente | Identificar PAL por instrucciones/referencias, generar nombres trazables. No copiar 00102EA0 NTSC-U | Pendiente |
| P1 kernel/servicios | engine/host/src/kernel.cpp, iop.cpp, devices.cpp, disc.cpp | Memoria KSEG, hilos, interrupciones, semáforos, temporizadores, alarmas, SIF, DMA/disco ya compartidos | Su existencia no demuestra compatibilidad completa PAL. Kernel conserva alarmas al rechazar parches FC/FD/FE/FF explícitamente | Existentes; integración parcial observada |
| P2 libmc2 | overrides.cpp: GetInfo 00244218, Format 002445D0, Read 00246588, Write 00246788, Create 00245FC0, GetDir 002470D8, Lookup 00245398 | Sin equivalentes PAL acreditados | ps2host/mc2.h, CardOr delega al juego sin tarjeta; identificar siete handlers y contratos, después probar guardado | Pendiente, sin integrar |
| P2 esperas RW/DMA | AddIdleLoop 001F24E0; WaitsForInterrupt 001F2408 | Sin equivalentes PAL acreditados | Scheduler/interrupts, verificar bucles y callbacks al alcanzarlos | Pendiente |
| P3 opciones PC | widescreen, controls, mouse, prompts, SkipMovie 001313C0, FastLoading 0019A950/001322B0, PostEffects 002FA9A0 y menús | No portadas | Dependencias regionales a traducir con evidencia; no son diferencias PAL/NTSC del juego original | No aplicables al hito actual |

Los módulos PAL se inspeccionaron directamente mediante Iso9660 y ElfFile del
motor, sin extraer ni versionar IRX. Los SHA-256 se guardan bajo build.
No se ha demostrado identidad binaria con módulos estadounidenses ausentes.

## KernelStore

Se reutiliza el contrato del override estadounidense: escritura de los 32 bits
bajos de a1 a la dirección de 32 bits a0. El host devuelve al llamador nativo y
no tiene modos COP0. No se implementa eret ni se modifica el traductor. Los
registros temporales v0/v1/a2 que el original puede alterar no forman parte del
resultado del contrato; el override conserva todo Context. No se acredita
igualdad de estado CP0 ni equivalencia dinámica MIPS/C++ de esta rutina.

Las pruebas compilan el overrides.cpp real contra callbacks sintéticos. Verifican
las direcciones, valor, anchura, contexto completo y propagación de una excepción
del callback. No son una prueba del hardware de temporizador: esa integración se
observa en el arranque y se complementa con la suite original del host.

## AutoTest

Desde la raíz del repositorio, PowerShell:

```powershell
py -3.12 -B games/burnout3-pal/tools/autotest.py --mode QUICK --timeout 3
py -3.12 -B games/burnout3-pal/tools/autotest.py --mode FULL --timeout 3
.\AutoTest-PAL.bat --mode QUICK --timeout 3
```

QUICK selecciona el último build confirmado de native-relocated por build.json,
work.json, hashes de binario/objetos, comandos y suite host más containment limpio.
Puede seleccionarse --build-manifest explícitamente. Un cambio de override no
borra el valor histórico del build, pero se informa como inputs no actuales y FULL
lo recompila. Otros cambios de artefactos obligatorios rechazan la selección.
No se elige un exe suelto por fecha. El adaptador mínimo admite los manifiestos
nativos relocalizados actuales; futuros formatos necesitan un adaptador explícito.

FULL ejecuta tests propios, de relocalización y del override. Si los hashes
siguen vigentes, la compilación es no-op. Si cambian, reutiliza las órdenes del
grafo CMake/Ninja original y sus objetos inmutables mediante relocate_native.py;
solo compila unidades cambiadas, secuencialmente (un trabajo, por debajo del
máximo dos). No configura otro árbol ni reconstruye las 9383 traducciones.
Los cambios del runtime o del inventario que invaliden objetos se rechazan;
AutoTest no intenta repararlos. No hay ciclos de corrección automática.

Cada ejecución crea build/autotest/run-*/ con logs separados, informes JSON/MD,
codex-next-task.md, manifiesto y subdirectorios de tests/build/probe. Los procesos
usan el Job privado del runner y TEMP exclusivo. El timeout es 1..60 segundos,
con límite exterior adicional de limpieza/supervisión. Un bloqueo devuelve código
no cero; status=diagnosed solo significa que se recogió el diagnóstico.

Una línea base posterior se reutiliza únicamente con hashes de logs/artefactos y
containment válido, ELF y configuración compatibles. Menos funciones ejecutadas
es una posible regresión, no prueba de equivalencia. El primer FULL no disponía
de línea base AutoTest; la comparación 21 -> 43 se hizo con el informe histórico.
QUICK se probó sintéticamente; FULL realizó el arranque real de esta sesión.

Las ejecuciones originales port/runs/*.args y engine/tools/regress.py ya ofrecen
entradas por frame, --virtual-time y CRCs gráficos/tarjeta. Se reutilizarán cuando
PAL dibuje; no copiar tiempos ni CRCs NTSC-U como expectativas PAL. regress.py
puede escribir expectativas si faltan: no ejecutar aún contra PAL sin preparar
las rutas y la política de creación explícitamente.

El fallo host 0xC0000409 permanece sin causa identificada. Se conservan el log y
minidump anteriores; en esta sesión la suite pasó con TEMP exclusivo una vez.
Esto no prueba que el incidente esté resuelto.

## Política persistente y resolver TLB PAL

Procedimiento obligatorio: ../DEVELOPMENT_POLICY.md, enlazado desde los AGENTS.md
raíz y PAL. Consultar primero soluciones NTSC-U y motor; registrar únicamente
hallazgos comprobados, pruebas, limitaciones y siguiente bloqueo.

Problema: el arranque pedía 5B(selector=55) al resolver 80075000, aún ausente del
despacho. NTSC-U FINDINGS documenta la resolución de extensiones copiadas mediante
el descubrimiento del host y port.py. No hay override TLB específico comprobado.

Evidencia PAL: captura completa en build/tlb-diagnostics/run-c0c9f8b9310b;
816 bytes 004846A8..004849D8 -> 80075000..80075330, idénticos al ELF. SHA-256:
3812e2924e67b94a294d790deee44b51e4ee7b6eb2bb9aa9899380ce006cd5a2.
La entrada ordinaria termina en 80075038; lee seis pares desde 80075300 y devuelve
puntero o cero. Selector 3 devuelve datos _kExecArg, no una función. No tiene jal,
COP0 ni instrucciones TLB. Las demás entradas permanecen fuera de la selección.

Corrección: reutilizar port.discovered_functions/generate con par
80075000/004846A8, conservando 80076000/004854E0. relocate_native.py ahora reconoce
y fija hashes de stdout.log y stderr.log de AutoTest, además de host.log del probe
anterior; no se copian ni renombran logs históricos. No se recorre la tabla de
punteros para promover código privilegiado automáticamente.

Pruebas específicas: 13 tests de relocalización (incluyen streams separados y
no promoción de datos) y 17 AutoTest. Nueve casos sobre el resolver original en
el intérprete (55..59,3,0,54,FFFFFFFF), con map_faults=False: retornos correctos,
sin escrituras ni llamadas stub. Esto no acredita comparación C++/MIPS completa;
la traducción descubierta conserva NOT VERIFIED. Resultados nativos y siguiente
bloqueo se guardan en los manifiestos exclusivos de build y AutoTest.

Resultado de integración del resolver: una compilación incremental
build/native-relocated/run-194269489fb6, cinco unidades pequeñas más enlace,
reutilizando todos los objetos originales; suite host passed con TEMP exclusivo.
El resolver sigue NOT VERIFIED para equivalencia C++/MIPS completa. Nueve casos
MIPS aislados pasaron; 13 tests de relocalización y 18 de AutoTest pasaron.

QUICK build/autotest/run-69074dd872f5 avanzó de 43 a 964 funciones distintas,
361 primitivas GS y 4045156 píxeles escritos, VU cero. No se acredita menú ni
imagen presentada. Resolver devolvió las direcciones 55..59 y selector 3; no hubo
invocación real de servicios 55..59. Los Jobs terminaron limpios, sin timeout
exterior. El host alcanzó su límite de 3 segundos en 00443F30.

Siguiente problema comprobado: PAL lgDevInit, entrada 00443D80, servidor RPC
046D046D, procedimiento 12. El host registra "no host implementation: answered
with zeros". La cadena PAL 004D2A00 dice "wrong version of lgDev.irx" y 004D29E0
"system halted"; el flujo culmina en el bucle 00443F30. NTSC-U iop_servers.cpp
ya implementa versión 010B1B00 y enumeración sin dispositivo. Es candidato a
reutilización tras comprobar el contrato PAL; no se ha integrado ni supuesto
compatibilidad de otros procedimientos. Es la única próxima corrección recomendada.

El summary histórico conservó category=unknown para "time limit". Se corrigió
solo el clasificador y se añadió una prueba; la reinterpretación se guarda aparte
en resolver-integration.json del build, sin sobrescribir el informe ni ejecutar
otro QUICK. La incidencia host C0000409 sigue pendiente; no reapareció esta vez.

## Logitech PAL: integración de los procedimientos 12 y 1

Contrato acreditado con instrucciones PAL: lgDevInit 00443D80 registra/bindea
046D046D (00443DE0..00443DF8). En 00443E8C selecciona procedimiento 12, tamaños
send/recv 240 hex y buffer compartido (00443E94..00443EA8). Lee respuesta +4
(00443EF0) y compara 010B1B00 (00443EEC..00443EF8).
Enumerate 00444168 selecciona procedimiento 1 (004441C0), tamaños 240 hex y
coloca índice en +4 (004441DC); retorna respuesta +0 (00444224). Los llamadores
00442AD8..00442AE0 y 00442C64..00442C6C comprueban el bit 31 para terminar.

Se reutiliza LgDev de games/burnout3/port/iop_servers.cpp en PAL overrides.cpp,
sin importar direcciones NTSC-U, otros módulos IOP ni mejoras PC. Mismos valores,
respuesta de max(8,request.size()) y ceros en bytes restantes. Diferencia deliberada:
procedimientos distintos de 12/1 producen Unimplemented, evitando copiar el
fallback estadounidense de respuestas cero sin contrato comprobado.

Registro en RegisterGameOverrides, antes de ps2host::Main y de la carga/ejecución
EE (host/src/main.cpp). Servers() de iop.cpp persiste, no se borra al arrancar.
No existe otro registro PAL/engine para este SID. Un servidor ejecutado en la VM
IOP tendría precedencia; PAL no ha activado LGDEVW.IRX en la VM. Esto implementa
el protocolo de un dispositivo ausente, no el módulo IRX completo.

Tests: test_logitech.py compila el override real una vez y verifica registro,
versión/offset, tamaño/padding para seis longitudes, enumeración de cuatro índices,
request sin cambios y rechazo de procedimiento desconocido (cinco pruebas).
KernelStore conserva seis casos; su test pasa. 18 pruebas AutoTest pasan; FULL
incluye también test_logitech.py en futuras ejecuciones. Callbacks de registro y
memoria son sintéticos en los tests; la integración SIF se comprueba en QUICK.

Una compilación incremental: build/native-relocated/run-1d102d4496ac; suite host
passed con TEMP exclusivo. QUICK: build/autotest/run-c7c27960ba60, 964 -> 1095
funciones, timeout 3 segundos en 0010C2F0; Jobs limpios. GS: 361 primitivas,
4045156 píxeles; VU cero. No se acredita menú. Logitech se bindea sin el aviso de
handler ausente y el arranque supera lgDevInit/Enumerate; no se repitió QUICK.

Siguiente incidencia: espera D_STAT 1000F000 & 4 en 0010C2F0, llamada desde
00102E08 -> 0010C2B0. NTSC-U names.csv identifica sceGsSyncPath en 00102EA0;
FINDINGS documenta su override por nombre en engine/host/src/sdk.cpp. La entrada
PAL 00102E08 es candidata, pendiente de acreditar el contrato completo antes de
activar la solución del motor. No se aplicó esa segunda corrección.

Resumen trazable: build/native-relocated/run-1d102d4496ac/logitech-integration.json.
La incidencia histórica host C0000409 sigue abierta; no reapareció en esta suite.

## Correcci?n de identidad SDK: candidata rechazada

La hip?tesis anterior sobre 00102E08 se refuta por instrucciones y por el
analizador original naming.diagnostic_names. PAL sceGsSyncPath est? en 00102EA0,
788 bytes hasta 001031B4: referencia 00103004 a la cadena 004B1C38, espera DMA
Ch1/Ch2 y estados VIF/GIF/VU. NTSC-U names.csv identifica tambi?n 00102EA0;
functions.csv distingue 00102E08. No se dispone de ELF NTSC-U para comparar bytes.

00102E08 tiene 148 bytes hasta 00102E9C, usa contexto de 00102510 y no consume
los argumentos mode/timeout del contrato SDK. Llama 0010C2B0 en 00102E2C; este
helper espera D_STAT 1000F000 bit 4 en 0010C2F0. La candidata puede devolver el
bit 13 del CSR GS 12001000. Forzar el retorno cero del override GsSyncPath no
preserva ese contrato. El nombre real de 00102E08 no se declara demostrado.

No se cambi? names.csv, despacho ni runtime; no se compil? ni repiti? QUICK.
La l?nea base v?lida sigue en 1095 funciones. La comprobaci?n est?tica del
analizador original identifica 00102EA0 y rechaza la identidad propuesta para
00102E08; no se presenta como equivalencia din?mica.

Informe: build/sdk-diagnostics/run-2a2ed12a01e9/audit.json

Siguiente acci?n ?nica: investigar el contrato de la espera 00102E08/0010C2B0
y su interacci?n D_STAT/interrupts, contrastando primero la rutina NTSC-U
separada y las soluciones DMA/vblank existentes. No activar overrides por
coincidencia de direcci?n ni modificar manualmente el bit de estado.

## Diagnóstico posterior: INTC/VBLANK, no bloqueo DMA demostrado

Corrige la denominación D_STAT usada en notas anteriores: 1000F000 es INTC_STAT;
D_STAT es 1000E010. threads.cpp define VBLANK_START=2 y DeliverFrame publica ese
bit. 0010C2B0 borra INTC_STAT & 4, espera el siguiente VBLANK_START y vuelve a
borrarlo; 00102E08 obtiene después el campo GS si su contexto lo pide. No se
acredita identidad binaria NTSC-U sin su ELF, ni se renombra como sceGsSyncPath.

Captura limitada sin trace de llamadas, con watch-write para GIF_CHCR/D_STAT/
INTC_STAT y frame-times: build/interrupt-diagnostics/run-0d7642a50949/diagnosis.json.
594 frames y 1177 escrituras de borrado INTC, incluidas parejas de entrada/salida
de 0010C2B0. Se completan repetidas esperas; el timeout es una muestra en la espera
actual, no prueba de un bucle único infinito. GIF_CHCR final 70000004 (STR=0),
D_STAT final 00000106 (bit 4 activo). Los accesos generados a ventana 1 pasan por
PortRead/PortWrite y hooks; no se demuestra bypass de registros.

El llamador superior 002AF6E0 espera flags de inicialización de audio y llama la
espera de frame en bucles acotados a 600 iteraciones (002AF7C0..002AF7EC y
002AF87C..002AF8A8). La captura no leyó los valores exactos de esos flags ni
distingue con certeza cuál de las dos fases estaba pendiente. El módulo RWA.IRX
se registra como cargado, pero PAL no lo opta a ejecución en la VM y no aparecen
hilos IOP. NTSC-U FINDINGS documenta la negociación RenderWare Audio por comandos
SIF crudos y su solución: iop::RunModule("RWA.IRX") desde iop_servers.cpp.

No se justifica modificar DMA ni engine. Siguiente acción única: contrastar el
contrato PAL RWA de inicialización con NTSC-U para preparar la activación del
módulo mediante el mecanismo IOP existente. No se implementó esa corrección ni
se compiló. Captura terminó con salida 2 y limpieza de Job correcta.

## RWA PAL: contrato e incompatibilidad de importaciones

Auditoría: build/rwa-diagnostics/run-97f3dbc67ba1/audit.json. ISO inspeccionada con
Iso9660 y ELF con ElfFile, sin extraer ni distribuir IRX. SHA-256 RWA PAL:
00094cd1e87e9991b0d842367be842027edf81ec00fdfe62963627dc87a6a76a.
64588 bytes, módulo renderware_audio_spu2, versión 0115. No existe IRX/ELF NTSC-U
local para acreditar igualdad binaria; la comparación usa FINDINGS y código host.

Inicialización EE 002AF6E0; envío 002AF030 por comando SIF usuario 0 con registros
30 hex y tipo en +10 hex. Recepción comando 1: callback 002AF3B0 copia 30 hex a
un ring de 18 hex entradas; thread 002AF310 despacha mediante 002AF430.
Tabla 004C2070: tipo 9 -> 002AF46C, lee +24 hex y escribe gp-5A38; el inicializador
espera ese flag y exige 0115 (002AF844). Tipo 10 -> 002AF478, escribe gp-5A40,
suma 400 hex para gp-5A3C y toma dato +28 para gp-5980. La segunda espera consulta
gp-5A40. Ambos bucles esperan como máximo 600 iteraciones con VBLANK. Esto
coincide con los comandos, tamaños y versión documentados para NTSC-U; no se
ha observado aún respuesta dinámica PAL ni cambio de flag.

El segmento PT_LOAD y tipos de relocalización 2/4/5/6 son admitidos por la VM.
La inspección del segmento encontró 80 stubs de importación. Ocho no están en
la tabla que consulta FindLibrary: ioman #4,#5,#6,#7,#8 y sysclib #28(strncat),
#34(strstr),#35(strtok). iop_exports.csv solo ofrece nombres, no implementaciones.
LoadModule conserva esos stubs como trampas que llaman Stop si se usan; informar
carga exitosa pese a esto no demostraría compatibilidad. La alcanzabilidad de
estos ocho imports durante _start/handshake no se ha comprobado.

Por la condición explícita de parada ante imports sin soporte, no se añadió
RunModule, no se cambió engine/NTSC-U, no se compiló ni se repitió QUICK. No se
crearon tests que presenten el módulo como compatible. PAL sigue sin ejecutar
RWA; el último resultado dinámico conserva 1095 funciones y espera de audio.
El punto de registro sería RegisterGameOverrides, antes de host Main, siguiendo
NTSC-U; es suficiente para optar el módulo pero no soluciona funciones ausentes.

Siguiente acción única: auditar alcanzabilidad de esos ocho imports durante el
arranque y handshake de RWA, contrastando NTSC-U, para delimitar cualquier soporte
adicional necesario antes de autorizar cambios al runtime compartido.

## Auditoría de alcanzabilidad RWA: recomendación A

Informe estático: build/rwa-import-audits/run-8d9002534409/REPORT.md y audit.json.
Los cinco stubs ioman 8954/895C/8964/896C/8974 (versión 0104) no tienen referencias
directas, literales o relocalizadas detectadas. La numeración API no está acreditada:
el CSV local mezcla etiquetas/duplicados y no hay argumentos de llamada que arbitren.

Sysclib 28/34/35 (0104) no tiene llamadas directas, pero sí punteros relocalizados:
13AC -> stub8A84 -> slotB478; 13DC ->8A9C ->B484; 142C ->8AA4 ->B4A0. Es uso
indirecto posible, no ejecución demostrada durante init. No llamarlas inalcanzables.

_start 8C84, setup8D20, constructor de threads9BE4/trampoline9B7C, callbacks8ED8/
8280 y handler SIF0 7F04 son rutas relevantes. Dispatcher792C responde a tipo1
con versión0115 y tipo9 en 79B8..79D8. El recorrido directo estático cubre7867
posiciones y53 imports soportados, sin alcanzar ninguno ausente; quedan15 sitios
indirectos sin resolver exhaustivamente. No se afirma equivalencia dinámica.

Las trampas existentes de LoadModule fallan con Stop/Unimplemented al invocarse,
no al enlazar. Host captura ese fallo; Job/timeout protegen de otros bloqueos.
Recomendación A: futura activación RWA conservando todas las trampas y una prueba
acotada, detenerse al primer fallo real. No se activó, compiló ni ejecutó AutoTest
ahora; ningún import fue implementado, motor/NTSC-U intactos. Esto sustituye el
bloqueo preventivo por importación declarada, sin declarar compatibilidad total.

## Prueba controlada RWA: ejecución real

Cambio mínimo autorizado: overrides.cpp incluye iop_vm.h y registra exclusivamente
RunModule("RWA.IRX") antes de Main. No se modifican loader, bibliotecas ni trampas.
El test nativo existente incorpora un espía que exige un único registro RWA;
KernelStore mantiene sus seis casos y Logitech sus cinco pruebas. Una compilación
incremental en build/native-relocated/run-93951a1b8ffc reutiliza las traducciones;
suite host passed con TEMP exclusivo (C0000409 no reproducido, incidencia abierta).

Único QUICK: build/autotest/run-d3aca468fdd0, timeout8s. RWA se carga realmente en
base00010000, _start00018C84. Hay ocho hilos IOP al parar; el hilo de carga ya no
figura y EE recibe la respuesta de loadfile y envía comandos reales. Persisten
los ocho avisos de imports ausentes, pero no se invoca ninguna trampa en esta
prueba. No equivale a demostrar que nunca se usarán.

Se observa request SIF0 tipo1, callback EE002AF3B0, dispatcher002AF430 y request2
posterior. Por el flujo acreditado (IOP responde tipo9/0115; EE compara0115 antes
de enviar2), el handshake y cambio del flag gp-5A38 se deducen de la ejecución.
No se capturaron directamente el valor del flag ni bytes del paquete de respuesta;
no presentarlos como volcado dinámico directo. También avanzan comandos11,13,15,
4E,30 y49. No se acredita salida de audio funcional.

Resultado1095 ->1297 funciones; GS1661 primitivas/14706256 píxeles, VU0. La ventana
anterior era3s y la actual8s: comparación descriptiva, no regresión con duración
idéntica. El fallo aparece antes del watchdog: lectura EE inválida0C620034 en
003E9EA0, llamada por003EA8E0. Salida2; Job limpio, cero activos, timeoutfalse.
No se inició otra corrección ni se repitió QUICK.

Consulta NTSC-U: iop_servers.cpp ejecuta GTFSCDVD.IRX y B3ROUTE.IRX además de RWA;
FINDINGS documenta lecturas GTFS incluyendo Data/PrgData.bin. PAL todavía recibe
ceros de RPC00475453 y de B3ROUTE. Es evidencia relevante para el nuevo puntero
inválido, pero no demuestra su causa. Única siguiente acción: diagnosticar
003E9EA0/0C620034 contrastando carga GTFS PAL y solución NTSC-U antes de modificar.
Resumen compacto: build/native-relocated/run-93951a1b8ffc/rwa-integration.json.


### Diagn?stico dirigido: estructura PrgData y primer fotograma v?lido

Una sola ejecuci?n, sin compilaci?n: build/pointer-diagnostics/run-b652e437f453.
1297 funciones, GS1661 primitivas/14706256 p?xeles, salida2, sin timeout y Job limpio.
Watchpoint observa 004E31B8 <-0115 en 002AF430; RAM confirma0115.
frames/frame_000010.bmp muestra pantalla de carga multiling?e coherente, no men?.

003E9EA0 sigue NOT VERIFIED (MemoryFault en perfiles del lifter). Original y C++
contienen lw v1,34(v0) en003E9F24. Reproducci?n MIPS aislada sobre RAM capturada
(a0=0, primera iteraci?n del llamador; no captura de registros nativos) llega
a8C620034; memory.cpp normaliza KSEG a0C620034, igual al fallo nativo.
Cadena: [518C10]=7E6E00, [7E6E04]=7E6DFF, [7E6DFF]=0; count=7E6DFF00.
El recorrido desde array0 alcanza registro75000 (?ndice4E00), c?digo del resolver
TLB copiado; consume su instrucci?n8C620000 como puntero. No justifica un mapeo.

Watchpoint identifica001331F0 publicando ra?z7E6E00. La ruta usa nombre
Data/PrgData.bin (4B62A0) y llama368050 para fixups; sw368060 suma base al offset
del campo+4. El valor final base-1 apunta a offset-1 previo, pero no se observ?
directamente su ?ltima escritura: no atribuir a?n causalidad definitiva a GTFS.
RPC00475453 s? se solicita y responde ceros sin servidor (procedimientos1,3,4);
NTSC-U ejecuta GTFSCDVD.IRX y FINDINGS documenta PrgData con fixups. Pr?xima acci?n:
comprobar contrato de carga PrgData y compatibilidad/imports GTFS PAL antes de
activar el m?dulo original. No se modific? runtime, c?digo generado ni m?dulos.
Evidencias y hashes: summary.json; diagn?stico compacto: summary.md.


### GTFS PAL: ejecuci?n real y carga de recursos

Integraci?n m?nima: RunModule("GTFSCDVD.IRX") tras RWA, como NTSC-U.
Auditor?a build/gtfs-integration/run-152816064c6c/audit.json: ELF MIPS, entry224,
13056 bytes, segmento37808 bytes, relocs2/4/5/6; ninguna importaci?n sin soporte
seg?n tabla host actual. Trampas originales intactas. Registro comprobado en
arn?s real del override; 3 tests GTFS, 1 kernel/registro (6 casos), 5 Logitech,
19 AutoTest. AutoTest a?ade --dump-ram opcional bajo su propio directorio.
Una compilaci?n incremental: native-relocated/run-20827551d59f, host tests passed.
Un QUICK10s: autotest/run-c5cac0f37416, 1394 funciones frente1297; GS15896
primitivas/131043199 p?xeles; sin nueva captura ni men? acreditado. Salida2 por
l?mite de tiempo entre hilos, ning?n hilo declarado en bucle, Job limpio.

GTFS ejecuta entry23B44 en base23920, servidor00475453 handler23D98. RPC1
48/4 bytes; RPC3 64/16; RPC4 y5 16/0, servidos por el m?dulo. Solicitudes reales:
Data/Global.txd, Data/GlobalEn.bin, Data/HeadEn.bin, pveh/vlist.bin,
Tracks/tlist.bin, Data/PrgData.bin. PAL est? usando En en esta ejecuci?n;
no afirmar espa?ol activo. PrgData pide9 sectores hacia7E6E00 (18432 bytes).
Ra?z518C10=7E6E00, campo+4=7E6E20, count+8=2, RWA flag4E31B8=115.
003E9EA0 se ejecuta73 veces sin el acceso0C620034 anterior.

RAM final comparada con PrgData de disco: se deshicieron188 campos del fixup
seg?n estructura final; diferencias13 bytes: seis palabras adicionales cumplen
exactamente offsetDisco+base (la lista actual tiene6 entradas frente7 en disco),
y count en offset24 cambi?7->6. No hay captura previa a fixups y no se observ?
la escritura de ese count; no afirmar igualdad ?ntegra antes de fixups ni
atribuir sin evidencia la modificaci?n posterior. Resource-check.json guarda
hashes y todas las diferencias. Global.txd tampoco se afirma id?ntico en RAM
final despu?s del procesamiento. Los callbacks DMA existen en SetDmaIntr/Run
original y el flujo EE contin?a; no hay traza individual que acredite cada uno.

Primera incidencia restante: l?mite temporal con fase de inicializaci?n9 en
51C020; consulta repetida0028B3F0 desde001334CC. NTSC-U documenta esperas de
streams RWA/EA Trax, pero su equivalencia concreta con esta fase no est? probada.
?nica siguiente acci?n: diagnosticar esa consulta sobre RAM/logs existentes y
comparar inicializaci?n de audio NTSC-U, sin activar B3ROUTE por sospecha.
Resumen con evidencias: build/gtfs-integration/run-152816064c6c/summary.json.


### Selecci?n espa?ola y switch libmc2 PAL recuperado

Informes pal-spanish-diagnostics/select-6290a60964a4 y confirm-186638d96d33:
cursor01E89EB0 (lista01E89E68+48):0 ingl?s,1 neerland?s,2 espa?ol (bandera
resaltada1450). down1200-1211, down1300-1311, cross1500-1511 confirma idioma5
en004E2E98; t?tulo en RAM "Selecciona idioma", rutas GlobalSp.bin/HeadSp.bin.
La primera confirmaci?n encontr? destino242AC8 no cubierto.

NTSC-U FINDINGS documenta trabajador libmc2/comando menos2/tabla4BB320 y
GetInfo244218; PAL usa tabla4BB880 con99 entradas y GetInfo244208. jr242AC0,
delay slot nop242AC4, llega leg?timamente al caso242AC8. DB conserva inicio
242A50..242AC8 y chunk242D40..242D50: faltan los casos intermedios del trabajador.
No es una nueva funci?n independiente ni un puntero incorrecto demostrado.

relocate_native --complete-function 00242A50:00242D50 usa lifter original con
secciones ELF completas, mismo s?mbolo y copia bajo build; rechaza solapamientos
y extremo distinto del chunk final conocido. AutoTest FULL conserva esa opci?n.
No se modifica fuente publicada ni cuerpos de otras funciones (la copia a?ade
un separador de l?nea);9383 s?mbolos originales. Sigue NOT VERIFIED, sin
verificaci?n diferencial completa. Tests:5 switch,13 relocation,19 AutoTest.
Una compilaci?n incremental native-relocated/run-3a1758905dab; host suite passed.

Validaci?n GPU ?nica, misma secuencia espa?ola: frame2200,1895 funciones;
242AC8 desaparece. Captura1600 acredita di?logo en espa?ol de memory card
ausente, opciones S?/No (No resaltado). No se acredita50/60; no se pulsa nada
m?s. Captura2000 pierde texto: incidencia gr?fica conservada. Sin nuevo fallo,
Job limpio. Motor/NTSC-U intactos. Pr?xima acci?n: revisar una respuesta
reproducible al di?logo de memory card y comprobar qu? pantalla PAL sigue.
Evidencia: build/switch-integration/run-e00fbcdc0efa/summary.json.


### Tarjeta virtual PAL: CardOr y primera pantalla50/60

Reutilizaci?n directa de ps2host::mc2 y patr?n CardOr NTSC-U. Direcciones PAL
obtenidas de jal/slots/tabla004BB880, no copiadas por desplazamiento:
GetInfo US244218->PAL244208; Format2445D0->2445C0; Read246588->246578;
Write246788->246778; Create/Mkdir245FC0->245FB0; GetDir2470D8->2470C8;
Lookup245398->245388. Contratos a0 socket,a1 info/nombre; Read/Write a?aden
a2 buffer,a3 offset,t0 tama?o; Create a2 modo; GetDir a2 offset,a3 maxent,
t0 entries,t1 count. SearchFile conserva conversi?n original raw-entry y errores.
Con --memcard ejecutan host; sin opci?n ejecutan handler PAL original. Cola,
worker/polling y extensi?n242A50..242D50 permanecen; FULL conserva completion.

Pruebas:3 memory-card (14 rutas sint?ticas con/sin directorio, ABI/slots PAL,
completion),1 kernel,5 Logitech,19 AutoTest. Una compilaci?n incremental
native-relocated/run-a9a2554c61d4, suite host passed incluidas pruebas reales
mc2 de archivos/errores. No equivale a guardar/cargar un perfil PAL en el juego.
Una integraci?n GPU ?nica hasta2200, misma secuencia de Espa?ol,1898 funciones.
GetInfo2 llamadas retorna0; RAM518BF0={2,1,7998}: PS2,formateada,KB libres.
GetDir1 llamada retorna0/count0 para BESLES-52584 (nombre guardado PAL).
Aviso sin tarjeta desaparece y frame1600 acredita selecci?n50Hz/60Hz espa?ola,
50Hz resaltado sin confirmar. Idioma efectivo5. Job limpio, sin nuevo fallo.

Directorio persistente separado, fuera de build e ignorado por PAL .gitignore:
memcards/test-e123ea986bc5. Sigue vac?o y existe tras salir. Reutilizar con
--memcard games/burnout3-pal/memcards/test-e123ea986bc5. No se sobrescribi?
ninguna partida. Guardado y recarga de perfil siguen pendientes; no segunda
prueba porque no se ha escrito perfil. Incidencia gr?fica de texto desaparecido
persiste en captura2000, sin investigaci?n adicional.
Informe: build/memcard-integration/run-1e685a0bddcb/summary.json.
Pr?xima acci?n: revisar selecci?n expl?cita de modo de v?deo para continuar
hacia creaci?n de perfil; no afirmar persistencia funcional hasta recarga nueva.


### Selecci?n60Hz PAL: prueba y confirmaci?n adicional

Dos ejecuciones sin recompilar con la misma tarjeta test-e123ea986bc5.
Secuencia espa?ola anterior +down1800-1811: select-1f7eecc7c3e1/frame1950
acredita60Hz resaltado, cursor01E89EB0=1 (en esta lista,0=50Hz).
Segunda ejecuci?n a?ade cross2000-2011; estado01E989EC pasa0->1->2.
Frame2400/2800 muestra "?Se mostr? correctamente la pantalla de prueba?",
S?(USAR60Hz)/No(USAR50Hz), No resaltado. No se confirma ese di?logo.
Elegir60Hz inicialmente funciona, pero su aceptaci?n definitiva sigue pendiente;
no afirmar60Hz guardado/aplicado definitivamente. El patr?n de prueba no qued?
capturado en estos tres frames. No se alter? manualmente el reloj del host.
1924 funciones, frame2800, sin fallo nuevo y Job limpio. Espa?ol5 y tarjeta
{2,1,7998} conservados; solo GetInfo2/GetDir1, ning?n Create/Write ni archivo.
No se alcanza perfil a?n. Informe: build/pal-60hz-diagnostics/confirm-5c0f71f31b10/summary.json.
Siguiente acci?n: verificar patr?n de prueba y preparar respuesta expl?cita
S?(USAR60Hz); evitar confirmar por defecto No(USAR50Hz).


### Primera carrera manual y diagn?stico inicial de rendimiento

Hito comunicado por el usuario: espa?ol/60Hz confirmados, perfil creado/guardado,
Carrera Libre/coche/tutorial/carrera, aceleraci?n/direcci?n y audio funcional.
Sesi?n manual run-2f30715f8dda sale0;3529 funciones. Tarjeta manual contiene
BESLES-52584(61772), icon.sys(964), view.ico(15234). Recarga tras reiniciar
no comprobada aqu?. No inferir carrera funcional solo del n?mero de funciones.

Log confirma --gpu/Vulkan RTX3070TiLaptop y build Release/O2.366.2s, ?ltimo
VBLANK registrado20400 (~55.7/s global; NO FPS espec?ficos de carrera).
2764065587 pares VU,0 compilados/0 entradas; vu_programs.cpp tiene0 programas.
VU1 25%, VU0 3.2%, GPU/memoria24.8%, esperaGS20.4%; readback63.8s/18374.
Son tiempos de toda la sesi?n con hilos/escenas: no sumar como partes exclusivas.
Cuello mixto: interpretaci?n VU y sincronizaci?n; no demuestra problema IOP.
No hay --frame-times ni sampler/race input grabado: falta medici?n por escena.

NTSC/motor ya ofrecen --vu-record, --vu-interpret, vurecomp y comprobaci?n por
bytes contra el programa cargado. No copiar microc?digo NTSC. El adaptador PAL
crea build nuevo y busca all? vu_programs.txt: debe recibir/pinear un manifiesto
PAL capturado para no compilar siempre0 programas. No se implement? ni compil?.
Tri?ngulo ya llega por V del teclado o bot?n norte(SDL, XboxY/PS Triangle).
NTSC distingue Back en men?s y ChangeCamera en carrera. No remapear globalmente
c?rculo/tri?ngulo ni copiar direcciones de detecci?n de carrera sin validaci?n.
Informe: build/performance-audits/run-38fdaa2368fd/summary.json.


### Primera compilacion VU PAL: captura manual de 97 programas

Captura original build/vu-captures/run-1d27d7abf9a5/vu_programs.txt:
SHA256 deff4afda9b0d277532c222ff8e56919d5829743a850122c0ef1d10cf98d1a82.
97 programas (24 VU0,73 VU1),17008 pares,sin registros duplicados.
Origen PAL acreditado por launch.json y hash ELF; captura original intacta.
El usuario confirma ahora tambien recarga del perfil tras reiniciar.

Reutilizado port.generate -> vu_manifest_path -> vurecomp.main, como NTSC-U.
relocate_native --vu-manifest copia/pinea la captura en el build exclusivo y
compila/enlaza todos los vu_blocks adicionales. AutoTest FULL conserva la entrada.
No cambios en engine,NTSC-U,EE publicado ni microcodigo original. No vu-fast.
Build native-relocated/run-180cb57a8a54:61 unidades VU,13.1MB C++,359056 lineas;
compilacion secuencial Release/O2,308.641s,host tests passed,TEMP exclusivo.
Se conserva /arch:AVX2 del arbol original: la frase SSE2 de PORTING esta
atrasada respecto al CMake actual; no se cambio este ajuste.

64 pruebas Python passed (5 integracion,13 discovery,19 AutoTest,17 VU,10 VU0).
Arnes local reutiliza PrepareVuRun/VuCompiledIsTheInterpreter del host y objetos
VU del juego:97 programas,582 estados sinteticos,0 diferencias en registros,
memoria,pipelines,ciclos;582 ejecuciones entraron en codigo compilado.
Fallback por entrada ausente y microcodigo modificado:passed. No equivale a
validacion completa PS2 ni a estados reales de carrera. Primeros intentos del
arnes fallaron por nombre de regla Ninja y cabecera vu_core; se conservaron.

Unico probe GPU/audio/tiempo virtual:1841 funciones,actividad GS,timeout del
host esperado a12s (13.7s registrados),Job limpio,sin error nuevo confirmado.
No escena3D:VU1 total0,compilado0,entradas0,stale0. Validacion real3D PENDIENTE.
Captura,9383 funciones publicadas,ejecutable anterior y tarjeta manual intactos
por hashes. Incidencia historica C0000409 no reproducida; causa aun pendiente.
Informe:build/native-relocated/run-180cb57a8a54/vu-validation-summary.json.

Launcher independiente: Jugar Burnout 3 PAL VU compilado.bat,binario fijado;
Jugar Burnout 3 PAL anterior.bat vuelve a run-a9a2554c61d4. Launcher normal
sin editar:su selector existente encuentra el nuevo build validado.
GPU/audio,tarjeta manual-playtest,sin entradas programadas/virtual-time/VU-fast.

Referencia PC:memcards/pc_settings.ini ausente;valores por defecto pacing On,
VSync Off,resolucion Original(escala1),ventana1280x896,aspect4:3,AA Off.
F9/L3+R3 esta integrado en pc_menu/window compartido;apertura visual aun no
probada en esta sesion. No portar PC SETTINGS NTSC. Pacing solo programa la
presentacion en tiempo real;no interpretar como mayor FPS de simulacion.
Preparada comparacion manual posterior:launcher VU --pacing=on / --pacing=off
usa --setting del host y --frame-times,con otros ajustes de referencia iguales.
No escribe pc_settings.ini por si solo;no cambiar ajustes en F9 durante la
comparacion. No se ha ejecutado ni medido esta comparacion.
Siguiente accion unica:probar carrera con launcher VU,comprobar F9 sin cambiar
ajustes,cerrar normalmente y recuperar contadores VU/log antes de comparar pacing.

Comprobacion adicional:4 combinaciones de los launchers nuevo/anterior con
pacing On/Off pasaron con subprocess/filesystem simulados (launcher-checks.json);
ninguna carrera adicional ejecutada. Preferencias y partidas sin cambios.


### Carrera manual con VU compilado y salto PAL de FMV mediante Start

Usuario acredita carrera completada,takedowns,coleccionables,mayor fluidez,
16:9 y cambios de resolucion funcionales. Sesion manual vu-compiled-e90ddb21ea36
identificada por launch.json/hash del ejecutable run-180cb57a8a54;salida0.
VU1:40174902433 pares totales,40173411573 compilados,1490860 interpretados;
99.996289% compilado,60711910 entradas,4663 candidatos stale (fallback por
microcodigo cambiado,no discrepancias de equivalencia). Son contadores de toda
la sesion con carrera,no una medicion aislada de carrera.937.4s;VU1 29.7%,
VU0 3.1%,GPU bookkeeping6.1%,GPU draw CPU0.5%,GPU/memoria44.3%,esperaGS63.2%.
Tiempos solapados,no sumables ni FPS comparables entre sesiones distintas.
Dos lineas MPEG slice_start_code/Skip next picture;el juego continua y sale0.
No comparador diferencial activo durante la partida:no equivalencia global.

Referencia NTSC-U:SkipMovie001313C0 y FINDINGS "Skipping movies". No ELF NTSC
local;comparacion contractual,NO igualdad de bytes entre regiones. En ELF PAL:
00131410..001314C8 guarda a0 en s0,comprueba estado pantalla+2F8=17 y llama
003251B0 con a0=pantalla+1C8 en delay slot00131448. Flags pelicula48=activa,
49=terminada,4A=parada solicitada,4B=bucle.00325050 escribe1 en4A en delay
slot00325058.003251B0 lee48/4A,consulta stream+6C/metodo24 y cancela con
metodo0C si corresponde;003252AC..003252BC completa/baja flags originales.
003253A8 comprueba4B para reiniciar el stream al final. Llamadores PAL
00130CC0/00131150 usan las mismas rutinas originales de parada y actualizacion.

Overrides PAL:SkipMovie registrado00131410,flanco de Start (PadButtonsHeld,
bit3 confirmado en dbc.cpp),solo pelicula activa/no terminada/no pendiente/
no loop. Escribe exclusivamente solicitud4A,conserva actualizacion original;
no consume input ni escribe49. PS2HOST_MOVIE_LOG registra solo la solicitud.
Tests:2 SkipMovie(8 escenarios sinteticos y contrato ELF),3 mc2,1 KernelStore,
5 Logitech,7 integracion VU/reuse,13 discovery:31 metodos passed. Continuacion
original comprobada con callback sintetico,no con reproduccion real de FMV.

Para cumplir sin recompilar VU/EE:relocate_native --reuse-build apunta al build
VU con evidencia diferencial directa,verifica los61 objetos VU y reutiliza
los objetos/link originales;solo compila overrides.cpp y reenlaza. Pruebas
comprueban ausencia de generador y rechazo de objeto VU modificado.
Build Release run-75fc2363eedf passed,host tests passed,Job limpio. Captura,
objetosVU,binario anterior y tarjeta manual inalterados por hashes.
No ejecucion de juego adicional:la secuencia automatizada acreditada no llega
a logos/tutorial;validacion visual real del salto queda PENDIENTE.

Jugar Burnout 3 PAL FMV.bat fija el nuevo binario,GPU/audio/tarjeta manual y
preferencias existentes,sin entradas automaticas ni flags interpret/fast.
Jugar Burnout 3 PAL VU compilado.bat conserva el binario anterior;se permite
su lanzamiento historico tras cambios de fuente,pero siguen verificandose
binario,inputs inmutables y evidencia diferencial de ese build.
Informe:build/movie-audit/run-33c4d90f641b/summary.json y summary.md.
Siguiente accion unica:probar Start en logo/tutorial no-loop con launcher FMV,
comprobar que continua por el flujo original y recuperar stdout/stderr al salir.


### Recuperacion funcional y auditoria grafica (2026-10-18)

Usuario confirma SkipMovie real,perfil recargado,carrera completada y mayor
suavidad con Frame Pacing On. Esto actualiza los pendientes manuales anteriores.
Ultima sesion fmv-skip-0d77722ceaef:salida0,Vulkan,4x,16:9,VSyncOff,pacingOn;
solicitudes de parada originales para peliculas7,9,0,24 registradas.
151.4s,VU1 28.0%,GPU/memoria42.3%,esperaGS54.5%;10402 readbacks48.2s,
GPUbusy48.8s. Son metricas solapadas de sesion,no de carrera aislada.

Receta --reuse-build validada de nuevo sin compilar:2 comandos,todos los hashes
compatibles,61 objetosVU reutilizables. Manifiesto97 intacto y excluido de Git.
RECOVERY.md documenta rutas/hash/recetas y dependencia de artefactos locales;
no prometer reconstruccion desde un checkout sin los datos del usuario.
PERFORMANCE_AUDIT.md inventaria optimizaciones compartidas/especificas y limites.

Revision detecta --frame-times sin intervalo en modos --pacing:corregido a120.
Launcher anterior permite binario historico tras cambios de fuente,con evidencia
inmutable de su build;no se declara compatible con los overrides nuevos.
FMV --medir usa --frame-times120 y pacingOn,conservando los otros ajustes.
No se ejecuta nueva carrera ni se modifica runtime,GS,GPU,DMA o preferencias.
Siguiente ajuste candidato:comparar4x/2x en el mismo tramo,pacingOn,constantes
los demas ajustes;calidad/coste,no mejora de FPS demostrada ni aplicada.
