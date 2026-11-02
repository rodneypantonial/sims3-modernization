# 234. EA 1.69 product/entitlement gate: live diagnosis (BaseGame-only flags)

EA laptop, EA app 1.69.47 (`Sims3Launcher.exe` = TS3.exe, base 0x400000, no ASLR), 2026-11-02 13:09-13:20 PHT. Question: do the
19 installed packs stay unmounted because the entitlement answer arrives after the gate (timing), or because the game does not
match what the EA app answers (data)? Static context: mount at 0x596A70, gate 0x596990 -> service create + wait 0xA42A30, loaded test
0xA42080, fields `[0x1244FB0]+0x28` (answered) / `+0x24`, flags dwords `[0x1227E50]`, `[0x1227E54]`. Player thread:
https://forums.ea.com/discussions/the-sims-3-technical-issues-en/all-19-owned-packs-installed-but-game-loads-base-game-only-sunset-valley/13626580

## Runs (times PHT; t = seconds after the game process appeared)
| Run | Method | Result |
|---|---|---|
| A 13:09:36 | Frida attach 0.5 s after start, `tools/probe/ep-gate-probe.js` (hooks + 100 ms reads) | .text decrypted at t=0.6; service pointer, flags stayed 0 for 60 s; no LSX traffic in the EA app log; game exited by itself at t=63 s, EA app: `Game process exit code=[-1]`, `Game session was too short: [63] seconds`. [verified] Early Frida attach is not usable on the EA build (cause inferred: DRM). |
| B 13:15:17 | OpenProcess + ReadProcessMemory, 100 ms, no injection | t=1.507 service created (`+0x28`=0); reader bug; re-read at t=49: `+0x28`=1, `+0x24`=0, `[0x1227E54]`=0x1, `[0x1227E50]`=0x1. |
| C 13:18:46 | same, from process start, 75 s | t=0: all 0. t=1.508: service created, `+0x28`=0, `+0x24`=0x45F0C48 (pointer-like, not 0). t=2.011: `+0x28`=1, `+0x24`=0, `[0x1227E54]`=0x1, `[0x1227E50]`=0x1, all in the same 100 ms tick. Unchanged until the kill at t=75. |

EA app log (`EADesktopVerbose.log`, run C, UTC): `Launching [...Sims3Launcher.exe]` 05:18:48.577, LSX `ChallengeResponse` 05:18:50.096
(t≈1.5), `QueryEntitlements` request 05:18:50.112, `QueryEntitlementsResponse` 05:18:50.513 (t≈1.9; GraphQL `sdkEntitlements` 401 ms).
Run of 13:02 (other agent, loaded a lot): same shape, request 05:02:02.331, response 05:02:02.733 (399 ms).

## Verdict
- (a) Flags after init = **0x1 (BaseGame only)** in both read runs: the player problem is reproduced on this laptop. [verified]
- (b) The answer arrives ~0.4-0.5 s after the service is created, inside the 3000 ms wait; the flags are written in the same 100 ms tick
  as `+0x28` turns 1. The gate waited for the answer and then mounted only the base game. [verified by 100 ms sampling]
- (c) Skip-wait branch (`jne` 0xA42A38): not taken. Live: `[0x1244FB0]` was 0 until the gate ran (t=1.5). Static: 0xA42A30 has three
  callers (0x596994 = gate, 0x59498A, 0x594D38) and the parent 0x597E0F calls the gate (0x597E1B) before the other two (0x597E28, 0x597E2F). [verified]
- (d) **Not a timing problem; no "always wait" patch**: it would change nothing. The mismatch is in what the game compares against the
  answer (data), so more read-only RE is needed, not a runtime patch.

## Data found for the next step
- Answer: 55 entitlements (filter `ORIGIN_DOWNLOAD`), 26 distinct `ItemId`s. 21 exist as strings in the game image (offer-id string
  table 0x1021F90-0x10227xx, e.g. `OFB-EAST:55107`, `DR:156751300`, `Origin.OFR.50.0000058`); 5 do not: `OFB-EAST:46851`,
  `Origin.OFR.50.0001139` (22 of the 55 entries), `.0002168`, `.0003439`, `.0004172`. [verified]
- The gate's product loop (0x5969C2-0x596A54) compares each pending product's id `[edi+8]` with 12-byte entries at 0x11A7B40 and with the
  list `[0x1227E48]`, then calls 0xA421F0 (entitlement lookup by name) and mounts via 0x596810 only on a hit. Which name each pack expects
  and what 0xA421F0 matches is the open point. [inferred from the disassembly, not traced]
- `0x1227E54` has no code reference in the image; `0x1227E50` has ~140 and read 0x1 live, so the "registry mask at `[0x1227E50]+0x10`"
  attribution is wrong (the value is not a pointer). Which of the two is GetProductFlags is unverified; both say BaseGame only.
- Registry `HKLM\SOFTWARE\WOW6432Node\Sims`: all 19 pack keys exist with only `Install Dir`; the base key also has `DisplayName`,
  `Locale`, `Product GUID`, `ProductID`, `SKU`. Compare with the Steam machine before assuming the game needs the extra values. [verified]

## Next
1. Read-only trace of one pack through the loop (late attach after the menu is safe for Frida, docs/230 dump-image pattern): log the
   id `[edi+8]`, the matched table entry and the 0xA421F0 argument/result; compare with the 26 answered `ItemId`s.
2. Diff the pack registry values with the Steam machine; if the EA app install lacks values the loop reads, that is an EA install bug:
   report it in the forum thread with this timeline as evidence.
Scripts: `tools/probe/ep-gate-probe.js` (Frida, late attach only); the ReadProcessMemory reader lived in the scratchpad (not kept).

## Diagnostic run 2026-11-02 (EaDiag hooks)
13:44:24 PHT, `ts3native-ea169.asi` 2ac69a35 (CI 27dea5d) with `[EaDiag] ProductGate=1`, probe launch, no save loaded, log
`probe-out/ea-eadiag-1102.ts3native.log` (gitignored). EA app LSX: `QueryEntitlements` 05:44:25.051Z, response 05:44:25.488Z
(t≈2.4 s, 55 entitlements, 26 distinct `ItemId`, as before; fields `Type ItemId EntitlementId EntitlementTag Group ResourceId ...`,
`Group=""`, `EntitlementTag="ORIGIN_DOWNLOAD"`: the only pack-identifying field is `ItemId`).
- `OK   EaDiag: ProductGate log-only hooks, 5/5 installed` but **the three gate hooks never fired** (no `after wait`, no product
  line, no mount line): the gate 0x596990 was not called. 296 lookups, all from 0x5949BC/0x5949F9 (the follow-up scan 0x594980,
  which tries every row of the offer table 0x11A7B40 and ORs the row's product bit into this+0xC) plus one `"TESTID"`.
- `eadiag: after gate: flags [0x1227E50] 0x00000000 [0x1227E54] 0x00000000; service ... +0x28 1 +0x24 0x00000000; 296 lookups, 0 products`
  (the flags get their 0x1 later than 0x597E34; runs B/C read them at 100 ms granularity).
- Lookups: 273 distinct offer ids tried, 21 found (`-> 1`), none `-> 2`: the answer was loaded in time. Found per product id of the
  12-byte table (pid, bit, folder name from 0x594780, matched `ItemId`): 1 base `OFB-EAST:55107` · 4 EP01 `DR:137311600` ·
  5 SP01 `DR:156751300` · 6 EP02 `DR:169598400` · 7 SP02 `DR:191313700` · 8 EP03 `DR:194011300`+`Origin.OFR.50.0001227` ·
  9 SP03 `DR:215625800` · 10 EP04 `DR:219720800` · 11 SP04 `DR:227821700` · 12 EP05 `OFB-EAST:60968` · 13 SP05 `DR:231553700` ·
  14 EP06 `OFB-EAST:60970` · 15 SP06 none (folder absent, not owned) · 16 SP07 `OFB-EAST:43769` · 17 EP07 `OFB-EAST:49019` ·
  18 EP08 `OFB-EAST:60964` · 19 SP08 `OFB-EAST:60962` · 20 EP09 `OFB-EAST:109545788` · 21 EP10 `Origin.OFR.50.0000058` ·
  22 SP09 `Origin.OFR.50.0000124` · 23 EP11 `OFB-EAST:64320`. Every installed pack has an answered id in the game's own table, so
  the bundle id `Origin.OFR.50.0001139` (22 entries) is irrelevant: the game compares `ItemId` and would have matched. [verified]

### Verdict: the packs never reach the gate (not pending); the entitlement data is complete
Static trace of the gate's caller (0x5979xx-0x597E34, image `probe-out/ts3-ea169.bin`) [inferred from the disassembly, not traced live]:
1. Per product row of 0x11A7A38 `{seq, pid, pid+1}` the loop calls 0x7D1820(seq): `RegOpenKeyExW(HKLM, "Software\Sims")`, enumerates
   the subkeys, reads each key's `ProductID` (1001 = base, else must equal `seq`: EP01=2, SP01=3, EP02=4 ... EP11=21) and only then its
   `Install Dir`. The EA app wrote the 19 pack keys with **`Install Dir` only** (section above, re-checked today), so no pack is found.
2. Fallback: `GetFileAttributesW(<found dir> + "/" + "EP01")` must be a directory, then `<dir>/GameData/Shared/Packages/FullBuild_pNN.package`
   (table 0x11A8940) must exist. The found dir is the empty string when 0x7D1820 failed, so the path is `/EP01` = drive root: not a
   directory, product skipped (0x597C1A -> 0x597DB5). The folders `C:\Program Files\EA Games\The Sims 3\EP01..EP11, SP01..SP09` and their
   FullBuild packages exist, the game never looks there.
3. Only the base (`ProductID` 1001 present) is found and mounted directly (0x597D84 -> 0x596810, no entitlement check). The pending list
   this+0x4BC stays empty, `0x597E0F: cmp [eax],eax; je 0x597E20` skips the gate, the follow-up scan 0x594980 still builds the full
   entitlement mask (20 bits) but nothing uses it for mounting. Flags end as 0x1 = BaseGame only.
Not verified live: the `/EP01` path string and the mask check `(bit & this+8) == bit` at 0x597D0F (both need one more log-only run with hooks
at 0x597B33, 0x597C18, 0x597CA1, 0x597D14). The registry diff with the Steam machine (expected: pack keys with `ProductID`) is still open.

### EA-app-side fix options (no code patch of the check, project policy)
- Repair the game in the EA app (Manage > Repair) and re-add the packs in Manage > Add-ons: the installer should write `ProductID` (plus the
  `DisplayName`/`Locale`/`Product GUID`/`SKU` the base key has) into every `HKLM\SOFTWARE\WOW6432Node\Sims\The Sims 3 <Pack>` key.
- Re-login / entitlement refresh is **not** the fix: the answer already carries every installed pack.
- Forum post to EA: 1.69.47 EA-app installs of the packs write only `Install Dir` to the pack registry keys; the game identifies an
  installed pack by the key's `ProductID` value (1001 base, 2..21 packs) and otherwise looks for `/EP01` on the drive root, so all 19 packs
  are treated as not installed although the entitlements are granted; ask for the installer to write `ProductID` per pack (give the
  registry dump and this timeline; log lines above as evidence).
