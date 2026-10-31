# Protocolo SysEx del KeyStep Pro (firmware 2.5.20)

Arturia no lo publica. Todo esto viene de las notas de ingeniería inversa de
[KSCON](https://github.com/canadianblaken/keystep-con) (licencia MIT): `findings.md` (protocolo, verificado en
hardware) y `mcc-params.md` (tabla de parámetros de MIDI Control Center). Cuando algo no está acá, buscarlo ahí.

## Trama

```
F0 00 20 6B <dev> 42 <cmd> <proyecto> <param> <k> <item> <idx x k> [count] [valores] F7
```

- `dev` 0x7F = todos. `42` = KeyStep Pro. `proyecto` 0 = globales / actual, 1..16 = ese slot.
- Identidad: `F0 7E 7F 06 01 F7` -> `F0 7E 7F 06 02 00 20 6B 02 00 09 00 <b> <patch> <minor> <major> F7`.

| cmd | qué hace | respuesta |
| --- | --- | --- |
| 0x01 / 0x31 | GET de un global (0x31 para IDs >= 128, índice = id - 128) | 0x02 / 0x32 + ACK 0x1C |
| 0x02 / 0x32 | SET de un global | ninguna |
| 0x05 | abre sesión: copia el proyecto de la flash a un buffer | ninguna |
| 0x0B | bulk GET de `count` valores en un camino de índices (base 1) | 0x0C con los valores |
| 0x0C | bulk SET | - |
| 0x06 | commit: escribe el buffer en flash y recarga el proyecto (~3 s) | - |

Items: 0x78 proyecto, 0x79 scene, 0x7A track control, 0x7B track 1 / drum, 0x7C-0x7E tracks 2-4.
Globales: param 0x41 con el ID 1..158 en el byte de item.

## Trampas conocidas (KSCON)

- Las sesiones leen la FLASH, no la RAM: lo que se edita en el equipo y no se guardó no se ve.
- Un commit invierte la marca válido/default de cada pattern y scene que no se reenvíe: leer los 16 estados de scene
  (0x79 p0x26) y los 80 de pattern (0x7A y tracks, p0x28) y reescribirlos igual.
- Valores >= 0xF7 cortan la respuesta: releer ese bloque valor por valor.
- Slots de drum vacíos: 127 en todos los campos, o el equipo marca "192 notes limit".
- Un bloque de proyecto borrado (tempo 127 127 3) carga con defaults en el panel aunque sus patterns se lean bien.
- Los SET de globales no devuelven ACK; solo los GET responden.
- El KSP no reenvía SysEx (no hay thru de SysEx).

## Plan de lectura completo

MCC lee un proyecto con un "recall plan" de 85 grupos (KSCON lo extrajo a `web/src/1c-bulk.js`). La fase 1 lo porta a
C++ como tabla en `Source/Ksp/`.
