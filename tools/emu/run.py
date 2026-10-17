"""Differential test runner for single TS3W.exe functions (see docs/63-emu-harness.md).

  python tools/emu/run.py [--exe PATH] [--cases N] [--seed S] tools/emu/specs/<spec>.py ...
  python tools/emu/run.py --self-check      # the example specs; skips cleanly without the exe

Exe path: --exe, else env TS3W_EXE, else <repo>/game-copy/bin/TS3W.exe.
Exit code: 0 all specs pass (outputs match and coverage target met), 1 a spec failed, 0 with
a SKIP line when the exe or the Python packages are missing.
"""
import argparse
import importlib.util
import os
import random
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SELF_CHECK = ['fnv64.py', 'lot_world_to_local.py', 'refpack.py', 'object_id_lookup.py', 'object_id_erase.py', 'phys_shape_release.py',
              'lot_table_contains.py', 'lot_front_door.py']


def load_spec(path):
    spec = importlib.util.spec_from_file_location(os.path.splitext(os.path.basename(path))[0], path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run_spec(path, exe, n_cases, seed):
    from emu import Emu, EmuError
    s = load_spec(path)
    emu = Emu(exe)
    getattr(s, 'setup', lambda e: None)(emu)
    rng = random.Random(seed)
    cases = s.cases(rng, n_cases or getattr(s, 'CASES', 200))
    t0 = time.time()
    got = []
    for c in cases:
        emu.reset_heap()
        try:
            got.append(s.run(emu, c))
        except EmuError as e:
            got.append(('EmuError', str(e)))
    t_emu = time.time() - t0
    want = s.reference_batch(cases) if hasattr(s, 'reference_batch') else [s.reference(c) for c in cases]
    compare = getattr(s, 'compare', lambda g, w: None if g == w else f'got {g!r}, want {w!r}')
    fails = [(c, d) for c, g, w in zip(cases, got, want) if (d := compare(g, w))]

    print(f'== {s.NAME}  target {s.TARGET if isinstance(s.TARGET, str) else hex(s.TARGET)}  conv {s.CONV}')
    print(f'cases: {len(cases)}  pass {len(cases) - len(fails)}  fail {len(fails)}  ({t_emu:.1f} s emulated)')
    for c, d in fails[:5]:
        print(f'  MISMATCH [{c["label"]}]: {d}'[:400])
    entries = [emu.resolve(f) for f in [s.TARGET] + list(getattr(s, 'FOLLOW', []))]
    cov_ok = True
    for f, reached in emu.coverage_report(entries):
        total = len(f.blocks)
        pct = 100.0 * len(reached) / total if total else 100.0
        missed = [hex(b) for b in f.blocks if b not in reached]
        role = 'target' if f.entry == entries[0] else 'followed'
        print(f'coverage {role} {f.name} {f.entry:#x}: {len(reached)}/{total} blocks ({pct:.1f}%)'
              + (f'  missed: {" ".join(missed)}' if missed else ''))
        if f.indirect:
            print(f'  note: indirect jumps at {" ".join(hex(a) for a in f.indirect)}; block list may be incomplete')
        if f.entry == entries[0] and pct + 1e-9 < 100.0 * getattr(s, 'COVERAGE_TARGET', 1.0):
            cov_ok = False
    stubbed = {st.name: st for st in emu.stubs.values()}
    print('stubbed (not run, replaced by Python): ' + (', '.join(sorted(stubbed)) or 'none'))
    for kind, addrs in (('read', emu.globals_read), ('written', emu.globals_written)):
        print(f'image .data/.bss {kind}: {len(addrs)} addresses'
              + (': ' + ' '.join(hex(a) for a in sorted(addrs)[:8]) if addrs else ''))
    for note in getattr(s, 'NOTES', []):
        print(f'note: {note}')
    ok = not fails and cov_ok
    print('RESULT: ' + ('PASS' if ok else 'FAIL' + ('' if cov_ok else ' (coverage below target)')))
    return ok


def harness_negative_check(exe):
    """The strict default must fail loudly: HashStringFNV64_W without its callee allowed or stubbed."""
    from emu import Emu, EmuError
    e = Emu(exe)
    try:
        e.call(0x7CFE40, [e.wstr('x')])
    except EmuError as x:
        ok = '0x4aeb90' in str(x)
        print(f'== harness negative check: unstubbed callee -> {"PASS" if ok else "FAIL"} ({x})')
        return ok
    print('== harness negative check: unstubbed callee ran without error -> FAIL')
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('specs', nargs='*')
    ap.add_argument('--exe')
    ap.add_argument('--cases', type=int, default=0, help='random cases per spec (default: the spec CASES)')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--self-check', action='store_true')
    a = ap.parse_args()
    specs = [os.path.join(HERE, 'specs', f) for f in SELF_CHECK] if a.self_check else a.specs
    if not specs:
        ap.error('give spec files or --self-check')
    sys.path.insert(0, HERE)
    import emu as emu_mod  # noqa: E402  (after sys.path; raises when unicorn/capstone are missing)
    exe = a.exe or emu_mod.DEFAULT_EXE
    if not os.path.exists(exe):
        print(f'SKIP: {exe} not found (copy your own TS3W.exe 1.67 to game-copy/bin/ or set TS3W_EXE)')
        return 0
    ok = all([run_spec(p, exe, a.cases, a.seed) for p in specs])
    if a.self_check:
        ok = harness_negative_check(exe) and ok
    return 0 if ok else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except ModuleNotFoundError as e:
        if e.name in ('unicorn', 'capstone'):
            print(f'SKIP: Python package {e.name} missing (pip install unicorn capstone)')
            sys.exit(0)
        raise
