# Queue nraas-upstream-verify 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/nraas-upstream-verify-2026-10-22.jsonl`.

## 2026-10-22T03:15 UTC, build 6ea9ffa, jobs bp-nraas-upstream-base, bp-nraas-upstream-v2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-nraas-upstream-base | 03:15 (11:15) | 0.1 | **FAIL** | install-fixes install exit 1 | - | ? -> ? | - | docs/145 §5 + docs/146 §5 behaviour A/B, side 1 (6 min, fresh load): NRaas Overwatch + ErrorTrap built from Chain-Reaction/NRaas master with our pipeline (fix nraas-upstream-base, installed for the job, originals restored after), repro bp-nraas-upstream-verify under /taskstall 250, ts3fixes spread switches off. Needs build/nraas-upstream (node tools/setup/build-nraas-upstream.ts <NRaas clone>). Re |
| bp-nraas-upstream-v2 | 03:16 (11:16) | 0.1 | **FAIL** | install-fixes install exit 1 | - | ? -> ? | - | side 2 (6 min, fresh load, same save/seed/build): the PR #45/#46 v2 builds (fix nraas-upstream-v2). Pass = same results as side 1: same CorrectionLogger count, etspread verify 0 after the load-up scan, overwatchspread verify 'after 0' with the same 'before' count, OnTimer ran all cleanups (owspread_theirs +1, one notice); plus the markers seen (owspread_upstream 1, etspread_upstream 1) and no /tas |
