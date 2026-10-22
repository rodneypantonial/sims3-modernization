# Dummy test of the main-loop stage timers in tools/harness/frame-times.js (docs/167; probe-safety rule): 32-bit ping.exe,
# never the game.
#   python tools/probe/frame-stages-test.py      (Windows, frida installed; prints the overhead and RESULT PASS|FAIL)
# A helper script builds stubs with the game's shapes: a main loop thread calling load, idle, services (a list walk shaped
# like ServiceList_Update: node +8 service, +0x1A enabled byte, call through vtable +0x1C; services A, R (-> consume ->
# read pair + drain, `ret 0x14`, eax = message count), S (disabled: never called), D (walks a second list with service E
# through the same walk code: nested)), capture (-> render callback -> frame(present = 0)), begin,
# frame(present = 1) (thiscall `ret 4`: draw, call present, after-present part), end, plus an extra call to the load stub
# from a second call site (must not count as load) and a thread B that calls consume (other thread: must not count).
# Each stub sleeps its configured ms (timeBeginPeriod(1)). The script under test is the unchanged file with test-only
# `addrs` (stub addresses, their bytes, the loop's return addresses).
# Checks: every stage column near its sleep, nesting (svc >= consume, capture >= rcb), stage sum + other = frameMs, the
# second call site and thread B ignored, consume counters (normal, TryEnter failed, not ready), refusal of a stage whose
# bytes differ (column `na`, logged), per-service top 3 (R, D, A near their sleeps; S and the nested E never listed; the
# services refused with changed walk bytes = `na`, logged), per-frame overhead (loop rate with no sleeps, hooks on vs off),
# and an unload while the main thread is inside a 2 s service call (dummy alive and looping afterwards).
import frida, pathlib, statistics, subprocess, tempfile, time
HERE = pathlib.Path(__file__).parent
ROOT = HERE.parent.parent
src = (ROOT / 'tools' / 'harness' / 'frame-times.js').read_text(encoding='utf-8')
ok = True
def check(c, msg):
    global ok
    print(('ok   ' if c else 'FAIL ') + msg); ok = ok and bool(c)

HELPER = r"""
const k = Process.getModuleByName('kernel32.dll'), sleep = k.getExportByName('Sleep');
const tbp = new NativeFunction(Module.load('winmm.dll').getExportByName('timeBeginPeriod'), 'uint', ['uint']);
const CT = new NativeFunction(k.getExportByName('CreateThread'), 'pointer', ['pointer', 'uint', 'pointer', 'pointer', 'uint', 'pointer']);
// ctl: 0 iterations, 4 thread B iterations, 8.. sleep ms per stub, 0x40 tryfail, 0x44 notready, 0x48 msgs
const ctl = Memory.alloc(0x100);
const SL = { load: 8, load2: 12, idle: 16, svc: 20, consume: 24, capture: 28, rcb: 32, begin: 36, draw: 40, present: 44, post: 48, end: 52, other: 56,
  svcA: 0x50, svcR: 0x54, svcS: 0x58, svcD: 0x5c, svcE: 0x60 };
const page = Memory.alloc(Process.pageSize * 2); Memory.protect(page, Process.pageSize * 2, 'rwx');
let at = page; const fns = {}, rets = {};
function emit(name, body) { const w = new X86Writer(at, { pc: at }); body(w); w.flush(); fns[name] = at; at = at.add((w.offset + 31) & ~15); }
const pro = w => { w.putPushReg('ebp'); w.putMovRegReg('ebp', 'esp'); for (let i = 0; i < 5; i++) w.putNop(); };
const nap = (w, slot) => { // if ([ctl+slot]) Sleep([ctl+slot])
  const l = 'n' + slot + '_' + Math.random().toString(36).slice(2);
  w.putMovRegAddress('eax', ctl.add(slot)); w.putMovRegRegPtr('eax', 'eax'); w.putTestRegReg('eax', 'eax'); w.putJccShortLabel('je', l, 'no-hint');
  w.putPushReg('eax'); w.putCallAddress(sleep); w.putLabel(l);
};
const leaf = (name, slot, retImm) => emit(name, w => { pro(w); nap(w, slot); w.putPopReg('ebp'); retImm ? w.putRetImm(retImm) : w.putRet(); });
leaf('load', SL.load); leaf('idle', SL.idle); leaf('begin', SL.begin); leaf('end', SL.end); leaf('present', SL.present);
emit('readpair', w => { pro(w); w.putPopReg('ebp'); w.putRetImm(8); });
emit('drain', w => { pro(w); w.putMovRegAddress('eax', ctl.add(0x48)); w.putMovRegRegPtr('eax', 'eax'); w.putPopReg('ebp'); w.putRetImm(0x14); });
emit('consume', w => {
  pro(w); nap(w, SL.consume);
  w.putMovRegAddress('eax', ctl.add(0x40)); w.putMovRegRegPtr('eax', 'eax'); w.putTestRegReg('eax', 'eax'); w.putJccNearLabel('jne', 'out', 'no-hint');
  w.putPushU32(0); w.putPushU32(0); w.putCallAddress(fns.readpair);
  w.putMovRegAddress('eax', ctl.add(0x44)); w.putMovRegRegPtr('eax', 'eax'); w.putTestRegReg('eax', 'eax'); w.putJccNearLabel('jne', 'out', 'no-hint');
  for (let i = 0; i < 5; i++) w.putPushU32(0); w.putCallAddress(fns.drain);
  w.putLabel('out'); w.putPopReg('ebp'); w.putRet();
});
// walk(list): ServiceList_Update's loop; rets.wstart = call setup (esi = node), rets.wend = after the call (also the skip target)
emit('walk', w => {
  w.putPushReg('esi'); w.putPushReg('edi'); w.putMovRegRegOffsetPtr('edi', 'esp', 12); w.putMovRegRegPtr('esi', 'edi');
  w.putLabel('top'); w.putCmpRegReg('esi', 'edi'); w.putJccNearLabel('je', 'done', 'no-hint');
  w.putBytes(new Uint8Array([0x80, 0x7e, 0x1a, 0x00]).buffer); w.putJccNearLabel('je', 'next', 'no-hint'); // cmp byte [esi+1Ah], 0
  rets.wstart = w.pc; w.putMovRegRegOffsetPtr('ecx', 'esi', 8); w.putMovRegRegPtr('edx', 'ecx'); w.putCallRegOffsetPtr('edx', 0x1c);
  w.putLabel('next'); rets.wend = w.pc; w.putMovRegRegPtr('esi', 'esi'); w.putJmpNearLabel('top');
  w.putLabel('done'); w.putPopReg('edi'); w.putPopReg('esi'); w.putRet();
});
const lists = [Memory.alloc(16), Memory.alloc(16)], names = {}, keep = []; // list objects: +0 first node, the last node points back; keep = no GC of Memory.alloc blocks
function service(name, emitBody) {
  emit('svc' + name, w => { pro(w); emitBody(w); w.putPopReg('ebp'); w.putRet(); });
  const vt = Memory.alloc(0x40), obj = Memory.alloc(16); keep.push(vt, obj); vt.add(0x1c).writePointer(fns['svc' + name]); obj.writePointer(vt);
  names[vt.toUInt32()] = name; return obj;
}
const callWalk = (w, l) => { w.putPushU32(lists[l].toUInt32()); w.putCallAddress(fns.walk); w.putAddRegImm('esp', 4); };
function list(l, items) { // [service object, enabled]
  let prev = lists[l];
  for (const [obj, en] of items) { const n = Memory.alloc(0x20); keep.push(n); n.add(8).writePointer(obj); n.add(0x14).writeU32(1); n.add(0x1a).writeU8(en); prev.writePointer(n); prev = n; }
  prev.writePointer(lists[l]);
}
list(1, [[service('E', w => nap(w, SL.svcE)), 1]]);
list(0, [[service('A', w => nap(w, SL.svcA)), 1], [service('R', w => { nap(w, SL.svcR); w.putCallAddress(fns.consume); }), 1],
  [service('S', w => nap(w, SL.svcS)), 0], [service('D', w => { nap(w, SL.svcD); callWalk(w, 1); }), 1]]);
const listVar = Memory.alloc(4); listVar.writePointer(lists[0]);
emit('svc', w => { pro(w); nap(w, SL.svc); callWalk(w, 0); w.putPopReg('ebp'); w.putRet(); });
emit('frame', w => { // thiscall frame(present), ret 4
  pro(w); nap(w, SL.draw);
  w.putMovRegRegOffsetPtr('eax', 'ebp', 8); w.putTestRegReg('eax', 'eax'); w.putJccNearLabel('je', 'np', 'no-hint');
  w.putCallAddress(fns.present); rets.present = w.pc; nap(w, SL.post);
  w.putLabel('np'); w.putPopReg('ebp'); w.putRetImm(4);
});
emit('rcb', w => { pro(w); nap(w, SL.rcb); w.putPushU32(0); w.putCallAddress(fns.frame); w.putPopReg('ebp'); w.putRet(); });
emit('capture', w => { pro(w); nap(w, SL.capture); w.putCallAddress(fns.rcb); w.putPopReg('ebp'); w.putRet(); });
emit('loopA', w => {
  w.putLabel('top');
  w.putMovRegAddress('eax', ctl); w.putIncRegPtr('dword', 'eax');
  w.putCallAddress(fns.load); rets.load = w.pc;
  w.putCallAddress(fns.idle); rets.idle = w.pc;
  w.putCallAddress(fns.svc); rets.svc = w.pc;
  w.putCallAddress(fns.capture); rets.capture = w.pc;
  w.putCallAddress(fns.begin); rets.begin = w.pc;
  w.putPushU32(1); w.putCallAddress(fns.frame);
  w.putCallAddress(fns.end); rets.end = w.pc;
  nap(w, SL.other);
  w.putMovRegAddress('eax', ctl.add(SL.load2)); w.putMovRegRegPtr('eax', 'eax'); w.putTestRegReg('eax', 'eax'); w.putJccNearLabel('je', 'top', 'no-hint');
  w.putCallAddress(fns.load); // second call site: must not count as load (its own sleep is load2 via the gap below)
  nap(w, SL.load2);
  w.putJmpNearLabel('top');
});
emit('loopB', w => { w.putLabel('top'); w.putMovRegAddress('eax', ctl.add(4)); w.putIncRegPtr('dword', 'eax'); w.putCallAddress(fns.consume); w.putPushU32(3); w.putCallAddress(sleep); w.putJmpNearLabel('top'); });
const hex = p => Array.from(new Uint8Array(p.readByteArray(8))).map(b => b.toString(16).padStart(2, '0')).join(' ');
rpc.exports = {
  addrs() {
    const a = { frame: [fns.frame.toUInt32(), hex(fns.frame)] };
    for (const n of ['load', 'idle', 'svc', 'consume', 'capture', 'rcb', 'begin', 'present', 'end', 'readpair', 'drain'])
      a[n] = [fns[n].toUInt32(), hex(fns[n]), rets[n] ? rets[n].toUInt32() : 0];
    a.walk = { list: listVar.toUInt32(), check: [[rets.wstart.toUInt32(), hex(rets.wstart)], [rets.wend.toUInt32(), hex(rets.wend)]],
      start: rets.wstart.toUInt32(), end: rets.wend.toUInt32(), names };
    return a;
  },
  spawn(b) { tbp(1); const x = Memory.alloc(4); CT(NULL, 0, b ? fns.loopB : fns.loopA, NULL, 0, x); return x.readU32(); },
  set(cfg) { for (const [n, v] of Object.entries(cfg)) ctl.add(SL[n] ?? { tryfail: 0x40, notready: 0x44, msgs: 0x48 }[n]).writeU32(v); },
  mem(a, n) { return hex(ptr(a)).split(' ').slice(0, n).join(' '); },
  count() { return [ctl.readU32(), ctl.add(4).readU32()]; },
};"""

p = subprocess.Popen([r'C:\Windows\SysWOW64\PING.EXE', '-n', '300', '127.0.0.1'], stdout=subprocess.DEVNULL)
tmp = pathlib.Path(tempfile.mkdtemp(prefix='frame-stages-'))
logs = []
try:
    time.sleep(1)
    s = frida.attach(p.pid)
    helper = s.create_script(HELPER); helper.on("message", lambda m, d: m["type"] == "error" and print("helper:", m.get("description"), m.get("lineNumber"))); helper.load(); h = helper.exports_sync
    A = h.addrs()
    def load(out, addrs):
        sc = s.create_script(src)
        sc.set_log_handler(lambda level, text: logs.append(text))
        sc.load(); sc.exports_sync.init('late', {'out': str(out).replace('\\', '/'), 'addrs': addrs})
        return sc
    def rows(f):
        ls = f.read_text(encoding='utf-8').splitlines()
        head = ls[0].split(',')
        return [dict(zip(head, l.split(','))) for l in ls[1:] if l.strip()]
    num = lambda r, k: float(r[k])
    med = lambda rs, k: statistics.median(num(r, k) for r in rs)

    # 1. overhead: loop with no sleeps, iterations per second unhooked vs hooked (the loop thread is the main thread)
    h.set({'msgs': 5}); h.spawn(False); time.sleep(0.5)
    def rate():
        c0 = h.count()[0]; t = time.perf_counter(); time.sleep(1.0); return (h.count()[0] - c0) / (time.perf_counter() - t)
    r_off = statistics.median(rate() for _ in range(3))
    # attach and detach while the loop sleeps: Frida patches the 5 entry bytes without suspending the thread, so a tight loop
    # with no sleeps can execute a half-written jmp (seen 2026-10-22: access violation at a stub's entry +4 / +1). The game
    # calls each hooked site a few times per frame, so the window there is a few ns per frame.
    h.set({'other': 20}); time.sleep(0.1)
    sc = load(tmp / 'bench.csv', A); h.set({'other': 0}); time.sleep(0.3)
    r_on = statistics.median(rate() for _ in range(3))
    h.set({'other': 20}); time.sleep(0.1); sc.unload(); time.sleep(0.3)
    per = 1e6 / r_on - 1e6 / r_off
    print(f'overhead: {r_off:.0f} -> {r_on:.0f} iterations/s, {per:.2f} us per frame (13 hooked calls + 9 service probes + the frame hook per iteration)')
    check(per < 50, f'per-frame overhead {per:.2f} us < 50 us')

    # 2. one stage with other bytes: refused, logged, column na; the rest runs
    sleeps = {'load': 2, 'idle': 1, 'svc': 10, 'consume': 5, 'capture': 3, 'rcb': 1, 'begin': 2, 'draw': 4, 'present': 6, 'post': 3, 'end': 8, 'other': 2, 'load2': 7,
              'svcA': 2, 'svcR': 4, 'svcS': 50, 'svcD': 1, 'svcE': 3}
    h.set(sleeps)
    bad = dict(A); bad['end'] = [A['end'][0], '55 8b ec 90 90 90 90 91', A['end'][2]]
    bad['walk'] = dict(A['walk']); bad['walk']['check'] = [A['walk']['check'][0], [A['walk']['check'][1][0], '90 90 90 90 90 90 90 90']]
    logs.clear(); sc = load(tmp / 'bad.csv', bad); time.sleep(1.5); sc.unload(); time.sleep(0.3)
    rb = rows(tmp / 'bad.csv')
    check(len(rb) > 5 and all(r['endMs'] == 'na' for r in rb) and all(r['svcMs'] != 'na' for r in rb), f'refused stage: endMs na in {len(rb)} rows, others timed')
    check(any('stage NOT hooked' in l and 'end @' in l for l in logs), 'refusal logged')
    check(all(r['svc1'] == 'na' and r['svc1Ms'] == 'na' for r in rb) and any('services @' in l for l in logs), 'changed walk bytes: services na, logged')
    check(h.mem(A['walk']['start'], 8) == A['walk']['check'][0][1] and h.mem(A['walk']['end'], 8) == A['walk']['check'][1][1], 'probes removed on unload (walk bytes restored)')

    # 3. the real run, thread B calling consume from another thread
    h.spawn(True)
    sc = load(tmp / 'run.csv', A); time.sleep(3); sc.unload(); time.sleep(0.3)
    rr = rows(tmp / 'run.csv')[2:]
    check(len(rr) > 20, f'{len(rr)} frames')
    # svcMs = 6 sleeps (svc, A, R, consume, D, E), each up to ~1 ms over: +7 allowed
    # rcb also runs frame(present = 0): its draw sleep (4) is in rcb and capture, not a frame. other = 2 + the second-site
    # load call (2) + load2 (7)
    want = {'loadMs': 2, 'idleMs': 1, 'svcMs': 25, 'consumeMs': 5, 'captureMs': 8, 'rcbMs': 5, 'beginMs': 2, 'drawMs': 4, 'presentMs': 6, 'postMs': 3, 'endMs': 8, 'otherMs': 11}
    for k_, v in want.items():
        m = med(rr, k_)
        check(v - 0.3 <= m <= v + (7 if k_ == 'svcMs' else 3), f'{k_} median {m:.2f} ms (sleeps {v} ms)')
    tops = ['loadMs', 'idleMs', 'svcMs', 'captureMs', 'beginMs', 'drawMs', 'presentMs', 'postMs', 'endMs', 'otherMs']
    check(all(abs(sum(num(r, k_) for k_ in tops) - num(r, 'frameMs')) < 0.1 for r in rr), 'top-level stages + other = frameMs on every frame')
    check(all(num(r, 'svcMs') >= num(r, 'consumeMs') and num(r, 'captureMs') >= num(r, 'rcbMs') for r in rr), 'nesting: svc >= consume, capture >= rcb')
    check(all((r['consumes'], r['tryFail'], r['notReady'], r['drains'], r['msgs']) == ('1', '0', '0', '1', '5') for r in rr), 'counters 1 consume, 1 drain, 5 msgs (thread B ignored)')
    check(h.count()[1] > 100, f'thread B looped {h.count()[1]} times')
    # services: R = 4 + consume 5, D = 1 + nested E 3, A = 2; S disabled, E only inside D
    check(all((r['svc1'], r['svc2'], r['svc3']) == ('R', 'D', 'A') for r in rr), f'top 3 R, D, A on every frame: {[(r["svc1"], r["svc2"], r["svc3"]) for r in rr[:3]]}')
    for k_, v in {'svc1Ms': 9, 'svc2Ms': 4, 'svc3Ms': 2}.items():
        m = med(rr, k_)
        check(v - 0.3 <= m <= v + 3, f'{k_} median {m:.2f} ms (sleeps {v} ms)')
    check(all(sum(num(r, k_) for k_ in ('svc1Ms', 'svc2Ms', 'svc3Ms')) <= num(r, 'svcMs') for r in rr), 'services sum <= svcMs on every frame')
    check(any(l.startswith('[frames] services (+0x1C, call order): A R D; not called: 1') for l in logs), 'service list logged in call order')

    # 4. consume skipped: TryEnter failed (no read pair), then not ready (pair, no drain)
    h.set({'tryfail': 1}); sc = load(tmp / 'tf.csv', A); time.sleep(1.2); sc.unload(); time.sleep(0.3)
    rt = rows(tmp / 'tf.csv')[2:]
    check(rt and all((r['consumes'], r['tryFail'], r['notReady'], r['drains'], r['msgs']) == ('1', '1', '0', '0', '0') for r in rt), f'TryEnter failed: {[(r["tryFail"], r["drains"]) for r in rt[:3]]}')
    h.set({'tryfail': 0, 'notready': 1}); sc = load(tmp / 'nr.csv', A); time.sleep(1.2); sc.unload(); time.sleep(0.3)
    rn = rows(tmp / 'nr.csv')[2:]
    check(rn and all((r['consumes'], r['tryFail'], r['notReady'], r['drains']) == ('1', '0', '1', '0') for r in rn), f'not ready: {[(r["notReady"], r["drains"]) for r in rn[:3]]}')
    h.set({'notready': 0})

    # 5. unload while the main thread sleeps 2 s inside service A (between the two walk probes): dummy alive, loop continues
    sc = load(tmp / 'unload.csv', A); time.sleep(0.5)
    h.set({'svcA': 2000}); time.sleep(0.6)
    t = time.perf_counter(); sc.unload(); dt = time.perf_counter() - t
    h.set({'svcA': 2})
    c0 = h.count(); time.sleep(3); c1 = h.count()
    check(p.poll() is None and c1[0] > c0[0] and c1[1] > c0[1], f'unload inside service A took {dt:.2f} s; dummy alive, both threads looping ({c0} -> {c1})')
finally:
    p.kill()
print('RESULT', 'PASS' if ok else 'FAIL')
