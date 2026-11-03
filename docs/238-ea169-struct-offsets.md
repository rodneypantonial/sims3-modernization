# 238. EA app 1.69.47: struct and field offsets of ts3native (static, 2026-11-03 PHT)

Question: do the struct/field displacements our native code uses still hold in EA app 1.69.47? This closes the port gap
"1.69 struct offsets unchecked" (docs/232 §3). Static only, no game run. Inputs: Steam 1.67 `game-copy/bin/TS3W.exe`, the
decrypted 1.69 image `probe-out/ts3-ea169.bin`, `native/ts3native/ts3addr-ea169.h` (branch ea169-port at df04177),
`tools/hook-sites.ts`. Tool: `python tools/struct-offsets.py --csv F --md F --changes F` (iced-x86, `--self-check`).
[verified] = read in the images by the tool or by hand this session, [inferred] = reading of them.

## 1. Verdict [verified]
- **0 offsets differ.** 177 distinct values in 691 (file, value) rows: same 459, same-global 213, same-census 6, n/a 13
  (eadiag.cpp, EA-only). Nothing to fix, no override or `#if` added.
- Across 4,436 aligned function pairs (873 seeds = every code pair of the table that a native file names, plus aligned
  `call rel32` targets 2 levels deep), the only displacements that moved are one class in the EA product gate:
  `0x594A90`/`0x594AB0`/`0x594C20` (Steam) read `+0x4BC/+0x4C0` -> `+0x4E8/+0x4EC`, `+0xC` -> `+0x1C`,
  `+0x4C/+0x50` -> `+0x5C/+0x60` (1.69 `0x594AA0`/`0x594AC0`/`0x594C30`); at depth 4 (7,391 pairs) also
  `0x594A10` `+0x49C/+0x4A0` -> `+0x4C8/+0x4CC`. No Steam-side native file reads that class; eadiag.cpp reads the 1.69
  gate with 1.69 offsets taken from the 1.69 image (docs/234), so its rows are n/a.
- The lot fields named in the task: `Lot+0x174` / `+0x178` (size in metres, `0x654F9D` / `0x654FB8` -> `0x65623D` /
  `0x656258`) and `Lot+0x290` (per-level floor-height grid, `0x654EBB` -> `0x65615B`): same. 184/167/149 aligned uses each,
  none moved.
- The five `offset-changed` rows of `map.offsets.csv` (steerro `0x6119F0`, watchpt `0x62627B`, rtoro `0xF20360`, memexp
  `0xD819AA`, routesnap `0x74B600`) were wrong address matches or address immediates, not moved fields: the table maps
  them through `docs/data/ea169-overrides.csv` (same bytes at the override, e.g. rtoro getter `8B 41 20 C3` at
  `0x79BF70`), and the tool finds their fields unchanged (steerro 19 rows same, rtoro 18 same, watchpt 2 same + 2
  same-global, memexp 2 same + 3 same-global + 1 census, lotfd 12 same).

## 2. Mono runtime (MonoThread, MonoFast, AutShadow) [verified]
There is no mono.dll in either edition: Mono 1.2.3 and the Boehm GC are linked into the exe (docs/29), and both
`Game\Bin` folders hold no mono*.dll. So a file hash cannot answer it; the code comparison does. All 553 aligned function
pairs inside the Mono/Boehm range `0xE40000-0xEB2000` are identical up to relocated addresses (same instruction count,
shapes, and every displacement and immediate that is not an image address). The one listed difference, `0xE59880`, is the
mint jump table (data in .text, decoded as code). The range moved by +0x20..+0x70 in pieces (EA code elsewhere grew), not
inside functions. Verdict: every Mono struct offset (MonoThread, MonoDomain, interpreter frame, mint, GC) is the same.
The script host object outside that range (`MonoScriptHost`, `[0x011D4BBC]`) is covered by the rows below:
`+0x1658` same-global, `+0x1678` and `+0xBFC` same-census (6 uses each, same instruction sequence, `0xD74601` ->
`0xD74901`).

## 3. Method and limits
- Offsets: `+ 0xNN` (0x4-0x3FFF) in comment-stripped code of native/ts3native and native/worldimage/wimage.h, inline asm
  `[reg+N]`, and `static const WORD/DWORD k[] = {...}` offset lists (p4clone `kZero`). Tests, fakes, selftest.cpp and the
  generated tables are skipped. Decimal offsets (`+ 8`) are not collected; they are small and appear in every aligned
  function (same everywhere, see §1).
- Alignment: difflib over instruction shapes with displacements and immediates masked, so a moved field still lines
  up; every aligned `[reg+disp]` (base not esp/ebp) and `add/sub reg, imm` is compared.
- Verdicts: `same` = used in the file's own sites or their aligned callees, all unchanged; `same-global` = only other
  aligned functions use the value, none moved (also the class for offsets into our own buffers and formats, which the
  regex cannot tell apart from game fields); `same-census` = not in aligned code, but the count of `[reg+disp]`
  instructions with that value is equal in both .text sections (`0xE0C`/`0xE10` are TEB offsets, Windows ABI, and
  `0xFFF` an alignment mask in memexp.cpp: not game fields); `n/a` = EA-only file.
- Struct displacements embedded in expected or patched bytes (prologue arrays, `kSnIcDef`, arenatail patches) are not
  in this table: patch-bytes-check / `ts3bytes-ea169.h` already compare those bytes with the 1.69 image (docs/232 §3).
- Not done: offsets reached only through vtable slots (docs/232: 407 slot-shifted vtables, a separate class) and the
  meaning of each `same-global` value for its struct; the field column is the source comment of the first user line.
- Follow-up for ea-ini.ts (not changed here): the `ALWAYS_OFF` reason "struct layouts" for SteerRO, FpRebuildLock,
  Watchpoint, RouteToObjRO and MemDiag is not borne out by this check; turning them on is a separate decision that also
  needs their address rows and a smoke run.

## 4. Table
One row per value. Users: first line per file (all rows in the tool's `--csv`). Evidence: first aligned instruction pair
(Steam, 1.69); `-` where none.

| offset | struct/field (source comment) | users in native/ | Steam evidence | 1.69 evidence | verdict |
|---|---|---|---|---|---|
| 0x4 | parent | optsinit.cpp:28, arenatail.cpp:191, autshadow.cpp:733, crashfx.cpp:383, mintcall.cpp:84, monothread-m5.cpp:394 (+4 files) | 00401006 00401BF2 | 00401006 00401BE2 | same-global / same |
| 0x6 | inline cache pair (class, RuntimeMethod) | mintcall.cpp:243 | 00E5148E 00E514D4 | 00E514DE 00E51524 | same |
| 0x7 |  | mintcall.cpp:29 | 00E9B1FA 00E54A87 | 00E9B25A 00E54AD7 | same |
| 0x8 | table | planrepeat.cpp:67, autshadow.cpp:725, crashfx.cpp:403, mintcall.cpp:68, monothread-m5.cpp:768, p4.cpp:156 (+5 files) | 0040100D 00401C00 | 0040100D 00401BF0 | same-global / same / n/a |
| 0xA |  | hostcalls.cpp:113 | 0061150F 0061150F | 006125EF 006125EF | same-global |
| 0xB | docs/122 §2.2 object rows, docs/79 0x12, §14.17 5 | autshadow.cpp:1078 | 00702AA7 007218F5 | 00703777 00722615 | same-global |
| 0xC | caller retries with a larger table | wimage.h:191, ghost.cpp:38, lotuse.cpp:54, p4door.h:57, p4lotdump.h:36, unseenfb.cpp:240 (+29 files) | 00401013 00401C07 | 00401013 00401BF7 | same-global / same / n/a |
| 0xD |  | hostcalls.cpp:113 | 004E4876 0050B230 | 004E4AD6 0050B0C0 | same-global |
| 0xE | docs/122 §2.2 object rows, docs/79 0x12, §14.17 5 | autshadow.cpp:1078, hostcalls.cpp:113 | 0064A70F 0064C855 | 0064BE7F 0064DFC5 | same-global |
| 0xF |  | hostcalls.cpp:113 | 00E51548 00702A97 | 00E51598 00703767 | same-global |
| 0x10 | none, or a forward count inside a large object | hostcalls.cpp:113, lotsincos.cpp:131, lotuse.cpp:93, p4door.h:48, p4lotdump.h:49, unseenfb.cpp:241 (+27 files) | 00401016 00401C19 | 00401016 00401C09 | same-global / same / n/a |
| 0x12 | docs/122 §2.2 object rows, docs/79 0x12, §14.17 5 | autshadow.cpp:1078 | 0068FEFB 0068FF4F | 0069147B 006914CF | same-global |
| 0x14 | the `this` the execute passes to fn | p4lotdump.h:54, watchpt.cpp:102, autshadow-run.cpp:668, autshadow.cpp:726, imgguard.cpp:83, lotfd.cpp:94 (+13 files) | 00401019 00446029 | 00401019 00445EF9 | same-global / same / n/a |
| 0x18 | no blocks, empty free list | memdiag.cpp:73, p4lotdump.h:54, pools.cpp:133, replandedupe.cpp:92, unseenfb.cpp:242, autshadow.cpp:727 (+19 files) | 0040101C 00401BE8 | 0040101C 00401BD8 | same-global / same |
| 0x1B | the game's request2size 0x4E4770 | alloc.cpp:62 | 004E646B 004E4774 | 004E66CB 004E49E4 | same |
| 0x1C | the controller itself is alive: the walker's list holds a reference | unseenfb.cpp:243, crashfx.cpp:777, eafix.cpp:166, earlyout.cpp:145, gcroots.cpp:51, lotfd.cpp:110 (+9 files) | 00446063 004460E5 | 00445F33 00445FB5 | same-global / same / n/a |
| 0x1E | R | wastar.cpp:43 | 006C94B0 00C2CCFE | 006CA560 00C2B1DE | same-global |
| 0x20 | 0x80E8A0 would rebuild it (0x80E5B0, W6a) | lotsincos.cpp:132, autshadow.cpp:1535, avoidro.cpp:162, crashfx.cpp:942, eafix.cpp:174, footprint.cpp:226 (+16 files) | 00401023 004A792A | 00401023 004A78BA | same-global / same |
| 0x21 | docs/122 §2.2 object rows, docs/79 0x12, §14.17 5 | autshadow.cpp:1078 | 00668A97 004B3699 | 00669EF7 004B3509 | same-global |
| 0x22 |  | optsinit.cpp:29 | 006DBEB0 006DBFAB | 006DCD30 006DCE2B | same-global |
| 0x24 | EA lock kind byte: nonzero = CRITICAL_SECTION at +0 | autledger.cpp:126, earlyout.cpp:346, hostcalls.cpp:259, lotuse.cpp:80, p4lotdump.h:80, stalelot.cpp:45 (+28 files) | 00401026 00452E12 | 00401026 00452FE2 | same-global / same / n/a |
| 0x28 | another type, or the slot is someone else's: the game's path | gcthreads.cpp:40, memdiag.cpp:103, pools.cpp:249, savecompress.cpp:177, unseenfb.cpp:244, eafix.cpp:368 (+14 files) | 00401029 00580420 | 00401029 00A34C40 | same-global / same / n/a |
| 0x2C | the current waypoint's record: +0 n, +1 flags, +5 WORD refs[n] | earlyout.cpp:147, p4door.h:68, unseenfb.cpp:245, wastar.cpp:51, crashfx.cpp:77, jobqueue.cpp:45 (+7 files) | 0040102C 0050B274 | 0040102C 0050B104 | same-global / same / n/a |
| 0x30 | the container (0x68EC50 [verified]) | lotsincos.cpp:133, autshadow.cpp:1818, crashfx.cpp:77, monothread-m5.cpp:752, p4clone.cpp:190, replay.cpp:1825 (+3 files) | 0040102F 0049C715 | 0040102F 0049C865 | same-global / same |
| 0x34 |  | p4lotdump.h:57, arenatail.cpp:124, heightcache.cpp:175, memexp.cpp:209, mintcall.cpp:107, monothread-m5.cpp:767 (+4 files) | 00452D4D 00452D56 | 00452F1D 00452F26 | same-global / same |
| 0x36 |  | arenatail.cpp:124, mintcall.cpp:111 | 00E54024 00E56415 | 00E54074 00E56465 | same |
| 0x38 | 0x81C970: 8-byte ids in [+0x38, +0x3C) | memexp.cpp:210, p4lotdump.h:60, arenatail.cpp:125, avoidro.cpp:161, gcroots.cpp:337, mintcall.cpp:100 (+10 files) | 00401032 00452D4A | 00401032 00452F1A | same-global / same |
| 0x3C | init locals: memset([ebp-0x14], 0, rm+0x3C) as EA's CRT call | arenatail.cpp:125, goalgen.cpp:34, mintcall.cpp:133, obstructro.cpp:58, routesnap.cpp:477, rtoro.cpp:248 (+2 files) | 00E53F26 00E56434 | 00E53F76 00E56484 | same / n/a |
| 0x3E |  | arenatail.cpp:124, mintcall.cpp:102 | 00E53FD8 00E563FD | 00E54028 00E5644D | same |
| 0x40 | visitor 0x662DB0 with an options record: not modelled (0x634F10 always pushes 1) | plandeadline.cpp:122, crashfx.cpp:849, fgro.cpp:47, mintcall.cpp:160, obstructro.cpp:65, p4.cpp:1118 (+7 files) | 00401038 0049C60A | 00401038 0049C75A | same-global / same |
| 0x44 |  | autshadow-run.cpp:672, lotsincos.cpp:156, lotuse.cpp:85, p4door.h:54, crashfx.cpp:850, goalgen.cpp:33 (+9 files) | 0040103B 00401C34 | 0040103B 00401C24 | same-global / same |
| 0x48 | 0x62EF30 (GetOrInsert) on the goal's lot, called per survivor by the original: same answer... | earlyout.cpp:224, p4door.h:54, avoidro.cpp:149, crashfx.cpp:81, fgro.cpp:44, gvro.cpp:48 (+12 files) | 00401C75 00452E04 | 00401C65 00452FD4 | same-global / same |
| 0x4A |  | p4door.h:69 | 0063CC8D 0063CD25 | 0063E27D 0063E315 | same-global |
| 0x4C | 0x62EF30 (GetOrInsert) on the goal's lot, called per survivor by the original: same answer... | lotsincos.cpp:157, p4door.h:42, avoidro.cpp:149, earlyout.cpp:344, fgro.cpp:44, gvro.cpp:48 (+12 files) | 00452DE9 00452DF3 | 00452FB9 00452FC3 | same-global / same |
| 0x50 | R | p4.cpp:1132, p4door.h:43, wastar.cpp:42, cellmemo.cpp:54, lotfd.cpp:88, replay.cpp:822 (+1 files) | 00452E48 004AA1B4 | 00453018 004AA084 | same-global / same |
| 0x54 | a START (I-109-37), so such a cut or clone is refused, not rebased | unseenfb.cpp:236, wastar.cpp:113, fgro.cpp:42, p4clone.cpp:296, replay.cpp:804, satb.cpp:392 (+2 files) | 00452E7C 005DDD64 | 0045304C 005DECB4 | same-global / same |
| 0x58 | a START (I-109-37), so such a cut or clone is refused, not rebased | wimage.h:188, lotsincos.cpp:134, p4.cpp:611, eafix.cpp:309, p4clone.cpp:296, replay.cpp:1293 (+2 files) | 00452D8B 0059979D | 00452F5B 00599F8D | same-global / same |
| 0x5C |  | p4door.h:71, wastar.cpp:143, crashfx.cpp:849, eafix.cpp:310, satb.cpp:1575 | 00452DF0 0049C766 | 00452FC0 0049C8B6 | same-global / same |
| 0x60 |  | p4door.h:71, wastar.cpp:169, waterjps.cpp:195, values.cpp:455 | 00452650 00452E0A | 00452820 00452FDA | same-global / same |
| 0x64 | a START (I-109-37), so such a cut or clone is refused, not rebased | p4clone.cpp:296, earlyout.cpp:344, replay.cpp:804, snap.cpp:423 | 00452E4B 00452E6C | 0045301B 0045303C | same-global / same |
| 0x68 | a START (I-109-37), so such a cut or clone is refused, not rebased | p4.cpp:611, earlyout.cpp:350, p4clone.cpp:296, replay.cpp:1293, snap.cpp:423 | 00452E7F 0049C7A4 | 0045304F 0049C8F4 | same-global / same |
| 0x6C |  | monothread-m5.cpp:397 | 00E6EEEC | 00E6EF4C | same |
| 0x70 | returns 0 (no decrement first), else stores count - 1 and returns it. | lotfd.cpp:61, replay.cpp:271, slotro.cpp:45 | 0064E8E0 0064E8E4 | 0064FEC0 0064FEC4 | same |
| 0x74 | the level-0 Q of lot graph g (as 0x6320D0), else the first set | p4.cpp:1116, p4clone.cpp:308, p4lotdump.h:71, replay.cpp:772, snap.cpp:424, steerro.cpp:525 | 0049C7D1 00632500 | 0049C921 00633AF0 | same-global / same |
| 0x78 |  | p4.cpp:1117, p4clone.cpp:308, p4door.h:70, p4lotdump.h:72, planrepeat.cpp:70, replandedupe.cpp:63 (+4 files) | 0049C7EE 005997E5 | 0049C93E 00599FD5 | same-global / same / n/a |
| 0x7C |  | p4door.h:70, planrepeat.cpp:70, replandedupe.cpp:63, replay.cpp:1499, eadiag.cpp:102 | 0049C7FA 0049CB03 | 0049C94A 0049CC53 | same-global / same / n/a |
| 0x80 |  | values.cpp:157 | 006763EA 006763EA | 006777BA 006777BA | same |
| 0x88 | +0x88 W, +0x8C job block (polygon set) | earlyout.cpp:57, p4clone.cpp:782, satb.cpp:411 | 0049C843 0059A0FF | 0049C993 0059A8EF | same-global / same |
| 0x8C |  | memdiag.cpp:103, p4clone.cpp:328, snap.cpp:380, p4.cpp:633, values.cpp:217, verify.cpp:619 | 0049C857 005E21A3 | 0049C9A7 005E3083 | same-global / same |
| 0x90 |  | lotuse.cpp:85, replay.cpp:477, values.cpp:220 | 0049C880 005E213B | 0049C9D0 005E301B | same-global / same |
| 0x94 | status | lotuse.cpp:85, memdiag.cpp:102, p4.cpp:630, planrepeat.cpp:90, replandedupe.cpp:73, rctrim.cpp:39 (+5 files) | 0049C871 005997FD | 0049C9C1 00599FED | same-global / same |
| 0x98 |  | memdiag.cpp:102, planrepeat.cpp:87, p4.cpp:634, rctrim.cpp:39, verify.cpp:618 | 0049C888 004E1DFA | 0049C9D8 004E1FEA | same-global / same |
| 0x9C |  | planrepeat.cpp:88, gcroots.cpp:50, p4.cpp:635 | 0049C651 0049C659 | 0049C7A1 0049C7A9 | same-global / same |
| 0xA0 | &klass->byval_arg (0xE6339A); MonoType.type at +6 (0xE9C3B9) | memdiag.cpp:94, arenatail.cpp:71, earlyout.cpp:222, p4.cpp:1142, rtoro.cpp:144, satb.cpp:461 (+3 files) | 0049C614 0049CC08 | 0049C764 0049CD58 | same-global / same |
| 0xA4 |  | memdiag.cpp:95, p4clone.cpp:255, earlyout.cpp:342, verify.cpp:613 | 0049C61E 004B1AFC | 0049C76E 004B196C | same-global / same |
| 0xA8 | +0xA8 start point (+0xB4 goal ref and +0xB8 goal point stay out) | earlyout.cpp:58, p4clone.cpp:254 | 0049C637 0059A037 | 0049C787 0059A827 | same-global |
| 0xAC |  | p4clone.cpp:254, eafix.cpp:266 | 00401C56 0049C92B | 00401C46 0049CA7B | same-global / same |
| 0xB0 |  | p4.cpp:631, autshadow.cpp:1549, crashfx.cpp:989, lotfd.cpp:85, p4clone.cpp:286, steerro.cpp:535 | 0049C9F0 0062519F | 0049CB40 006266DF | same-global / same |
| 0xB4 |  | unseenfb.cpp:247, earlyout.cpp:343, values.cpp:481 | 00636CD0 0063D075 | 006382C0 0063E665 | same-global / same |
| 0xB8 | not static, or thread-static | wimage.h:187, p4.cpp:632, unseenfb.cpp:247, memexp.cpp:190, satb.cpp:400, steerro.cpp:718 | 005DDD06 005DDE80 | 005DEC56 005DEDD0 | same-global / same |
| 0xBC |  | p4clone.cpp:925 | 0062FA44 0063355D | 00631034 00634B4D | same-global |
| 0xC0 | group link or a parented rider | p4clone.cpp:740, steerro.cpp:537, unseen.cpp:746 | 006251D6 00636C21 | 00626716 00638211 | same-global / same |
| 0xC4 |  | autshadow.cpp:976, p4clone.cpp:740, verify.cpp:596 | 00658420 0081C4CC | 006594E0 0081B67C | same-global / same |
| 0xC8 |  | p4clone.cpp:740 | 0059930B 0062FA3E | 00599AFB 0063102E | same-global |
| 0xCC | bucket array 0x114C900, which every empty hash then sees (crash G, docs/67 §9) | p4.cpp:309, p4clone.cpp:768, heightcache.cpp:170 | 0059934C 005AD915 | 00599B3C 005AE165 | same-global / same |
| 0xD0 | 0x8307F3: the object record's bit | heightcache.cpp:170, rtoro.cpp:483, steerro.cpp:538 | 00C2A5E2 00C2A868 | 00C28B32 00C28DB8 | same |
| 0xD8 | bucket array 0x114C900, which every empty hash then sees (crash G, docs/67 §9) | p4clone.cpp:768 | 00452E60 005E2287 | 00453030 005E3167 | same-global |
| 0xDC |  | p4clone.cpp:149, snap.cpp:294 | 00452E66 004E1E20 | 00453036 004E2010 | same-global |
| 0xE0 | facing flags, destination set | replandedupe.cpp:68, snap.cpp:295, p4clone.cpp:150, rtoro.cpp:153, unseen.cpp:742 | 00452E4E 004E1E29 | 0045301E 004E2019 | same-global / same |
| 0xE4 | reference (0x652C66, 0x652C8E) and appends its 10 blocks to our list | p4clone.cpp:775 | 004E1E7D 0074C860 | 004E206D 0074D7A0 | same-global |
| 0xEA |  | mintcall.cpp:29 | 00E5C876 | 00E5C8C6 | same-global |
| 0xEC | no release of the clone's P or of W' itself | mintcall.cpp:29, p4clone.cpp:738 | 0062DA2D 0074C96C | 0062F01D 0074D8AC | same-global |
| 0xF0 | no release of the clone's P or of W' itself | p4clone.cpp:738 | 004E51C5 | 004E5425 | same |
| 0xF4 |  | p4clone.cpp:776 | 0074C916 00760ECF | 0074D856 00761E8F | same-global |
| 0xF8 |  | p4clone.cpp:776 | 00452638 00452D39 | 00452808 00452F09 | same-global |
| 0xFC |  | p4clone.cpp:741 | 005935AA 0059361E | 0059331A 0059338E | same-global |
| 0x100 |  | p4clone.cpp:741, values.cpp:157 | 00758CA2 00758CB1 | 00759CB2 00759CC1 | same-global / same |
| 0x104 |  | p4clone.cpp:741 | 0083CF6D 0083CF83 | 0083BBDD 0083BBF3 | same-global |
| 0x108 |  | p4clone.cpp:769 | 00452DDB 005178FA | 00452FAB 0051775A | same-global |
| 0x10E |  | rtsites.cpp:115 | 0082C429 0082C485 | 0082B0F9 0082B155 | same |
| 0x114 |  | p4clone.cpp:769 | 00C62006 00D5DA07 | 00C613D6 00D5DA27 | same-global |
| 0x11C | lot-exit masks and +0x124 (rebuilt per plan) | p4clone.cpp:784 | 005178CF 0065EA79 | 0051772F 0065FB39 | same-global |
| 0x120 |  | replay.cpp:765 | 0062E052 0062E067 | 0062F642 0062F657 | same |
| 0x124 |  | replay.cpp:764 | 0062AB49 0062B96E | 0062BF09 0062CD2E | same |
| 0x127 |  | eafix.cpp:459 | 00B7BF05 00B85445 | 00B7A4C5 00B83945 | same-global |
| 0x128 | finally stack depth, pending clause | mintcall.cpp:78, p4clone.cpp:745 | 0065EADB 0065EAE9 | 0065FB9B 0065FBA9 | same-global |
| 0x12C | finally stack depth, pending clause | mintcall.cpp:79, p4clone.cpp:770 | 00819086 00846A14 | 00818236 00845434 | same-global |
| 0x130 |  | p4clone.cpp:770, mintcall.cpp:491 | 005DDC2C 005DDC2C | 005DEB7C 005DEB7C | same-global / same |
| 0x138 |  | p4clone.cpp:770 | 00651E08 | 00653448 | same |
| 0x13C | arena chunk with room | p4clone.cpp:770, mintcall.cpp:197, monothread-m5.cpp:395 | 005DDBC3 005DDBC3 | 005DEB13 005DEB13 | same-global / same |
| 0x140 | facing flags, destination set | replandedupe.cpp:67, mintcall.cpp:69, monothread-m5.cpp:526 | 0068D13F 006EBE79 | 0068E72F 006ECC09 | same-global / same |
| 0x143 |  | eafix.cpp:460 | 00818FD4 00814C3C | 00818184 00813CFC | same-global |
| 0x144 | facing flags, destination set | replandedupe.cpp:67, mintcall.cpp:72 | 007607D0 007608B8 | 00761790 00761878 | same-global / same |
| 0x149 |  | mintcall.cpp:318 | 00819018 00E544A7 | 008181C8 00E544F7 | same-global |
| 0x150 |  | p4clone.cpp:770 | 0068D19E 006EBFD6 | 0068E78E 006ECD66 | same-global |
| 0x154 |  | p4clone.cpp:770 | 00792E50 00C6C4B4 | 00793FE0 00C6B834 | same-global |
| 0x158 | 0x81C970: 8-byte ids in [+0x38, +0x3C) | p4clone.cpp:770, obstructro.cpp:57, replandedupe.cpp:59, ts3native.cpp:3876, unseen.cpp:269 | 0065F04F 006606BC | 0066010F 0066177C | same-global / same |
| 0x160 |  | p4clone.cpp:745 | 005DE025 005DE025 | 005DEF75 005DEF75 | same-global |
| 0x164 |  | p4clone.cpp:770 | 0065F820 0065F820 | 006608E0 006608E0 | same-global |
| 0x168 |  | p4clone.cpp:770 | 00714188 00D5DDFC | 00714E98 00D5DE1C | same-global |
| 0x16C | the active lot: plans skip it too | p4clone.cpp:311, replay.cpp:1792 | 0082B70C 0082B70C | 0082A43C 0082A43C | same |
| 0x170 |  | p4clone.cpp:770 | 0068D1B2 006EBE65 | 0068E7A2 006ECBF5 | same-global |
| 0x174 |  | p4clone.cpp:770, verify.cpp:596, (game) Lot size x in metres (steerro fake world writes it; game reads it at 0x654F9D) | 006254B5 | 006269F5 | same |
| 0x178 |  | (game) Lot size y in metres (game read 0x654FB8) | 00654FB8 | 00656258 | same |
| 0x188 |  | p4clone.cpp:770 | 00820620 00AC1AD3 | 0081F580 00AC1213 | same-global |
| 0x18C |  | p4clone.cpp:770 | 00AC1ADB 00658089 | 00AC121B 00659149 | same-global |
| 0x190 | dx | p4.cpp:1139, p4clone.cpp:770, p4door.h:47, lotfd.cpp:92, lotsincos.cpp:61, replay.cpp:746 (+1 files) | 00627E2E 0062E6BE | 006291DE 0062FCAE | same-global / same |
| 0x198 | dz | lotsincos.cpp:63 | 00D7E649 00D7E649 | 00D7EA09 00D7EA09 | same-global |
| 0x1A0 |  | eafix.cpp:371, lotsincos.cpp:86 | 005DDB79 005DDC17 | 005DEAC9 005DEB67 | same |
| 0x1B4 |  | values.cpp:456 | 00C61FE0 | 00C613B0 | same |
| 0x1CC |  | rtsites.cpp:115 | 00C6888F 00C688A6 | 00C67C0F 00C67C26 | same |
| 0x1E0 |  | rctrim.cpp:54 | 00733E73 00737597 | 00734B63 00738247 | same |
| 0x1E4 |  | rctrim.cpp:54 | 00733E8D 007375A8 | 00734B7D 00738258 | same |
| 0x1F4 | the IC's own replan count | replandedupe.cpp:169 | 007654BA 00765589 | 0076647A 00766549 | same-global |
| 0x200 |  | values.cpp:157 | 006E4FA1 006E4FA9 | 006E5D61 006E5D69 | same-global |
| 0x210 | the member handle 0x6DB810 read: [m+8], low bits 3 set = unresolved (returns 0) | crashfx.cpp:314 | 006F735C 006F735C | 006F805C 006F805C | same |
| 0x270 | the lock FUN_004A8E00 takes per record | savecompress.cpp:472 | 005E2377 0065854E | 005E3257 0065960E | same-global |
| 0x290 |  | (game) Lot per-level floor-height grid (docs/101, read 0x654EBB) | 00654EBB | 0065615B | same |
| 0x29C |  | crashfx.cpp:850 | 006F7B93 006F7BE3 | 006F8893 006F88E3 | same |
| 0x2A0 |  | crashfx.cpp:849 | 006F738E 006F7398 | 006F808E 006F8098 | same |
| 0x2C0 | slot children ids (0x952E40 iterator): not cloned | rtoro.cpp:394 | 0082E8F7 | 0082D567 | same |
| 0x2DC |  | crashfx.cpp:850 | 00ABFBF3 00ABFC10 | 00ABEFD3 00ABEFF0 | same-global |
| 0x2EC |  | savecompress.cpp:202 | 00AC1BCA 004A908B | 00AC130A 004A8FDB | same-global |
| 0x2F0 |  | savecompress.cpp:207 | 004A7924 | 004A78B4 | same |
| 0x2FC |  | savecompress.cpp:203 | 004A791B 004A7937 | 004A78AB 004A78C7 | same |
| 0x348 |  | eadiag.cpp:277 | - | - | n/a |
| 0x35C |  | snap.cpp:1000 | 00AC1C86 0066F7C6 | 00AC13C6 00670CC6 | same-global |
| 0x360 |  | p4clone.cpp:331, snap.cpp:245 | 0066F97C 0066F896 | 00670E7C 00670D96 | same-global |
| 0x364 |  | p4clone.cpp:332, snap.cpp:246 | 00ABFAC3 00ABFC0A | 00ABEEA3 00ABEFEA | same-global |
| 0x368 | built, cells, water cells, bodies, blockedNodeMap size | p4clone.cpp:646, values.cpp:459 | 004A858B 00ABFBDF | 004A84DB 00ABEFBF | same-global / same |
| 0x390 |  | savecompress.cpp:58 | 0068DB67 004A912A | 0068F157 004A907A | same-global |
| 0x39C |  | snap.cpp:188 | 00789E7F 00ABFBC3 | 0078AFFF 00ABEFA3 | same-global |
| 0x448 |  | memdiag.cpp:73 | 00AC1E14 004E57D4 | 00AC1554 004E5A34 | same-global |
| 0x460 |  | memdiag.cpp:72 | 0071259C 00AC1E08 | 0071328C 00AC1548 | same-global |
| 0x488 |  | memdiag.cpp:56 | 004E52FE 004E5834 | 004E555E 004E5A94 | same-global |
| 0x48C |  | memdiag.cpp:57 | 004E52F8 007CFEB4 | 004E5558 007D0C54 | same-global |
| 0x4BC |  | eadiag.cpp:127 | - | - | n/a |
| 0x4E4 |  | p4.cpp:823 | 004E5924 004E69D4 | 004E5B84 004E6C34 | same |
| 0x7A0 |  | p4clone.cpp:578, snap.cpp:167 | 006705FF 006709F7 | 00671AFF 00671EF7 | same-global / same |
| 0x7A4 |  | p4clone.cpp:578, snap.cpp:167 | 0067061C 00670A01 | 00671B1C 00671F01 | same-global / same |
| 0x7A8 |  | snap.cpp:167 | 00670A42 | 00671F42 | same-global |
| 0x7AC |  | p4clone.cpp:328, snap.cpp:167 | 00670624 00670A0C | 00671B24 00671F0C | same-global / same |
| 0x7C0 |  | p4clone.cpp:329, snap.cpp:1004 | 0067062C 00670A14 | 00671B2C 00671F14 | same-global / same |
| 0x7C4 |  | p4clone.cpp:618 | 0060F51B 00C12530 | 006105AB 00C10840 | same-global |
| 0x7C8 |  | p4clone.cpp:618 | 0060F521 00C12538 | 006105B1 00C10848 | same-global |
| 0x7D4 | only reader 0x670510 erases and EA-frees the node it finds. The copy gets it empty (SnEnte... | p4clone.cpp:711 | 0067056D 0060F533 | 00671A6D 006105C3 | same-global |
| 0x8B8 |  | discourage.cpp:123 | 00832113 008327D6 | 00830D83 00831446 | same |
| 0x8BC |  | discourage.cpp:124 | 0083212E | 00830D9E | same |
| 0x8C0 |  | discourage.cpp:125 | 00832128 008334DB | 00830D98 0083214B | same |
| 0x8C4 | Remove / 0x833560 look the id up only when the map holds entries | discourage.cpp:120 | 008320F1 008323B7 | 00830D61 00831027 | same |
| 0x918 | nested under +0x918 (discourage Add/Remove, D's drains and the applies themselves), a job ... | p4.cpp:1300, p4clone.cpp:431, p4door.h:126, p4lotdump.h:151, p5.cpp:419, replay.cpp:1991 (+6 files) | 00829415 008299E4 | 008281D5 00828784 | same-global / same |
| 0x95C | RoutingWorld_GetLotPlanner 0x829BC0 | p4.cpp:1130, p4door.h:41, replay.cpp:1038, avoidro.cpp:194, discourage.cpp:131, gvro.cpp:40 (+7 files) | 008299F6 00829A05 | 00828796 008287A5 | same-global / same |
| 0x960 |  | snap.cpp:379, p4clone.cpp:333, values.cpp:504 | 00829A20 00829A2F | 008287C0 008287CF | same-global / same |
| 0x964 | 0x82EEC8: the planner as 0x82EDB0 passes it | unseenfb.cpp:237, avoidro.cpp:193, gvro.cpp:41, obstructro.cpp:138, rtoro.cpp:355, rtsites.cpp:114 (+1 files) | 00829D8D 0082A03C | 00828B2D 00828DDC | same-global / same |
| 0x974 |  | discourage.cpp:115 | 0082A0C7 0082D296 | 00828E67 0082BF66 | same |
| 0x983 |  | values.cpp:457 | 00835395 008358BA | 00834005 0083452A | same |
| 0xB84 |  | gcroots.cpp:58 | 00D7FB3C 00D817B0 | 00D7FEFC 00D81B70 | same-global |
| 0xBC8 | set with the task id (+0xBD0) for each task it runs (0xD7F000) | gcroots.cpp:64, ts3native.cpp:3731 | 00D761EB 00D76200 | 00D7644B 00D76460 | same-global / same |
| 0xBD0 | (no yield mark), the running task gets it back after | autledger.cpp:47, gcroots.cpp:169, ts3native.cpp:3100, unseenfbc.cpp:160 | 00D7FC44 00D6D074 | 00D80004 00D6D054 | same-global |
| 0xBD4 |  | gcroots.cpp:169, unseenfbc.cpp:160 | 00D7FC4A 00D6D069 | 00D8000A 00D6D049 | same-global |
| 0xBFC | listed. An id on it is unknown here, not "no type": it may have been listed at the rows' l... | autshadow.cpp:971 | 00D74601 00D74C33 | 00D74901 00D74F33 | same-census |
| 0xC20 |  | satb.cpp:1535 | 00D7E93D 00D7E93D | 00D7ECFD 00D7ECFD | same-global |
| 0xE0C |  | threadstack.cpp:65 | 00888DAD | 0088845D | same-census |
| 0xE10 | This thread's P4 frame slot as memory (TEB TlsSlots +0xE10, index < 64): what fs:[0xE10 + ... | monothread-m5.cpp:543 | 00888DB3 00889161 | 00888463 00888811 | same-census |
| 0xFFF |  | memexp.cpp:239 | 00415EAF 004165DC | 00415D9F 004164AC | same-census |
| 0x1000 |  | allocsites.cpp:653, memexp.cpp:107 | 00E4B8B3 00E4B818 | 00E4B903 00E4B868 | same-global |
| 0x1008 |  | memexp.cpp:109 | 00E4B88A | 00E4B8DA | same-global |
| 0x1024 |  | idcache.cpp:81 | 00939134 | 009394F4 | same |
| 0x1028 |  | idcache.cpp:57, verify.cpp:274 | 00938D3A 00938D60 | 009390FA 00939120 | same |
| 0x15F0 | MT index after the 624 state words at host+0xC30 (docs/27 §1.1) [inferred offset, I-123-23... | worldimage.cpp:33 | 00D776A1 | 00D77911 | same-census |
| 0x1658 |  | autshadow.cpp:968 | 00D74B2D | 00D74E2D | same-global |
| 0x1678 |  | autshadow.cpp:968 | 00D743CB 00D745E6 | 00D746CB 00D748E6 | same-census |
| 0x3110 | esi = effects list, entries at [esi+0x3110], 0x50 bytes each | crashfx.cpp:319 | 0071D54C 0071D54C | 0071E1CC 0071E1CC | same |
| 0x3114 |  | crashfx.cpp:530 | 0071FDB8 0071FF23 | 00720AD8 00720C43 | same |
| 0x3130 |  | crashfx.cpp:384 | 0071D4EA 0071D501 | 0071E16A 0071E181 | same |
| 0x38EC |  | crashfx.cpp:567 | 0071D450 007213B6 | 0071E0D0 007220D6 | same |
| 0x3980 |  | crashfx.cpp:567 | 0071D536 0071D536 | 0071E1B6 0071E1B6 | same |
