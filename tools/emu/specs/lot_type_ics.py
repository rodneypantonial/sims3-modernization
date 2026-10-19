"""docs/100 L9: what the lot pair +0x408 / +0x40C is (the pair (1, 0x50) skips the lot-outline stamp of the G build).

Runs the three script ICs that write the pair, on a constructed lot, in random sequences:
  IC_World_SetLotType 0x78DF80 (cdecl(idLo, idHi, type)): when the type differs, +0x40C = 0, +0x408 = type;
  IC_World_SetResidentialLotSubType 0x785540 (cdecl(idLo, idHi, sub)): +0x40C = sub only when +0x408 == 0;
  IC_World_SetCommercialLotSubType 0x7855C0 (cdecl(idLo, idHi, sub)): +0x40C = sub only when +0x408 == 1.
The lot comes from WorldManager_FindLotByIdTyped 0xC62D40 (stubbed: our lot for our id, else 0) on the world manager
[0x11ECBC4] (set to a dummy; NULL is a case too). So +0x408 is ScriptCore LotType (Residential 0, Commercial 1, Tutorial 2)
and +0x40C the residential or commercial subtype; tools/portal-answer-check.ts checks that CommercialLotSubType 0x50 is
kEP10_Diving, so the skipped pair is a commercial dive lot (Island Paradise).
"""
import struct

NAME = 'Lot type / subtype ICs (L9)'
TARGET = 0x78DF80
CONV = 'cdecl'
CASES = 200
FOLLOW = []
OTHERS = {'res': 0x785540, 'com': 0x7855C0}
WM_SLOT = 0x11ECBC4
LOT = [0]
LOT_ID = (0x1234, 0x5)
NOTES = ['stub: WorldManager_FindLotByIdTyped 0xC62D40 (thiscall, 3 args) returns the case lot for its id, else 0']


def setup(emu):
    emu.stub(0xC62D40, lambda e, a: LOT[0] if (a(0), a(1)) == LOT_ID else 0, pop=12, name='FindLotByIdTyped')


def cases(rng, n):
    out = [dict(label='diving', wm=True, ops=[('type', 1, True), ('com', 0x50, True)]),
           dict(label='res-sub-on-commercial-ignored', wm=True, ops=[('type', 1, True), ('res', 7, True)]),
           dict(label='same-type-keeps-sub', wm=True, ops=[('type', 1, True), ('com', 0x50, True), ('type', 1, True)]),
           dict(label='type-change-clears-sub', wm=True, ops=[('type', 1, True), ('com', 0x51, True), ('type', 0, True)]),
           dict(label='unknown-lot', wm=True, ops=[('type', 1, False), ('com', 3, False)]),
           dict(label='no-world-manager', wm=False, ops=[('type', 1, True), ('com', 3, True)])]
    for i in range(n):
        ops = [(rng.choice(['type', 'res', 'com']), rng.choice([0, 1, 2, rng.randrange(0, 90)]), rng.random() < 0.9)
               for _ in range(rng.randrange(1, 7))]
        out.append(dict(label=f'rand{i}', wm=rng.random() < 0.95, ops=ops))
    return out


def run(emu, c):
    lot = emu.alloc(0x500)
    emu.write(lot + 0x408, struct.pack('<II', 0, 0))
    LOT[0] = lot
    emu.w32(WM_SLOT, emu.alloc(0x10) if c['wm'] else 0)
    for kind, v, ours in c['ops']:
        lo, hi = LOT_ID if ours else (0x999, 0)
        emu.call(TARGET if kind == 'type' else OTHERS[kind], [lo, hi, v], CONV)
    return struct.unpack('<II', emu.read(lot + 0x408, 8))


def reference(c):
    t, s = 0, 0
    for kind, v, ours in c['ops']:
        if not (ours and c['wm']):
            continue
        if kind == 'type' and t != v:
            t, s = v, 0
        elif kind == 'res' and t == 0 or kind == 'com' and t == 1:
            s = v
    return t, s
