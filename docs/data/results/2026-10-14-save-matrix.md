# Save matrix 2026-10-14

Tool: `tools/harness/save-matrix.ts` (docs/TESTING.md). Times UTC. Save ms = ts3native `save:` total per save.

## 2026-10-14T06:23 UTC NewDowntownWorld, start save Matrix-NewDowntownWorld-off-2.sims3, build c0803b8+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 06:24:21 | control | PASS | Matrix-NewDowntownWorld-off-1, Matrix-NewDowntownWorld-off-2 | 11409, 11233 | info unstable types: d84e7fc6 (ridgeport_0x09ffe3d7.nhd d84e7fc6:00000000:6c1100337657b720 changed) |
| 06:24:46 | identical | PASS | Matrix-NewDowntownWorld-off-3, Matrix-NewDowntownWorld-on-1 | 11225, 8762 | all checks pass |
| 06:25:57 | per-fix | PASS | Matrix-NewDowntownWorld-pf-off-1, Matrix-NewDowntownWorld-pf-fastreftable-1, Matrix-NewDowntownWorld-pf-footprint-1, Matrix-NewDowntownWorld-pf-compress-1, Matrix-NewDowntownWorld-pf-flush-1 | 11204, 9437, 11165, 12075, 12654 | all checks pass |
| 06:26:30 | overwrite | PASS | Matrix-NewDowntownWorld-ow-1, Matrix-NewDowntownWorld-ow-1, Matrix-NewDowntownWorld-ow-1 | 9352, 9198, 9166 | all checks pass |
| 06:26:52 | back-to-back | PASS | Matrix-NewDowntownWorld-b2b-1, Matrix-NewDowntownWorld-b2b-2 | 9168, 9133 | info flush waits: +0 (0 means the worker had finished before the second save) |
| 06:27:09 | unpaused | PASS | Matrix-NewDowntownWorld-run-1 | 9842 | all checks pass |
| 06:27:40 | build-mode | **FAIL** |  |  | case ran: Error: save accepted: not reached within 30 s |
| 06:28:11 | verify-counters | **FAIL** |  |  | case ran: Error: save accepted: not reached within 30 s |
| 06:28:41 | reload | **FAIL** |  |  | case ran: Error: save accepted: not reached within 30 s |

9 cases, 6 passed, 3 failed (build-mode, verify-counters, reload)

## 2026-10-14T06:29 UTC NewDowntownWorld, start save Matrix-probe-save.sims3, build c0803b8+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 06:30:04 | build-mode | PASS | Matrix-NewDowntownWorld-build-1 | - | skip save in build mode: game refuses: the game cannot save right now |
| 06:30:15 | verify-counters | **FAIL** | Matrix-NewDowntownWorld-verify-1 | 9161 | compress verify ran: log mismatches no compress line |
| 06:30:26 | reload | pending | Matrix-NewDowntownWorld-reload-1 | 9243 | all checks pass |

2 cases, 1 passed, 1 failed (verify-counters), pending: reload

## 2026-10-14T06:33 NewDowntownWorld reload check of Matrix-NewDowntownWorld-reload-1, build c0803b8+dirty

| time | case | result | saves | save ms | failed checks / notes |
|---|---|---|---|---|---|
| 06:33:58 | reload | **FAIL** |  |  | loaded the reload save: loaded null, expected Matrix-NewDowntownWorld-reload-1<br>no new ScriptError files: ScriptError_<pc>__10-14-2026__14-33-08__83403516.xml |

1 cases, 0 passed, 1 failed (reload)
