# Queue lotstream 2026-10-16

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/lotstream-2026-10-16.jsonl`.

## 2026-10-16T00:39 UTC, build def4796, jobs bp-lotstream-s3ss-on, bp-lotstream-s3ss-off, ip-lotstream-s3ss-on

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-lotstream-s3ss-on | 00:39 (08:39) | 8.9 | PASS | PASS: profile 6 min, unseeded (rng-compare.ts) | 0 | 2147.8/1852.1/1532.9 -> 2265.5/2038.6/1340.2 | lotAddMaxUs -284, pumpCalls +22519, pumpUs +1762245, pumpMaxUs -53527, lotScoreCalls +5640, lotScoreUs +84927 | G13 / docs/28 §5.5: lot streaming cost during the street-zoom pan, S3SS LotStreamingOptimizations on (installed default) |
| bp-lotstream-s3ss-off | 00:48 (08:48) | 8.5 | PASS | PASS: profile 6 min, unseeded (rng-compare.ts) | 0 | 2178.4/1857.3/1532.9 -> 2284.1/2085.3/1323.2 | lotAdds +2, lotAddUs +389, lotAddMaxUs -115, lotAddObjs +284, lotAddMaxObjs +65, lotRemoves +2, lotRemoveUs +289, lotRemoveMaxUs +209, pumpCalls +25313, pumpUs +1849513, pumpMaxUs -18602, pumpLotPasses +2, lotScoreCalls +6025, lotScoreUs +179433, lotScoreChanged +2, lotSetOn +2, lotSetOff +2 | same with S3SS LotStreamingOptimizations off: the game's own unthrottled add (compare lotAddMaxMs, pumpMaxMs) |
| ip-lotstream-s3ss-on | 00:57 (08:57) | 8.3 | PASS | PASS: profile 6 min, unseeded (rng-compare.ts) | 0 | 2161.8/1864.5/1548.8 -> 2306.6/2054.4/1324 | lotAdds +81, lotAddUs +2048, lotAddMaxUs -266, lotAddObjs +7919, lotRemoves +8, lotRemoveUs +644, lotRemoveMaxUs +105, pumpCalls +16901, pumpUs +2176504, pumpMaxUs -9119, pumpLotPasses +83, lotScoreCalls +8278, lotScoreUs +194284, lotScoreChanged +13, lotSetOn +8, lotSetOff +8 | same as bp-lotstream-s3ss-on on Isla Paradiso (other lot density, houseboats) |
