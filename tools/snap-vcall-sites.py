"""Indirect call/jump sites of the routing closure, for the [ProbeSnap] int3 sampler (native/ts3native/vcall.cpp, docs/94 §7).

    python tools/snap-vcall-sites.py [--exe game-copy/bin/TS3W.exe] [--functions game-copy/analysis/functions.csv]
                                     [--depth 12] [--out native/ts3native/vcallsites.h]
                                     [--extra-roots FILE] [--arm-thunk-prologues] [--list FILE] [--base HEADER]
    python tools/snap-vcall-sites.py --self-check

Pass 2 (docs/109): --extra-roots adds roots from a file (one `0xADDR name` per line, # comments) to the built-in ROOTS.
An extra root that functions.csv lacks (a vtable-only function Ghidra never made) is read to its first ret/int3 or
outgoing jmp, capped at the next functions.csv entry. --arm-thunk-prologues keeps the sites in the first 16 bytes of
those csv-less extra roots (vtable thunks: no detour relocates them; the arm-time byte check still applies).
--list writes one tab-separated row per site (site, len, call/jmp, function, name, hops, root, pass1/new vs --base).

Direct-call closure (E8 calls and direct jumps that leave the function) from the roots below, depth-limited and stopped at
Lock_Acquire / Lock_Release / EA_AllocTagged like docs/91 §4. Every `FF /2` (call r/m32) and `FF /4` (jmp r/m32) in a closure
function becomes a site, except absolute `[disp32]` forms (import thunks) and sites the sampler must not touch: the first
16 bytes of a function (detour prologues get relocated, ours, S3SS's, Frida's) and +-16 bytes around any address docs/68
names, or tools/probe/agent.js and tools/frida/*.js do (hook sites). Output: a header with one row per site (address, length, instruction bytes for the arm-time check).
Python because capstone is (tools/emu precedent); needs capstone 5.
"""
import argparse
import bisect
import csv
import os
import re
import struct
import sys

import capstone

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ROOTS = {
    # plan path: Route_PlanLocked, Route_PlanCommon, the four route ICs (docs/87 §2)
    0x82C6E0: "Route_PlanLocked", 0x7606E0: "Route_PlanCommon", 0x765160: "IC_Route_Plan", 0x765490: "IC_Route_Replan",
    0x765240: "IC_Route_PlanFromPoint", 0x765550: "IC_Route_ReplanFromPosition",
    # writer layer (docs/94 §2.2, docs/83 W1-W6)
    0x8321F0: "UpdateObjectFootprintCore", 0x831BA0: "AddPrivateFootprint", 0x82A0C0: "RemoveFootprint",
    0x635700: "LotRouting_RemoveObjectFootprints", 0x829D50: "writer 0x829D50", 0x834DC0: "lot rebuild", 0x834DF0: "lot walls",
    0x834F00: "lot rebuild", 0x8350C0: "lot rebuild", 0x665C50: "W1", 0x666750: "W2", 0x62F8A0: "W3", 0x63CB90: "W4 LotGraph_Build",
    0x62E680: "W5 LotTable_Insert", 0x80E5B0: "W6a", 0x80E610: "W6b", 0x80E980: "W6c", 0x80EC30: "W6d", 0x80E0B0: "W6e",
    0x80F6A0: "W6f", 0x80CB40: "W6g", 0x806770: "W6g", 0x666A40: "LotQuadTree_FlushDeferred", 0x82C420: "ApplyPendingUpdates",
    0x82B590: "R blocked bits", 0x832890: "R blocked bits / lot rebuild", 0x670510: "G mark", 0x670750: "G mark",
    # portal and lock-key ICs (docs/94 E8, E9)
    0x757E10: "IC_AddPortal", 0x74D6B0: "IC_RemovePortals", 0x74B440: "IC_AddStairPortals", 0x74B410: "IC_AddPlatformPortals",
    0x82F710: "diving board portals", 0x82F730: "water slide portals", 0x74D3F0: "IC_AddPortalLocationVariation",
    0x6379E0: "LotRouting_AddPortal", 0x6399E0: "LotRouting_RemovePortals", 0x637C30: "AddPortalLocationVariation",
    0x74B540: "IC_FootprintSetLockKeys", 0x74B5C0: "IC_FootprintAddLockKey", 0x74B600: "IC_FootprintRemoveLockKey",
    0x74B640: "IC_FootprintHasLockKey", 0x74B680: "IC_FootprintClearLockKeys", 0x74D160: "IC_Route_SetLockKeys",
    0x82B3E0: "FootprintLockKeyById",
}
STOP = {0x4E16F0, 0x4E17B0, 0x4E3F90}  # Lock_Acquire, Lock_Release, EA_AllocTagged


def load_pe(path):
    b = open(path, "rb").read()
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    opt = struct.unpack_from("<H", b, pe + 20)[0]
    base = struct.unpack_from("<I", b, pe + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + opt + i * 40
        va, vsz = struct.unpack_from("<II", b, o + 12)[0], struct.unpack_from("<I", b, o + 8)[0]
        rsz, raw = struct.unpack_from("<II", b, o + 16)
        secs.append((base + va, vsz, raw, rsz))

    def read(a, n):
        for va, vsz, raw, rsz in secs:
            if va <= a < va + min(vsz, rsz):
                return b[raw + a - va: raw + min(a - va + n, rsz)]
        return b""
    return read


def decode_len(code):
    """length of an FF /2 or /4 near form, None for absolute [disp32] or anything else"""
    if len(code) < 2 or code[0] != 0xFF or (code[1] >> 3 & 7) not in (2, 4):
        return None
    mod, rm = code[1] >> 6, code[1] & 7
    if mod == 3:
        return 2
    if mod == 0 and rm == 5:
        return None  # call [abs]: import thunk, target static
    n = 2
    if rm == 4:
        if len(code) < 3:
            return None
        n += 1
        if mod == 0 and (code[2] & 7) == 5:
            n += 4  # [index*s + disp32]: switch table
    return n + (1 if mod == 1 else 4 if mod == 2 else 0)


def self_check():
    assert decode_len(bytes([0xFF, 0x50, 0x08])) == 3        # call [eax+8]
    assert decode_len(bytes([0xFF, 0xD1])) == 2              # call ecx
    assert decode_len(bytes([0xFF, 0x15, 0, 0, 0, 0])) is None  # call [abs]
    assert decode_len(bytes([0xFF, 0x24, 0x85, 0, 0, 0, 0])) == 7  # jmp [eax*4+disp32]
    assert decode_len(bytes([0xFF, 0x54, 0x24, 0x10])) == 4  # call [esp+0x10]
    assert decode_len(bytes([0xFF, 0x92, 1, 2, 3, 4])) == 6  # call [edx+disp32]
    assert decode_len(bytes([0xFF, 0x30])) is None           # push [eax]
    assert parse_roots(["# c", "0x61E500 QuadTreeCellAStar x", "", "4F65B0"]) == {0x61E500: "QuadTreeCellAStar x", 0x4F65B0: "root 0x4F65B0"}
    code = bytes([0x83, 0xE9, 0x04, 0xFF, 0x60, 0x08, 0xCC, 0x90])  # sub ecx,4; jmp [eax+8]; int3
    assert linear_end(lambda a, n: code[a - 0x1000:a - 0x1000 + n], 0x1000, 0x2000) == 0x1006
    print("snap-vcall-sites self-check ok")


def parse_roots(lines):
    """extra roots: `0xADDR [name]` per line, # comments"""
    out = {}
    for ln in lines:
        ln = ln.split("#", 1)[0].strip()
        if ln:
            a, _, n = ln.partition(" ")
            out[int(a, 16)] = n.strip() or "root 0x%X" % int(a, 16)
    return out


def linear_end(read, r, cap):
    """end of a csv-less function: after its first ret/int3 or a jmp that leaves it, at most cap"""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for ins in md.disasm(read(r, cap - r), r):
        e = ins.address + ins.size
        if ins.mnemonic in ("ret", "int3") or ins.mnemonic == "jmp" and not (
                ins.op_str.startswith("0x") and r <= int(ins.op_str, 16) < cap):
            return e
    return cap


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(REPO, "game-copy", "bin", "TS3W.exe"))
    ap.add_argument("--functions", default=os.path.join(REPO, "game-copy", "analysis", "functions.csv"))
    ap.add_argument("--depth", type=int, default=12)
    ap.add_argument("--out", default=os.path.join(REPO, "native", "ts3native", "vcallsites.h"))
    ap.add_argument("--extra-roots")
    ap.add_argument("--arm-thunk-prologues", action="store_true")
    ap.add_argument("--list")
    ap.add_argument("--base", default=os.path.join(REPO, "native", "ts3native", "vcallsites.h"))
    ap.add_argument("--self-check", action="store_true")
    a = ap.parse_args()
    if a.self_check:
        return self_check()
    extra = {}
    if a.extra_roots:
        with open(a.extra_roots, encoding="utf-8") as f:
            extra = {r: n for r, n in parse_roots(f).items() if r not in ROOTS}
    roots = {**ROOTS, **extra}
    read = load_pe(a.exe)
    funcs, names = [], {}
    with open(a.functions, newline="") as f:
        for r in csv.DictReader(f):
            e, end = int(r["entry"], 16), int(r["end"], 16)
            funcs.append((e, end))
            names[e] = r["name"]
    funcs.sort()
    starts = [e for e, _ in funcs]
    missing = [hex(r) for r in roots if r not in names]
    thunks = set()  # csv-less extra roots
    for r in sorted(roots):  # a root functions.csv lacks (0x80E610, a slot target inside a Ghidra range): up to the next entry
        if r not in names:
            i = bisect.bisect_right(starts, r)
            end = starts[i] if i < len(starts) else r + 0x100
            if r in extra:
                end = linear_end(read, r, end)
                thunks.add(r)
            funcs.insert(i, (r, end))
            starts.insert(i, r)
            names[r] = roots[r]
    stop = STOP
    hooks = set()
    frida = os.path.join(REPO, "tools", "frida")
    srcs = [os.path.join(REPO, "docs", "68-hook-conflict-map.md"), os.path.join(REPO, "tools", "probe", "agent.js")]
    srcs += [os.path.join(frida, n) for n in sorted(os.listdir(frida)) if n.endswith(".js")]
    for p in srcs:  # every code address the docs/68 map or a Frida script names: a hook may sit there
        with open(p, encoding="utf-8") as f:
            for m in re.finditer(r"0x([0-9A-Fa-f]{6,8})\b", f.read()):
                v = int(m.group(1), 16)
                if 0x401000 <= v < 0xF95000:
                    hooks.add(v)
    hook_sorted = sorted(hooks)

    def near_hook(s, n):
        i = bisect.bisect_left(hook_sorted, s - 16)
        return i < len(hook_sorted) and hook_sorted[i] < s + n + 16

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.skipdata = True
    depth = {r: 0 for r in roots if r in names}
    origin = {r: r for r in depth}  # the root each function was first reached from
    queue = list(depth)
    sites, skipped = {}, {"prologue": 0, "hook": 0}
    while queue:
        fe = queue.pop(0)
        i = bisect.bisect_right(starts, fe) - 1
        end = funcs[i][1]
        code = read(fe, end - fe)
        for ins in md.disasm(code, fe):
            b = ins.bytes
            if b[0] in (0xE8, 0xE9) and len(b) == 5:
                t = (ins.address + 5 + struct.unpack("<i", b[1:])[0]) & 0xFFFFFFFF
                if b[0] == 0xE9 and fe <= t < end:
                    continue
                if t in names and t not in stop and t not in depth and depth[fe] < a.depth:
                    depth[t] = depth[fe] + 1
                    origin[t] = origin[fe]
                    queue.append(t)
                continue
            n = decode_len(b)
            if n is None or n != len(b):
                continue
            if ins.address - fe < 16 and not (a.arm_thunk_prologues and fe in thunks):
                skipped["prologue"] += 1
            elif near_hook(ins.address, n):
                skipped["hook"] += 1
            else:
                sites[ins.address] = (b, fe)
    calls = sum(1 for b, _ in sites.values() if (b[1] >> 3 & 7) == 2)
    with open(a.out, "w", newline="\n") as o:
        o.write("// Generated by tools/snap-vcall-sites.py (do not edit): indirect call/jmp sites of the routing closure for the\n")
        o.write(f"// [ProbeSnap] int3 sampler (vcall.cpp). {len(depth)} functions within {a.depth} direct-call hops of {len(roots)} roots,\n")
        if extra:
            o.write(f"// incl. {len(extra)} extra roots from {os.path.basename(a.extra_roots)}"
                    f"{' (thunk prologues armed)' if a.arm_thunk_prologues else ''},\n")
        o.write(f"// {len(sites)} sites ({calls} call, {len(sites) - calls} jmp); skipped {skipped['prologue']} in function prologues,\n")
        o.write(f"// {skipped['hook']} near docs/68 or Frida script addresses. Row: address, length, instruction bytes (checked before arming).\n")
        o.write("static const VcSiteDef kVcSites[] = {\n")
        for s in sorted(sites):
            b = sites[s][0]
            o.write("    {0x%06X, %d, {%s}},\n" % (s, len(b), ", ".join("0x%02X" % x for x in b)))
        o.write("};\n")
    if a.list:
        base = set()
        if os.path.exists(a.base):
            with open(a.base, encoding="utf-8") as f:
                base = {int(m.group(1), 16) for m in re.finditer(r"^\s*\{0x([0-9A-F]+),", f.read(), re.M)}
        with open(a.list, "w", newline="\n") as o:
            o.write("# site\tlen\tkind\tfunction\tname\thops\troot\tvs-base\n")
            for s in sorted(sites):
                b, fe = sites[s]
                o.write("0x%06X\t%d\t%s\t0x%06X\t%s\t%d\t0x%06X %s\t%s\n" % (
                    s, len(b), "call" if (b[1] >> 3 & 7) == 2 else "jmp", fe, names.get(fe, "?"), depth[fe],
                    origin[fe], roots[origin[fe]], "pass1" if s in base else "new"))
        print(f"wrote {a.list} ({sum(1 for s in sites if s not in base)} sites not in {os.path.basename(a.base)})")
    print(f"{len(depth)} functions, {len(sites)} sites ({calls} call), skipped {skipped}, roots not in functions.csv: {missing or 'none'}")
    print(f"wrote {a.out}")


if __name__ == "__main__":
    sys.exit(main())
