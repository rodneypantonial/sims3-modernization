# Save matrix 2026-10-21

Tool: `tools/harness/save-matrix.ts` (docs/TESTING.md). Times UTC. Save ms = ts3native `save:` total per save.

## 2026-10-21T09:01 UTC NewDowntownWorld, start save null, build 02b4324+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:02:04 | control | PASS | Matrix-NewDowntownWorld-off-9, Matrix-NewDowntownWorld-off-10 | 10123, 8866 | info unstable types: d84e7fc6 (ridgeport_0x09ffe3d7.nhd d84e7fc6:00000000:6c1100337657b720 changed) |
| 09:02:28 | identical | PASS | Matrix-NewDowntownWorld-off-11, Matrix-NewDowntownWorld-on-3 | 9010, 8830 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |
| 09:03:03 | per-fix | PASS | Matrix-NewDowntownWorld-pf-off-3, Matrix-NewDowntownWorld-pf-routeasync-1, Matrix-NewDowntownWorld-pf-discourage-1 | 8993, 8938, 8735 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |
| 09:03:36 | overwrite | PASS | Matrix-NewDowntownWorld-ow-3, Matrix-NewDowntownWorld-ow-3, Matrix-NewDowntownWorld-ow-3 | 8867, 8785, 8801 | all checks pass |
| 09:03:58 | back-to-back | PASS | Matrix-NewDowntownWorld-b2b-7, Matrix-NewDowntownWorld-b2b-8 | 8812, 8765 | info flush waits: +0 (0 means the worker had finished before the second save) |
| 09:04:16 | unpaused | PASS | Matrix-NewDowntownWorld-run-3 | 9007 | all checks pass |
| 09:04:47 | build-mode | PASS | Matrix-NewDowntownWorld-build-1 | - | skip save in build mode: game refuses: the game cannot save right now |
| 09:04:59 | verify-counters | PASS | Matrix-NewDowntownWorld-verify-3 | 9044 | skip compress verify ran: save compress not loaded<br>skip footprint verify ran: verify off: set [FootprintIndex] Verify=1 in ts3native.ini (read at game start only) |
| 09:05:22 | reload | pending | Matrix-NewDowntownWorld-reload-off-2, Matrix-NewDowntownWorld-reload-on-2 | 8945, 9260 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |

8 cases, 8 passed, 0 failed, pending: reload

## 2026-10-21T09:08 NewDowntownWorld reload check of Matrix-NewDowntownWorld-reload-on-2 (ON), build 02b4324+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:08:02 | reload | pending |  |  | info sims in world: 270 before save, 270 after load (NPCs come and go)<br>info ScriptError files (ON): none since the save at 09:05:22 |

0 cases, 0 passed, 0 failed, pending: reload

## 2026-10-21T09:10 NewDowntownWorld reload check of Matrix-NewDowntownWorld-reload-off-2 (OFF), build 02b4324+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:10:44 | reload | PASS |  |  | info sims in world: 270 before save, 270 after load (NPCs come and go)<br>info ScriptError files (OFF): none since the save at 09:05:22<br>info ScriptError files per save: ON 0, OFF 0 |

1 cases, 1 passed, 0 failed

## 2026-10-21T09:12 UTC NewDowntownWorld, start save null, build 02b4324+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:15:42 | travel | PASS | Matrix-NewDowntownWorld-travel-start-37, Matrix-NewDowntownWorld-travel-away-24, Matrix-NewDowntownWorld-travel-home-14 | 10274, 10980, 10424 | info fixes not available: cachedreflection (PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off), footprint, compress, flush<br>info trip route: travel_future: queued portal Travel<br>info travel saves target:  untouched (travel saves went to the Matrix save) |

1 cases, 1 passed, 0 failed

## 2026-10-21T09:17 UTC NewDowntownWorld, start save null, build 02b4324+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:20:11 | travel | PASS | Matrix-NewDowntownWorld-travel-start-38, Matrix-NewDowntownWorld-travel-away-25, Matrix-NewDowntownWorld-travel-home-15 | 10280, 10394, 10595 | info fixes not available: cachedreflection (PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off), footprint, compress, flush<br>info trip route: travel_future: direct (no interaction)<br>info travel saves target:  untouched (travel saves went to the Matrix save) |

1 cases, 1 passed, 0 failed
