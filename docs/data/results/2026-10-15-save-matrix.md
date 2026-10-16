# Save matrix 2026-10-15

Tool: `tools/harness/save-matrix.ts` (docs/TESTING.md). Times UTC. Save ms = ts3native `save:` total per save.

## 2026-10-15T06:36 UTC SunsetValley, start save Matrix-SunsetValley-off-2.sims3, build 4380b0f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 06:36:34 | control | PASS | Matrix-SunsetValley-off-3, Matrix-SunsetValley-off-4 | 11987, 11584 | info unstable types: none<br>info known per-save type d84e7fc6: did not differ in this world |
| 06:37:00 | identical | **FAIL** | Matrix-SunsetValley-off-5, Matrix-SunsetValley-on-1 | 11016, 9948 | fixes switched on: not available or did not switch: footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes) |
| 06:37:26 | per-fix | PASS | Matrix-SunsetValley-pf-off-1, Matrix-SunsetValley-pf-fastreftable-1 | 11286, 9304 | skip footprint alone: fix not available in this game (ini/mod)<br>skip compress alone: fix not available in this game (ini/mod)<br>skip flush alone: fix not available in this game (ini/mod) |
| 06:38:00 | overwrite | **FAIL** | Matrix-SunsetValley-ow-1, Matrix-SunsetValley-ow-1, Matrix-SunsetValley-ow-1 | 9943, 10490, 9089 | fixes switched on: not available or did not switch: footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes) |
| 06:38:22 | back-to-back | **FAIL** | Matrix-SunsetValley-b2b-1, Matrix-SunsetValley-b2b-2 | 9394, 9263 | fixes switched on: not available or did not switch: footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes) |
| 06:38:39 | unpaused | **FAIL** | Matrix-SunsetValley-run-1 | 9167 | fixes switched on: not available or did not switch: footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes) |
| 06:39:10 | build-mode | PASS | Matrix-SunsetValley-build-1 | - | skip save in build mode: game refuses: the game cannot save right now |
| 06:39:21 | verify-counters | **FAIL** | Matrix-SunsetValley-verify-1 | 9808 | fixes switched on: not available or did not switch: footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes) |
| 06:39:32 | reload | pending | Matrix-SunsetValley-reload-1 | 9239 | fixes switched on: not available or did not switch: footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes) |

8 cases, 3 passed, 5 failed (identical, overwrite, back-to-back, unpaused, verify-counters), pending: reload

## 2026-10-15T06:43 UTC SunsetValley, start save null, build 8f1b204+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 06:43:45 | control | PASS | Matrix-SunsetValley-off-6, Matrix-SunsetValley-off-7 | 11706, 10541 | info unstable types: none<br>info known per-save type d84e7fc6: did not differ in this world |
| 06:44:09 | identical | **FAIL** | Matrix-SunsetValley-off-8, Matrix-SunsetValley-on-2 | 10747, 8836 | Matrix-SunsetValley-on-2 flush fails: counter +1, log 1 |
| 06:45:11 | per-fix | **FAIL** | Matrix-SunsetValley-pf-off-2, Matrix-SunsetValley-pf-fastreftable-2, Matrix-SunsetValley-pf-footprint-1, Matrix-SunsetValley-pf-compress-1, Matrix-SunsetValley-pf-flush-1 | 10740, 8820, 10627, 10681, 10528 | Matrix-SunsetValley-pf-flush-1 flush fails: counter +1, log 1 |
| 06:45:42 | overwrite | **FAIL** | Matrix-SunsetValley-ow-2, Matrix-SunsetValley-ow-2, Matrix-SunsetValley-ow-2 | 8868, 8876, 9734 | Matrix-SunsetValley-ow-2 flush fails: counter +1, log 1<br>Matrix-SunsetValley-ow-2 flush fails: counter +1, log 1<br>Matrix-SunsetValley-ow-2 flush fails: counter +1, log 1 |
| 06:46:02 | back-to-back | **FAIL** | Matrix-SunsetValley-b2b-3, Matrix-SunsetValley-b2b-4 | 8870, 9389 | Matrix-SunsetValley-b2b-3 flush fails: counter +0, log 1<br>Matrix-SunsetValley-b2b-4 flush fails: counter +1, log 1 |
| 06:46:19 | unpaused | **FAIL** | Matrix-SunsetValley-run-2 | 10030 | Matrix-SunsetValley-run-2 flush fails: counter +1, log 1 |
| 06:46:51 | build-mode | PASS | Matrix-SunsetValley-build-1 | - | skip save in build mode: game refuses: the game cannot save right now |
| 06:47:02 | verify-counters | **FAIL** | Matrix-SunsetValley-verify-2 | 9315 | Matrix-SunsetValley-verify-2 flush fails: counter +1, log 1 |
| 06:47:13 | reload | pending | Matrix-SunsetValley-reload-2 | 9899 | Matrix-SunsetValley-reload-2 flush fails: counter +1, log 1 |

8 cases, 2 passed, 6 failed (identical, per-fix, overwrite, back-to-back, unpaused, verify-counters), pending: reload

## 2026-10-15T06:51 SunsetValley reload check of Matrix-SunsetValley-reload-2, build a9e4285+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 06:51:21 | reload | **FAIL** |  |  | loaded the reload save: loaded null, expected Matrix-SunsetValley-reload-2 |

1 cases, 0 passed, 1 failed (reload)

## 2026-10-15T06:55 UTC SunsetValley, start save null, build 2fb3339+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 06:55:39 | control | PASS | Matrix-SunsetValley-off-9, Matrix-SunsetValley-off-10 | 11429, 10541 | info unstable types: none<br>info known per-save type d84e7fc6: did not differ in this world |
| 06:56:02 | identical | PASS | Matrix-SunsetValley-off-11, Matrix-SunsetValley-on-3 | 11079, 9227 | all checks pass |
| 06:57:01 | per-fix | PASS | Matrix-SunsetValley-pf-off-3, Matrix-SunsetValley-pf-fastreftable-3, Matrix-SunsetValley-pf-footprint-2, Matrix-SunsetValley-pf-compress-2, Matrix-SunsetValley-pf-flush-2 | 11463, 8668, 10798, 10785, 10660 | all checks pass |
| 06:57:31 | overwrite | PASS | Matrix-SunsetValley-ow-3, Matrix-SunsetValley-ow-3, Matrix-SunsetValley-ow-3 | 8903, 9037, 9445 | all checks pass |
| 06:57:53 | back-to-back | PASS | Matrix-SunsetValley-b2b-5, Matrix-SunsetValley-b2b-6 | 10321, 9489 | info flush waits: +0 (0 means the worker had finished before the second save) |
| 06:58:09 | unpaused | PASS | Matrix-SunsetValley-run-3 | 10123 | all checks pass |
| 06:58:41 | build-mode | PASS | Matrix-SunsetValley-build-1 | - | skip save in build mode: game refuses: the game cannot save right now |
| 06:58:51 | verify-counters | PASS | Matrix-SunsetValley-verify-3 | 8996 | skip compress verify ran: no batched compression in a save (measurement only) |
| 06:59:01 | reload | pending | Matrix-SunsetValley-reload-3 | 8911 | all checks pass |

8 cases, 8 passed, 0 failed, pending: reload

## 2026-10-15T07:02 SunsetValley reload check of Matrix-SunsetValley-reload-3, build 417745f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 07:02:07 | reload | PASS |  |  | info sims in world: 240 before save, 240 after load (NPCs come and go) |

1 cases, 1 passed, 0 failed

## 2026-10-15T07:04 UTC IslaParadiso, start save null, build 417745f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 07:04:54 | control | PASS | Matrix-IslaParadiso-off-1, Matrix-IslaParadiso-off-2 | 13514, 11589 | info unstable types: none<br>info known per-save type d84e7fc6: did not differ in this world |
| 07:05:19 | identical | PASS | Matrix-IslaParadiso-off-3, Matrix-IslaParadiso-on-1 | 10983, 11054 | all checks pass |
| 07:06:30 | per-fix | **FAIL** | Matrix-IslaParadiso-pf-off-1, Matrix-IslaParadiso-pf-fastreftable-1, Matrix-IslaParadiso-pf-footprint-1, Matrix-IslaParadiso-pf-compress-1, Matrix-IslaParadiso-pf-flush-1 | 11800, 10130, 13834, 13933, 13028 | footprint alone vs off identical: differs: cf84ec98 (aParadiso_0x0c50c382.nhd cf84ec98:00000000:0000000000000001 changed); 06b981ed (aParadiso_0x0c50c382.nhd 06b981ed:00000000:0000000000000001 changed); 05ed1226 (aParadiso_0x0c50c382.nhd 05ed1226:00f0b54d:08f300517570c750 changed); 4d1a5589 (aParadiso_0x0c50c382.nhd 4d1a5589:00000000:08f300517570c750 changed)<br>compress alone vs off identical: differs: cf84ec98 (aParadiso_0x0c50c382.nhd cf84ec98:00000000:0000000000000001 changed); 06b981ed (aParadiso_0x0c50c382.nhd 06b981ed:00000000:0000000000000001 changed); 05ed1226 (aParadiso_0x0c50c382.nhd 05ed1226:00f0b54d:08f300517570c750 changed, aParadiso_0x0c50c382.nhd 05ed1226:00f0b54d:08f30052dfc0cb10 changed, aParadiso_0x0c50c382.nhd 05ed1226:00f0b54d:08f30052dfc0cb20 changed, aParadiso_0x0c50c382.nhd 05ed1226:00f0b54d:08f30052dfc0caa0 changed, aParadiso_0x0c50c382.nhd); 4d1a5589 (aParadiso_ |
| 07:07:07 | overwrite | PASS | Matrix-IslaParadiso-ow-1, Matrix-IslaParadiso-ow-1, Matrix-IslaParadiso-ow-1 | 11921, 10790, 11333 | all checks pass |
| 07:07:33 | back-to-back | PASS | Matrix-IslaParadiso-b2b-1, Matrix-IslaParadiso-b2b-2 | 10815, 13197 | info flush waits: +0 (0 means the worker had finished before the second save) |
| 07:07:51 | unpaused | PASS | Matrix-IslaParadiso-run-1 | 11635 | all checks pass |
| 07:08:23 | build-mode | PASS | Matrix-IslaParadiso-build-1 | - | skip save in build mode: game refuses: the game cannot save right now |
| 07:08:37 | verify-counters | PASS | Matrix-IslaParadiso-verify-1 | 13276 | skip compress verify ran: no batched compression in a save (measurement only) |
| 07:08:50 | reload | pending | Matrix-IslaParadiso-reload-1 | 11309 | all checks pass |

8 cases, 7 passed, 1 failed (per-fix), pending: reload

## 2026-10-15T07:09 UTC IslaParadiso, start save Matrix-IslaParadiso-reload-1.sims3, build 417745f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 07:10:11 | control | PASS | Matrix-IslaParadiso-off-4, Matrix-IslaParadiso-off-5 | 11379, 11485 | info unstable types: none<br>info known per-save type d84e7fc6: did not differ in this world |
| 07:11:17 | per-fix | PASS | Matrix-IslaParadiso-pf-off-2, Matrix-IslaParadiso-pf-fastreftable-2, Matrix-IslaParadiso-pf-footprint-2, Matrix-IslaParadiso-pf-compress-2, Matrix-IslaParadiso-pf-flush-2 | 12772, 9779, 12614, 12109, 11251 | all checks pass |

2 cases, 2 passed, 0 failed

## 2026-10-15T07:14 IslaParadiso reload check of Matrix-IslaParadiso-reload-1, build 23b3b66+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 07:14:31 | reload | PASS |  |  | info sims in world: 248 before save, 248 after load (NPCs come and go) |

1 cases, 1 passed, 0 failed

## 2026-10-15T07:33 UTC IslaParadiso, start save Matrix-IslaParadiso-travel-start-1.sims3, build 82cefe0+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 07:36:42 | travel | PASS | Matrix-IslaParadiso-travel-start-2, Matrix-IslaParadiso-travel-away-1, Matrix-IslaParadiso-travel-home-1 | 11922, 19767, 16391 | info travel saves target: Matrix-IslaParadiso-travel-start-1.sims3 untouched (travel saves went to the Matrix save) |

1 cases, 1 passed, 0 failed

## 2026-10-15T07:38 IslaParadiso, build d7988e4+dirty (game died)

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 07:38:34 | run | **FAIL** |  |  | case ran: game died |

1 cases, 0 passed, 1 failed (run)

## 2026-10-15T09:02 UTC NewDowntownWorld, start save null, build ce8da4f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:03:03 | control | PASS | Matrix-NewDowntownWorld-off-4, Matrix-NewDowntownWorld-off-5 | 18576, 16648 | info unstable types: d84e7fc6 (ridgeport_0x09ffe3d7.nhd d84e7fc6:00000000:6c1100337657b720 changed) |
| 09:03:33 | back-to-back | PASS | Matrix-NewDowntownWorld-b2b-3, Matrix-NewDowntownWorld-b2b-4 | 13530, 11972 | info flush waits: +0 (0 means the worker had finished before the second save) |

2 cases, 2 passed, 0 failed

## 2026-10-15T09:06 UTC NewDowntownWorld, start save null, build efda0df+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:27:16 | travel | **FAIL** |  |  | case ran: Error: arrived in live mode: not reached within 1200 s |

1 cases, 0 passed, 1 failed (travel)
