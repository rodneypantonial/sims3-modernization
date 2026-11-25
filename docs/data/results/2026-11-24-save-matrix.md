# Save matrix 2026-11-24

Tool: `tools/harness/save-matrix.ts` (docs/TESTING.md). Times UTC. Save ms = ts3native `save:` total per save.

## 2026-11-24T15:34 UTC NewDowntownWorld, start save null, build 5516fb5a+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 15:35:19 | control | PASS | Matrix-NewDowntownWorld-off-1, Matrix-NewDowntownWorld-off-2 | 9091, 7990 | info unstable types: d84e7fc6 (ridgeport_0x09ffe3d7.nhd d84e7fc6:00000000:6c1100337657b720 changed) |
| 15:35:44 | identical | PASS | Matrix-NewDowntownWorld-off-3, Matrix-NewDowntownWorld-on-1 | 7982, 8413 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |
| 15:36:22 | per-fix | PASS | Matrix-NewDowntownWorld-pf-off-1, Matrix-NewDowntownWorld-pf-routeasync-1, Matrix-NewDowntownWorld-pf-discourage-1 | 8093, 8064, 7950 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |
| 15:36:58 | overwrite | PASS | Matrix-NewDowntownWorld-ow-1, Matrix-NewDowntownWorld-ow-1, Matrix-NewDowntownWorld-ow-1 | 8234, 8089, 8338 | all checks pass |
| 15:37:21 | back-to-back | PASS | Matrix-NewDowntownWorld-b2b-1, Matrix-NewDowntownWorld-b2b-2 | 7930, 7987 | info flush waits: +0 (0 means the worker had finished before the second save) |
| 15:37:39 | unpaused | PASS | Matrix-NewDowntownWorld-run-1 | 9207 | all checks pass |
| 15:38:11 | build-mode | PASS | Matrix-NewDowntownWorld-build-1 | - | skip save in build mode: game refuses: the game cannot save right now |
| 15:38:24 | verify-counters | PASS | Matrix-NewDowntownWorld-verify-1 | 9371 | skip compress verify ran: save compress not loaded<br>skip footprint verify ran: verify off: set [FootprintIndex] Verify=1 in ts3native.ini (read at game start only) |
| 15:38:49 | reload | pending | Matrix-NewDowntownWorld-reload-off-1, Matrix-NewDowntownWorld-reload-on-1 | 8863, 8914 | info routegate: N/A: no runtime switch, installed on both sides; its gates wait only while an async job runs (none with routeasync 0) |

8 cases, 8 passed, 0 failed, pending: reload
