# 859. `[PaintShift]`: terrain paint of a lot placed from the bin lands half a tile off (2026-11-21, contributor, default off, NOT RUN)

Code: `native/ts3native/paintshift.cpp` (selftest `paintshift-test.h`), switch `[PaintShift] Enabled` (ts3native.ini, default 0), counters v924 `ps*` at 0x6B68 after the v923 hold (docs/registry.md). Tags: [verified] = read in the disassembly of the Steam 1.67 TS3W.exe (or the decompiled scripts, read locally, not committed); [inferred] = reasoning, not traced.

Players report that terrain paint does not line up with the house after a lot goes through the Library or the town bin: "about a tile" off, sometimes depending on the lot size or rotation.

## 1. Export: no change to the paint [verified]

Saving a lot to the bin writes the lot terrain with the same function as a normal save (`0xAF2970`, version 14, callers `0xAC5F09` and the export `0xAC62EF` in `0xAC6200`). Each paint layer is a block-compressed 2D array of `tiles * 4 + 1` samples per side (4 samples per tile; LotTerrain init `0xAEB730`), written as stored (`0xAF08B0`). No transform runs at export, so bin files hold the paint exactly as painted.

## 2. Import: the paint gets its own offset [verified]

Placing runs `DownloadContent.ImportLotContents` (`LotRotationAngle`, `LotPosition` front-left .. back-right) -> `0x997540` -> `0xAD5000` -> lot import `0xACE4A0`. Its header step `0xAC7420` -> `0xAC44C0` picks the rotation and stores two transforms built by `0xCB3120` (thiscall: dst width, dst depth, src width, src depth, rotation, position):

- `ctx+0x54`: cells (lot tiles), `0xAC4ED2`;
- `ctx+0x64`: grid points (tiles + 1), `0xAC4F02`. Objects (`0xAB5FC0`, float `0xCB32B0`), stilts (`0xC1DF40`) and heights (`0xAECCE0`, integer `0xCB3210`) use it.

`0xCB3080` sets the offset per axis from the position: 0, the far side `dst - src`, or centred `(dst - src) >> 1` (an arithmetic shift, so it rounds down). Rotation then adds `src - 1` on the turned axes.

The terrain paint import `0xAF34F0` (lot import -> `0xAC79E0` -> `0xAF34F0`, only caller `0xAC7D28`) builds a third transform for the paint samples at `0xAF36ED`: `0xCB3120(4*dstW + 1, 4*dstD + 1, 4*srcW + 1, 4*srcD + 1, ctx rotation, ctx position)`, then moves every sample with `0xCB3210` and writes it with `0xAEFD60`.

- Far side and rotation terms: `4 * (dst - src)` and `4 * src` samples, exactly 4 x the object terms.
- Centred axis: `(4 * (dst - src)) >> 1 = 2 * (dst - src)` samples, while objects get `4 * ((dst - src) >> 1)`. With an odd size difference the paint lands 2 samples (half a tile) further along that axis than the house, walls, floors and stilts.

Example: a 20x30 lot placed centred on a 25x30 lot. The house moves 2 tiles in, the paint 2.5 tiles. Same-size placement, corner positions and even differences match. With a quarter turn the source width and depth swap, so which axis shifts depends on the rotation. Standard lot sizes differ by 5 or 10 tiles, so a 5-tile difference is the common case.

Not found: a whole-tile shift. Every other term agrees exactly, so a same-size placement moves the paint with the house [verified for the import transform; the renderer side, `LotRenderer/FloorGeometry` paint UVs, not read]. A report of a full tile on a same-size lot would be another cause.

## 3. Fix (`Enabled=1`)

The call at `0xAF36ED` goes to `PsXform`. It runs EA's `0xCB3120` as before, then builds the grid-point transform for the same sizes, rotation and position (EA's `0xCB3120` on tiles + 1, as `0xAC4F02` does) and sets the paint offsets to 4 x its offsets. Only the odd centred case changes. `psImports` counts paint transforms, `psFixed` the corrected ones. The log line `paint shift: lot WxD on WxD, rotation r, position p: paint offset (x, z) -> (x', z') samples` shows the first 8, then powers of two.

The argument block `0xAF36AD`-`0xAF36EC` (the `* 4 + 1` sizes and context reads) and EA's call are byte-checked first. On a mismatch nothing is written (`SKIP PaintShift`, `psMode` -1). EA 1.69 and retail 1.67: no address, SKIP. Household rule: not applicable (lot build data).

Existing data: bin files are not damaged (§1). A lot already placed with the shift keeps its shifted paint in the save; the fix cannot tell those lots apart and does not move them. Placing the lot again from the bin with the fix on puts the paint in place. A shifted lot saved to the bin again carries its shift.

## 4. Evidence and repro

- Static: §1-§3 [verified]. Build: `build.cmd` with the selftest (a port of the transform's offset rules; same size, odd centred, quarter turn, corner, even difference, invalid rotation; patch site on a buffer); never run in game.
- Repro (not added to the agent): paint a border along the walls of a 20x30 lot, save it to the Library, place it on an empty 25x30 lot with the centre position. Off: the border is half a tile off the walls along the 25-tile axis. On: `psFixed` 1, the border matches. A 20x30 lot on a 20x30 lot shows no shift either way (`psFixed` 0).
