## Summary

Draft handover for the EA app laptop session: **Eli diagnostics kit, EA 1.69 smoke** (after the rc-1119b EA test). Do not merge; close after the report.

- Asset (draft release, collaborators only): `eli-diag-ea169-1120.zip`, sha256 `05c9475cb221957c2e0de1a5b8ca2e60e05747df97045ac43248b34d7af0e4f9`
  Download: `gh release download handover-eli-diag-ea169-1120 -R rodneypantonial/sims3-modernization` (draft: if the tag is not found, use `gh release list`)
- Build: ts3native 0.36.247-elidiag1 on the stable v0.2.12 EA tree, read-only diagnostics only (`[PcCheck]`, `[Diag] ModTime`, `[FrameDiag]`, on only in the kit's diag ini). ts3fixes rel0211b. Source branch `test/ea169-eli-diag-1120` (local, commits d34154d33, 93c7dbea0).

## Test (game alone; no Apex/bridge on that machine)

1. Back up `Documents\Electronic Arts\The Sims 3` and the Game\Bin asi/ini pair.
2. Run `TS3Kit.cmd`, pick the game folder, answer **D**.
3. Play ~15 min. Check `ts3native.log`: header 0.36.247-elidiag1, `OK   framediag`, `framediag: Present timed`, a `framediag:` line about each minute, pccheck lines (clock from PDH, power plan name not `?`), modtime lines naming assemblies/packages.
4. `Game\Bin\fps-summary.txt` rewritten each minute with a bottleneck verdict and the GPU line (adapter list, GPU preference for TS3.exe).
5. Run the kit's collect option; the zip must be masked (no user name or paths).
6. No crash or new ScriptError. Restore the Bin pair.

Under-bridge Present timing stays untested here (no bridge); note it.

## Report

PASS/FAIL, header line, the check lines above, collect-zip masking OK, any crash/ScriptError file names (masked).

🤖 Assisted with AI together with @rodneypantonial
