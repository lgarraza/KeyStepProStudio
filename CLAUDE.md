# KeyStepPro Studio

Nombre del producto: **KeyStepPro Studio** (sin espacio donde no se permite: `KeyStepProStudio`).
App JUCE 8 (Standalone, Windows y Mac) para el Arturia KeyStep Pro: editor de proyectos (tracks, patterns, scenes) por SysEx y, más adelante, host de VSTi por track para tocar en vivo sin un DAW. Proyecto público (AGPL-3.0, 2026-10-30): la documentación habla solo del KeyStep Pro y de esta app, sin mencionar otros proyectos de Leo.

## Decisiones (2026-10-29)
- Dirección (2026-10-29, Leo): KeyStepPro Studio es un software ÚNICO que hostea VSTs (fase 3) y genera música ambient para el KSP. Es independiente: no se conecta ni depende de otro software.
- Modelo de datos (Leo, 2026-10-29): la VERDAD es la biblioteca local de la app, hasta ~200 proyectos del Studio (cada uno: proyecto del KSP + VST asignados a los 4 tracks + efectos VST + ajustes del generador). La memoria del KSP es VOLÁTIL: sus 16 slots son lugares de trabajo temporarios, no importa qué tengan ni si se pierde; la app decide qué sube (normalmente 1 proyecto, a veces varios) y qué saca. Antes de pisar un slot: backup y aviso. La app abre y edita los proyectos de la biblioteca sin el KSP conectado. La lista que manda es la de la app (por eso la "copia local por slot" del PR #10 queda superada: un proyecto de la biblioteca tiene nombre e identidad propios, no número de slot).
- Alcance de la fase 3 (Leo, 2026-10-29): 4 tracks de VST, uno por cada track del KSP (las notas llegan por USB por el canal de cada track); el KSP manda el tiempo (la app sigue su clock, Start y Stop); transporte completo; grabación de audio a WAV, a elección la mezcla o un stem por track. Hosting: lista de plugins, escáner fuera de proceso, archivo "dead man", carga y estado en el hilo de mensajes y solo process() en el de audio.
- Firmware objetivo: 2.5.20 oficial (el de Leo, build 2085). No se usa el firmware parcheado de KSCON; los nombres de proyecto los guarda la app.
- La app solo ESCRIBE en el equipo si la identidad dice 2.5.20 (`ksp::Identity::isKnownFirmware`). Antes de cada escritura, backup automático del proyecto y relectura para verificar.
- El SysEx lee lo guardado en flash, no el estado en vivo: el editor baja, edita y sube (librarian). En vivo la app solo ve lo que llega por MIDI (notas, clock, Start/Stop, Program Change).
- Protocolo: tomado de KSCON (https://github.com/canadianblaken/keystep-con, MIT, findings.md y mcc-params.md). Resumen y trampas en docs/protocolo.md.
- Informe de viabilidad y plan por fases: https://claude.ai/artifact/EC85p7aq4GvKh6hMD9ghy6

## Reglas
- Slots (Leo, 2026-10-29): la app puede cargar proyectos en los 16 slots del KSP (son memoria volátil de trabajo), siempre con backup previo y aviso de lo que hay. Sin límite de slots en el código (App/Main.cpp, KspGen), 2026-10-30; el botón único Read/Send de arriba usa el slot de origen del proyecto.
- Source/Ksp/ (protocolo) y el modelo del proyecto: header-only, C++17 + std, sin JUCE, con tests sin JUCE en tests/. Lo que usa JUCE (MidiLink.h, UI, audio) va aparte.
- Archivos nuevos header-only cuando se pueda; un .cpp nuevo se agrega a CMakeLists.txt.
- Tiempo real (cuando haya audio, fase 3): nada de new/delete, locks, logs ni I/O en el hilo de audio.
- Nada que escriba en el equipo (SET, bulk SET, commit) sin backup previo y verificación; las herramientas de solo lectura lo garantizan con `ksp::isReadOnlyRequest`.
- No mencionar otros proyectos de Leo en el código ni en los docs (Leo, 2026-10-30): el repo es público y trata solo del KSP.
- Testear lo justo (app personal): `protocol_test` y los tests sin JUCE de lo que se toca.

## Build
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` y `cmake --build build --config Release`. JUCE 8.0.4 se descarga solo (FetchContent) o se usa uno local con `-DJUCE_DIR=...`.
- Windows: Visual Studio 2022/2026 con CMake (o `cmake -G "Visual Studio 17 2022"`). Mac: Xcode o Ninja.
- Solo tests, sin JUCE: `cmake -S . -B build-tests -DKSP_TESTS_ONLY=ON && cmake --build build-tests && ctest --test-dir build-tests`.
- CI (.github/workflows/build.yml): compila y corre los tests en Windows en cada PR. Mac salió del CI (2026-10-29, decisión de Leo: no se usa); el código sigue sin APIs de Windows, pero no se compila en Mac desde entonces.

## Mapa del código
- Source/Ksp/Protocol.h: tramas SysEx (identidad, globales, sesión, bulk GET), parseo de respuestas, guardia de solo lectura, nombres de notas y escalas.
- Source/Ksp/MidiLink.h: puerto USB del KSP con JUCE; send() espera la respuesta con timeout, request() reintenta (solo lecturas); setGapMs (12 ms al escribir, como KSCON).
- Tools/KspProbe/Main.cpp: fase 0, prueba de conexión de solo lectura (identidad, globales, patterns de un proyecto); guarda ksp_probe_report.txt.
- Source/Ksp/RecallPlan.h y Globals.h: GENERADOS con Tools/gen_from_kscon.py desde KSCON (plan de lectura de MCC, 8951 pedidos por proyecto; 129 ajustes globales). No se editan a mano.
- Source/Ksp/Backup.h: lee un proyecto entero con el plan (re-lee valor por valor los bloques cortados por bytes >= 0xF7) y escribe el backup en JSON con el formato de KSCON ("keystep-pro-backup" v1).
- Tools/KspBackup/Main.cpp: fase 1a, backup de solo lectura de globales y proyectos a Documentos/KeyStepPro Studio/Backups.
- Source/Ksp/Restore.h: lector de JSON (backup o proyecto suelto), plan de escritura (bulk SET de todo el plan, estados al final para que el commit no los invierta, nulls y acordes ilegibles no se mandan, slots de drum vacíos en 127) y comparación de la relectura. Portado de storeProject de KSCON.
- Tools/KspRestore/Main.cpp: fase 1b, `--file X --project N [--to M] [--write]`: backup previo del slot (ksp-pre-restore-P<M>-*.json), sin `--write` es un dry run; con `--write` abre sesión, manda, commit (3 s) y relee. Solo manda tramas al slot M (`ksp::isWriteFor`).
- App/KspStyle.h: look del KeyStep Pro (paleta chasis blanco / panel negro / colores de track verde-naranja-amarillo-rojo, botones grises con bisel, brillo en lo encendido, KspLookAndFeel, TrackTabs, SevenSegDisplay de 7 segmentos). Los colores van en ksp::ui::palette; no hardcodear colores en las ventanas.
- App/Main.cpp: KeyStepProStudio (fase 2): editor de patterns, abre un backup o lee del equipo, escribe a los 16 slots con backup previo y relectura. Source/Ksp/Device.h (JUCE): open, readSlot, backupSlot, writeSlot; lo usan la app y KspRestore.
- Source/Model/Overdub.h: grabación en tiempo real (overdub) sobre un Pattern desde MIDI crudo: clock desde Start, paso más cercano módulo el largo, solo suma notas y salta las que ya están en el paso (el KSP tocando lo guardado). Lo usa el botón Rec de la app con Device::setListener.
- Source/Model/Pattern.h: un pattern de secuenciador (largo, root, escala, pasos con notas) leído de y escrito en un ProjectData, sin tocar el equipo; después `writePlan` lo manda. Portado de edReadPattern / edWritePattern de KSCON. No toca drums ni arpegiador.
- Source/Model/NoteMove.h: mover / copiar un grupo de notas de un Pattern (arrastre del grid), todo o nada, y sameSteps para el Undo del grid; tests/notemove_test.cpp.
- Source/Model/DrumPattern.h: un pattern de drums del track 1 (24 lanes, hasta 192 golpes, largo por lane, máscaras de lane) leído de y escrito en un ProjectData; portado de edReadDrum / edWriteDrum de KSCON; verificado en el equipo 2026-10-31 (KspGen --style techno al slot 16). tests/drum_test.cpp. App/DrumGrid.h: el grid de drums (24 lanes, el track 1 en drum mode lo muestra en vez del de notas).
- Source/Model/ProjectMap.h + App/ProjectMapWindow.h: Project Map (botón "Project Map"): escenas x tracks con miniaturas, timeline de chains y banco de patterns de solo lectura; tests/projectmap_test.cpp. Sin drums todavía.
- Source/Model/Scene.h: una scene (pattern por track, decodificada en el equipo); tests/scene_test.cpp.
- Source/Studio/StudioProject.h + Library.h: la biblioteca (proyecto del Studio = proyecto KSP + 4 tracks de VST + generador; carpeta con índice, escrituras atómicas); App/LibraryWindow.h: la ventana. tests/studio_test.cpp.
- Source/Host/TrackEffect.h + App/FxWindow.h: efectos VST3 en inserts por track y cadena master (20 puestos en el motor).
- Source/Mix/Limiter.h (limitador master); tests/mix_test.cpp. La ventana Mix, los 6 sends y Source/Fx se quitaron (2026-10-30, Leo).
- App/MidiMonitor.h: monitor MIDI (controles vistos + registro) para ver qué manda cada control del KSP; docs/manual/07-live-sync.md (PR #29) define qué se puede mostrar en vivo.
- Source/Mix/Transport.h (el KSP como reloj maestro: Start/Stop/Continue, posición y tempo) + Source/Host/Recorder.h (grabación WAV de la mezcla y los stems, con compuerta que abre el Start del KSP y cierra el Stop más la cola).
- Source/Host/TrackInstrument.h + Engine.h (JUCE): instrumento VST3 por track y el motor de audio (4 instrumentos, MIDI del KSP por canal, FIFOs sin locks); Tools/KspHostCheck prueba un plugin sin ventana.
- Source/Host/PluginList.h + Tools/KspScanner: lista de VST3 (escáner fuera de proceso, plugins ocultables); App/PluginTable.h: la ventana con casillas para ocultar.
- App/GeneratorWindow.h: la ventana Generate... (estilo, mood, semilla, cadenas, velocidad) que escribe patterns y scenes en el proyecto abierto, con deshacer.
- Source/Gen/Theory.h (Chord, Params, escalas, RNG) + Roles.h (motor de roles: RoleSpec = drone/pad/melody/bells/bass/stab con densidad, registro, largo, pulsos y gate; euclid; makeDrumPattern con DrumSpec) + Ambient.h (el preset ambient = 4 RoleSpec, misma salida que antes: hash congelado en tests/gen_test.cpp) + Styles.h (presets techno/house/dnb: drums en el track 1 en drum mode, bajo, acordes, lead; `applyStyle`). En drum mode el instrumento del track 1 escucha el canal 10.
- Source/Gen/Ambient.h: generador ambient (4 tracks, largos 16/13/17/19, semilla, scenes, cadenas, moods ethereal/dark/meditative). Tools/KspGen/Main.cpp: lo escribe a un slot 10-16 con backup y relectura. tests/gen_test.cpp.
- tests/protocol_test.cpp: tramas contra las capturas de KSCON en hardware. tests/backup_test.cpp: plan, lectura con un equipo falso y JSON. tests/restore_test.cpp: JSON ida y vuelta, orden de envío, arreglo de drums, relectura. tests/pattern_test.cpp: leer / editar / escribir un pattern, límites. tests/overdub_test.cpp: cuantizado al paso, dedupe, canal, Start/Stop/Continue, límites.

## Manual
- docs/manual/ (inglés): README con el estado por fase, protocolo y reglas de seguridad, herramientas, modelo, app y bitácora de hardware. Cada PR que cambie comportamiento, herramientas o resultados lo actualiza; es el contexto para tareas futuras. Conciso.

## Fases
0 prueba de conexión (KspProbe, hecha 2026-10-29: identidad, globales y notas de patterns verificadas contra el KSP de Leo; ~4 ms por pedido; solo se ve lo guardado en flash) · 1 librarian: 1a backup de solo lectura (KspBackup, hecho 2026-10-29: 16 proyectos en 9.6 min), 1b restore (KspRestore, hecho 2026-10-29: proyecto 1 al slot 16 verificado en el equipo) · 2 editor (en curso: modelo de pattern en Model/Pattern.h, falta la ventana) de tracks, patterns y scenes · 3 host de VSTi por track (hecho: VST3, escáner fuera de proceso) · 4 generador propio: música ambient como patterns, scenes y ajustes por pattern (hecho en parte: Source/Gen, moods, cadenas, escrito a los slots 10-16); seguirá evolucionando y se afina escuchando. Sin monitor en vivo obligatorio.
