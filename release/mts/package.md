# MTS download: what goes in the zip

One file for MTS: `Sims3Modernization_v<version>.zip`, built by `tools/mts-zip.ts` from the **same** three files as the GitHub release.

## Contents (allow-list, nothing else)

| Path in the zip | Source (default) | Checks before it goes in |
|---|---|---|
| `Mods/Packages/ts3fixes.package` | `build/ts3fixes.package` | named exactly `ts3fixes.package`; DBPF; has the `Ts3Fixes` assembly; **no** `Ts3Agent` or `PortalPlan` (test agent, `ts3-portalplan-test`); the four trip shortcuts `True` (docs/RELEASE.md "Before publishing" 6) |
| `Game/Bin/ts3native.asi` | `native/ts3native/out/ts3native.asi` | named exactly; 32-bit (x86) PE; has the `ts3native` marker |
| `Game/Bin/ts3native.ini` | `native/ts3native/out/ts3native.ini` | named exactly; text; has `[GC]` and `[RouteAsync]` |
| `README.txt` | `release/mts/README.txt` | text template; `{VERSION}` filled in, CRLF line ends for Notepad |

The folder layout mirrors where each file goes, so a player can see it at a glance; `README.txt` says the same in words. Not included on purpose: the ASI loader (`wininet.dll`, linked instead: other authors' files stay on their pages), `ts3agent.package`, `ts3-nointro.package`, `ts3-overwatch-nostuckcheck.package`, test packages, logs, PDBs.

## Build

```
node tools/mts-zip.ts --self-check
node tools/mts-zip.ts --final
```

`--final` also compares the sha256 of the package, asi and ini with the `docs/RELEASE.md` "v0.2.0 release build" table and fails on a mismatch or a `[to fill]`. Other paths: `--package`, `--asi`, `--ini`, `--readme`, `--version`, `--out` (default `release/mts/out/`, git-ignored).

The zip is deterministic: entries sorted, timestamp fixed at 2026-10-23 00:00, deflate level 9. The same inputs give the same zip bytes, so its sha256 can go into the checklist.

Trial build 2026-10-21 (the current local `build/ts3fixes.package`, the repo ini, an older 0.33.2 asi; written to a scratch folder only): 4 entries, 0.36 MB zip; read back by `tar -tvf` and .NET `ZipFile` without errors; passing `build/ts3agent.package` is refused.
