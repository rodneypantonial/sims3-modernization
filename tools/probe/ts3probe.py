"""Live inspection server for TS3W.exe (The Sims 3).

Python, not TypeScript, because Frida's Python bindings are already installed (pip frida-tools);
the Node binding is a native npm module. Standard library otherwise.

  python tools/probe/ts3probe.py serve [--launch [--env NAME=VALUE ...]] [--world X.sims3] [--profile] [--port 47891]
  python tools/probe/ts3probe.py get status|threads|memory|rescache|modules|events|scripterrors
  python tools/probe/ts3probe.py get "read?addr=0xd81840&len=32"
  python tools/probe/ts3probe.py get "agent?cmd=ping"        (in-game ts3agent mod, see mods/ts3agent)
  python tools/probe/ts3probe.py get detach                  (always stop the server this way, not by killing it)
  python tools/probe/ts3probe.py get reload                  (reload tools/probe/agent.js in place)

serve attaches to a running TS3W.exe (or starts it with --launch), loads tools/probe/agent.js
(and tools/frida/stutter-profile.js with --profile), serves JSON on 127.0.0.1 only, and appends
every event (native exceptions, script errors, detach/crash) to probe-out/<timestamp>/events.jsonl.
"""
import argparse, bisect, csv, glob, json, os, subprocess, sys, threading, time, urllib.error, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

frida = None  # imported in serve only, so `get` runs without it

ROOT = Path(__file__).resolve().parents[2]
GAME_BIN = Path(os.environ.get('TS3_BIN', r'C:\Program Files (x86)\Steam\steamapps\common\The Sims 3\Game\Bin'))
USER_DIR = Path(os.environ.get('TS3_USER', Path.home() / 'Documents' / 'Electronic Arts' / 'The Sims 3'))
FUNCTIONS_CSV = ROOT / 'game-copy' / 'analysis' / 'functions.csv'


class Symbols:
    """Maps addresses to 'TS3W.exe!Name+0x12' (Ghidra export) or 'module+0x1234'."""

    def __init__(self, script):
        self.script = script
        self.starts, self.ends, self.names = [], [], []
        if FUNCTIONS_CSV.exists():
            with open(FUNCTIONS_CSV, newline='') as f:
                for row in csv.DictReader(f):
                    self.starts.append(int(row['entry'], 16)); self.ends.append(int(row['end'], 16)); self.names.append(row['name'])
        self.modules = []
        self.refresh_modules()

    def refresh_modules(self):
        self.modules = sorted((int(m['base'], 16), int(m['base'], 16) + m['size'], m['name']) for m in self.script.exports_sync.modules())

    def name(self, addr_text):
        try:
            a = int(addr_text, 16)
        except (TypeError, ValueError):
            return addr_text
        mod = next((m for m in self.modules if m[0] <= a < m[1]), None)
        if mod is None:  # a DLL loaded after attach: refresh once
            self.refresh_modules()
            mod = next((m for m in self.modules if m[0] <= a < m[1]), None)
        if mod and mod[2].lower() == 'ts3w.exe' and self.starts:
            i = bisect.bisect_right(self.starts, a) - 1
            if i >= 0 and a <= self.ends[i]:
                return f'{addr_text} TS3W.exe!{self.names[i]}+0x{a - self.starts[i]:x}'
        if mod:
            return f'{addr_text} {mod[2]}+0x{a - mod[0]:x}'
        return f'{addr_text} ?'

    def stack(self, frames):
        return [self.name(f) for f in frames]


class Probe:
    def __init__(self, args):
        self.args = args
        self.out = ROOT / 'probe-out' / time.strftime('%Y%m%d-%H%M%S')
        self.out.mkdir(parents=True, exist_ok=True)
        self.events_file = open(self.out / 'events.jsonl', 'a', encoding='utf-8')
        self.lock = threading.Lock()
        self.agent_lock = threading.Lock()
        self.script_errors = []
        self.alive = True

    def log(self, event):
        with self.lock:
            if 'stack' in event and hasattr(self, 'symbols'):
                event['stack'] = self.symbols.stack(event['stack'])
            self.events_file.write(json.dumps(event) + '\n')
            self.events_file.flush()
        if event.get('kind') != 'note':
            print(f"[ts3probe] {event.get('kind')}: {event.get('type') or event.get('file') or event.get('reason', '')}", flush=True)

    def launch(self):
        cmd = [str(GAME_BIN / 'TS3W.exe')]
        if self.args.world:
            cmd.append(f'-worldFile:{self.args.world}')
        cmd += self.args.extra or []
        print(f'[ts3probe] launching {cmd}', flush=True)
        env = dict(os.environ, **dict(e.split('=', 1) for e in self.args.env))
        if self.args.env:
            print(f'[ts3probe] game environment: {self.args.env}', flush=True)
        subprocess.Popen(cmd, cwd=GAME_BIN, env=env)
        for _ in range(120):  # the exe may hand over to another instance; wait for the process by name
            try:
                return frida.attach('TS3W.exe')
            except frida.ProcessNotFoundError:
                time.sleep(0.5)
        raise SystemExit('TS3W.exe did not start within 60 s')

    def start(self):
        self.session = self.launch() if self.args.launch else frida.attach('TS3W.exe')
        self.session.on('detached', self.on_detached)
        self.script = self.session.create_script((ROOT / 'tools' / 'probe' / 'agent.js').read_text(encoding='utf-8'))
        self.script.on('message', self.on_message)
        self.script.load()
        self.symbols = Symbols(self.script)
        if self.args.exception_stacks:
            self.script.exports_sync.enable_exceptions()
        if self.args.profile:
            prof = self.session.create_script((ROOT / 'tools' / 'frida' / 'stutter-profile.js').read_text(encoding='utf-8'))
            prof.on('message', lambda m, d: print('[profile]', m.get('payload', m), flush=True))
            prof.load()
            prof.exports_sync.init('late', {'out': str(self.out / 'profile').replace('\\', '/')})
        threading.Thread(target=self.watch_script_errors, daemon=True).start()
        threading.Thread(target=self.sample_memory, daemon=True).start()
        if sys.platform == 'win32':
            threading.Thread(target=self.keep_window_place, daemon=True).start()
        print(f'[ts3probe] attached; serving http://127.0.0.1:{self.args.port}; events in {self.out}', flush=True)

    def on_message(self, message, data):
        if message['type'] == 'send' and message['payload'].get('type') == 'event':
            self.log(message['payload']['event'])
        elif message['type'] == 'error':
            self.log({'kind': 'agent-error', 'description': message.get('description'), 'stack': []})

    def on_detached(self, reason, crash):
        info = {'kind': 'detached', 'reason': reason, 'time': time.strftime('%Y-%m-%dT%H:%M:%S')}
        if crash is not None:
            info['crash'] = {'summary': crash.summary, 'report': crash.report}
        self.log(info)
        self.alive = False

    def sample_memory(self):
        """Address-space timeline (memory.csv): shows what fills the 4 GB before an out-of-memory crash."""
        cols = ['time', 'commitMB', 'reserveOnlyMB', 'freeMB', 'largestFreeMB', 'imageMB', 'mappedMB', 'privateMB', 'threads']
        with open(self.out / 'memory.csv', 'a', encoding='utf-8') as f:
            f.write(','.join(cols) + '\n')
            n, threads = 0, 0
            while self.alive:
                try:
                    m = self.script.exports_sync.address_space()
                    if n % 6 == 0:  # counting threads enumerates (suspends) them all; once a minute is enough
                        threads = self.script.exports_sync.status()['threads']
                    n += 1
                    m['threads'] = threads
                    m['time'] = time.strftime('%H:%M:%S')
                    f.write(','.join(str(m[c]) for c in cols) + '\n')
                    f.flush()
                    if m['largestFreeMB'] < 200:
                        self.log({'kind': 'low-address-space', 'largestFreeMB': m['largestFreeMB'], 'freeMB': m['freeMB']})
                except Exception:
                    pass  # game busy or gone; the detach handler reports that
                time.sleep(self.args.memory_interval)

    def keep_window_place(self):
        """Puts the game window where it was last time (the game centers it on start), so tests can run beside other work.
        The position is saved whenever it changes; on start the window is moved back while it still sits where the game put it."""
        import ctypes
        from ctypes import wintypes
        u = ctypes.windll.user32
        place = ROOT / 'probe-out' / 'window.json'
        saved = json.loads(place.read_text()) if place.exists() else None
        pid = frida.get_local_device().get_process('TS3W.exe').pid

        def game_window():
            found = []
            def cb(h, _):
                p = wintypes.DWORD()
                u.GetWindowThreadProcessId(h, ctypes.byref(p))
                r = wintypes.RECT()
                if p.value == pid and u.IsWindowVisible(h) and not u.IsIconic(h) and u.GetWindowRect(h, ctypes.byref(r)):
                    found.append(((r.right - r.left) * (r.bottom - r.top), h, r.left, r.top))
                return True
            u.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(cb), 0)
            return max(found, default=None)

        first, t0 = None, None
        while self.alive:
            w = game_window()
            if w and w[0] > 200 * 200:
                _, h, x, y = w
                if first is None:
                    first, t0 = (x, y), time.time()
                    print(f'[ts3probe] game window at {first}; saved place {saved}', flush=True)
                if saved and (x, y) == first and (x, y) != (saved['x'], saved['y']) and time.time() - t0 < 120:
                    u.SetWindowPos(h, 0, saved['x'], saved['y'], 0, 0, 0x1 | 0x4 | 0x10)  # NOSIZE | NOZORDER | NOACTIVATE
                    print(f'[ts3probe] moved game window to {saved}', flush=True)
                elif (x, y) != first and (not saved or (x, y) != (saved['x'], saved['y'])):
                    saved = {'x': x, 'y': y}  # moved by Rodney (or by us, then equal to saved): remember it
                    place.parent.mkdir(exist_ok=True)
                    place.write_text(json.dumps(saved))
                    print(f'[ts3probe] remembered game window place {saved}', flush=True)
            time.sleep(2)

    def watch_script_errors(self):
        """The game writes one ScriptError_*.xml per script exception, with the managed stack trace inside."""
        seen = set(glob.glob(str(USER_DIR / 'ScriptError_*.xml')))
        while self.alive:
            for path in sorted(set(glob.glob(str(USER_DIR / 'ScriptError_*.xml'))) - seen):
                seen.add(path)
                time.sleep(0.5)  # let the game finish writing
                text = Path(path).read_text(encoding='utf-8', errors='replace')
                err = {'kind': 'script-error', 'file': Path(path).name, 'time': time.strftime('%Y-%m-%dT%H:%M:%S'), 'xml': text}
                with self.lock:
                    self.script_errors.append(err)
                self.log(err)
            time.sleep(2)

    def handle(self, path, query):
        q = {k: v[0] for k, v in parse_qs(query).items()}
        rpc = self.script.exports_sync
        if path == 'status':
            return dict(rpc.status(), alive=self.alive, out=str(self.out))
        if path == 'threads':
            ts = rpc.threads(int(q.get('frames', 32)))
            for t in ts:
                t['stack'] = self.symbols.stack(t['stack'])
            return ts
        if path == 'net':  # network calls per API and target (start=1 hooks them once); does the game go online while playing?
            return rpc.net(q.get('start') == '1')
        if path == 'native':  # ts3native.asi counters (route cache, route worker), null when the plugin is not loaded; routeworker=0|1 flips the worker at runtime
            rw = q.get('routeworker')
            return rpc.native_counters(int(rw) if rw in ('0', '1') else None)
        if path == 'routelock':  # routing-lock hold/wait and plan overlap (docs/18 step 1a); start=1 hooks once, stop=1 unhooks
            return rpc.route_lock(q.get('start') == '1', q.get('stop') == '1')
        if path == 'managedstack':  # C# call stack of the sim thread, innermost first (interpreter frames)
            return rpc.interp_stack(int(q.get('frames', 64)))
        if path == 'interp':  # Mono interpreter frame chain of the sim thread (read-only)
            return rpc.interp_frames(int(q.get('frames', 32)))
        if path == 'simstack':  # sim thread only; safe for frequent use (the all-thread /threads is not)
            r = rpc.sim_stack(int(q.get('frames', 24)))
            r['stack'] = self.symbols.stack([f.rstrip('?') for f in r['stack']])
            return r
        if path == 'memory':
            return rpc.address_space()
        if path == 'rescache':  # resource cache budget and fill (docs/08 phase 0), read-only
            return rpc.res_cache()
        if path == 'modules':
            self.symbols.refresh_modules()
            return rpc.modules()
        if path == 'read':
            return {'addr': q['addr'], 'hex': rpc.read(q['addr'], int(q.get('len', 64)))}
        if path == 'string':
            return {'addr': q['addr'], 'text': rpc.read_string(q['addr'], q.get('kind', 'ascii'))}
        if path == 'symbolize':
            return {'name': self.symbols.name(q['addr'])}
        if path == 'events':
            evs = rpc.events(int(q.get('since', 0)))
            for e in evs:
                if 'stack' in e:
                    e['stack'] = self.symbols.stack(e['stack'])
            return evs
        if path == 'reload':  # swap tools/probe/agent.js in place after editing it, without detaching from the game
            old = self.script
            self.script = self.session.create_script((ROOT / 'tools' / 'probe' / 'agent.js').read_text(encoding='utf-8'))
            self.script.on('message', self.on_message)
            self.script.load()
            old.unload()
            self.symbols.script = self.script
            if self.args.exception_stacks:
                self.script.exports_sync.enable_exceptions()
            return {'reloaded': 'tools/probe/agent.js'}
        if path == 'detach':  # stop probing without killing the host mid-call (that can take the game down)
            self.alive = False
            threading.Thread(target=self.session.detach, daemon=True).start()
            return {'detaching': True}
        if path == 'exceptions':  # per-site counts (only with --exception-stacks)
            return rpc.exception_counts()
        if path == 'agentstatus':
            return rpc.agent_status()
        if path == 'agent':  # in-game ts3agent command, e.g. agent?cmd=list_sims&args={"household":"active"}
            args = json.loads(q['args']) if 'args' in q else None
            with self.agent_lock:  # one request in the mailbox at a time
                seq = rpc.agent_send(q['cmd'], args)
                deadline = time.time() + int(q.get('timeout', 30000)) / 1000
                while time.time() < deadline:
                    reply = rpc.agent_reply(seq)
                    if reply is not None:
                        return reply
                    time.sleep(0.02)
            return {'ok': False, 'error': f"no reply to '{q['cmd']}' (game paused, loading or blocked?)", 'seq': seq}
        if path == 'scripterrors':
            with self.lock:
                return self.script_errors[int(q.get('since', 0)):]
        raise KeyError(path)


def serve(args):
    global frida
    import frida
    probe = Probe(args)
    probe.start()

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            u = urlparse(self.path)
            try:
                body, code = probe.handle(u.path.strip('/'), u.query), 200
            except KeyError as e:
                body, code = {'error': f'unknown endpoint or parameter {e}'}, 404
            except Exception as e:  # report agent errors (game gone, bad address) instead of dying
                body, code = {'error': f'{type(e).__name__}: {e}'}, 500
            data = json.dumps(body, indent=1).encode()
            self.send_response(code)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, *a):
            pass

    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)  # loopback only: the probe reads game memory
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        while probe.alive:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    server.shutdown()
    print('[ts3probe] stopped', flush=True)


def get(args):
    try:
        with urllib.request.urlopen(f'http://127.0.0.1:{args.port}/{args.path}', timeout=60) as r:
            print(r.read().decode())
    except urllib.error.HTTPError as e:  # the server puts the reason in the body
        print(e.read().decode())
        sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('serve')
    s.add_argument('--launch', action='store_true', help='start TS3W.exe first')
    s.add_argument('--env', action='append', default=[], metavar='NAME=VALUE', help='environment for the launched game, e.g. GC_INITIAL_HEAP_SIZE=536870912 (docs/13)')
    s.add_argument('--world', help='save to auto-load, e.g. Baseline-SV.sims3 (passed as -worldFile)')
    s.add_argument('--profile', action='store_true', help='also load the stutter profiler')
    s.add_argument('--exception-stacks', action='store_true',
                   help='also log stacks for native exceptions (JS handler; the game raises thousands per second, which can starve the probe)')
    s.add_argument('--port', type=int, default=47891)
    s.add_argument('--memory-interval', type=float, default=10, help='seconds between address-space samples')
    s.add_argument('extra', nargs='*', help='extra TS3W.exe arguments after --')
    g = sub.add_parser('get')
    g.add_argument('path')
    g.add_argument('--port', type=int, default=47891)
    args = ap.parse_args()
    serve(args) if args.cmd == 'serve' else get(args)


if __name__ == '__main__':
    main()
