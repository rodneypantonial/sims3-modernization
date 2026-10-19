# Dummy test of tools/frida/live-reads2.js (probe-safety rule): 32-bit ping.exe, never the game.
#   python tools/probe/live-reads2-test.py      (Windows, frida installed; prints RESULT PASS|FAIL)
# A helper script builds fake game state in the dummy (world manager with bounds, routing world -> P with a 6-cell footprint
# grid, a route with a follower id, a path with two ids at +0x60, start-point vectors in G and beyond the far Z edge, a
# houseboat lot with corners) and stubs with the game's calling shapes: Route_PlanCommon (thiscall, 2 stack args, ret 8)
# calls RouteFollow_EditPathForRoute (cdecl path, ids) as the game does; grid fill / remove, follow slot +0x38, SetHouseboatParent;
# a LotGraph_Build stand-in that calls the fill; and the two call targets 0xC6BAB0 / 0xC68990 as real thiscall ret 8 functions
# (answer = x < 300, lot = z < 150). Thread A ("sim") plans, fills, removes, follows and parents; thread B ("worker") fills
# through the build stand-in and removes directly. The script under test is the unchanged file; only addresses come in
# through the test-only init parameter `addrs`. Checks: refusal on wrong bytes, chaining on an existing jmp, every read,
# dumps and calls on the sim thread, off-sim classification, fail-fast, clean unhook (threads keep running, dummy alive).
# Section 4 (unload race, docs/51 §81): slow copies of the two call targets (~45 ms each, 40 calls per dump) make the start dump
# last ~2 s; the probe runs in its own host with -t 1, so it unloads while thread A is inside a call. 5 times; the dummy must
# survive each and the file must show the bounded wait with nothing left in flight.
#   python tools/probe/live-reads2-test.py [--race] [script.js]   (--race: section 4 only; script.js: e.g. an older copy)
import frida, json, pathlib, subprocess, sys, tempfile, time
HERE = pathlib.Path(__file__).parent
ROOT = HERE.parent.parent
args = [x for x in sys.argv[1:] if not x.startswith('--')]
RACE_ONLY = '--race' in sys.argv
src = pathlib.Path(args[0] if args else ROOT / 'tools' / 'frida' / 'live-reads2.js').read_text(encoding='utf-8')
ok = True
def check(c, msg):
    global ok
    print(('ok   ' if c else 'FAIL ') + msg); ok = ok and bool(c)

HELPER = r"""
const k = Process.getModuleByName('kernel32.dll');
const CT = new NativeFunction(k.getExportByName('CreateThread'), 'pointer', ['pointer', 'uint', 'pointer', 'pointer', 'uint', 'pointer']);
const heap = Memory.alloc(0x10000); let top = 0x100;
const alloc = n => { const a = heap.add(top); top += (n + 15) & ~15; return a; };
const u = p => p.toUInt32();
const wm = alloc(0x500), wmg = alloc(4), world = alloc(0x960), wg = alloc(4), P = alloc(0xf0), cells = alloc(32 * 6);
[0, 0, 600, 300, 200].forEach((v, i) => wm.add(0xb4 + 4 * i).writeFloat(v)); wm.add(0x100).writeFloat(300); wm.add(0x104).writeFloat(150);
wmg.writePointer(wm); wg.writePointer(world); world.add(0x95c).writePointer(P);
P.add(0xdc).writePointer(cells); P.add(0xe0).writePointer(cells.add(32 * 6)); P.add(0xe4).writePointer(cells.add(32 * 6));
const rec = (lot, key, next) => { const r = alloc(0x50); r.writeU32(key); r.add(0x40).writeU32(lot); r.add(0x48).writePointer(next); return r; };
const bk = alloc(8); bk.writePointer(rec(7, 1, rec(0, 2, NULL))); bk.add(4).writeU32(0xffffffff);
cells.add(4).writePointer(bk); cells.add(8).writeU32(1); cells.add(12).writeU32(2);
const route = alloc(0x200); route.add(0x40).writeU32(0x55);
const path = alloc(0x80), ids = alloc(16); ids.writeU32(0x55); ids.add(8).writeU32(0x77);
path.add(0x60).writePointer(ids); path.add(0x64).writePointer(ids.add(16));
const startVec = z => { const r = alloc(64); r.writeFloat(20); r.add(8).writeFloat(z); const v = alloc(8); v.writePointer(r); v.add(4).writePointer(r.add(64)); return v; };
const vA = startVec(2110), vB = startVec(100);
const lot = alloc(0x500); lot.add(0x444).writeU8(1);
[[10, 2100], [30, 2100], [30, 2120], [10, 2120]].forEach(([x, z], i) => { lot.add(0x134 + 8 * i).writeFloat(x); lot.add(0x138 + 8 * i).writeFloat(z); });
const page = Memory.alloc(Process.pageSize * 3); Memory.protect(page, Process.pageSize * 3, 'rwx');
const stub = i => page.add(i * 64);
// 0 editPath, 1 fill, 2 remove, 3 follow, 4 setParent, 5 idle: push ebp; mov ebp,esp; 8 nops; pop ebp; ret (caller cleans)
for (let i = 0; i < 6; i++) stub(i).writeByteArray([0x55, 0x8b, 0xec, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x5d, 0xc3]);
// 6 isRoutable(x, z): al = x < [ecx+0x100]; 7 lotLookup(x, z): eax = z < [ecx+0x104] ? ecx : 0; both ret 8
stub(6).writeByteArray([0xf3, 0x0f, 0x10, 0x44, 0x24, 0x04, 0x0f, 0x2f, 0x81, 0x00, 0x01, 0x00, 0x00, 0x0f, 0x92, 0xc0, 0xc2, 0x08, 0x00]);
stub(7).writeByteArray([0xf3, 0x0f, 0x10, 0x44, 0x24, 0x08, 0x0f, 0x2f, 0x81, 0x04, 0x01, 0x00, 0x00, 0xb8, 0, 0, 0, 0, 0x0f, 0x42, 0xc1, 0xc2, 0x08, 0x00]);
// 11, 12 slow copies of 6 and 7: push ecx; mov ecx, 40000000; loop $; pop ecx; then the same body (unload race, section 4)
for (const [i, j] of [[11, 6], [12, 7]]) stub(i).writeByteArray([0x51, 0xb9, 0x00, 0x5a, 0x62, 0x02, 0xe2, 0xfe, 0x59, ...new Uint8Array(stub(j).readByteArray(24))]);
// 8 a jmp rel32 to stub 3 (an existing hook on the follow slot)
{ const s = stub(8); s.writeU8(0xe9); s.add(1).writeS32(u(stub(3)) - (u(s) + 5)); }
// 9 Route_PlanCommon(a1, starts) thiscall ret 8: EditPathForRoute(path, path+0x60, 0); 10 build: fill(ecx = P)
stub(9).writeByteArray([0x55, 0x8b, 0xec, 0x90, 0x90, 0x90, 0x90, 0x90]); // push ebp; mov ebp,esp (8B EC form, as the game); 5 nops
const w9 = new X86Writer(stub(9).add(8), { pc: stub(9).add(8) });
w9.putPushU32(0); w9.putPushU32(u(path.add(0x60))); w9.putPushU32(u(path)); w9.putCallAddress(stub(0)); const ra9 = u(w9.pc); w9.putAddRegImm('esp', 12);
w9.putPopReg('ebp'); w9.putRetImm(8); w9.flush();
const w10 = new X86Writer(stub(10), { pc: stub(10) });
w10.putPushReg('ebp'); w10.putMovRegReg('ebp', 'esp'); w10.putMovRegAddress('ecx', P); w10.putCallAddress(stub(1)); w10.putPopReg('ebp'); w10.putRet(); w10.flush();
const ctr = alloc(16); ctr.add(8).writeU32(300000);
const loopA = page.add(Process.pageSize), loopB = page.add(Process.pageSize * 2);
{
  const w = new X86Writer(loopA, { pc: loopA });
  w.putLabel('top');
  w.putMovRegAddress('eax', ctr); w.putIncRegPtr('dword', 'eax');
  w.putPushU32(0); w.putPushU32(0x99); w.putMovRegAddress('ecx', lot); w.putCallAddress(stub(4)); w.putAddRegImm('esp', 8);
  w.putPushU32(u(vA)); w.putPushU32(0); w.putMovRegAddress('ecx', route); w.putCallAddress(stub(9));
  w.putPushU32(u(vB)); w.putPushU32(0); w.putMovRegAddress('ecx', route); w.putCallAddress(stub(9));
  w.putMovRegAddress('ecx', P); w.putCallAddress(stub(1));
  w.putMovRegAddress('ecx', P); w.putCallAddress(stub(2));
  w.putMovRegAddress('ecx', route); w.putCallAddress(stub(8));
  w.putMovRegAddress('edx', ctr.add(8)); w.putMovRegRegPtr('edx', 'edx');
  w.putLabel('d'); w.putDecReg('edx'); w.putJccNearLabel('jne', 'd', 'no-hint');
  w.putJmpNearLabel('top');
  w.flush();
}
{
  const w = new X86Writer(loopB, { pc: loopB });
  w.putLabel('top');
  w.putMovRegAddress('eax', ctr.add(4)); w.putIncRegPtr('dword', 'eax');
  w.putCallAddress(stub(10));
  w.putMovRegAddress('ecx', P); w.putCallAddress(stub(2));
  w.putMovRegAddress('edx', ctr.add(8)); w.putMovRegRegPtr('edx', 'edx');
  w.putLabel('d'); w.putDecReg('edx'); w.putJccNearLabel('jne', 'd', 'no-hint');
  w.putJmpNearLabel('top');
  w.flush();
}
rpc.exports = {
  addrs() { return { stubs: Array.from({ length: 13 }, (_, i) => u(stub(i))), wm: u(wmg), world: u(wg), loopA: u(loopA), build: u(stub(10)), ra9, text: [u(page), u(page) + Process.pageSize * 3] }; },
  spawn() { const a = Memory.alloc(4), b = Memory.alloc(4); CT(NULL, 0, loopA, NULL, 0, a); CT(NULL, 0, loopB, NULL, 0, b); return [a.readU32(), b.readU32()]; },
  count() { return [ctr.readU32(), ctr.add(4).readU32()]; },
};"""

STUB = '55 8b ec 90 90 90 90 90'
p = subprocess.Popen([r'C:\Windows\SysWOW64\PING.EXE', '-n', '150', '127.0.0.1'], stdout=subprocess.DEVNULL)
tmp = pathlib.Path(tempfile.mkdtemp(prefix='livereads2-test-'))
try:
    time.sleep(1)
    s = frida.attach(p.pid)
    helper = s.create_script(HELPER); helper.load(); h = helper.exports_sync
    a = h.addrs(); tidA, tidB = h.spawn(); time.sleep(0.5)
    st = a['stubs']
    hooks = {'editPathForRoute': 0, 'planCommon': 9, 'gridFill': 1, 'gridRemove': 2, 'follow38': 3, 'setHouseboatParent': 4}
    def params(out, minutes, bytes_=STUB, call_bytes=('f3 0f 10 44 24 04 0f 2f', 'f3 0f 10 44 24 08 0f 2f')):
        return {'out': str(out).replace('\\', '/'), 'minutes': minutes,
                'addrs': {'wm': a['wm'], 'routingWorld': a['world'], 'build': [[a['build'], a['build'] + 32]], 'text': a['text'],
                          'hooks': {n: [st[i], bytes_] for n, i in hooks.items()},
                          'calls': {'isRoutable': [st[6], call_bytes[0]], 'lotLookup': [st[7], call_bytes[1]]}}}
    def wait_json(f, secs):
        end = time.time() + secs
        while time.time() < end:  # the file appears before its content is flushed: wait for valid JSON
            time.sleep(0.5)
            try: return json.loads(f.read_text(encoding='utf-8'))
            except (OSError, ValueError): pass
        return None
    logs = []
    def load(prm):
        sc = s.create_script(src)
        sc.on('message', lambda m, d: logs.append(m))
        sc.load(); sc.exports_sync.init('late', prm)
        return sc
    RA = {str(a['ra9']): 'Route_PlanCommon'}  # return address of the EditPath call inside the PlanCommon stub

    def race():
        # 4. unload race: own host (frida_tools.repl -t 1, as profile-world.ts runs it), unloaded while the start dump is in a call
        js = tmp / 'probe.js'; js.write_text(src, encoding='utf-8')
        for i in range(5):
            prm = params(tmp / f'race{i}', 0.5); prm['addrs']['ra'] = RA
            prm['addrs']['calls'] = {'isRoutable': [st[11], '51 b9 00 5a 62 02 e2 fe'], 'lotLookup': [st[12], '51 b9 00 5a 62 02 e2 fe']}
            subprocess.run([sys.executable, '-m', 'frida_tools.repl', '-p', str(p.pid), '-l', str(js), '-P', json.dumps(prm), '-q', '-t', '1'],
                           capture_output=True, text=True, timeout=60)
            time.sleep(0.5)
            alive = p.poll() is None
            try: c1 = h.count()[0]; time.sleep(0.3); moving = h.count()[0] > c1
            except Exception: moving = False
            r = wait_json(tmp / f'race{i}-livereads2.json', 3)
            uw = r and r.get('unloadWait')
            check(alive and moving, f'race {i}: dummy alive and thread A still looping after an unload mid-call')
            check(r and r['reason'] == 'detached before the end' and uw and uw['inCall'] == 0 and r['errors'] == [],
                  f"race {i}: file {r and r['reason']}, dumps {r and [(d['which'], len(d['points'].get('points', []))) for d in r['dumps']]}, wait {uw}, errors {r and r['errors'][:1]}")
            if not alive: return
    if RACE_ONLY:
        race(); print('RESULT', 'PASS' if ok else 'FAIL'); sys.exit(0 if ok else 1)

    # 1. wrong prologue bytes everywhere, the follow hook on an existing jmp: refused / chained; no 0x85FF30 hook = fail at once
    prm = params(tmp / 'bad', 0.5, '55 8b ec 90 90 90 90 91', ('00', '00')); prm['addrs']['ra'] = RA
    prm['addrs']['hooks']['follow38'][0] = st[8]
    sc = load(prm); bad = wait_json(tmp / 'bad-livereads2.json', 10)
    check(bad and bad['reason'].startswith('fail: no 0x85FF30 hook') and set(bad['refused']) == set(hooks) - {'follow38'} | {'isRoutable', 'lotLookup'}, f"wrong bytes refused: {bad and (bad['reason'], sorted(bad['refused']))}")
    check(bad and list(bad['chained']) == ['follow38'] and bad['hooks'] == 1, f"existing jmp chained: {bad and bad['chained']}")
    sc.unload()
    jb = s.create_script(f"rpc.exports = {{ b() {{ return ptr({st[8]}).readU8(); }} }};"); jb.load()
    check(jb.exports_sync.b() == 0xe9, 'the existing jmp is back after detach'); jb.unload()

    # 2. the real run, 0.5 min: start dump at the first EditPath call, end dump at 10 s (minimum), file at the end
    c0 = h.count()
    prm = params(tmp / 'run', 0.5); prm['addrs']['ra'] = RA
    sc = load(prm); r = wait_json(tmp / 'run-livereads2.json', 45)
    check(r is not None, 'result file written before the deadline')
    if r is None: print(chr(10).join(str(m) for m in logs[-20:])); raise SystemExit(1)
    check(r['reason'] == 'complete' and r['hooks'] == 6 and not r['refused'] and not r['chained'], f"complete, 6 hooks ({r['reason']}, {r['refused']})")
    check([d['which'] for d in r['dumps']] == ['start', 'end'] and all(d['tid'] == tidA for d in r['dumps']) and r['simTid'] == tidA, f"dumps on the sim thread {[(d['which'], d['tid']) for d in r['dumps']]} sim {r['simTid']} A {tidA}")
    g = r['dumps'][-1]['grid']
    check(g['errors'] == [] and [g['cells'], g['expectCells'], g['records'], g['nonEmpty'], g['noLot'], g['elemCount']] == [6, 6, 2, 1, 1, 2], f"grid read {g}")
    pts = r['dumps'][0]['points']['points']
    check(len(pts) == 20 and all(q['ans'] == (1 if q['x'] < 300 else 0) and q['lot'] == (1 if q['z'] < 150 else 0) for q in pts), f"20 point answers from the call targets: {pts[:3]}")
    o = r['o58']; pc = o['byCaller'].get('Route_PlanCommon')
    check(pc and pc['calls'] > 10 and pc['nonEmpty'] == pc['calls'] and o['idsIsPath60'] == o['calls'] and o['rows'][0]['ids'] == ['0x0:0x55', '0x0:0x77'] and o['rows'][0]['hasFollower'], f"O+0x58 reads {pc} {o['rows'][:1]}")
    pl = r['plan']
    check(pl['calls'] > 10 and abs(pl['beyondFarZ'] - pl['inG']) <= 1 and pl['rows'][0]['hbLot'] == 0 and pl['routeListNonEmpty'] == 0 and pl['rows'][0]['follower'] == '0x0:0x55', f"plan starts {pl['beyondFarZ']}/{pl['inG']}, row {pl['rows'][:1]}")
    gf, gr = r['grid']['gridFill'], r['grid']['gridRemove']
    check(set(gf['threads']) == {str(tidA), str(tidB)} and gf['ecxIsP'] == gf['calls'] and gf['offSim'] and all(x['inBuild'] and x['tid'] == tidB for x in gf['offSim']), f"fill threads {gf['threads']}, off-sim {gf['offSim'][:1]}")
    check(gr['offSim'] and not any(x['inBuild'] for x in gr['offSim']), f"remove off-sim calls not under the build: {gr['offSim'][:1]}")
    f = r['follow38']
    check(f['calls'] > 10 and len(f['ras']) == 1 and f['threads'] == {str(tidA): f['calls']} and f['bt'], f"follow38 {f['ras']} {f['threads']}")
    hb = r['houseboat']['rows']
    check(hb and hb[0]['flag'] == 1 and hb[0]['corners'] == [[10, 2100], [30, 2100], [30, 2120], [10, 2120]] and hb[0]['maxZ'] == 300 and hb[0]['boat'] == '0x0:0x99', f"houseboat row {hb[:1]}")
    check(r['errors'] == [], f"no probe errors ({r['errors'][:2]})")
    c1 = h.count(); time.sleep(1.0); c2 = h.count()
    check(c2[0] > c1[0] > c0[0] and c2[1] > c1[1] and p.poll() is None, f'dummy alive and both threads looping after the script finished ({c0} -> {c1} -> {c2})')
    sc.unload(); time.sleep(0.3)
    c3 = h.count(); time.sleep(0.5)
    check(h.count()[0] > c3[0] and p.poll() is None, 'still alive after unload')

    # 3. fail-fast: plan, EditPath and follow hooks on a stub nobody calls -> 2 progress checks of 30 s without a call = fail
    prm = params(tmp / 'idle', 2); prm['addrs']['ra'] = RA
    for n in ('editPathForRoute', 'planCommon', 'follow38'): prm['addrs']['hooks'][n][0] = st[5]
    sc = load(prm); r3 = wait_json(tmp / 'idle-livereads2.json', 80)
    check(r3 is not None and r3['reason'].startswith('fail: no route plan for 60 s') and r3['dumps'] == [] and r3['grid']['gridFill']['calls'] > 0, f"fail-fast after 60 s: {r3 and r3['reason']}")
    sc.unload()
    check(p.poll() is None, 'alive after the fail-fast run')
    rep = subprocess.run(['node', str(ROOT / 'tools' / 'harness' / 'live-reads2-report.ts'), str(tmp / 'run-livereads2.json')], capture_output=True, text=True)
    check(rep.returncode == 0 and 'I-107-15: verified' in rep.stdout and 'I-107-19 (end): size verified' in rep.stdout and 'I-100-13: verified' in rep.stdout
          and 'I-107-20 0x62D9F0: REFUTED' in rep.stdout and 'I-107-21: 40 answers logged' in rep.stdout, 'report reads the file')
    print(rep.stdout, rep.stderr[-2000:])
    print('\n'.join(m['payload'] for m in logs if m['type'] == 'log')[-1500:])
    race()
finally:
    p.kill()
print('RESULT', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
