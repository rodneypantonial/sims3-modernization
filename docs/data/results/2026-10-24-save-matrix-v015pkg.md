# Save matrix 2026-10-24

Tool: `tools/harness/save-matrix.ts` (docs/TESTING.md). Times UTC. Save ms = ts3native `save:` total per save.

## 2026-10-24T09:20 UTC NewDowntownWorld, start save null, build 85cf1978+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:21:13 | control | PASS | Matrix-NewDowntownWorld-off-26, Matrix-NewDowntownWorld-off-27 | 9731, 8887 | info unstable types: d84e7fc6 (ridgeport_0x09ffe3d7.nhd d84e7fc6:00000000:6c1100337657b720 changed) |
| 09:21:39 | identical | PASS | Matrix-NewDowntownWorld-off-28, Matrix-NewDowntownWorld-on-8 | 10290, 9265 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |
| 09:22:14 | per-fix | PASS | Matrix-NewDowntownWorld-pf-off-5, Matrix-NewDowntownWorld-pf-routeasync-3, Matrix-NewDowntownWorld-pf-discourage-3 | 8773, 8480, 8733 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |
| 09:22:46 | overwrite | PASS | Matrix-NewDowntownWorld-ow-5, Matrix-NewDowntownWorld-ow-5, Matrix-NewDowntownWorld-ow-5 | 8422, 8558, 8432 | all checks pass |
| 09:23:08 | back-to-back | PASS | Matrix-NewDowntownWorld-b2b-11, Matrix-NewDowntownWorld-b2b-12 | 8516, 8290 | info flush waits: +0 (0 means the worker had finished before the second save) |
| 09:23:27 | unpaused | PASS | Matrix-NewDowntownWorld-run-5 | 10409 | all checks pass |
| 09:23:59 | build-mode | PASS | Matrix-NewDowntownWorld-build-1 | - | skip save in build mode: game refuses: the game cannot save right now |
| 09:24:11 | verify-counters | PASS | Matrix-NewDowntownWorld-verify-5 | 9898 | skip compress verify ran: save compress not loaded<br>skip footprint verify ran: verify off: set [FootprintIndex] Verify=1 in ts3native.ini (read at game start only) |
| 09:24:36 | reload | pending | Matrix-NewDowntownWorld-reload-off-4, Matrix-NewDowntownWorld-reload-on-4 | 10069, 9773 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |

8 cases, 8 passed, 0 failed, pending: reload

## 2026-10-24T09:27 NewDowntownWorld reload check of Matrix-NewDowntownWorld-reload-on-4 (ON), build 85cf1978+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:27:05 | reload | pending |  |  | info sims in world: 270 before save, 270 after load (NPCs come and go)<br>info ScriptError files (ON): none since the save at 09:24:36 |

0 cases, 0 passed, 0 failed, pending: reload

## 2026-10-24T09:29 NewDowntownWorld reload check of Matrix-NewDowntownWorld-reload-off-4 (OFF), build 85cf1978+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:29:34 | reload | PASS |  |  | info sims in world: 270 before save, 270 after load (NPCs come and go)<br>info ScriptError files (OFF): none since the save at 09:24:36<br>info ScriptError files per save: ON 0, OFF 0 |

1 cases, 1 passed, 0 failed

## 2026-10-24T09:31 UTC NewDowntownWorld, start save null, build 85cf1978+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:33:51 | travel | PASS | Matrix-NewDowntownWorld-travel-start-40, Matrix-NewDowntownWorld-travel-away-27, Matrix-NewDowntownWorld-travel-home-17 | 9069, 17291, 10976 | skip fixes skipped (not in this build/ini): cachedreflection (PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off), footprint (footprint index disabled (Enabled=0)), compress (save compress off (Enabled=0)), flush (save flush off (Enabled=0))<br>info trip route: travel_vacation: queued car on the world lot<br>info travel saves target:  untouched (travel saves went to the Matrix save) |

1 cases, 1 passed, 0 failed
