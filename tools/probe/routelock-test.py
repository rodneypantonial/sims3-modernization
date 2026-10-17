# Dummy test of agent.js /routelock (CModule hooks + ring drain; probe-safety rule): 32-bit ping.exe, never the game.
#   python tools/probe/routelock-test.py      (Windows, frida installed; prints RESULT PASS|FAIL)
# A helper script writes 17 stub functions (push ebp; mov ebp,esp; nops; pop ebp; ret) and two threads that call them in
# a loop like a plan: Route_PlanLocked(ecx=world), IC_Route_Plan, Lock_Acquire/Release(ecx=world+0x918) around qReset,
# goalStep unlocked, an acquire/release of another lock, LocomotionUpdate. The agent loads with the game addresses
# replaced by the stubs (test-only; agent.js is unchanged), thread 1 as the sim thread.
import frida, re, subprocess, sys, time, pathlib
HERE = pathlib.Path(__file__).parent
agent = (HERE / 'agent.js').read_text(encoding='utf-8')
ok = True
def check(c, msg):
    global ok
    print(('ok   ' if c else 'FAIL ') + msg); ok = ok and bool(c)

GATES = ['lotTableFind', 'goalLotPlanner', 'roomConnectivity', 'lazyLotBuild', 'lotRebuild', 'roomGraphRefresh',
         'qReset', 'qResetClear', 'searchStamp', 'qNodeClear', 'goalStep', 'connectedRooms']
HELPER = """
const k = Process.getModuleByName('kernel32.dll');
const CT = new NativeFunction(k.getExportByName('CreateThread'), 'pointer', ['pointer', 'uint', 'pointer', 'pointer', 'uint', 'pointer']);
const page = Memory.alloc(Process.pageSize * 2), world = Memory.alloc(0x1000), other = Memory.alloc(16), ctr = Memory.alloc(16);
Memory.protect(page, Process.pageSize * 2, 'rwx');
const stub = i => page.add(i * 16); // 0 Route_PlanLocked, 1 Lock_Acquire, 2 Lock_Release, 3 LocomotionUpdate, 4 IC_Route_Plan, 5.. gates
for (let i = 0; i < 17; i++) stub(i).writeByteArray([0x55, 0x8b, 0xec, 0x90, 0x90, 0x90, 0x90, 0x90, 0x5d, 0xc3]);
{ const at = stub(5 + %ROOMS%), w = new X86Writer(at.add(8), { pc: at.add(8) }); // connectedRooms calls searchStamp: a nested gate entry
  w.putCallAddress(stub(5 + %STAMP%)); w.putBytes([0x5d, 0xc3]); w.flush(); }
ctr.add(8).writeU32(%DELAY%); // busy-loop count per iteration
const lock = world.add(0x918), GQ = 5 + %QRESET%, GS = 5 + %GOALSTEP%;
function loop(at, counter) {
  const w = new X86Writer(at, { pc: at });
  w.putLabel('top');
  w.putMovRegAddress('eax', counter); w.putIncRegPtr('dword', 'eax');
  w.putMovRegAddress('ecx', world); w.putCallAddress(stub(0));           // learns the lock
  w.putCallAddress(stub(4));                                              // IC_Route_Plan
  w.putMovRegAddress('ecx', lock); w.putCallAddress(stub(1));
  w.putMovRegAddress('ecx', lock); w.putCallAddress(stub(1));            // nested acquire / release
  w.putMovRegAddress('ecx', lock); w.putCallAddress(stub(2));
  w.putCallAddress(stub(GQ));                                             // gate under the lock
  w.putMovRegAddress('ecx', lock); w.putCallAddress(stub(2));
  w.putCallAddress(stub(GS));                                             // gate without it
  w.putCallAddress(stub(5 + %ROOMS%));                                    // gate without it, with a nested gate inside
  w.putMovRegAddress('ecx', other); w.putCallAddress(stub(1));            // engine-wide lock, other ecx
  w.putMovRegAddress('ecx', other); w.putCallAddress(stub(2));
  w.putCallAddress(stub(3));
  w.putMovRegAddress('edx', ctr.add(8)); w.putMovRegRegPtr('edx', 'edx');
  w.putLabel('d'); w.putDecReg('edx'); w.putJccNearLabel('jne', 'd', 'no-hint');
  w.putJmpNearLabel('top');
  w.flush();
}
loop(page.add(Process.pageSize), ctr); loop(page.add(Process.pageSize + 0x400), ctr.add(4));
rpc.exports = {
  addrs() { return { stubs: Array.from({ length: 17 }, (_, i) => stub(i).toString()), world: world.toString() }; },
  spawn(i) { const id = Memory.alloc(4); CT(NULL, 0, page.add(Process.pageSize + i * 0x400), NULL, 0, id); return id.readU32(); },
  count() { return [ctr.readU32(), ctr.add(4).readU32()]; },
  delay(n) { ctr.add(8).writeU32(n); return n; },
};"""

def agent_src(stubs, sim, ring=None, bad=False):
    s = agent
    s = re.sub(r'const ROUTE_PLAN_LOCKED = ptr\(0x82c6e0\).*?;\n',
               f"const ROUTE_PLAN_LOCKED = ptr('{stubs[0]}'), LOCK_ACQUIRE = ptr('{stubs[1]}'), LOCK_RELEASE = ptr('{stubs[2]}'), LOCOMOTION_UPDATE = ptr('{stubs[3]}');\n", s)
    body = ', '.join(f"{g}: [{stubs[5 + i]}, '{'55 8b ec 90 90 90 90 91' if bad and g == 'qReset' else '55 8b ec 90 90 90 90 90'}']" for i, g in enumerate(GATES))
    s = re.sub(r'const RL_GATES = \{.*?\};\n', 'const RL_GATES = { ' + body + ' };\n', s, flags=re.S)
    s = re.sub(r'const RL_ROUTE_PLAN = \[.*?\];', f"const RL_ROUTE_PLAN = [{stubs[4]}, '55 8b ec 90 90 90 90 90'];", s)
    s = s.replace('let interpTid = null;', f'let interpTid = {sim};')
    if ring: s = s.replace('const RL_RING_LOG2 = 18, RL_DRAIN_MS = 25;', f'const RL_RING_LOG2 = {ring[0]}, RL_DRAIN_MS = {ring[1]};')
    assert s.count(stubs[0]) == 1 and s.count(stubs[16]) == 1 and f'interpTid = {sim}' in s and (not ring or f'RL_RING_LOG2 = {ring[0]}' in s)
    return s

def rate(h, secs=1.0):
    a = h.count(); t = time.perf_counter(); time.sleep(secs); b = h.count(); d = time.perf_counter() - t
    return [(b[i] - a[i]) / d for i in range(2)]

p = subprocess.Popen([r'C:\Windows\SysWOW64\PING.EXE', '-n', '120', '127.0.0.1'], stdout=subprocess.DEVNULL)
try:
    time.sleep(1)
    s = frida.attach(p.pid)
    helper = s.create_script(HELPER.replace('%DELAY%', '300000').replace('%ROOMS%', str(GATES.index('connectedRooms'))).replace('%STAMP%', str(GATES.index('searchStamp'))).replace('%QRESET%', str(GATES.index('qReset'))).replace('%GOALSTEP%', str(GATES.index('goalStep'))))
    helper.load(); h = helper.exports_sync
    a = h.addrs(); stubs = [int(x, 16) for x in a['stubs']]; world = int(a['world'], 16)
    t1, t2 = h.spawn(0), h.spawn(1)
    time.sleep(0.5)
    base = rate(h)
    print(f'no hooks: {base[0]:.0f} + {base[1]:.0f} iterations/s')

    # byte check: one wrong prologue, nothing hooked
    sc = s.create_script(agent_src([hex(x) for x in stubs], t1, bad=True)); sc.on('message', lambda m, d: None); sc.load()
    try:
        sc.exports_sync.route_lock(True, False); check(False, 'byte mismatch refused')
    except Exception as ex:
        check('qReset' in str(ex) and 'nothing hooked' in str(ex), 'byte mismatch refused, nothing hooked')
    check(sc.exports_sync.route_lock(False, False)['hooked'] is False, 'not hooked after refusal')
    sc.unload()

    sc = s.create_script(agent_src([hex(x) for x in stubs], t1)); sc.on('message', lambda m, d: print(m) if m['type'] == 'error' else None); sc.load()
    rpc = sc.exports_sync
    st = rpc.route_lock(True, False)
    check(st['hooked'], 'hooked')
    hooked = rate(h, 3.0)
    r = rpc.route_lock(False, False)
    print(f'hooked: {hooked[0]:.0f} + {hooked[1]:.0f} iterations/s; ring {r["ring"]}; acquireHooksPerSec {r["acquireHooksPerSec"]}, gateHooksPerSec {r["gateHooksPerSec"]}')
    calls = 14  # hooked calls per iteration: RPL, plan, acq x3, rel x3, qReset, goalStep, connectedRooms, searchStamp, loco
    check(r['lock'] == hex(world + 0x918), f"lock learned = world+0x918 ({r['lock']})")
    check(r['simThread'] == t1, 'sim thread = thread 1')
    th = r['threads']
    check(str(t1) in th and str(t2) in th and th[str(t1)]['sim'] and not th[str(t2)]['sim'], f'per-thread rows for both threads ({list(th)})')
    tot = sum(t['acquires'] for t in th.values())
    check(abs(r['acquireHooksPerSec'] - 1.5 * tot / r['since']) / r['acquireHooksPerSec'] < 0.25 if r['since'] else False,
          f'acquire hooks ~ 1.5x matching acquires (other lock filtered in C, nested counted): {r["acquireHooksPerSec"]}/s vs {tot} matching in {r["since"]} s')
    it = r['iterations']
    check(abs(tot - 2 * it) / (2 * it) < 0.05,
          f'2 matching acquires per iteration (outer + nested): {tot} for {it} iterations')
    check(all(t['holdMs'] >= 0 and t['acquires'] > 0 for t in th.values()), 'hold/wait recorded per thread')
    check(r['plans']['n'] > 0 and r['overlap']['n'] > 0, f"Route_PlanLocked plans {r['plans']['n']}, overlap gaps {r['overlap']['n']}")
    g = r['g11']
    check(g and g['plans'] > 0, f"g11 sim plans {g and g['plans']}")
    check(g['sites']['qReset']['entries'] > 0 and g['sites']['qReset']['unlocked'] == 0, f"qReset under the lock: {g['sites']['qReset']}")
    check(g['sites']['goalStep']['unlockedOuter'] > 0 and g['sites']['goalStep']['unlockedSim'] > 0, f"goalStep unlocked: {g['sites']['goalStep']}")
    ss, cr = g['sites']['searchStamp'], g['sites']['connectedRooms']
    check(ss['entries'] > 0 and ss['unlocked'] == ss['entries'] and ss['unlockedOuter'] == 0 and 0 < ss['unlockedSim'] < ss['entries'],
          f'nested gate folded in C: searchStamp {ss}')
    check(abs(cr['unlockedOuter'] - ss['entries']) <= 2 + ss['entries'] // 100, f"connectedRooms outer {cr['unlockedOuter']} = nested searchStamp {ss['entries']}")
    check(r['ring']['folded'] >= g['sites']['qReset']['entries'] + ss['entries'] - 4, f"ring folded {r['ring']['folded']}")
    check(all(t['maxHoldMs'] < 50 for t in th.values()), f"holds short (nested depth handled): {[t['maxHoldMs'] for t in th.values()]}")
    check(r['iterations'] > 0, f"LocomotionUpdate counted {r['iterations']}")
    check(r['ring']['lost'] == 0 and r['ring']['events'] > 0, f"ring: {r['ring']['events']} events, lost {r['ring']['lost']}")
    rs = rpc.route_lock(False, True)
    check(rs['hooked'] is False and rs['ring']['lost'] == 0, 'stopped; final drain without loss')
    g1 = rpc.route_lock(False, False)['ring']['events']; c1 = h.count(); time.sleep(0.5)
    check(rpc.route_lock(False, False)['ring']['events'] == g1 and h.count()[0] > c1[0], 'no events after stop; threads still run')
    after = rate(h)
    print(f'after stop: {after[0]:.0f} + {after[1]:.0f} iterations/s')
    # restart in the same script reuses the CModule and ring
    check(rpc.route_lock(True, False)['hooked'], 'restart hooked'); time.sleep(0.5)
    r2 = rpc.route_lock(False, True)
    check(r2['ring']['events'] > 0 and r2['ring']['lost'] == 0 and r2['g11']['plans'] > 0, f"restart measured afresh: {r2['ring']['events']} events")
    # throughput: no delay, report the drain's capacity (informational)
    # (attach and detach only while the threads sit in the delay loop: Frida patches the stub's first 5 bytes, and a thread
    # executing them at that moment crashes; seen with empty JS callbacks too, so a test artifact of 10-byte stubs at 1M calls/s)
    h.delay(400); fast = rate(h); h.delay(300000); time.sleep(0.2)
    rpc.route_lock(True, False); h.delay(400); hot = rate(h, 2.0); h.delay(300000); time.sleep(0.2); rt = rpc.route_lock(False, True)
    print(f"hook cost at full speed: {', '.join(f'{(1 / hot[i] - 1 / fast[i]) * 1e9 / calls:.0f}' for i in range(2))} ns per hooked call "
          f"({fast[0]:.0f} -> {hot[0]:.0f} iterations/s per thread, 2 threads)")
    print(f"stress: {rt['ring']['events'] / rt['since']:.0f} records/s drained, lost {rt['ring']['lost']}, drain {rt['ring']['drainMsPerSec']} ms/s, acquire hooks {rt['acquireHooksPerSec']}/s" if rt['since'] else f"stress: {rt['ring']}")
    sc.unload(); time.sleep(0.3)

    # overflow: 2^8 records and a drain every 100 s; loss must show in the report
    sc = s.create_script(agent_src([hex(x) for x in stubs], t1, ring=(8, 100000))); sc.on('message', lambda m, d: print(m) if m['type'] == 'error' else None); sc.load()
    rpc = sc.exports_sync
    rpc.route_lock(True, False); time.sleep(1)
    r3 = rpc.route_lock(False, True)
    check(r3['ring']['lost'] > 0, f"overflow counted: lost {r3['ring']['lost']}, kept {r3['ring']['events']}")
    sc.unload(); time.sleep(0.5)
    # the helper owns the stub and loop memory, so it stays loaded: the threads must keep calling the unhooked stubs
    c1 = h.count(); time.sleep(0.5); c2 = h.count()
    check(p.poll() is None and c2[0] > c1[0] and c2[1] > c1[1], 'dummy alive, threads running after the agent unloaded')
finally:
    p.kill()
print('RESULT', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
