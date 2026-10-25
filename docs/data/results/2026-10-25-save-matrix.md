# Save matrix 2026-10-25

Tool: `tools/harness/save-matrix.ts` (docs/TESTING.md). Times UTC. Save ms = ts3native `save:` total per save.

## 2026-10-25T00:18 UTC NewDowntownWorld, start save null, build d2c0ad42+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 00:19:02 | control | PASS | Matrix-NewDowntownWorld-off-29, Matrix-NewDowntownWorld-off-30 | 9990, 8394 | info unstable types: d84e7fc6 (ridgeport_0x09ffe3d7.nhd d84e7fc6:00000000:6c1100337657b720 changed) |
| 00:19:35 | overwrite | PASS | Matrix-NewDowntownWorld-ow-6, Matrix-NewDowntownWorld-ow-6, Matrix-NewDowntownWorld-ow-6 | 8353, 8332, 8443 | all checks pass |
| 00:19:56 | back-to-back | PASS | Matrix-NewDowntownWorld-b2b-13, Matrix-NewDowntownWorld-b2b-14 | 8599, 8420 | info flush waits: +0 (0 means the worker had finished before the second save) |
| 00:20:15 | unpaused | PASS | Matrix-NewDowntownWorld-run-6 | 10564 | all checks pass |
| 00:20:41 | reload | pending | Matrix-NewDowntownWorld-reload-off-5, Matrix-NewDowntownWorld-reload-on-5 | 10604, 9953 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |

4 cases, 4 passed, 0 failed, pending: reload

## 2026-10-25T00:23 NewDowntownWorld reload check of Matrix-NewDowntownWorld-reload-on-5 (ON), build d2c0ad42+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 00:23:14 | reload | pending |  |  | info sims in world: 270 before save, 270 after load (NPCs come and go)<br>info ScriptError files (ON): none since the save at 00:20:41 |

0 cases, 0 passed, 0 failed, pending: reload

## 2026-10-25T00:25 NewDowntownWorld reload check of Matrix-NewDowntownWorld-reload-off-5 (OFF), build d2c0ad42+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 00:25:49 | reload | PASS |  |  | info sims in world: 270 before save, 270 after load (NPCs come and go)<br>info ScriptError files (OFF): none since the save at 00:20:41<br>info ScriptError files per save: ON 0, OFF 0 |

1 cases, 1 passed, 0 failed

## 2026-10-25T00:27 UTC IslaParadiso, start save null, build d2c0ad42+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 00:27:44 | control | PASS | Matrix-IslaParadiso-off-12, Matrix-IslaParadiso-off-13 | 9366, 8336 | info unstable types: none<br>info known per-save type d84e7fc6: did not differ in this world |
| 00:28:13 | overwrite | PASS | Matrix-IslaParadiso-ow-4, Matrix-IslaParadiso-ow-4, Matrix-IslaParadiso-ow-4 | 8381, 8905, 8023 | all checks pass |
| 00:28:32 | back-to-back | PASS | Matrix-IslaParadiso-b2b-7, Matrix-IslaParadiso-b2b-8 | 8173, 7927 | info flush waits: +0 (0 means the worker had finished before the second save) |
| 00:28:49 | unpaused | PASS | Matrix-IslaParadiso-run-4 | 10535 | all checks pass |
| 00:29:10 | reload | pending | Matrix-IslaParadiso-reload-off-2, Matrix-IslaParadiso-reload-on-2 | 9444, 9320 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |

4 cases, 4 passed, 0 failed, pending: reload

## 2026-10-25T00:31 IslaParadiso reload check of Matrix-IslaParadiso-reload-on-2 (ON), build d2c0ad42+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 00:31:40 | reload | pending |  |  | info sims in world: 258 before save, 258 after load (NPCs come and go)<br>info ScriptError files (ON): ScriptError_<pc>__10-25-2026__08-31-19__FED401E4.xml |

0 cases, 0 passed, 0 failed, pending: reload

## 2026-10-25T00:34 IslaParadiso reload check of Matrix-IslaParadiso-reload-off-2 (OFF), build d2c0ad42+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 00:34:19 | reload | **FAIL** |  |  | new errors with fixes on: 1 not seen with OFF: -- Script Errors -- / Object: / Name: Yvonne Nixon / Type: Sims#.Gameplay.Actors.Sim / Object id: # / Position: (#.#, #.#, #.#) / Room id: # / Level:  |

1 cases, 0 passed, 1 failed (reload)

## 2026-10-25T00:36 UTC NewDowntownWorld, start save null, build d2c0ad42+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 00:39:03 | travel | PASS | Matrix-NewDowntownWorld-travel-start-41, Matrix-NewDowntownWorld-travel-away-28, Matrix-NewDowntownWorld-travel-home-18 | 11528, 20704, 11496 | skip fixes skipped (not in this build/ini): cachedreflection (PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off), footprint (footprint index disabled (Enabled=0)), compress (save compress off (Enabled=0)), flush (save flush off (Enabled=0))<br>info trip route: travel_vacation: queued car on the world lot<br>info travel saves target:  untouched (travel saves went to the Matrix save) |

1 cases, 1 passed, 0 failed

## 2026-10-25T00:56 UTC IslaParadiso, start save null, build d2c0ad42+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 00:56:35 | unpaused | PASS | Matrix-IslaParadiso-run-5 | 10462 | all checks pass |

1 cases, 1 passed, 0 failed

## 2026-10-25T01:18 UTC IslaParadiso, start save null, build 6303d236+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 01:19:20 | control | PASS | Matrix-IslaParadiso-off-14, Matrix-IslaParadiso-off-15 | 9163, 7974 | info unstable types: none<br>info known per-save type d84e7fc6: did not differ in this world |
| 01:19:49 | overwrite | PASS | Matrix-IslaParadiso-ow-5, Matrix-IslaParadiso-ow-5, Matrix-IslaParadiso-ow-5 | 8340, 8022, 8178 | all checks pass |
| 01:20:08 | back-to-back | PASS | Matrix-IslaParadiso-b2b-9, Matrix-IslaParadiso-b2b-10 | 8052, 7928 | info flush waits: +0 (0 means the worker had finished before the second save) |
| 01:20:24 | unpaused | PASS | Matrix-IslaParadiso-run-6 | 9648 | all checks pass |
| 01:20:45 | reload | pending | Matrix-IslaParadiso-reload-off-3, Matrix-IslaParadiso-reload-on-3 | 9129, 9328 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |

4 cases, 4 passed, 0 failed, pending: reload

## 2026-10-25T01:23 IslaParadiso reload check of Matrix-IslaParadiso-reload-on-3 (ON), build 6303d236+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 01:23:11 | reload | pending |  |  | info sims in world: 259 before save, 259 after load (NPCs come and go)<br>info ScriptError files (ON): none since the save at 01:20:45 |

0 cases, 0 passed, 0 failed, pending: reload

## 2026-10-25T01:25 IslaParadiso reload check of Matrix-IslaParadiso-reload-off-3 (OFF), build 6303d236+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 01:25:39 | reload | PASS |  |  | info sims in world: 259 before save, 259 after load (NPCs come and go)<br>info ScriptError files (OFF): none since the save at 01:20:45<br>info ScriptError files per save: ON 0, OFF 0 |

1 cases, 1 passed, 0 failed
