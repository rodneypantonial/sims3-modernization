# Dummy test of tools/frida/watch-stalk.js (probe-safety rule): 32-bit ping.exe, never the game.
#   python tools/probe/watch-stalk-test.py      (Windows, frida installed; prints RESULT PASS|FAIL; ~2.5 min)
# A helper script builds, inside the dummy: a fake .data block, two fake routes (vtable word, counter at +0x1F4), stubs with
# the game's shapes (a replan stub that adds 1 to [esi+0x1F4] right before the hooked site, one whose esi is a bad pointer,
# a reader of [esi+0x1F4], a plan stub that writes the fake .data by absolute, register, x87 and rep stosd stores, calls an
# excluded writer and a "0x66AB70" block), a sim thread (replans + plans) and a second thread (reader + plans).
# The script under test is the unchanged file; only its addresses come in through the test-only init parameter `addrs`.
# Checks: refusal on wrong bytes and on a Frida-style jump, bad route pointer counted (no crash), watchpoint hits on two
# threads, drop on vtable change, no hit after unset, Stalker follow/unfollow on the sim thread only with the expected
# write set, coverage and counter, clean detach (both threads keep running), fail-fast after 60 s without a replan.
# docs/113 §8: a route released through the dtor stub (vtable left intact) ends its watch as 'released'; worker mode follows
# only the second thread (the "route worker"), takes looser default caps, accepts an e9 detour into a named module and still
# refuses one into anonymous memory.
# docs/113 §8.6 (2d): the bp-watch-stalk-worker shape: a third thread enters a hook in another module through an e9 and waits
# there, is hit in the watch phase, and is followed after the switch with no workerTid given.
import frida, json, pathlib, subprocess, sys, tempfile, time
HERE = pathlib.Path(__file__).parent
ROOT = HERE.parent.parent
src = (ROOT / 'tools' / 'frida' / 'watch-stalk.js').read_text(encoding='utf-8')
ok = True
def check(c, msg):
    global ok
    print(('ok   ' if c else 'FAIL ') + msg); ok = ok and bool(c)

HELPER = r"""
const k = Process.getModuleByName('kernel32.dll');
const CT = new NativeFunction(k.getExportByName('CreateThread'), 'pointer', ['pointer', 'uint', 'pointer', 'pointer', 'uint', 'pointer']);
const VT = 0x10047cc;
const D = Memory.alloc(0x100), cells = Memory.alloc(64), r1 = Memory.alloc(0x200), r2 = Memory.alloc(0x200), r3 = Memory.alloc(0x200);
r1.writeU32(VT); r2.writeU32(VT); r3.writeU32(VT);
const routeCell = cells, badCell = cells.add(4), simTid = cells.add(8), ctrA = cells.add(16), ctrB = cells.add(20), delay = cells.add(24), relCell = cells.add(28);
routeCell.writePointer(r1); badCell.writeU32(0x10); delay.writeU32(3000000);
const le = v => [v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, (v >>> 24) & 0xff];
const u = p => p.toUInt32();
const page = Memory.alloc(Process.pageSize * 2); Memory.protect(page, Process.pageSize * 2, 'rwx');
const at = o => page.add(o);
function emit(o, f) { const w = new X86Writer(at(o), { pc: at(o) }); f(w); w.flush(); }
const g = o => u(D) + o;
emit(0x100, w => { w.putBytes([0xa3, ...le(g(0x30))]); w.putRet(); });                                  // excluded writer
emit(0x200, w => { w.putBytes([0x90, 0xb9, 0x22, 0, 0, 0, 0x89, 0x0d, ...le(g(0x40))]); w.putRet(); }); // "0x66AB70" block, c37 at +1
emit(0x000, w => {                                                                                      // plan stub
  w.putBytes([0x55, 0x8b, 0xec, 0xb8, 0x11, 0x11, 0x11, 0x11, 0xa3, ...le(g(0x10)), 0xd9, 0xe8, 0xd9, 0x1d, ...le(g(0x20)),
    0xb9, ...le(u(D)), 0x89, 0x41, 0x50, 0x57, 0xbf, ...le(g(0x60)), 0xb9, 4, 0, 0, 0, 0xf3, 0xab, 0x5f]);
  w.putCallAddress(at(0x100)); w.putCallAddress(at(0x200)); w.putBytes([0x5d]); w.putRet();
});
const NOP8 = [0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90];
emit(0x300, w => w.putBytes([0x56, 0x8b, 0x35, ...le(u(routeCell)), 0x83, 0x86, 0xf4, 0x01, 0, 0, 0x01, ...NOP8, 0x5e, 0xc3])); // site +0x30e
emit(0x400, w => w.putBytes([0x56, 0x8b, 0x35, ...le(u(badCell)), ...NOP8, 0x5e, 0xc3]));                                      // site +0x407
emit(0x500, w => w.putBytes([0x56, 0x8b, 0x35, ...le(u(routeCell)), 0x8b, 0x86, 0xf4, 0x01, 0, 0, 0x5e, 0xc3]));              // next +0x50d
emit(0x600, w => w.putBytes([...NOP8, 0xc3]));                                                                                  // never called
emit(0x700, w => w.putBytes([0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0xc3]));                                                       // a Frida-style jump
const beep = k.getExportByName('Beep');                                                                                         // nobody calls it
emit(0x740, w => w.putBytes([0xe9, ...le(u(beep) - (u(page) + 0x745)), 0x90, 0x90, 0x90]));                                    // a ts3native-style detour
emit(0xa00, w => w.putBytes([0x83, 0xec, 0x08, 0x53, 0x8b, 0xd9, 0x8b, 0x83, 0xd8, 0x01, 0, 0, 0x5b, 0x83, 0xc4, 0x08, 0xc3])); // Route dtor shape
emit(0xa80, w => { // if [relCell]: ecx = it, [relCell] = 0, call the dtor stub
  w.putBytes([0x8b, 0x0d, ...le(u(relCell)), 0x85, 0xc9, 0x74, 0x0f, 0xc7, 0x05, ...le(u(relCell)), 0, 0, 0, 0]);
  w.putCallAddress(at(0xa00)); w.putRet();
});
function loop(o, ctr, calls) {
  emit(o, w => {
    w.putLabel('top');
    w.putMovRegAddress('eax', ctr); w.putIncRegPtr('dword', 'eax');
    for (const c of calls) w.putCallAddress(at(c));
    w.putMovRegAddress('edx', delay); w.putMovRegRegPtr('edx', 'edx');
    w.putLabel('d'); w.putDecReg('edx'); w.putJccNearLabel('jne', 'd', 'no-hint');
    w.putJmpNearLabel('top');
  });
}
loop(0x800, ctrA, [0x000, 0x300, 0x400]);
loop(0x900, ctrB, [0x500, 0x000, 0xa80]);
// docs/113 §8.6 worker shape: a "PlanLockedHook" inside another module (spare bytes of PING.EXE's header page, made rwx) that
// calls the plan stub and then a real wait (SwitchToThread), reached through an e9 at +0x780; a third thread loops reader + jump
const mainMod = Process.mainModule, modHook = mainMod.base.add(0xe00), ctrC = cells.add(32);
Memory.protect(mainMod.base, Process.pageSize, 'rwx');
{ const w = new X86Writer(modHook, { pc: modHook }); w.putBytes([0x55, 0x8b, 0xec, 0x56, 0x57]); w.putCallAddress(at(0x000));
  w.putCallAddress(k.getExportByName('SwitchToThread')); w.putBytes([0x5f, 0x5e, 0x5d]); w.putRet(); w.flush(); }
emit(0x780, w => w.putBytes([0xe9, ...le(u(modHook) - (u(page) + 0x785)), 0x90, 0x90, 0x90]));
loop(0xb00, ctrC, [0x500, 0x780]);
rpc.exports = {
  addrs() { return { page: u(page), D: u(D), simTid: u(simTid), r1: u(r1), r2: u(r2), r3: u(r3), beepMod: Process.findModuleByAddress(beep).name, mainMod: mainMod.name }; },
  spawnWorker() { const id = Memory.alloc(4); CT(NULL, 0, at(0xb00), NULL, 0, id); return id.readU32(); },
  workerCount() { return ctrC.readU32(); },
  spawn() {
    const id = Memory.alloc(4);
    CT(NULL, 0, at(0x800), NULL, 0, id); simTid.writeU32(id.readU32());
    const id2 = Memory.alloc(4); CT(NULL, 0, at(0x900), NULL, 0, id2);
    return [id.readU32(), id2.readU32()];
  },
  swap() { routeCell.writePointer(r2); r1.writeU32(0xdead); },
  release() { routeCell.writePointer(r3); relCell.writePointer(r2); }, // r2 keeps its vtable: only the dtor hook can end it
  counts() { return [ctrA.readU32(), ctrB.readU32()]; },
};"""

p = subprocess.Popen([r'C:\Windows\SysWOW64\PING.EXE', '-n', '400', '127.0.0.1'], stdout=subprocess.DEVNULL)
tmp = pathlib.Path(tempfile.mkdtemp(prefix='watchstalk-test-'))
try:
    time.sleep(1)
    s = frida.attach(p.pid)
    helper = s.create_script(HELPER); helper.load(); h = helper.exports_sync
    a = h.addrs(); P = a['page']; D = a['D']
    sim, other = h.spawn(); time.sleep(0.5)
    NOP8 = '90 90 90 90 90 90 90 90'
    def params(out, minutes, **kw):
        prm = {'out': str(out).replace('\\', '/'), 'minutes': minutes, 'watchS': 10, 'everyS': 3, 'windowS': 2, 'perWindow': 2,
               'maxPlans': 6, 'maxHits': 100000,
               'addrs': {'simTid': a['simTid'], 'routeVt': 0x10047cc, 'data': [D, D + 0x100], 'cov': [P + 0x200, P + 0x220], 'c37': P + 0x201,
                         'exclude': [P + 0x100],
                         'hooks': {'replan': [P + 0x30e, NOP8], 'replanPos': [P + 0x407, NOP8], 'plan': [P, '55 8b ec b8 11 11 11 11'],
                                   'routeDtor': [P + 0xa00, '83 ec 08 53 8b d9 8b 83']}}}
        prm.update(kw); return prm
    logs = []
    def load(prm):
        sc = s.create_script(src)
        sc.on('message', lambda m, d: logs.append(m))
        sc.load(); sc.exports_sync.init('late', prm)
        return sc
    def wait_json(f, secs):
        deadline = time.time() + secs
        while time.time() < deadline:  # the file appears before its content is flushed: wait for valid JSON
            time.sleep(0.5)
            try: return json.loads(f.read_text(encoding='utf-8'))
            except (OSError, ValueError): pass
        return None

    # 1. wrong bytes on the watch sites and a jump on the plan site: nothing hooked, the file says why
    prm = params(tmp / 'bad', 0.5)
    for n in ('replan', 'replanPos'): prm['addrs']['hooks'][n][1] = '90 90 90 90 90 90 90 91'
    prm['addrs']['hooks']['plan'] = [P + 0x700, '81 ec d8 00 00 00 53 56']
    sc = load(prm); bad = wait_json(tmp / 'bad-watchstalk.json', 5)
    check(bad and bad['reason'] == 'fail: no usable hook' and set(bad['refused']) == {'replan', 'replanPos', 'plan'} and bad['hooks'] == 0,
          f"wrong bytes refused: {bad and bad['reason']}, {bad and list(bad['refused'])}")
    check(bad and 'another Frida session' in bad['refused']['plan'], 'a jump on the plan prologue is named as another Frida hook')
    sc.unload()

    # 2. the real run, 0.5 min: watch 10 s (swap routes at ~4 s, release r2 at ~7 s), then stalk windows of 2 s every 3 s, 6 plans max
    c0 = h.counts()
    sc = load(params(tmp / 'run', 0.5))
    time.sleep(4); h.swap(); time.sleep(3); h.release()
    r = wait_json(tmp / 'run-watchstalk.json', 45)
    check(r is not None, 'result file written before the deadline')
    w, st = r['watch'], r['stalk']
    check(r['reason'] == 'complete' and not r['refused'], f"complete, nothing refused ({r['reason']}, {r['refused']})")
    hits = {x['pc']: x for x in w['hits']}
    wr, rd = hits.get(hex(P + 0x30e)), hits.get(hex(P + 0x50d))
    check(wr and wr['vtOk'] and wr['sim'] == wr['n'] > 5, f"writer hits on the sim thread at the site after the add ({wr and wr['n']})")
    check(rd and str(other) in rd['tids'] and rd['sim'] == 0 and rd['n'] > 5, f"reader hits on the second thread ({rd and rd['n']})")
    check(w['routes'] == 3 and w['dropped']['freed'] >= 1 and w['dropped']['end'] >= 1, f"three routes, one dropped on vtable change, one live at the end ({w['routes']}, {w['dropped']})")
    rel = [e for e in w['ended'] if e['route'] == hex(a['r2'])]
    check(w['dropped']['released'] == 1 and w['dtors'] >= 1 and rel and rel[0]['end'] == 'released' and rel[0]['released'] is not None,
          f"route released through the dtor ends its watch although its vtable is intact ({w['dtors']} dtors, {rel})")
    check(w['badRoute'] > 0, f"bad route pointer counted, no crash ({w['badRoute']})")
    check(w['stray'] < 50 and w['noDr'] == 0 and w['armErrors'] == 0 and w['unsetErrors'] == 0, f"few stray traps after unset ({w['stray']}), debug registers in the saved context, no arm/unset errors")
    check(st['plans'] == 6 and st['stopped'] == 'maxPlans 6' and st['forced'] == 0, f"6 plans followed, stopped at the cap ({st['plans']}, {st['stopped']}, forced {st['forced']})")
    check(st['otherThread'] > 0 and st['simCalls'] > st['plans'], f"other-thread plans not followed ({st['otherThread']}), sim calls {st['simCalls']}")
    got = {int(x['addr'], 16) - D for x in st['written']}
    check(got == {0x10, 0x20, 0x40, 0x50, 0x60}, f"written set = abs, x87, block, register, rep stosd; excluded writer absent ({sorted(hex(x) for x in got)})")
    check(all(x['plans'] == 6 for x in st['written']), 'every address written in every followed plan')
    check(st['c37plans'] == 6 and st['c37'] == 6 and hex(P + 0x200) in st['cov'], f"0x66AC37 counter and block coverage ({st['c37']}, {st['cov']})")
    check(r['errors'] == [], f"no probe errors ({r['errors'][:2]})")
    c1 = h.counts(); time.sleep(1.0); c2 = h.counts()
    check(c2[0] > c1[0] > c0[0] and c2[1] > c1[1] > c0[1] and p.poll() is None, f'dummy threads alive and looping after the script finished ({c0} -> {c1} -> {c2})')
    sc.unload(); time.sleep(0.3)
    c3 = h.counts(); time.sleep(0.5); c4 = h.counts()
    check(c4[0] > c3[0] and c4[1] > c3[1] and p.poll() is None, 'still alive after unload (no stray debug register, no followed thread)')

    # 2b. worker mode, stalk only: the second thread is the worker; caps default to the worker caps unless given
    prm = params(tmp / 'wk', 0.3, mode='stalk', worker=True); del prm['maxPlans']
    sc = load(prm); rw = wait_json(tmp / 'wk-watchstalk.json', 30)
    sw = rw and rw['stalk']
    check(rw and rw['reason'] == 'complete' and not rw['refused'] and rw['params']['maxPlanMs'] == 5000 and rw['params']['perWindow'] == 2,
          f"worker run complete, worker caps applied, explicit parameters win ({rw and rw['reason']}, {rw and rw['params']['maxPlanMs']}, {rw and rw['params']['perWindow']})")
    check(sw and sw['worker'] and sw['workerTid'] == other and set(sw['tids']) == {str(other)} and sw['simCalls'] > 0 and sw['workerCalls'] >= sw['plans'] > 6,
          f"worker tid = the second thread, sim plans not followed ({sw and sw['workerTid']} vs {other}, tids {sw and sw['tids']}, plans {sw and sw['plans']}, sim {sw and sw['simCalls']})")
    got = {int(x['addr'], 16) - D for x in sw['written']} if sw else set()
    check(got == {0x10, 0x20, 0x40, 0x50, 0x60} and sw['c37plans'] == sw['plans'] and sw['forced'] == 0 and rw['errors'] == [],
          f"worker write set, counter, no forced unfollow, no errors ({sorted(hex(x) for x in got)}, {rw and rw['errors'][:2]})")
    sc.unload()
    # 2c. worker mode: an e9 into a module named by detourMod is ts3native's detour (accepted), into anonymous memory a Frida hook
    prm = params(tmp / 'det', 0.2, mode='stalk', worker=True)
    prm['addrs']['hooks']['plan'] = [P + 0x740, '81 ec d8 00 00 00 53 56']; prm['addrs']['detourMod'] = a['beepMod']
    sc = load(prm); rd2 = wait_json(tmp / 'det-watchstalk.json', 20)
    check(rd2 and rd2['reason'] == 'complete' and not rd2['refused'] and a['beepMod'] in (rd2['stalk']['planVia'] or ''),
          f"detour jump into {a['beepMod']} accepted ({rd2 and rd2['reason']}, {rd2 and rd2['stalk']['planVia']})")
    sc.unload()
    prm = params(tmp / 'fr', 0.2, mode='stalk', worker=True)
    prm['addrs']['hooks']['plan'] = [P + 0x700, '81 ec d8 00 00 00 53 56']
    sc = load(prm); rf = wait_json(tmp / 'fr-watchstalk.json', 5)
    check(rf and rf['reason'] == 'fail: no usable hook' and 'another Frida session' in rf['refused'].get('plan', ''), 'worker mode still refuses a jump into anonymous memory')
    sc.unload()
    check(p.poll() is None, 'alive after the worker runs')

    # 2d. docs/113 §8.6 crash shape (bp-watch-stalk-worker): mode both + worker, no workerTid (unknown until the first call), a
    # worker thread that is armed and hit in the watch phase and is inside the module "hook" (or its wait) at the switch, then
    # followed through the jump into the other module; the sim thread never calls the hook
    wk = h.spawnWorker(); time.sleep(0.3)
    prm = params(tmp / 'ws', 0.4, worker=True, watchS=8, everyS=2, windowS=1)
    prm['addrs']['hooks']['plan'] = [P + 0x780, '81 ec d8 00 00 00 53 56']; prm['addrs']['detourMod'] = a['mainMod']
    sc = load(prm); r4 = wait_json(tmp / 'ws-watchstalk.json', 45)
    w4, s4 = (r4 or {}).get('watch', {}), (r4 or {}).get('stalk', {})
    check(r4 and r4['reason'] == 'complete' and not r4['refused'] and a['mainMod'] in (s4.get('planVia') or '') and w4.get('endReason') == 'time',
          f"worker shape: complete, jump into {a['mainMod']} accepted, watch ended on time ({r4 and r4['reason']}, {r4 and r4['refused']}, {s4.get('planVia')})")
    rd4 = {x['pc']: x for x in w4.get('hits', [])}.get(hex(P + 0x50d))
    check(rd4 and str(wk) in rd4['tids'], f"watch hits on the worker thread before the switch ({rd4 and rd4['tids']})")
    got = {int(x['addr'], 16) - D for x in s4.get('written', [])}
    check(s4.get('workerTid') == wk and s4.get('simCalls') == 0 and s4.get('plans', 0) > 0 and got == {0x10, 0x20, 0x40, 0x50, 0x60},
          f"worker followed through the module hook ({s4.get('workerTid')} vs {wk}, plans {s4.get('plans')}, {sorted(hex(x) for x in got)})")
    check(r4 and r4['errors'] == [] and w4.get('unsetErrors') == 0 and w4.get('armErrors') == 0, f"no probe errors ({r4 and r4['errors'][:2]})")
    sc.unload(); time.sleep(0.3)
    c5, n5 = h.counts(), h.workerCount(); time.sleep(0.5)
    c6, n6 = h.counts(), h.workerCount()
    check(p.poll() is None and c6[0] > c5[0] and c6[1] > c5[1] and n6 > n5, f'all three threads alive after unload ({n5} -> {n6})')

    # 3. fail-fast: watch hooks on a stub nobody calls -> 2 progress checks of 30 s without a replan = fail with the reason
    prm = params(tmp / 'idle', 2, mode='watch')
    prm['addrs']['hooks']['replan'] = prm['addrs']['hooks']['replanPos'] = [P + 0x600, NOP8]
    sc = load(prm); r3 = wait_json(tmp / 'idle-watchstalk.json', 75)
    check(r3 is not None and r3['reason'] == 'fail: no route replan for 60 s' and r3['watch']['routes'] == 0, f"fail-fast after 60 s: {r3 and r3['reason']}")
    sc.unload()
    check(p.poll() is None, 'alive after the fail-fast run')
    rep = subprocess.run(['node', str(ROOT / 'tools' / 'harness' / 'watch-stalk-report.ts'), str(tmp / 'run-watchstalk.json')], capture_output=True, text=True)
    check(rep.returncode == 0 and 'U10: REFUTED' in rep.stdout and 'U1: REFUTED' in rep.stdout and 'U3: verified' in rep.stdout,
          'report reads the file (dummy addresses: U10/U1 unknown by design, U3 seen)')
    print(rep.stdout)
    print('\n'.join(m['payload'] if m['type'] == 'log' else str(m) for m in logs)[-2500:])
finally:
    p.kill()
print('RESULT', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
