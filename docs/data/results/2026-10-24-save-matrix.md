# Save matrix 2026-10-24

Tool: `tools/harness/save-matrix.ts` (docs/TESTING.md). Times UTC. Save ms = ts3native `save:` total per save.

## 2026-10-24T01:11 UTC NewDowntownWorld, start save null, build 85cf1978+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 01:11:52 | control | PASS | Matrix-NewDowntownWorld-off-23, Matrix-NewDowntownWorld-off-24 | 8981, 7878 | info unstable types: d84e7fc6 (ridgeport_0x09ffe3d7.nhd d84e7fc6:00000000:6c1100337657b720 changed) |
| 01:12:14 | identical | PASS | Matrix-NewDowntownWorld-off-25, Matrix-NewDowntownWorld-on-7 | 7881, 7978 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |
| 01:12:47 | per-fix | PASS | Matrix-NewDowntownWorld-pf-off-4, Matrix-NewDowntownWorld-pf-routeasync-2, Matrix-NewDowntownWorld-pf-discourage-2 | 7958, 8054, 7976 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |
| 01:13:18 | overwrite | PASS | Matrix-NewDowntownWorld-ow-4, Matrix-NewDowntownWorld-ow-4, Matrix-NewDowntownWorld-ow-4 | 8011, 8004, 7931 | all checks pass |
| 01:13:39 | back-to-back | PASS | Matrix-NewDowntownWorld-b2b-9, Matrix-NewDowntownWorld-b2b-10 | 7866, 7886 | info flush waits: +0 (0 means the worker had finished before the second save) |
| 01:13:57 | unpaused | PASS | Matrix-NewDowntownWorld-run-4 | 9192 | all checks pass |
| 01:14:28 | build-mode | PASS | Matrix-NewDowntownWorld-build-1 | - | skip save in build mode: game refuses: the game cannot save right now |
| 01:14:40 | verify-counters | PASS | Matrix-NewDowntownWorld-verify-4 | 9148 | skip compress verify ran: save compress not loaded<br>skip footprint verify ran: verify off: set [FootprintIndex] Verify=1 in ts3native.ini (read at game start only) |
| 01:15:03 | reload | pending | Matrix-NewDowntownWorld-reload-off-3, Matrix-NewDowntownWorld-reload-on-3 | 9203, 9244 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |

8 cases, 8 passed, 0 failed, pending: reload

## 2026-10-24T01:17 NewDowntownWorld reload check of Matrix-NewDowntownWorld-reload-on-3 (ON), build 85cf1978+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 01:17:32 | reload | pending |  |  | info sims in world: 270 before save, 270 after load (NPCs come and go)<br>info ScriptError files (ON): none since the save at 01:15:03 |

0 cases, 0 passed, 0 failed, pending: reload

## 2026-10-24T01:20 NewDowntownWorld reload check of Matrix-NewDowntownWorld-reload-off-3 (OFF), build 85cf1978+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 01:20:01 | reload | PASS |  |  | info sims in world: 270 before save, 270 after load (NPCs come and go)<br>info ScriptError files (OFF): none since the save at 01:15:03<br>info ScriptError files per save: ON 0, OFF 0 |

1 cases, 1 passed, 0 failed

## 2026-10-24T01:21 UTC NewDowntownWorld, start save null, build 85cf1978+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 01:24:19 | travel | PASS | Matrix-NewDowntownWorld-travel-start-39, Matrix-NewDowntownWorld-travel-away-26, Matrix-NewDowntownWorld-travel-home-16 | 9039, 17682, 10941 | skip fixes skipped (not in this build/ini): cachedreflection (PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off), footprint (footprint index disabled (Enabled=0)), compress (save compress off (Enabled=0)), flush (save flush off (Enabled=0))<br>info trip route: travel_vacation: queued car on the world lot<br>info travel saves target:  untouched (travel saves went to the Matrix save) |

1 cases, 1 passed, 0 failed
