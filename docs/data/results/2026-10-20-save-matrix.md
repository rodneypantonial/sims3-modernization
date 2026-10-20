# Save matrix 2026-10-20

Tool: `tools/harness/save-matrix.ts` (docs/TESTING.md). Times UTC. Save ms = ts3native `save:` total per save.

## 2026-10-20T09:53 UTC NewDowntownWorld, start save null, build 64f6b5d+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:53:34 | control | PASS | Matrix-NewDowntownWorld-off-6, Matrix-NewDowntownWorld-off-7 | 11118, 9975 | info unstable types: d84e7fc6 (ridgeport_0x09ffe3d7.nhd d84e7fc6:00000000:6c1100337657b720 changed) |
| 09:54:00 | identical | **FAIL** | Matrix-NewDowntownWorld-off-8, Matrix-NewDowntownWorld-on-2 | 10568, 8326 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 09:54:25 | per-fix | PASS | Matrix-NewDowntownWorld-pf-off-2, Matrix-NewDowntownWorld-pf-fastreftable-2 | 10652, 9002 | skip cachedreflection alone: fix not available in this game (ini/mod)<br>skip footprint alone: fix not available in this game (ini/mod)<br>skip compress alone: fix not available in this game (ini/mod)<br>skip flush alone: fix not available in this game (ini/mod) |
| 09:54:59 | overwrite | **FAIL** | Matrix-NewDowntownWorld-ow-2, Matrix-NewDowntownWorld-ow-2, Matrix-NewDowntownWorld-ow-2 | 8901, 8623, 8352 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 09:55:21 | back-to-back | **FAIL** | Matrix-NewDowntownWorld-b2b-5, Matrix-NewDowntownWorld-b2b-6 | 9019, 8677 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 09:55:38 | unpaused | **FAIL** | Matrix-NewDowntownWorld-run-2 | 9085 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 09:56:10 | build-mode | PASS | Matrix-NewDowntownWorld-build-1 | - | skip save in build mode: game refuses: the game cannot save right now |
| 09:56:22 | verify-counters | **FAIL** | Matrix-NewDowntownWorld-verify-2 | 9126 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 09:56:47 | reload | pending | Matrix-NewDowntownWorld-reload-off-1, Matrix-NewDowntownWorld-reload-on-1 | 11026, 8994 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |

8 cases, 3 passed, 5 failed (identical, overwrite, back-to-back, unpaused, verify-counters), pending: reload

## 2026-10-20T09:58 UTC SunsetValley, start save null, build 64f6b5d+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 09:58:57 | control | PASS | Matrix-SunsetValley-off-12, Matrix-SunsetValley-off-13 | 10486, 10265 | info unstable types: d84e7fc6 (et Valley_0x0859db3c.nhd d84e7fc6:00000000:6c11001b8b52bb30 changed) |
| 09:59:20 | identical | **FAIL** | Matrix-SunsetValley-off-14, Matrix-SunsetValley-on-4 | 10102, 8656 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 09:59:42 | per-fix | PASS | Matrix-SunsetValley-pf-off-4, Matrix-SunsetValley-pf-fastreftable-4 | 10225, 8529 | skip cachedreflection alone: fix not available in this game (ini/mod)<br>skip footprint alone: fix not available in this game (ini/mod)<br>skip compress alone: fix not available in this game (ini/mod)<br>skip flush alone: fix not available in this game (ini/mod) |
| 10:00:11 | overwrite | **FAIL** | Matrix-SunsetValley-ow-4, Matrix-SunsetValley-ow-4, Matrix-SunsetValley-ow-4 | 8675, 8614, 8507 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 10:00:31 | back-to-back | **FAIL** | Matrix-SunsetValley-b2b-7, Matrix-SunsetValley-b2b-8 | 8644, 8472 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 10:00:47 | unpaused | **FAIL** | Matrix-SunsetValley-run-4 | 8933 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 10:01:19 | build-mode | PASS | Matrix-SunsetValley-build-1 | - | skip save in build mode: game refuses: the game cannot save right now |
| 10:01:29 | verify-counters | **FAIL** | Matrix-SunsetValley-verify-4 | 8870 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 10:01:51 | reload | pending | Matrix-SunsetValley-reload-off-1, Matrix-SunsetValley-reload-on-1 | 10332, 8755 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |

8 cases, 3 passed, 5 failed (identical, overwrite, back-to-back, unpaused, verify-counters), pending: reload

## 2026-10-20T10:03 UTC IslaParadiso, start save null, build 682b72f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 10:04:01 | control | PASS | Matrix-IslaParadiso-off-9, Matrix-IslaParadiso-off-10 | 11208, 10133 | info unstable types: none<br>info known per-save type d84e7fc6: did not differ in this world |
| 10:04:23 | identical | **FAIL** | Matrix-IslaParadiso-off-11, Matrix-IslaParadiso-on-3 | 10234, 8720 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 10:04:46 | per-fix | PASS | Matrix-IslaParadiso-pf-off-4, Matrix-IslaParadiso-pf-fastreftable-4 | 10146, 8945 | skip cachedreflection alone: fix not available in this game (ini/mod)<br>skip footprint alone: fix not available in this game (ini/mod)<br>skip compress alone: fix not available in this game (ini/mod)<br>skip flush alone: fix not available in this game (ini/mod) |
| 10:05:16 | overwrite | **FAIL** | Matrix-IslaParadiso-ow-3, Matrix-IslaParadiso-ow-3, Matrix-IslaParadiso-ow-3 | 8845, 8917, 8654 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 10:05:36 | back-to-back | **FAIL** | Matrix-IslaParadiso-b2b-5, Matrix-IslaParadiso-b2b-6 | 8743, 8940 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 10:05:51 | unpaused | **FAIL** | Matrix-IslaParadiso-run-3 | 8861 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 10:06:23 | build-mode | PASS | Matrix-IslaParadiso-build-1 | - | skip save in build mode: game refuses: the game cannot save right now |
| 10:06:33 | verify-counters | **FAIL** | Matrix-IslaParadiso-verify-3 | 8995 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |
| 10:06:54 | reload | pending | Matrix-IslaParadiso-reload-off-1, Matrix-IslaParadiso-reload-on-1 | 10112, 8544 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |

8 cases, 3 passed, 5 failed (identical, overwrite, back-to-back, unpaused, verify-counters), pending: reload

## 2026-10-20T10:08 UTC NewDowntownWorld, start save null, build 682b72f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 10:11:19 | travel | PASS | Matrix-NewDowntownWorld-travel-start-29, Matrix-NewDowntownWorld-travel-away-18, Matrix-NewDowntownWorld-travel-home-10 | 9669, 16728, 9801 | info fixes not available: cachedreflection (PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off), footprint, compress, flush<br>info trip route: travel_vacation: queued car on the world lot<br>info travel saves target:  untouched (travel saves went to the Matrix save) |

1 cases, 1 passed, 0 failed

## 2026-10-20T10:13 UTC NewDowntownWorld, start save null, build 682b72f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 10:15:25 | travel | PASS | Matrix-NewDowntownWorld-travel-start-30, Matrix-NewDowntownWorld-travel-away-19, Matrix-NewDowntownWorld-travel-home-11 | 9444, 11260, 9728 | info fixes not available: cachedreflection (PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off), footprint, compress, flush<br>info trip route: travel_vacation: queued car on the world lot<br>info travel saves target:  untouched (travel saves went to the Matrix save) |

1 cases, 1 passed, 0 failed

## 2026-10-20T10:17 UTC NewDowntownWorld, start save null, build 682b72f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 10:19:43 | travel | PASS | Matrix-NewDowntownWorld-travel-start-31, Matrix-NewDowntownWorld-travel-away-20, Matrix-NewDowntownWorld-travel-home-12 | 9464, 8237, 9878 | info fixes not available: cachedreflection (PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off), footprint, compress, flush<br>info trip route: travel_vacation: queued car on the world lot<br>info travel saves target:  untouched (travel saves went to the Matrix save) |

1 cases, 1 passed, 0 failed

## 2026-10-20T10:21 UTC NewDowntownWorld, start save null, build 682b72f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 10:24:49 | travel | PASS | Matrix-NewDowntownWorld-travel-start-32, Matrix-NewDowntownWorld-travel-away-21, Matrix-NewDowntownWorld-travel-home-13 | 9540, 7837, 10127 | info fixes not available: cachedreflection (PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off), footprint, compress, flush<br>info trip route: travel_university: queued moving van<br>info travel saves target:  untouched (travel saves went to the Matrix save) |

1 cases, 1 passed, 0 failed

## 2026-10-20T10:26 UTC NewDowntownWorld, start save null, build 682b72f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 10:28:16 | travel | **FAIL** | Matrix-NewDowntownWorld-travel-start-33, Matrix-NewDowntownWorld-travel-away-22 | 9547, 10129 | trip back: Error: travel_home: trip not queued ({"state":"Inactive","almanac":true,"in_use":false,"portal_broken":false,"traveler_summoned":true,"portal_active":false,"modal_ok":false,"sim":"Tiara Angelista","queued":false}); at failure: world FutureWorld dest FutureWorld in_travel false household Angelista state LiveModeState; queue empty; travel_state {"away":true,"day_of_trip":1,"in_travel":false,"days_left":null,"on_vacation":true,"sims_in_world":206,"minisims":290,"world":"FutureWorld","destination":"FutureWorld","household":"Angelista","household_in_world":1,"world_file":"Oasis Landing.world","household_members":1,"in_world":true,"world_type":"Future","travelers":1,"trip_length":0} |

1 cases, 0 passed, 1 failed (travel)

## 2026-10-20T10:30 UTC NewDowntownWorld, start save null, build 682b72f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 10:31:21 | travel | **FAIL** | Matrix-NewDowntownWorld-travel-start-34, Matrix-NewDowntownWorld-travel-away-23 | 9494, 9866 | trip back: Error: travel_home: trip not queued ({"state":"Inactive","almanac":true,"in_use":false,"portal_broken":false,"traveler_summoned":true,"portal_active":false,"modal_ok":false,"sim":"Tiara Angelista","queued":false}); at failure: world FutureWorld dest FutureWorld in_travel false household Angelista state LiveModeState; queue empty; travel_state {"away":true,"day_of_trip":1,"in_travel":false,"days_left":null,"on_vacation":true,"sims_in_world":206,"minisims":290,"world":"FutureWorld","destination":"FutureWorld","household":"Angelista","household_in_world":1,"world_file":"Oasis Landing.world","household_members":1,"in_world":true,"world_type":"Future","travelers":1,"trip_length":0} |

1 cases, 0 passed, 1 failed (travel)

## 2026-10-20T12:23 UTC NewDowntownWorld, start save null, build 69c598a+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 12:24:53 | travel | PASS | Matrix-NewDowntownWorld-travel-start-35 | 11596 | info trip route: travel_university: queued moving van<br>info stay: staying in University (--stay) |

1 cases, 1 passed, 0 failed

## 2026-10-20T12:32 UTC NewDowntownWorld, start save null, build 6fd9cfc+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 12:34:07 | travel | PASS | Matrix-NewDowntownWorld-travel-start-36 | 10457 | info trip route: travel_vacation: queued car on the world lot<br>info stay: staying in France (--stay) |

1 cases, 1 passed, 0 failed
