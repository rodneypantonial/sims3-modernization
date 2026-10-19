# Dummy test of tools/frida/f4b-reads.js (probe-safety rule): 32-bit ping.exe, never the game.
#   python tools/probe/f4b-reads-test.py      (Windows, frida installed; prints RESULT PASS|FAIL)
# A helper script builds three route paths in the dummy (48-byte nodes at path+0x98..+0x9C, +0x20 / +0x24 / path+0x8C as
# CopyNodes writes them): a flat searched path (every y -100), a direct 2-node path at real heights and a searched path with
# real start/goal heights; and stubs with the game's calling shapes: CopyNodes (thiscall, 8 stack args, path = argument 7,
# ret 0x20, al = 1), path slot +0x100 (thiscall(path)(point*), ret 4; adds one 48-byte node to the end pointer, as a prepend
# grows the vector) and the terrain IC (cdecl(x, z, flag) -> st0 = z, plain ret). Thread A ("sim") copies the flat and the direct
# path and calls the prepend from a "follower" call site and from another site; thread B copies the ends path. The script under
# test is the unchanged file; only addresses come in through the test-only init parameter `addrs`. Checks: refusal on wrong
# bytes, chaining on an existing jmp, every read, the terrain call only from the follower site, fail-fast, clean unhook
# (threads keep running, dummy alive), and the report's outcome line on the file.
# Section 4 (unload race, docs/51 §81): thread C loops inside a slow hooked prepend whose onEnter calls a slow terrain stub
# through the thunk (no call cap), so a thread is inside the game call at almost every moment; the script is unloaded mid-call
# 5 times. The dummy must survive each and the file must show the bounded wait with nothing left in flight.
#   python tools/probe/f4b-reads-test.py [--race] [script.js]   (--race: section 4 only; script.js: e.g. an older copy to compare)
import frida, json, pathlib, subprocess, sys, tempfile, time
HERE = pathlib.Path(__file__).parent
ROOT = HERE.parent.parent
args = [x for x in sys.argv[1:] if not x.startswith('--')]
RACE_ONLY = '--race' in sys.argv
src = pathlib.Path(args[0] if args else ROOT / 'tools' / 'frida' / 'f4b-reads.js').read_text(encoding='utf-8')
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
// path with nodes as CopyNodes builds them: 3D direction / length / running length, total at +0x8C; spare room for one prepend
function mkPath(pts) {
  const path = alloc(0xc0), nodes = alloc(48 * (pts.length + 1));
  let run = 0;
  pts.forEach(([x, y, z], i) => {
    const o = nodes.add(48 * i), nx = pts[i + 1], len = nx ? Math.hypot(nx[0] - x, nx[1] - y, nx[2] - z) : 0;
    o.writeFloat(x); o.add(4).writeFloat(y); o.add(8).writeFloat(z); o.add(12).writeFloat(x); o.add(0x20).writeFloat(len); o.add(0x24).writeFloat(run);
    run += len;
  });
  path.add(0x8c).writeFloat(run); path.add(0x98).writePointer(nodes); path.add(0x9c).writePointer(nodes.add(48 * pts.length));
  return { path, end: u(nodes) + 48 * pts.length };
}
const flat = mkPath([[0, -100, 0], [3, -100, 4], [6, -100, 8], [6, -100, 9]]);            // 2D sum 11
const direct = mkPath([[0, 2, 0], [3, 2, 4]]);                                                // real heights, 2 nodes
const ends = mkPath([[0, 2, 0], [3, -100, 4], [6, -100, 8], [7, -100, 8], [9, 3, 8]]);      // 2D sum 13
const pre = mkPath([[1, 2.5, 7], [3, -100, 4], [6, -100, 8]]);                                 // the prepend target (nodes after)
const point = alloc(16); point.writeFloat(1); point.add(4).writeFloat(2.5); point.add(8).writeFloat(7);
const page = Memory.alloc(Process.pageSize * 3); Memory.protect(page, Process.pageSize * 3, 'rwx');
const stub = i => page.add(i * 64);
// 0 CopyNodes: push ebp; mov ebp,esp; 8 nops; pop ebp; mov al,1; ret 0x20
stub(0).writeByteArray([0x55, 0x8b, 0xec, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x5d, 0xb0, 0x01, 0xc2, 0x20, 0x00]);
// 1 prepend: push ebp; mov ebp,esp; 5 nops; add dword [ecx+0x9c], 0x30; pop ebp; ret 4
stub(1).writeByteArray([0x55, 0x8b, 0xec, 0x90, 0x90, 0x90, 0x90, 0x90, 0x83, 0x81, 0x9c, 0x00, 0x00, 0x00, 0x30, 0x5d, 0xc2, 0x04, 0x00]);
// 2 terrain IC: fld dword [esp+8] (z); ret
stub(2).writeByteArray([0xd9, 0x44, 0x24, 0x08, 0xc3, 0x90, 0x90, 0x90]);
// 3 idle (nobody calls it), 4 a jmp rel32 to stub 1 (an existing hook on the prepend slot)
stub(3).writeByteArray([0x55, 0x8b, 0xec, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x5d, 0xc3]);
{ const s = stub(4); s.writeU8(0xe9); s.add(1).writeS32(u(stub(1)) - (u(s) + 5)); }
// 5 slow terrain IC: mov ecx, 8000000; loop $; fld dword [esp+8]; ret (~10-20 ms inside the call)
stub(5).writeByteArray([0xb9, 0x00, 0x12, 0x7a, 0x00, 0xe2, 0xfe, 0xd9, 0x44, 0x24, 0x08, 0xc3]);
// 6 slow prepend-shaped slot: push ebp; mov ebp,esp; 5 nops; mov ecx, 200000; loop $; pop ebp; ret 4 (no node added)
stub(6).writeByteArray([0x55, 0x8b, 0xec, 0x90, 0x90, 0x90, 0x90, 0x90, 0xb9, 0x40, 0x0d, 0x03, 0x00, 0xe2, 0xfe, 0x5d, 0xc2, 0x04, 0x00]);
const ctr = alloc(16); ctr.add(8).writeU32(300000);
const loopC = page.add(0x800);
let raC = 0;
{
  const w = new X86Writer(loopC, { pc: loopC });
  w.putLabel('top');
  w.putMovRegAddress('eax', ctr.add(12)); w.putIncRegPtr('dword', 'eax');
  w.putPushU32(u(point)); w.putMovRegAddress('ecx', pre.path); w.putCallAddress(stub(6)); raC = u(w.pc);
  w.putJmpNearLabel('top'); w.flush();
}
const loopA = page.add(Process.pageSize), loopB = page.add(Process.pageSize * 2);
const copy = (w, p) => { w.putPushU32(0); w.putPushU32(u(p)); for (let i = 0; i < 6; i++) w.putPushU32(0); w.putMovRegAddress('ecx', ptr(0x1234)); w.putCallAddress(stub(0)); };
const delay = w => { w.putMovRegAddress('edx', ctr.add(8)); w.putMovRegRegPtr('edx', 'edx'); w.putLabel('d'); w.putDecReg('edx'); w.putJccNearLabel('jne', 'd', 'no-hint'); };
let raFollow = 0, raOther = 0;
{
  const w = new X86Writer(loopA, { pc: loopA });
  w.putLabel('top');
  w.putMovRegAddress('eax', ctr); w.putIncRegPtr('dword', 'eax');
  copy(w, flat.path); copy(w, direct.path);
  // prepend from the follower site and from another site; the end pointer is reset before each (3 nodes -> 4)
  for (const which of ['follow', 'other']) {
    w.putMovRegAddress('eax', ptr(pre.end)); w.putMovRegAddress('edx', pre.path.add(0x9c)); w.putMovRegPtrReg('edx', 'eax');
    w.putPushU32(u(point)); w.putMovRegAddress('ecx', pre.path); w.putCallAddress(stub(1));
    if (which === 'follow') raFollow = u(w.pc); else raOther = u(w.pc);
  }
  delay(w); w.putJmpNearLabel('top'); w.flush();
}
{
  const w = new X86Writer(loopB, { pc: loopB });
  w.putLabel('top');
  w.putMovRegAddress('eax', ctr.add(4)); w.putIncRegPtr('dword', 'eax');
  copy(w, ends.path);
  delay(w); w.putJmpNearLabel('top'); w.flush();
}
rpc.exports = {
  addrs() { return { stubs: Array.from({ length: 7 }, (_, i) => u(stub(i))), raFollow, raOther, raC, pre: u(pre.path) }; },
  spawnRace() { const c = Memory.alloc(4); CT(NULL, 0, loopC, NULL, 0, c); return c.readU32(); },
  spawn() { const a = Memory.alloc(4), b = Memory.alloc(4); CT(NULL, 0, loopA, NULL, 0, a); CT(NULL, 0, loopB, NULL, 0, b); return [a.readU32(), b.readU32()]; },
  count() { return [ctr.readU32(), ctr.add(4).readU32(), ctr.add(12).readU32()]; },
};"""

STUB = '55 8b ec 90 90 90 90 90'
p = subprocess.Popen([r'C:\Windows\SysWOW64\PING.EXE', '-n', '150', '127.0.0.1'], stdout=subprocess.DEVNULL)
tmp = pathlib.Path(tempfile.mkdtemp(prefix='f4b-test-'))
try:
    time.sleep(1)
    s = frida.attach(p.pid)
    helper = s.create_script(HELPER); helper.load(); h = helper.exports_sync
    a = h.addrs(); tidA, tidB = h.spawn(); time.sleep(0.5)
    st = a['stubs']
    def params(out, minutes, bytes_=STUB, terrain_bytes='d9 44 24 08 c3 90 90 90'):
        return {'out': str(out).replace('\\', '/'), 'minutes': minutes,
                'addrs': {'hooks': {'copyNodes': [st[0], bytes_], 'prepend': [st[1], bytes_]},
                          'calls': {'terrain': [st[2], terrain_bytes]}, 'followRa': [a['raFollow']]}}
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

    def race():
        # 4. unload race: C inside the slow prepend and the slow terrain call through the thunk; unload at 5 different moments
        # The probe runs in its own host (frida_tools.repl -t, as profile-world.ts runs it): its own frida-agent, loaded after
        # this one and unloaded first (LIFO, docs/TESTING.md), so the unload is the probe's alone, as in the game.
        h.spawn_race(); time.sleep(0.3)
        js = tmp / 'probe.js'; js.write_text(src, encoding='utf-8')
        for i in range(5):
            prm = params(tmp / f'race{i}', 0.25, terrain_bytes='b9 00 12 7a 00 e2 fe d9')
            prm['addrs']['hooks']['prepend'][0] = st[6]; prm['addrs']['calls']['terrain'][0] = st[5]; prm['addrs']['followRa'] = [a['raC']]
            prm['terrainCalls'] = 10 ** 9
            rp = subprocess.run([sys.executable, '-m', 'frida_tools.repl', '-p', str(p.pid), '-l', str(js), '-P', json.dumps(prm), '-q', '-t', '1'],
                                capture_output=True, text=True, timeout=60)
            time.sleep(0.5)
            alive = p.poll() is None
            try: c1 = h.count()[2]; time.sleep(0.3); moving = h.count()[2] > c1
            except Exception: moving = False
            r = wait_json(tmp / f'race{i}-f4b.json', 3)
            uw = r and r.get('unloadWait')
            check(alive and moving, f'race {i}: dummy alive and thread C still looping after an unload mid-call')
            check(r and r['reason'] == 'detached before the end' and r['terrain']['calls'] > 0 and uw and uw['inCall'] == 0 and r['errors'] == [],
                  f"race {i}: file {r and r['reason']}, terrain calls {r and r['terrain']['calls']}, wait {uw}, errors {r and r['errors'][:1]}")
            if not alive: return
    if RACE_ONLY:
        race(); print('RESULT', 'PASS' if ok else 'FAIL'); sys.exit(0 if ok else 1)

    # 1. wrong prologue bytes, the prepend hook on an existing jmp: refused / chained; no 0x819E10 hook = fail at once
    prm = params(tmp / 'bad', 0.5, '55 8b ec 90 90 90 90 91', '00'); prm['addrs']['hooks']['prepend'][0] = st[4]
    sc = load(prm); bad = wait_json(tmp / 'bad-f4b.json', 10)
    check(bad and bad['reason'].startswith('fail: no 0x819E10 hook') and set(bad['refused']) == {'copyNodes', 'terrain'}, f"wrong bytes refused: {bad and (bad['reason'], sorted(bad['refused']))}")
    check(bad and list(bad['chained']) == ['prepend'] and bad['hooks'] == 1, f"existing jmp chained: {bad and bad['chained']}")
    sc.unload()
    jb = s.create_script(f"rpc.exports = {{ b() {{ return ptr({st[4]}).readU8(); }} }};"); jb.load()
    check(jb.exports_sync.b() == 0xe9, 'the existing jmp is back after detach'); jb.unload()

    # 2. the real run: 0.25 min = the 15 s minimum window, file at the end
    c0 = h.count()
    sc = load(params(tmp / 'run', 0.25)); r = wait_json(tmp / 'run-f4b.json', 40)
    check(r is not None, 'result file written before the deadline')
    if r is None: print(chr(10).join(str(m) for m in logs[-20:])); raise SystemExit(1)
    check(r['reason'] == 'complete' and r['hooks'] == 2 and not r['refused'] and not r['chained'], f"complete, 2 hooks ({r['reason']}, {r['refused']})")
    c = r['copy']
    check(c['calls'] > 30 and c['retFalse'] == 0 and set(c['threads']) == {str(tidA), str(tidB)}, f"copy calls {c['calls']} on both threads {c['threads']}")
    check(set(c['layouts']) == {'flat/searched', 'real/direct', 'ends/searched'} and set(c['layoutsSearched']) == {'flat', 'ends'}, f"layouts {c['layouts']}")
    fr = next((x for x in c['rows'] if x['layout'] == 'flat'), None); er = next((x for x in c['rows'] if x['layout'] == 'ends'), None)
    check(fr and fr['n'] == 4 and fr['sum2d'] == 11 and fr['total'] == 11 and [d['i'] for d in fr['nodes']] == [0, 1, 2, 3] and all(d['y'] == -100 for d in fr['nodes']) and fr['tid'] == tidA, f"flat row {fr}")
    check(er and er['n'] == 5 and er['sum2d'] == 13 and er['total'] > 200 and abs(er['total'] - er['sum3d']) < 0.01 and [d['i'] for d in er['nodes']] == [0, 1, 3, 4]
          and er['nodes'][0]['y'] == 2 and er['nodes'][0]['len'] > 100 and er['tid'] == tidB, f"ends row {er}")
    P = r['prepend']
    check(P['calls'] > 20 and P['delta'] == {'1': P['calls']} and set(P['ras']) == {hex(a['raFollow']), hex(a['raOther'])} and P['threads'] == {str(tidA): P['calls']}, f"prepend {P['calls']} delta {P['delta']} ras {P['ras']}")
    pr = P['rows'][0]
    check(pr['nBefore'] == 3 and pr['delta'] == 1 and pr['point'] == [1, 2.5, 7] and pr['node0']['y'] == 2.5 and abs(pr['node0']['len'] - (2 ** 2 + 102.5 ** 2 + 3 ** 2) ** 0.5) < 0.01 and pr['node1']['y'] == -100, f"prepend row {pr}")
    T = r['terrain']
    check(T['calls'] > 10 and T['calls'] <= 200 and all(x['terrain'] == 7 and x['dy'] == -4.5 for x in T['rows']) and all(('terrain' in x) == (x['ra'] == hex(a['raFollow'])) for x in P['rows']), f"terrain only from the follower site: {T['calls']} calls {T['rows'][:1]}")
    check(r['errors'] == [], f"no probe errors ({r['errors'][:2]})")
    c1 = h.count(); time.sleep(1.0); c2 = h.count()
    check(c2[0] > c1[0] > c0[0] and c2[1] > c1[1] and p.poll() is None, f'dummy alive and both threads looping after the script finished ({c0} -> {c1} -> {c2})')
    sc.unload(); time.sleep(0.3)
    c3 = h.count(); time.sleep(0.5)
    check(h.count()[0] > c3[0] and p.poll() is None, 'still alive after unload')

    # 3. fail-fast: both hooks on a stub nobody calls -> 2 progress checks of 30 s without a call = fail
    prm = params(tmp / 'idle', 2)
    for n in ('copyNodes', 'prepend'): prm['addrs']['hooks'][n][0] = st[3]
    sc = load(prm); r3 = wait_json(tmp / 'idle-f4b.json', 80)
    check(r3 is not None and r3['reason'].startswith('fail: no route built for 60 s') and r3['copy']['calls'] == 0, f"fail-fast after 60 s: {r3 and r3['reason']}")
    sc.unload()
    check(p.poll() is None, 'alive after the fail-fast run')
    rep = subprocess.run(['node', str(ROOT / 'tools' / 'harness' / 'f4b-report.ts'), str(tmp / 'run-f4b.json')], capture_output=True, text=True)
    check(rep.returncode == 0 and 'outcome (docs/99 §12.6): C - ' in rep.stdout and 'I-99-33: verified' in rep.stdout and 'I-99-39: REFUTED (outcome C)' in rep.stdout
          and 'item 3 agent y vs terrain' in rep.stdout and 'candidate (a) supported' in rep.stdout, 'report reads the file')
    print(rep.stdout, rep.stderr[-2000:])
    print('\n'.join(m['payload'] for m in logs if m['type'] == 'log')[-1500:])
    race()
finally:
    p.kill()
print('RESULT', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
