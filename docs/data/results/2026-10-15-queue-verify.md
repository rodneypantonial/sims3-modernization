# Queue verify 2026-10-15

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/verify-2026-10-15.jsonl`.

## 2026-10-15T14:15 UTC, build d2bf89d, jobs bp-verify, ip-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-verify | 14:15 (22:15) | 9.2 | PASS | PASS: profile 7 min, unseeded (rng-compare.ts) | 0 | 2180.9/1835.5/1532.9 -> 2352/2097.8/1230.6 | vfThrD5D910Main +541, vfThrD5D910Sim +29, vfThrD5D910OffDevice +29, vfThrD5D910Us +127813, vfThr619540Main +30197, vfThr619540Us +11428012, vfThr619540MaxUs +16496, vfThr60D290Main +24378, vfThr60D290Us +6178341, vfThr6F3DC0Main +3177, vfThr6F3DC0Us +864594, vfThr599720Main +151195, vfThr599720Sim +7071, vfThr599720Other +97223, vfThr599720OffDevice +104294, vfThr599720Us +58057776, vfThr6F1B00Mai | docs/51: inference ledger probes, BP, one load: [Verify] all groups (vf counters v20), main-thread stacks, verify-sweep (moodlets, outfits, CAS, thumbnails + save, camera pan) |
| ip-verify | 14:25 (22:25) | 5.6 | **FAIL** | FAIL: profile 7 min, unseeded (rng-compare.ts) | 1 | 2194.4/1869.4/1516.9 -> 2257.6/2056.5/1310.6 | vfThrD5D910Main +710, vfThrD5D910Sim +9, vfThrD5D910OffDevice +9, vfThrD5D910Us +218588, vfThrD5D910MaxUs +6900, vfThr619540Main +13399, vfThr619540Us +6101580, vfThr619540MaxUs +25768, vfThr60D290Main +9307, vfThr60D290Us +3495517, vfThr6F3DC0Main +273, vfThr6F3DC0Us +32331, vfThr599720Main +66907, vfThr599720Sim +4123, vfThr599720Other +42356, vfThr599720OffDevice +46479, vfThr599720Us +29273268 | docs/51: the same in IP (second world for terrain LOD, water and lot streaming) |
