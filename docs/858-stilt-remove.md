# 858. `[StiltRemove]`: stilts of a lot placed from the bin cannot be removed (2026-11-21, contributor, default off, NOT RUN)

Code: `native/ts3native/stiltremove.cpp` (selftest `stiltremove-test.h`), switch `[StiltRemove] Enabled` (ts3native.ini, default 0), counters v923 `sr*` at 0x6B48 (docs/registry.md). Tags: [verified] = read in the disassembly of the Steam 1.67 TS3W.exe (or the decompiled scripts, read locally, not committed); [inferred] = reasoning, not traced.

Players report that the stilts of an Island Paradise stilted foundation "remain forever" once the lot went through the Library (saved to the bin and placed back): delete and sledgehammer leave them standing, also after a save and reload.

## 1. How stilts are stored [verified]

- Each lot owns a stilted foundation manager at `[lot+0x2B8]` (ctor `0xC1D860`, init `0xC1F780`, made in the lot loads `0xAC9B10` / `0xACA060`). It keeps three maps: stilt groups (`+0x1C`, "StiltGroupHashMap": group id -> group record with its own stilt map), a location map (`+0x3C`, "StiltLocHashMap": stilt key -> group id) and 8x8-tile partitions (`+0x5C`, "StiltPartitionData"), which the query `0xC1F4D0` rebuilds from the group maps (`0xC1F390`) when the dirty byte `+0x78` is set. Both loads set it.
- A stilt's key is `0xC1AB00({x, z, level})` = `floor(z*8+0.5) << 12 | level << 24 | floor(x*8+0.5)`; level is 0 at every call site.
- A stilt record holds a render position (`+0x00`, a vector) and its grid point (`+0x14` / `+0x18`). `0xC1ABE0` places the render position: it starts at the grid point and moves it 0.245·√2 ≈ 0.35 or 0.245/√2 ≈ 0.17 tile inward when neighbouring foundation cells are missing (edges and corners).
- Building (`0xC1E4D0`), the save load's upgrade of data version <= 2 (`0xC1DBB0`) and both lookups of the foundation tools key a stilt by its **grid point**. The save (`0xC1BE80`, chunk `0x0F3F5476`, version 3; per group `0xC1B830`) writes the stored key, and the version-3 load (`0xC1ECC0` -> `0xC1DBB0`) reads it back unchanged.

## 2. The bug [verified]

Placing a lot from the Library or the town bin runs `DownloadContent.ImportLotContents` (`Bin.PlaceLot`, the place tool's `PlayFlowModel.BeginPlaceLotContents`; internal call `0x997540`) -> `0xAD5000` -> the lot import `0xACE4A0` -> the stilt import `0xC1EDD0` -> one group at a time `0xC1DF40`. The group loader gives the group a new id, rotates the render position and the grid point into the new lot (`0xCB32B0`, rotation steps plus offsets) and stores both, but builds the key from the rotated **render position** (`[esp+0x1C]` / `[esp+0x40]`, call at `0xC1E2FB`), not from the grid point every other path uses. With the inset of §1 the two differ for every edge and corner stilt (0.17 tile is more than the key's 1/16-tile step).

So the stilts are registered (the guess "the import does not register them" is wrong: group map, location map and partitions all get them), only under a key nothing computes:

- Removal path (delete / sledgehammer of a foundation or deck): `RemoveAnyStiltedFoundationsInBlock` `0xCA7E00` (callers `0xCAEDA9`, `0xCAF1BB`) or the undoable `UserToolUndoableDeckFoundation` `0xD1EA20` (undo / redo `0xD1D2B0`) -> collect `0xC1F810` -> partitions `0xC1F4D0` -> per stilt `0xC1E9E0`, which keys by the partition's grid point and looks it up in the location map. A miss returns without an edit: the stilt is never collected. The apply step `0xC1EED0` -> remove `0xC1E8F0` would miss the same way.
- The save writes the bad key, the next load reads it back: the stilt stays for good. The same miss also drops the move edits `0xC1E9E0` makes when foundations around a stilt change [inferred: same lookup, not traced in game].
- Stilts whose render position equals the grid point (no inset) keep a matching key and stay removable [inferred from `0xC1ABE0`'s branches].

## 3. Fix (`Enabled=1`)

- Import: the call at `0xC1E2FB` goes to `StImportKey`, which keys the stilt by the rotated grid point the record stores (`[esp+0x64]` / `[esp+0x68]` in the group loader's frame). `srImport` counts imported stilts, `srImportFixed` those whose render-position key differed.
- Load: the version test at `0xC1DD49` (`cmp [ebp+0x10],2; ja`) jumps to `StLoadStub`, which keys every loaded stilt by its grid point (EA's own version <= 2 path) and continues at `0xC1DD86`. `srLoadFixed` counts version-3 keys that differed: stilts an earlier import broke, repaired in memory and written right by the next save. A stilt with a good key gets the same key back, so a clean save loads as before.
- Both sites and the frame offsets the import hook reads are byte-checked first (`0xC1DD49`, `0xC1E2D0`-`0xC1E2FF`, `0xC1E192`-`0xC1E1B2`); on a mismatch nothing is written (`SKIP StiltRemove`, `srMode` -1). EA 1.69 and retail 1.67: no address, SKIP.
- Log: `stilt key fixed at import` / `stilt key repaired at load` with grid point and keys, the first 8 of each, then powers of two.
- Save data: the repaired keys are saved, so the switch stays opt-in. Two stilts at one grid point would merge into one record at load [inferred: EA's inserts are find-or-insert; building never makes two]. Household rule: not applicable (lot build data).

## 4. Evidence and repro

- Static: all of §1-§3 [verified]. Build: `build.cmd` with the selftest (fake key function with EA's formula; import and load keys on fake frames; patch sites on buffers) and the retail table build compile; never run in game.
- Repro (not added to the agent): Island Paradise; build a stilted foundation with a room on a lot, save the lot to the Library, place it on an empty lot (Edit Town or the agent's MemStress place-lot cycle with `library: true`, which goes through `BeginPlaceLotContents`), then delete the foundation in Build mode. Off: the edge stilts stay, also after save and reload. On: `srImportFixed` > 0 at placement and the stilts go with the foundation. A save broken before: load it with `Enabled=1`, `srLoadFixed` > 0, the stilts can be deleted, and the next save keeps the fix. The agent has no foundation delete step yet; a native step calling `RemoveAnyStiltedFoundationsInBlock` would need its block arguments traced first.
