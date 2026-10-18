# Dummy test of tools/frida/inroom-reads.js (probe-safety rule): 32-bit ping.exe, never the game.
#   python tools/probe/inroom-reads-test.py      (Windows, frida installed; prints RESULT PASS|FAIL)
# A helper script builds a fake routing world in the dummy (world global -> P, lot table with one lot, four level trees: no
# wrap pair, two pairs, sub-cells, and one bad tree pointer), four stub functions with the game's calling shapes
# (InRoomPlan / ToPoint: ecx = P, X first on the stack; SetBounds: ecx = tree, 5 floats + 3 dwords; QuadTreeBuild: ecx = P),
# and a thread that calls them in a loop with a busy delay. The script under test is the unchanged file; only its
# addresses come in through the test-only init parameter `addrs`. Checks: hooks, dumps on the calling thread, counts,
# the JSON file, refusal on wrong bytes, clean unhook (the thread keeps running, the dummy stays alive).
import frida, json, os, pathlib, subprocess, sys, tempfile, time
HERE = pathlib.Path(__file__).parent
ROOT = HERE.parent.parent
src = (ROOT / 'tools' / 'frida' / 'inroom-reads.js').read_text(encoding='utf-8')
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
const fbits = f => { const b = Memory.alloc(4); b.writeFloat(f); return b.readU32(); };
const vt = alloc(16); vt.add(4).writeU32(0x806f70); vt.add(8).writeU32(0x807090); vt.add(12).writeU32(0x624450);
const fp = cls => { const f = alloc(0x50); f.writePointer(vt); f.add(0x34).writeU8(cls); f.add(0x35).writeU8(1); f.add(0x1c).writeFloat(1.5); return f; };
const fme = f => { const e = alloc(0x14); e.add(8).writePointer(f); return e; };
function leaf(finalMask, entries) {
  const n = alloc(0x14); n.add(4).writeU32(finalMask << 3);
  if (entries.length === 1) { n.add(8).writePointer(entries[0][0]); n.add(12).writeU32(entries[0][1]); n.add(16).writeU32(entries[0][2]); }
  return n;
}
function internal(kids) { const n = alloc(0x1c); n.add(4).writeU32(1); kids.forEach((c, i) => n.add(12 + 4 * i).writePointer(c)); return n; }
function tree(root, defCls) {
  const t = alloc(0x58); [-1, -1, 21, 31].forEach((v, i) => t.add(4 + 4 * i).writeFloat(v));
  t.add(0x24).writePointer(root); t.add(0x2c).writeU32(defCls); t.add(0x30).writeFloat(1); t.add(0x38).writeFloat(32); t.add(0x40).writeU32(6); t.add(0x44).writeU32(0);
  return t;
}
const walk = () => leaf(1, [[fme(fp(0)), 0xffffffff, 0xffffffff]]);
const t1 = tree(internal([walk(), NULL, NULL, walk()]), 12);                  // 0 pairs
const t2 = tree(internal([walk(), walk(), NULL, walk()]), 12);                // 2 pairs
const t3 = tree(leaf(0b11110, [[fme(fp(10)), 0x0f0f0f0f, 0]]), 0);           // halves, 2 pairs
const P = alloc(0xc0), world = alloc(0x960), wg = alloc(4), cell = alloc(8), bk = alloc(12), node = alloc(0x1a0), vec = alloc(20);
wg.writePointer(world); world.add(0x95c).writePointer(P); cell.writeFloat(0.5); P.add(0xb8).writeFloat(0.5);
P.add(0x4c).writePointer(bk); P.add(0x50).writeU32(2); bk.writePointer(node); node.writeU32(0x1234); node.add(0x190).writeU32(0);
[t1, t2, t3, NULL].forEach((t, i) => vec.add(4 * i).writePointer(t)); vec.add(12).writeU32(0x7ff00000); // last: unmapped tree
node.add(0x10 + 0x74).writePointer(vec); node.add(0x10 + 0x78).writePointer(vec.add(16));
const X1 = alloc(0x60), X2 = alloc(0x60), bt = alloc(0x58);
X1.add(0x40).writePointer(alloc(16)); X1.add(0x50).writeU32(0x10000); X1.add(0x4c).writeU32(0);
X2.add(0x50).writeU32(0x2010001); X2.add(0x4c).writeU32(2);                   // no agent, flags 0x1 and 0x2000000, X+0x4C = 2
[-1, -1, 21, 31].forEach((v, i) => bt.add(4 + 4 * i).writeFloat(v)); bt.add(0x38).writeFloat(32); bt.add(0x40).writeU32(7);
// stubs: push ebp; mov ebp,esp; 8 nops; pop ebp; ret (the caller cleans the stack)
const page = Memory.alloc(Process.pageSize * 2); Memory.protect(page, Process.pageSize * 2, 'rwx');
const stub = i => page.add(i * 16);
// stub 4: never called (fail-fast case)
for (let i = 0; i < 5; i++) stub(i).writeByteArray([0x55, 0x8b, 0xec, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x5d, 0xc3]);
const ctr = alloc(16); ctr.add(8).writeU32(300000);
const loopAt = page.add(Process.pageSize);
{
  const w = new X86Writer(loopAt, { pc: loopAt });
  w.putLabel('top');
  w.putMovRegAddress('eax', ctr); w.putIncRegPtr('dword', 'eax');
  w.putPushU32(u(X1)); w.putMovRegAddress('ecx', P); w.putCallAddress(stub(0)); w.putAddRegImm('esp', 4);
  w.putPushU32(u(X2)); w.putMovRegAddress('ecx', P); w.putCallAddress(stub(1)); w.putAddRegImm('esp', 4);
  for (const v of [0, 0, 0, fbits(0.5), fbits(30.5), fbits(20.5), fbits(-0.5), fbits(-0.5)]) w.putPushU32(v);
  w.putMovRegAddress('ecx', bt); w.putCallAddress(stub(2)); w.putAddRegImm('esp', 32);
  for (let i = 0; i < 4; i++) w.putPushU32(i);
  w.putMovRegAddress('ecx', P); w.putCallAddress(stub(3)); w.putAddRegImm('esp', 16);
  w.putMovRegAddress('edx', ctr.add(8)); w.putMovRegRegPtr('edx', 'edx');
  w.putLabel('d'); w.putDecReg('edx'); w.putJccNearLabel('jne', 'd', 'no-hint');
  w.putJmpNearLabel('top');
  w.flush();
}
rpc.exports = {
  addrs() { return { stubs: [0, 1, 2, 3, 4].map(i => u(stub(i))), world: u(wg), cell: u(cell), loop: u(loopAt) }; },
  spawn() { const id = Memory.alloc(4); CT(NULL, 0, loopAt, NULL, 0, id); return id.readU32(); },
  count() { return ctr.readU32(); },
};"""

STUB = '55 8b ec 90 90 90 90 90'
p = subprocess.Popen([r'C:\Windows\SysWOW64\PING.EXE', '-n', '120', '127.0.0.1'], stdout=subprocess.DEVNULL)
tmp = pathlib.Path(tempfile.mkdtemp(prefix='inroom-test-'))
try:
    time.sleep(1)
    s = frida.attach(p.pid)
    helper = s.create_script(HELPER); helper.load(); h = helper.exports_sync
    a = h.addrs(); tid = h.spawn(); time.sleep(0.5)
    names = ['inRoomPlan', 'inRoomToPoint', 'setBounds', 'quadTreeBuild']
    def params(out, minutes, bytes_=STUB):
        return {'out': str(out).replace('\\', '/'), 'minutes': minutes,
                'addrs': {'world': a['world'], 'cellStatic': a['cell'], 'callers': [[a['loop'], 'LoopCaller']],
                          'hooks': {n: [a['stubs'][i], bytes_] for i, n in enumerate(names)}}}
    logs = []
    def load(prm):
        sc = s.create_script(src)
        sc.on('message', lambda m, d: logs.append(m))
        sc.load(); sc.exports_sync.init('late', prm)
        return sc

    # 1. wrong prologue bytes everywhere: nothing hooked, file says why
    sc = load(params(tmp / 'bad', 0.5, '55 8b ec 90 90 90 90 91')); time.sleep(0.5)
    bad = json.loads((tmp / 'bad-inroom.json').read_text(encoding='utf-8'))
    check(bad['reason'].startswith('fail: no in-room hook') and set(bad['refused']) == set(names) and bad['hooks'] == 0, f"wrong bytes refused: {bad['reason']}, {list(bad['refused'])}")
    sc.unload()

    # 2. the real run, 0.5 min: start dump at once, end dump at 10 s (minimum), file at the end
    c0 = h.count()
    sc = load(params(tmp / 'run', 0.5))
    deadline = time.time() + 40
    r = None
    while r is None and time.time() < deadline:  # the file appears before its content is flushed: wait for valid JSON
        time.sleep(0.5)
        try: r = json.loads((tmp / 'run-inroom.json').read_text(encoding='utf-8'))
        except (OSError, ValueError): pass
    check(r is not None, 'result file written before the deadline')
    check(r['reason'] == 'complete' and r['hooks'] == 4 and not r['refused'], f"complete, 4 hooks ({r['reason']})")
    check([d['which'] for d in r['dumps']] == ['start', 'end'], f"dumps {[d['which'] for d in r['dumps']]}")
    check(all(d['tid'] == tid for d in r['dumps']), 'dumps on the calling (sim) thread')
    d = r['dumps'][-1]; tr = d['lots'][0]['trees']
    check(d['errors'] == [] and len(d['lots']) == 1 and d['lots'][0]['key'] == [0x1234, 0], f"one lot read ({d['errors']})")
    check([t.get('wrapPairs') for t in tr[:3]] == [0, 2, 2] and 'error' in tr[3], f"wrap pairs {[t.get('wrapPairs') for t in tr]}, bad tree -> error")
    check(abs(d['pB8']['f32'] - 0.5) < 1e-6 and abs(d['cellStatic']['f32'] - 0.5) < 1e-6, 'P+0xB8 and static read')
    bc = r['byCaller']
    ip, tp = bc.get('InRoomPlan@LoopCaller'), bc.get('InRoomPlanToPoint@LoopCaller')
    check(ip and tp and ip['n'] > 10 and abs(ip['n'] - tp['n']) <= 1, f"in-room counts {ip and ip['n']} / {tp and tp['n']}")
    check(ip['flags'] == {'0x10000': ip['n']} and tp['flags'] == {'0x2010001': tp['n']}, f"flags {ip['flags']} / {tp['flags']}")
    check(ip['x4c'] == {'0': ip['n']} and tp['x4c'] == {'2': tp['n']} and tp['noAgent'] == tp['n'] and ip['noAgent'] == 0, 'X+0x4C and agent per caller')
    check(r['builds'] > 10 and r['buildRows'][0]['lotRect'] == [0, 0, 20, 30] and r['buildRows'][0]['bounds'][:2] == [-1, -1] and r['buildRows'][0]['levels'] == 7, f"SetBounds rows {r['buildRows'][0]}")
    q = r['qtb'][0]
    check(q['ecxIsP'] and q['ecxB8']['f32'] == 0.5 and q['ecxB8']['u32'] == q['cellStatic']['u32'], f'QuadTreeBuild row {q}')
    check(r['errors'] == [], f"no probe errors ({r['errors'][:2]})")
    # unhooked: the counters stop, the thread runs on
    n1 = r['plans']; c1 = h.count(); time.sleep(1.0); c2 = h.count()
    check(c2 > c1 > c0 and p.poll() is None, f'dummy alive and looping after the script finished ({c0} -> {c1} -> {c2})')
    sc.unload(); time.sleep(0.3)
    c3 = h.count(); time.sleep(0.5)
    check(h.count() > c3 and p.poll() is None, 'still alive after unload')
    # 3. fail-fast: in-room hooks on a stub nobody calls -> 2 progress checks of 30 s without a plan = fail with the reason
    prm = params(tmp / 'idle', 2); prm['addrs']['hooks']['inRoomPlan'][0] = prm['addrs']['hooks']['inRoomToPoint'][0] = a['stubs'][4]
    sc = load(prm); r3 = None; deadline = time.time() + 75
    while r3 is None and time.time() < deadline:
        time.sleep(1)
        try: r3 = json.loads((tmp / 'idle-inroom.json').read_text(encoding='utf-8'))
        except (OSError, ValueError): pass
    check(r3 is not None and r3['reason'].startswith('fail: no in-room plan for 60 s') and r3['dumps'] == [] and r3['builds'] > 0, f"fail-fast after 60 s: {r3 and r3['reason']}")
    sc.unload()
    check(p.poll() is None, 'alive after the fail-fast run')
    rep = subprocess.run(['node', str(ROOT / 'tools' / 'harness' / 'inroom-report.ts'), str(tmp / 'run-inroom.json')], capture_output=True, text=True)
    check(rep.returncode == 0 and 'I-98-15: REFUTED' in rep.stdout and 'I-98-21: verified' in rep.stdout and 'I-101-4: verified' in rep.stdout, 'report reads the file')
    print(rep.stdout)
    progress = [m['payload'] for m in logs if m['type'] == 'log']
    print('\n'.join(progress[-6:]))
finally:
    p.kill()
print('RESULT', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
