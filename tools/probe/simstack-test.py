# Dummy test of agent.js simStack (/simstack, native sim-thread sampler; probe-safety rule): 32-bit ping.exe only, never the game.
#   python tools/probe/simstack-test.py [agent.js]     (Windows, frida installed; prints RESULT PASS|FAIL; default: this folder's agent.js)
# Covers docs/data/results/2026-10-16-sampler-hang.md: the sim thread comes from the stored id [0x11D8A8C] without enumerating
# (and suspending) every thread, a failed search is not repeated on every call, and a stack walk stays fast when the stack
# holds many non-code values (the old isCode refreshed the module map on every miss). Pass an older agent.js to compare timings.
import frida, subprocess, sys, time, pathlib
HERE = pathlib.Path(__file__).parent
agent = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else HERE / 'agent.js').read_text(encoding='utf-8')
ok = True
def check(c, msg):
    global ok
    print(('ok   ' if c else 'FAIL ') + msg); ok = ok and bool(c)
def per_call(f, n):
    t0 = time.perf_counter()
    for _ in range(n): r = f()
    return (time.perf_counter() - t0) * 1000 / n, r

proc = subprocess.Popen([r'C:\Windows\SysWOW64\PING.EXE', '-n', '60', '127.0.0.1'], stdout=subprocess.DEVNULL)
try:
    time.sleep(1)
    s = frida.attach(proc.pid)
    # Stored-id slots moved to a test buffer (as in threadstack-test.py); spawned threads sleep forever with no Simulate frame.
    helper = s.create_script("""
      const k = Process.getModuleByName('kernel32.dll');
      const CT = new NativeFunction(k.getExportByName('CreateThread'), 'pointer', ['pointer', 'uint', 'pointer', 'pointer', 'uint', 'pointer']);
      const buf = Memory.alloc(8);
      rpc.exports = {
        buf() { return buf.toString(); },
        spawn() { const id = Memory.alloc(4); CT(NULL, 0, k.getExportByName('Sleep'), ptr('0xffffffff'), 0, id); return id.readU32(); },
        put(main, sim) { buf.writeU32(main); buf.add(4).writeU32(sim); return true; },
      };""")
    helper.load()
    h = helper.exports_sync
    b0 = int(h.buf(), 16)
    src = agent.replace("ptr('0x11d8a88'), SIM_TID_AT = ptr('0x11d8a8c')", f"ptr('{hex(b0)}'), SIM_TID_AT = ptr('{hex(b0 + 4)}')")
    check(src != agent, 'stored-id addresses redirected to a test buffer')
    threads = [h.spawn() for _ in range(8)]  # more threads: a full search costs more, as with the game's ~144
    time.sleep(0.3)

    # 1. no stored id, no Simulate on any stack: the search fails; it must not run again on every call
    sc = s.create_script(src); sc.on('message', lambda m, d: None); sc.load()
    rpc = sc.exports_sync
    ms, r = per_call(lambda: rpc.sim_stack(24), 20)
    print(f'not found: {ms:.1f} ms per call')
    check(r['thread'] is None and r['stack'] == [], 'not found: empty reply')
    check(ms < 30, f'not found: < 30 ms per call (failed search not repeated each call)')
    sc.unload()

    # 2. stored sim id: used directly, stack read, fast
    h.put(threads[0], threads[1])
    sc = s.create_script(src); sc.on('message', lambda m, d: None); sc.load()
    rpc = sc.exports_sync
    ms, r = per_call(lambda: rpc.sim_stack(24), 50)
    print(f'stored id: {ms:.1f} ms per call, {len(r["stack"])} frames, top {r["stack"][:2]}')
    check(r['thread'] == threads[1], f'sim thread from the stored id = {threads[1]}')
    check(len(r['stack']) >= 1, 'stack read')
    check(ms < 30, 'stored id: < 30 ms per call')
    t = rpc.thread_stacks(str(threads[1]), 24)['threads'][0]
    check(t['stack'] is None and 'sim thread' in (t['error'] or ''), '/threadstack still refuses the sim thread')
    sc.unload()

    helper.unload(); s.detach(); time.sleep(1)
    check(proc.poll() is None, 'dummy alive after detach')
finally:
    proc.kill()
print('RESULT', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
