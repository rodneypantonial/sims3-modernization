# Dummy test of the task attribution in tools/frida/stutter-profile.js (docs/166; probe-safety rule): 32-bit ping.exe, never the game.
#   python tools/probe/stutter-tasks-test.py      (Windows, frida installed; prints the overhead and RESULT PASS|FAIL)
# A helper script builds fake Mono metadata with the layouts the profiler reads (MonoClass name +0x34 / namespace +0x38,
# MonoVTable +0 = klass, MonoMethod klass +8 / name +0x18, task state [0] method [1] object +0x140 depth, frame cells at
# +0x3C + 4 * depth with the MonoMethod at cell +4, map entry +8 = state, MonoClassField type +0 (attrs +4) / offset +8,
# MonoReflectionMethod +4 = MonoMethod) and stubs with the game's calling shapes: four seams (thiscall host), the task run
# (thiscall(host, &entry, 1, 1), ret 0xC), runtime invoke as `jmp [slot]` (cdecl(method, obj, params, exc)), the yield frame
# save (cdecl(state, ...), moves the depth from 0 to 1), the iteration boundary, and a field lookup stub (returns the field
# record at fake class +0x40 whatever the name). Thread A ("sim") loops: boundary, ScriptTick (static invoke), TaskObjects
# (slow wrapped delegate task that yields, a fast task, a medium task with a nested invoke), OneShot (invoke on a wrapped
# object), ThreadedObjects (ScriptProxy -> Sim), Sleep(1). Thread B runs the slow task outside any seam.
# The script under test is the unchanged file in its test-only mode (`taskOnly`, addresses through `addrs`).
# Checks: names, stages, order, from/to frames, run counts, nested invoke and other thread ignored, refusal on wrong bytes,
# per-task overhead (bench: 200,000 fast tasks in one TaskObjects call, attribution on vs off), the cost on a runtime invoke
# outside any seam, and unloads while the sim thread is inside a 2 s task run (dummy alive, thread still looping).
import frida, json, pathlib, subprocess, sys, tempfile, time
HERE = pathlib.Path(__file__).parent
ROOT = HERE.parent.parent
src = (ROOT / 'tools' / 'frida' / 'stutter-profile.js').read_text(encoding='utf-8')
ok = True
def check(c, msg):
    global ok
    print(('ok   ' if c else 'FAIL ') + msg); ok = ok and bool(c)

HELPER = r"""
const k = Process.getModuleByName('kernel32.dll');
const CT = new NativeFunction(k.getExportByName('CreateThread'), 'pointer', ['pointer', 'uint', 'pointer', 'pointer', 'uint', 'pointer']);
const heap = Memory.alloc(0x20000); let top = 0x100;
const alloc = n => { const a = heap.add(top); top += (n + 15) & ~15; return a; };
const u = p => p.toUInt32();
const str = s => { const p = alloc(s.length + 1); p.writeUtf8String(s); return p; };
const fieldRec = off => { const t = alloc(8), f = alloc(0x1c); f.writePointer(t); f.add(8).writeS32(off); return f; }; // attrs 0: instance
function klass(ns, name, field) { const c = alloc(0x80); c.add(0x34).writePointer(str(name)); c.add(0x38).writePointer(str(ns)); if (field) c.add(0x40).writePointer(field); const vt = alloc(16); vt.writePointer(c); return { c, vt }; }
const obj = (k, size) => { const o = alloc(size || 0x20); o.writePointer(k.vt); return o; };
function method(k, name) { const m = alloc(0x40); m.add(8).writePointer(k.c); m.add(0x18).writePointer(str(name)); return m; }
const cell = m => { const c = alloc(0x2c); c.add(4).writePointer(m); return c; };
function state(m, o, cells, depth) { const s = alloc(0x150); s.writePointer(m); s.add(4).writePointer(o); cells.forEach((c, i) => s.add(0x3c + 4 * i).writePointer(cell(c))); s.add(0x140).writeS32(depth); return s; }
const entries = [];
function entry(st, spin, yieldState, nested) { const e = alloc(0x20); e.add(8).writePointer(st); e.add(0x10).writeU32(spin); if (yieldState) e.add(0x14).writePointer(yieldState); e.add(0x18).writeU32(nested || 0); entries.push(e); return e; }
// classes and objects
const proxy = klass('ScriptCore', 'ScriptProxy', fieldRec(8)), fnTask = klass('', 'FunctionTask', fieldRec(8));
const fn = klass('Sims3.SimIFace', 'Function', fieldRec(0x14)), refl = klass('System.Reflection', 'MonoMethod');
const sim = klass('Sims3.Gameplay.Actors', 'Sim'), alarm = klass('Sims3.Gameplay.Utilities', 'AlarmManager');
const autonomy = klass('Sims3.Gameplay.Autonomy', 'AutonomyManager'), simulator = klass('Sims3.SimIFace', 'Simulator');
const stuck = klass('NRaas.OverwatchSpace.Alarms', 'StuckCheck'), interaction = klass('Sims3.Gameplay.Interactions', 'InteractionInstance');
const mPerform = method(stuck, 'PrivatePerformAction'), mDoLoop = method(interaction, 'DoLoop'), mRun = method(interaction, 'RunInteraction');
const mFnSim = method(fnTask, 'Simulate'), mProxySim = method(proxy, 'Simulate'), mAutSim = method(autonomy, 'Simulate'), mTick = method(simulator, 'Tick');
const reflObj = obj(refl); reflObj.add(4).writePointer(mPerform);
const delegate = obj(fn, 0x20); delegate.add(0x14).writePointer(reflObj);
const fnObj = obj(fnTask); fnObj.add(8).writePointer(delegate);
const simProxy = obj(proxy); simProxy.add(8).writePointer(obj(sim));
const alarmProxy = obj(proxy); alarmProxy.add(8).writePointer(obj(alarm));
const autObj = obj(autonomy);
// task states: E1 resumes at depth 0 (cells: RunInteraction) and yields to depth 1 (DoLoop < RunInteraction)
const s1 = state(mFnSim, fnObj, [mRun, mDoLoop], 0), s2 = state(mAutSim, autObj, [], -1), s3 = state(mAutSim, autObj, [], -1);
const s4 = state(mProxySim, simProxy, [mRun], 0);
const SLOW = 10000000, MED = 3000000, INV = 3000000;
const e1 = entry(s1, SLOW, s1), e2 = entry(s2, 1000), e3 = entry(s3, MED, null, INV), e4 = entry(s4, 6000000);
const ctl = alloc(0x40), host = alloc(0x2000);
const listTask = alloc(16), listThr = alloc(8), listBench = alloc(8), eBench = entry(s2, 1);
listTask.writePointer(e1); listTask.add(4).writePointer(e2); listTask.add(8).writePointer(e3); listThr.writePointer(e4); listBench.writePointer(eBench);
ctl.add(4).writeU32(1); ctl.add(8).writePointer(listTask); ctl.add(0x10).writeU32(INV);
const slot = alloc(4);
const page = Memory.alloc(Process.pageSize * 4); Memory.protect(page, Process.pageSize * 4, 'rwx');
let at = page;
const fns = {};
function emit(name, body) { const w = new X86Writer(at, { pc: at }); body(w); w.flush(); fns[name] = at; at = at.add((w.offset + 31) & ~15); }
const pro = w => { w.putPushReg('ebp'); w.putMovRegReg('ebp', 'esp'); for (let i = 0; i < 5; i++) w.putNop(); };
const epi = w => { w.putPopReg('ebp'); };
emit('boundary', w => { pro(w); epi(w); w.putRet(); });
emit('invokeBody', w => { w.putMovRegRegOffsetPtr('ecx', 'esp', 12); w.putBytes([0xe3, 0x02, 0xe2, 0xfe]); w.putRet(); }); // jecxz +2; loop $
emit('invoke', w => { const a = u(slot); w.putBytes([0xff, 0x25, a & 0xff, (a >>> 8) & 0xff, (a >>> 16) & 0xff, a >>> 24, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc]); });
slot.writePointer(fns.invokeBody);
emit('yieldSave', w => { pro(w); w.putMovRegRegOffsetPtr('eax', 'ebp', 8); w.putMovRegOffsetPtrU32('eax', 0x140, 1); epi(w); w.putRet(); });
emit('fieldFromName', w => { w.putBytes([0x8b, 0x44, 0x24, 0x04, 0x8b, 0x40, 0x40, 0xc3]); });
emit('run', w => {
  pro(w); w.putPushReg('esi');
  w.putMovRegRegOffsetPtr('eax', 'ebp', 8); w.putMovRegRegPtr('esi', 'eax');
  w.putMovRegRegOffsetPtr('ecx', 'esi', 0x10); w.putBytes([0xe3, 0x02, 0xe2, 0xfe]);
  w.putMovRegRegOffsetPtr('edx', 'esi', 0x14); w.putTestRegReg('edx', 'edx'); w.putJccShortLabel('je', 'noy', 'no-hint');
  w.putPushU32(0); w.putPushU32(0); w.putPushReg('edx'); w.putCallAddress(fns.yieldSave); w.putAddRegImm('esp', 12);
  w.putLabel('noy');
  w.putMovRegRegOffsetPtr('edx', 'esi', 0x18); w.putTestRegReg('edx', 'edx'); w.putJccShortLabel('je', 'non', 'no-hint');
  w.putPushU32(0); w.putPushReg('edx'); w.putPushU32(0); w.putPushU32(0); w.putCallAddress(fns.invoke); w.putAddRegImm('esp', 16);
  w.putLabel('non');
  w.putPopReg('esi'); epi(w); w.putRetImm(12);
});
// seam over a list of entry slots: reps x (each entry: run(host, &slot, 1, 1))
const seamList = (w, listAt) => {
  pro(w); w.putPushReg('esi'); w.putPushReg('edi');
  w.putMovRegAddress('edi', ctl.add(4)); w.putMovRegRegPtr('edi', 'edi');
  w.putLabel('rep'); w.putMovRegAddress('esi', listAt); w.putMovRegRegPtr('esi', 'esi');
  w.putLabel('l'); w.putMovRegRegPtr('eax', 'esi'); w.putTestRegReg('eax', 'eax'); w.putJccShortLabel('je', 'next', 'no-hint');
  w.putPushU32(1); w.putPushU32(1); w.putPushReg('esi'); w.putMovRegAddress('ecx', host); w.putCallAddress(fns.run);
  w.putAddRegImm('esi', 4); w.putJmpShortLabel('l');
  w.putLabel('next'); w.putDecReg('edi'); w.putJccNearLabel('jne', 'rep', 'no-hint');
  w.putPopReg('edi'); w.putPopReg('esi'); epi(w); w.putRet();
};
const listPtr = alloc(4), thrPtr = alloc(4); listPtr.writePointer(listTask); thrPtr.writePointer(listThr);
emit('taskObjects', w => seamList(w, ctl.add(8)));
const one = alloc(4); one.writeU32(1);
emit('threaded', w => { pro(w); w.putPushU32(1); w.putPushU32(1); w.putPushU32(u(listThr)); w.putMovRegAddress('ecx', host); w.putCallAddress(fns.run); epi(w); w.putRet(); });
const invokeCall = (w, m, o) => { w.putPushU32(0); w.putMovRegAddress('eax', ctl.add(0x10)); w.putMovRegRegPtr('eax', 'eax'); w.putPushReg('eax'); w.putPushU32(u(o)); w.putPushU32(u(m)); w.putCallAddress(fns.invoke); w.putAddRegImm('esp', 16); };
emit('oneShot', w => { pro(w); invokeCall(w, mProxySim, alarmProxy); epi(w); w.putRet(); });
emit('scriptTick', w => { pro(w); invokeCall(w, mTick, ptr(0)); epi(w); w.putRet(); });
const sleep = k.getExportByName('Sleep');
emit('loopA', w => {
  w.putLabel('top');
  w.putMovRegAddress('eax', ctl); w.putIncRegPtr('dword', 'eax');
  for (const f of ['boundary', 'scriptTick', 'taskObjects', 'oneShot', 'threaded']) { w.putMovRegAddress('ecx', host); w.putCallAddress(fns[f]); }
  w.putPushU32(1); w.putCallAddress(sleep);
  w.putJmpNearLabel('top');
});
// thread B: the slow task, outside any seam, on another thread
const slotB = alloc(4); slotB.writePointer(e1);
emit('loopB', w => {
  w.putLabel('top');
  w.putMovRegAddress('eax', ctl.add(0x14)); w.putIncRegPtr('dword', 'eax');
  w.putPushU32(1); w.putPushU32(1); w.putPushU32(u(slotB)); w.putMovRegAddress('ecx', host); w.putCallAddress(fns.run);
  w.putPushU32(5); w.putCallAddress(sleep);
  w.putJmpNearLabel('top');
});
// bench: n runtime invokes with no spin, called from the JS thread (outside any seam)
emit('benchInvoke', w => {
  w.putPushReg('esi'); w.putMovRegRegOffsetPtr('esi', 'esp', 8);
  w.putLabel('l'); w.putPushU32(0); w.putPushU32(0); w.putPushU32(0); w.putPushU32(0); w.putCallAddress(fns.invoke); w.putAddRegImm('esp', 16);
  w.putDecReg('esi'); w.putJccShortLabel('jne', 'l', 'no-hint');
  w.putPopReg('esi'); w.putRet();
});
const benchInvoke = new NativeFunction(fns.benchInvoke, 'void', ['uint']);
const qpc = new NativeFunction(k.getExportByName('QueryPerformanceCounter'), 'int', ['pointer']), qb = Memory.alloc(8);
const qpf = new NativeFunction(k.getExportByName('QueryPerformanceFrequency'), 'int', ['pointer']); qpf(qb); const tpm = Number(qb.readS64()) / 1000;
const ms = () => { qpc(qb); return Number(qb.readS64()) / tpm; };
const hex = p => Array.from(new Uint8Array(p.readByteArray(8))).map(b => b.toString(16).padStart(2, '0')).join(' ');
rpc.exports = {
  addrs() { return Object.fromEntries(Object.entries(fns).map(([n, p]) => [n, [u(p), hex(p)]])); },
  spawn() { const a = Memory.alloc(4), b = Memory.alloc(4); CT(NULL, 0, fns.loopA, NULL, 0, a); CT(NULL, 0, fns.loopB, NULL, 0, b); return [a.readU32(), b.readU32()]; },
  count() { return [ctl.readU32(), ctl.add(0x14).readU32()]; },
  bench(on) { // on: TaskObjects runs 200,000 copies of a 1-loop task instead of its list
    if (on) { ctl.add(4).writeU32(200000); ctl.add(8).writePointer(listBench); } else { ctl.add(8).writePointer(listTask); ctl.add(4).writeU32(1); }
  },
  slow(n) { e1.add(0x10).writeU32(n); },
  benchInvoke(n) { const t = ms(); benchInvoke(n); return ms() - t; },
  reset() { s1.add(0x140).writeS32(0); },
};"""

p = subprocess.Popen([r'C:\Windows\SysWOW64\PING.EXE', '-n', '300', '127.0.0.1'], stdout=subprocess.DEVNULL)
tmp = pathlib.Path(tempfile.mkdtemp(prefix='stutter-tasks-'))
logs = []
try:
    time.sleep(1)
    s = frida.attach(p.pid)
    helper = s.create_script(HELPER); helper.on("message", lambda m, d: m["type"] == "error" and print("helper:", m.get("description"), m.get("lineNumber"))); helper.load(); h = helper.exports_sync
    A = h.addrs()
    hooks = {'gcTryToCollect': A['boundary'], 'scriptTick': A['scriptTick'], 'taskObjects': A['taskObjects'], 'oneShot': A['oneShot'],
             'threaded': A['threaded'], 'taskRun': A['run'], 'scriptInvoke': A['invoke'], 'yieldSave': A['yieldSave']}
    inv_base = min(h.bench_invoke(200000) for _ in range(3))  # before any hook
    tidA, tidB = h.spawn(); time.sleep(0.3)
    def params(out, **kw):
        return {'out': str(out).replace('\\', '/'), 'hitchMs': 20, 'taskOnly': True, 'addrs': dict(hooks), 'fieldFromName': A['fieldFromName'][0], **kw}
    def load(prm):
        sc = s.create_script(src)
        sc.set_log_handler(lambda level, text: logs.append(text))
        sc.load(); sc.exports_sync.init('late', prm)
        return sc
    def lines(f):
        try: return [json.loads(l) for l in f.read_text(encoding='utf-8').splitlines() if l.strip()]
        except OSError: return []
    def log_text(): return '\n'.join(logs)

    # 1. wrong bytes on the task run hook: attribution off, the profiler keeps writing hitch lines (no tasks field)
    prm = params(tmp / 'bad'); prm['addrs']['taskRun'] = [A['run'][0], '55 8b ec 90 90 90 90 91']
    sc = load(prm); time.sleep(2); sc.unload(); time.sleep(0.3)
    hl = lines(tmp / 'bad-hitches.jsonl')
    check(len(hl) > 5 and all('tasks' not in x for x in hl), f'wrong bytes: {len(hl)} hitch lines without tasks')

    # 2. the real run
    sc = load(params(tmp / 'run')); time.sleep(3); sc.unload(); time.sleep(0.3)
    hl = lines(tmp / 'run-hitches.jsonl')[2:]  # the first lines may cover the attach
    check(len(hl) > 20, f'{len(hl)} hitch lines')
    x = hl[len(hl) // 2] if hl else {}
    print('sample line tasks:', json.dumps(x.get('tasks')), x.get('taskRuns'), x.get('taskDropped'))
    T = x.get('tasks') or []
    names = [(t['stage'], t['name']) for t in T]
    want = {('taskObjects', 'FunctionTask>NRaas.OverwatchSpace.Alarms.StuckCheck.PrivatePerformAction'), ('threaded', 'ScriptProxy>Sims3.Gameplay.Actors.Sim'),
            ('taskObjects', 'Sims3.Gameplay.Autonomy.AutonomyManager'), ('oneShot', 'ScriptProxy>Sims3.Gameplay.Utilities.AlarmManager: Simulate'),
            ('script', 'Sims3.SimIFace.Simulator.Tick')}
    check(set(names) == want, f'names and stages {names}')
    check([t['ms'] for t in T] == sorted([t['ms'] for t in T], reverse=True) and T and T[0]['name'].startswith('FunctionTask>'), 'slowest first (the 10M-loop task)')
    e1 = next((t for t in T if t['name'].startswith('FunctionTask')), {})
    check(e1.get('to') == 'Sims3.Gameplay.Interactions.InteractionInstance.DoLoop < Sims3.Gameplay.Interactions.InteractionInstance.RunInteraction'
          and e1.get('from', '').startswith('Sims3.Gameplay.Interactions.InteractionInstance.DoLoop'), f"yield frames: from {e1.get('from')} to {e1.get('to')}")
    e3 = next((t for t in T if t['name'].endswith('AutonomyManager')), {})
    check('from' not in e3 and 'to' not in e3, 'fresh task without yield: no from/to')
    check(all(y.get('taskRuns') == {'script': 1, 'taskObjects': 3, 'oneShot': 1, 'threaded': 1} for y in hl), f"run counts (nested invoke and thread B left out): {[y.get('taskRuns') for y in hl[:3]]}")
    check(all(len(y.get('tasks', [])) == 5 for y in hl) and all(y.get('taskDropped') == 0 for y in hl), 'every line: 5 kept, none dropped (the fast task is under taskMinMs)')
    check(all(sum(t['ms'] for t in y['tasks'] if t['stage'] == 'taskObjects') <= y['taskObjects'] + 0.05 for y in hl), 'task ms within their seam')
    c0 = h.count(); time.sleep(0.5); c1 = h.count()
    check(c1[0] > c0[0] and c1[1] > c0[1] and p.poll() is None, f'dummy alive, both threads looping after unload ({c0} -> {c1})')

    # 3. overhead: TaskObjects with 200,000 one-loop tasks, attribution off vs on (taskObjects ms in the hitch lines)
    h.bench(True); time.sleep(0.3)
    res = {}
    for name, on in (('off', False), ('on', True), ('off2', False), ('on2', True)):
        sc = load(params(tmp / f'bench-{name}', tasks=on)); time.sleep(4); sc.unload(); time.sleep(0.3)
        b = [y['taskObjects'] for y in lines(tmp / f'bench-{name}-hitches.jsonl')[2:]]
        res[name] = sorted(b)[len(b) // 2] if b else float('nan')
        if on: runs = [y.get('taskRuns', {}).get('taskObjects') for y in lines(tmp / f'bench-{name}-hitches.jsonl')[2:]]
    h.bench(False)
    per = (min(res['on'], res['on2']) - min(res['off'], res['off2'])) / 200000 * 1e6
    print(f"bench taskObjects median ms: {res}; per task run {per:.0f} ns")
    check(runs and all(r == 200000 for r in runs), f'bench run count 200000 ({runs[:3]})')
    check(per < 1000, f'per-task cost {per:.0f} ns < 1 us')
    sc = load(params(tmp / 'inv')); time.sleep(0.5)
    inv_on = min(h.bench_invoke(200000) for _ in range(3)); sc.unload(); time.sleep(0.3)
    per_inv = (inv_on - inv_base) / 200000 * 1e6
    print(f'runtime invoke outside the seams: {inv_base:.1f} -> {inv_on:.1f} ms for 200,000 calls, {per_inv:.0f} ns per call')
    check(per_inv < 1000, f'cost on a runtime invoke outside the seams {per_inv:.0f} ns < 1 us')

    # 4. unload while the sim thread is inside a 2 s task run (frida_tools.repl -t 1 in its own host, as profile-world.ts runs it)
    js = tmp / 'prof.js'; js.write_text(src, encoding='utf-8')
    h.slow(1000000000)
    for i in range(3):
        prm = params(tmp / f'race{i}')
        subprocess.run([sys.executable, '-m', 'frida_tools.repl', '-p', str(p.pid), '-l', str(js), '-P', json.dumps(prm), '-q', '-t', '1'],
                       capture_output=True, text=True, timeout=60)
        time.sleep(2.5)
        alive = p.poll() is None
        try: c0 = h.count()[0]; time.sleep(6); moving = h.count()[0] > c0
        except Exception: moving = False
        check(alive and moving, f'race {i}: dummy alive and the sim loop still running after an unload inside a task run (alive {alive}, moving {moving})')
        if not alive: break
    h.slow(10000000)

    # 5. early end by stop file (profile-world.ts stop rule, docs/168 §6): repl -t 60, flag after 3 s, the profiler says it
    # stopped, the harness kills the repl; dummy alive and looping, hitch file stops growing (hooks off, files closed)
    stopf = tmp / 'stop.flag'
    prm = params(tmp / 'early', stopFile=str(stopf).replace('\\', '/'))
    rp = subprocess.Popen([sys.executable, '-m', 'frida_tools.repl', '-p', str(p.pid), '-l', str(js), '-P', json.dumps(prm), '-q', '-t', '60'],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(3); stopf.write_text('')
    t_stop, seen = time.time(), False
    while time.time() - t_stop < 15 and not seen:
        line = rp.stdout.readline()
        seen = '[ts3prof] stopped early' in line
    rp.kill(); rp.wait(10)
    n1 = len(lines(tmp / 'early-hitches.jsonl')); time.sleep(2); n2 = len(lines(tmp / 'early-hitches.jsonl'))
    check(seen, f'stop file: "stopped early" line within {time.time() - t_stop:.1f} s')
    check(n1 > 5 and n1 == n2, f'stop file: hitch lines written ({n1}) and no more after the stop ({n2})')
    try: c0 = h.count()[0]; time.sleep(1); moving = h.count()[0] > c0
    except Exception: moving = False
    check(p.poll() is None and moving, 'stop file: dummy alive and looping after the early end')
    # script log messages arrive late (python side): checked last
    check('task attribution off: taskRun' in log_text(), f"wrong bytes: the log names the refused hook {[l[:160] for l in log_text().splitlines() if 'attribution' in l][:3]}")
finally:
    p.kill()
print(log_text()[-1500:])
print('RESULT', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
