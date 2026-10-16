# Dummy test of agent.js threadStacks (/threadstack, main-thread sampler; probe-safety rule): 32-bit dummies only, never the game.
#   python tools/probe/threadstack-test.py      (Windows, frida installed; prints RESULT PASS|FAIL)
# Phase 1: 32-bit PowerShell with an off-screen WinForms window (UI thread busy on a timer) + a spinning worker: window fallback, all mode.
# Phase 2: 32-bit ping.exe + 2 extra threads, fake stored main/sim ids: main from the stored id, sim thread never sampled.
import frida, json, subprocess, sys, time, pathlib
HERE = pathlib.Path(__file__).parent
agent = (HERE / 'agent.js').read_text(encoding='utf-8')
ok = True
def check(c, msg):
    global ok
    print(('ok   ' if c else 'FAIL ') + msg); ok = ok and bool(c)
def load(pid):
    s = frida.attach(pid)
    sc = s.create_script(agent); sc.on('message', lambda m, d: None); sc.load()
    return s, sc, sc.exports_sync

p = subprocess.Popen([r'C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(HERE / 'threadstack-dummy.ps1')])
try:
    time.sleep(6)
    s, sc, rpc = load(p.pid)
    r = rpc.thread_stacks('main', 24)
    m = r['threads'][0] if r['threads'] else {}
    check(r['main'] == r['window'] and m.get('thread') == r['window'] and m.get('role') == 'main' and len(m.get('stack') or []) >= 2,
          f"main = window owner thread {r['window']}, {len(m.get('stack') or [])} frames, top {(m.get('stack') or ['-'])[:2]}")
    a = rpc.thread_stacks('all', 24)
    check(len(a['threads']) >= 3 and r['window'] in [t['thread'] for t in a['threads']], f"all: {len(a['threads'])} threads incl. main")
    check(sum(1 for t in a['threads'] if t['stack']) >= len(a['threads']) - 1, 'all: stacks read')
    t0 = time.perf_counter()
    for _ in range(50): rpc.thread_stacks('main', 24)
    print(f'main sample: {(time.perf_counter() - t0) * 1000 / 50:.2f} ms per call (rpc round trip incl.)')
    t0 = time.perf_counter()
    for _ in range(10): rpc.thread_stacks('all', 24)
    print(f'all sample: {(time.perf_counter() - t0) * 100:.2f} ms per call ({len(a["threads"])} threads)')
    sc.unload(); s.detach(); time.sleep(1)
    check(p.poll() is None, 'dummy 1 alive after detach')
finally:
    p.kill()

p = subprocess.Popen([r'C:\Windows\SysWOW64\PING.EXE', '-n', '60', '127.0.0.1'], stdout=subprocess.DEVNULL)
try:
    time.sleep(1)
    # 0x11D8000 is inside a mapped reservation in every WOW64 process here, so the stored-id slots move to a buffer:
    # the agent source is loaded with its two addresses replaced (test-only; the agent file is unchanged).
    s = frida.attach(p.pid)
    helper = s.create_script("""
      const k = Process.getModuleByName('kernel32.dll');
      const CT = new NativeFunction(k.getExportByName('CreateThread'), 'pointer', ['pointer', 'uint', 'pointer', 'pointer', 'uint', 'pointer']);
      const buf = Memory.alloc(8);
      rpc.exports = {
        buf() { return buf.toString(); },
        spawn() { const id = Memory.alloc(4); CT(NULL, 0, k.getExportByName('Sleep'), ptr('0xffffffff'), 0, id); return id.readU32(); }, // thread sleeps forever
        put(main, sim) { buf.writeU32(main); buf.add(4).writeU32(sim); return true; },
      };""")
    helper.load()
    h = helper.exports_sync
    b0 = int(h.buf(), 16)
    src = agent.replace("ptr('0x11d8a88'), SIM_TID_AT = ptr('0x11d8a8c')", f"ptr('{hex(b0)}'), SIM_TID_AT = ptr('{hex(b0 + 4)}')")
    check(src != agent, 'stored-id addresses redirected to a test buffer')
    sc = s.create_script(src); sc.on('message', lambda m, d: None); sc.load()
    rpc = sc.exports_sync
    t1, t2 = h.spawn(), h.spawn()
    time.sleep(0.3)
    before = [t['thread'] for t in rpc.thread_stacks('all', 24)['threads']]
    check(t1 in before and t2 in before, f'all sees spawned threads {t1}, {t2} ({len(before)} threads)')
    check(h.put(t1, t2), 'fake main/sim ids written')
    d = rpc.thread_stacks('main', 24)
    check(d['main'] == t1 and d['threads'][0]['thread'] == t1 and d['threads'][0]['stack'], f'main from the stored id = {t1}, top {(d["threads"][0]["stack"] or [])[:2]}')
    b = rpc.thread_stacks('all', 24)
    check(t2 not in [t['thread'] for t in b['threads']] and t2 in b['sim'], f'all: stored sim thread {t2} excluded')
    c = rpc.thread_stacks(str(t2), 24)
    check(c['threads'][0]['stack'] is None and 'sim thread' in c['threads'][0]['error'], 'explicit sim tid refused (not suspended)')
    h.put(t1, t1)
    e = rpc.thread_stacks('main', 24)
    check(e['threads'][0]['stack'] is None, 'single-threaded mode (main == sim): not sampled')
    try:
        rpc.thread_stacks('bogus', 24); check(False, 'bad which rejected')
    except Exception as ex:
        check('which must be' in str(ex), 'bad which rejected')
    helper.unload(); sc.unload(); s.detach(); time.sleep(1)
    check(p.poll() is None, 'dummy 2 alive after detach')
finally:
    p.kill()
print('RESULT', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
