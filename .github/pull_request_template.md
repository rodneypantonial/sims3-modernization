## Summary

<!-- What changes and why. Link the issue: Closes #n -->

## Evidence

<!-- Build hash, ts3native version, non-default ts3native.ini keys, save, commands, and a before/after (or in-load A/B off/on) table. Link docs/data/results/<date>-<gh-user>-<topic>.md. Label claims [verified] / [community] / [inferred]. -->

| | before / off | after / on | change |
|---|---|---|---|
| sim iterations/s | | | |
| hitches/s (>= 50 ms) | | | |
| worst iteration (ms) | | | |
| stuck Sims | | | |

Crashes during testing: <!-- none, or crash-ledger.ts triage line + xcpt summary -->

## Checklist

- [ ] No EA files, decompiled code, third-party mod code, saves, dumps or captures in the diff (`git status` checked)
- [ ] No personal paths, machine names or emails
- [ ] Measured against the baseline on the same save (in-load A/B where a runtime switch exists), or explained why not
- [ ] New behaviour is off by default with a runtime switch
- [ ] Counter slots / ts3native counters version registered in `docs/registry.md`
- [ ] Repro added (`tools/harness/repros.json` + agent trigger) for bug fixes
- [ ] Self-checks pass (`native/ts3native/build.cmd`, `build-fixes.ts`, tool `--self-check`)
- [ ] Docs updated (`docs/NN-*.md`)
- [ ] `PLAN.md` "Resume here" updated
