# Save matrix 2026-10-22

Tool: `tools/harness/save-matrix.ts` (docs/TESTING.md). Times UTC. Save ms = ts3native `save:` total per save.

## 2026-10-22T03:42 UTC NewDowntownWorld, start save null, build 8b682e9+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 03:42:37 | control | PASS | Matrix-NewDowntownWorld-off-12, Matrix-NewDowntownWorld-off-13 | 15651, 11952 | info unstable types: d84e7fc6 (ridgeport_0x09ffe3d7.nhd d84e7fc6:00000000:6c1100337657b720 changed) |
| 03:43:02 | identical | **FAIL** | Matrix-NewDowntownWorld-off-14, Matrix-NewDowntownWorld-on-4 | 11075, 9102 | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |

2 cases, 1 passed, 1 failed (identical)

## 2026-10-22T03:45 UTC NewDowntownWorld, start save null, build ed2aee8+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 03:47:16 | control | **FAIL** | Matrix-NewDowntownWorld-off-15, Matrix-NewDowntownWorld-off-16 | -, - | Matrix-NewDowntownWorld-off-15 result 0: no save line in ts3native.log<br>Matrix-NewDowntownWorld-off-16 result 0: no save line in ts3native.log<br>unstable types: control saves failed; identical/per-fix cannot run |
| 03:47:16 | identical | **FAIL** |  |  | case ran: control did not produce a baseline |

2 cases, 0 passed, 2 failed (control, identical)

## 2026-10-22T07:39 UTC NewDowntownWorld, start save null, build 91ad3f6f+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 07:40:21 | control | PASS | Matrix-NewDowntownWorld-off-17, Matrix-NewDowntownWorld-off-18 | -, - | info unstable types: d84e7fc6 (ridgeport_0x09ffe3d7.nhd d84e7fc6:00000000:6c1100337657b720 changed) |
| 07:40:43 | identical | **FAIL** | Matrix-NewDowntownWorld-off-19, Matrix-NewDowntownWorld-on-5 | -, - | fixes switched on: not available or did not switch: cachedreflection, footprint, compress, flush (check ts3native.ini Enabled=1 and ts3fixes); cachedreflection: PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off |

2 cases, 1 passed, 1 failed (identical)

## 2026-10-22T07:54 UTC NewDowntownWorld, start save null, build 53bdb56d+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 07:54:47 | control | PASS | Matrix-NewDowntownWorld-off-20, Matrix-NewDowntownWorld-off-21 | 12270, 9674 | info unstable types: d84e7fc6 (ridgeport_0x09ffe3d7.nhd d84e7fc6:00000000:6c1100337657b720 changed) |
| 07:55:10 | identical | PASS | Matrix-NewDowntownWorld-off-22, Matrix-NewDowntownWorld-on-6 | 9364, 7862 | skip fixes skipped (not in this build/ini): cachedreflection (PersistedClass.Init: already compiled; PersistedClassDB.GetPersistedClass: already compiled; Persist.GetPersistedTypeCode: already compiled; CachedReflection: not patched, off), footprint (footprint index disabled (Enabled=0)), compress (save compress off (Enabled=0)), flush (save flush off (Enabled=0)) |

2 cases, 2 passed, 0 failed
