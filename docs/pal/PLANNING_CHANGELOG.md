# Registro de consolidación del roadmap y tracker

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
