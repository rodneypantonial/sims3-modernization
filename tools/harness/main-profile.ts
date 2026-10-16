// Main-thread (render/UI) profile from *-mainstack.jsonl (profile-world.ts --mainstack, 200 ms wall-clock samples). Reads only.
//   node tools/harness/main-profile.ts [--top 20] [files...]   default: every non-empty probe-out/profile/*-mainstack.jsonl
//   node tools/harness/main-profile.ts --self-check
// Names: game-copy/analysis/functions.csv (+ ghidra-user-symbols.tsv) unless FUN_*, else `Name 0xADDR` / `| `0xADDR` | `Name` |` in docs/*.md,
// else "unknown". Self = leaf-most TS3W frame. Inclusive = function on any TS3W frame (stacks are scanned, so stale return addresses
// inflate it; approximate). Bucket = nearest marker from the leaf (MARKERS below, from docs/41/37/39/40/47/52/48/42), else the doc the
// nearest doc-named frame came from, else "other". Phases = repro steps from probe-out/queue/<tag>.log (UTC HH:MM:SS of the sample).
// Long blocks = runs of >= 3 consecutive non-idle samples (>= ~400 ms) with the same innermost named frame (frame times are not recorded).
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { basename, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadFunctions, lookup } from './sample-split.ts';

const ROOT = join(import.meta.dirname, '..', '..');
type Fn = { entry: number; end: number; name: string };
type Sample = { t: number; frames: { a: number; mod: string }[] };

const BUCKETS: [number, number, string][] = [ // [from, to) entry ranges or single entries (to = from + 1)
  [0x5887a0, 0x5887a1, 'idle'],                                  // App_IdleIfNotForeground Sleep(10) unfocused (docs/41)
  [0x6e8810, 0x6e8811, 'render-job wait'], [0x6ebb70, 0x6ebb71, 'render-job wait'], // Renderer_End/BeginFrame (docs/41)
  [0x693780, 0x693781, 'sim-render consume'], [0x693660, 0x693661, 'sim-render consume'], [0x5937b0, 0x5937b1, 'sim-render consume'],
  [0x593440, 0x593441, 'sim-render consume'],                     // FrameDatabase lock +0x48 side (docs/41 S1)
  [0x50b270, 0x50b271, 'UI draw'], [0x50eb20, 0x50eb21, 'UI draw'], [0x50ab70, 0x50ab71, 'UI draw'], // WindowManager_Draw, display lists (docs/52)
  [0x7eb6d0, 0x7eb6d1, 'scene/culling'], [0x6fdfb0, 0x6fdfb1, 'scene/culling'],             // scene window, SceneObjMgr_FindObject (docs/41, 45)
  [0x608630, 0x608631, 'texture compositor'],                    // TextureCompositorQueue_Update
  [0x719000, 0x722000, 'swarm (effects)'],                        // Swarm service range (docs/44 §7)
  [0x5f0e50, 0x5f0e51, 'CAS SimService'], [0x5e2050, 0x5e2051, 'CAS SimService'], // SimService_Update, CAS model build (docs/42)
  [0x599a10, 0x599a11, 'job pump'],                               // JobManagerService_Update (docs/36)
  [0x72d470, 0x72dc00, 'resource'], [0x4aa1b0, 0x4aa1b1, 'resource'], [0x4ac9c0, 0x4ac9c1, 'resource'], [0x4affc0, 0x4affc1, 'resource'], // docs/39
  [0x4e5e40, 0x4e5e41, 'allocator'], [0x4e69d0, 0x4e69d1, 'allocator'], [0x4e6a20, 0x4e6a21, 'allocator'],
  [0x60f150, 0x611800, 'present/D3D'], [0x618000, 0x61a000, 'present/D3D'], [0xec9f00, 0xec9f01, 'present/D3D'], // device + texture wrappers, Frame_RenderAndPresent
];
const DOC_BUCKET: Record<string, string> = { '37': 'present/D3D', '40': 'present/D3D', '53': 'present/D3D', '47': 'UI draw', '52': 'UI draw', '55': 'UI draw',
  '43': 'UI draw', '54': 'UI draw', '48': 'scene/culling', '44': 'scene/culling', '46': 'render-job wait', '34': 'scene/culling', '39': 'resource', '42': 'CAS SimService' };
const GPU_MOD = /^(d3d9|dxgi|nv\w*|vulkan-1|winevulkan|dxvk\w*)\.dll$/i;

export function docNames(dir: string): Map<number, { name: string; doc: string }> {
  const m = new Map<number, { name: string; doc: string }>();
  for (const f of readdirSync(dir).filter(f => /^\d+.*\.md$/.test(f)).sort()) {
    const doc = f.slice(0, 2), text = readFileSync(join(dir, f), 'utf8');
    const add = (a: string, name: string) => { const k = parseInt(a, 16); if (!m.has(k) && !/^(FUN|LAB|DAT)_/.test(name)) m.set(k, { name, doc }); };
    for (const x of text.matchAll(/`([A-Za-z_][\w:]*) (0x[0-9A-Fa-f]{5,8})`/g)) add(x[2], x[1]);
    for (const x of text.matchAll(/\|\s*`(0x[0-9A-Fa-f]{5,8})`\s*\|\s*`([A-Za-z_][\w:]*)`/g)) add(x[1], x[2]);
  }
  return m;
}
export function parse(text: string): Sample[] {
  return text.split('\n').filter(Boolean).map(l => JSON.parse(l)).filter(j => j.role === 'main' && j.stack?.length).map(j => ({
    t: j.t, frames: j.stack.map((s: string) => { const [a, rest = ''] = s.split(' '); return { a: parseInt(a, 16), mod: rest.split(/[!+]/)[0] }; }),
  }));
}
const bucketOf = (entry: number) => BUCKETS.find(([f, t]) => entry >= f && entry < t)?.[2];

export function analyze(samples: Sample[], fns: Fn[], docs: Map<number, { name: string; doc: string }>) {
  const nameOf = (f: Fn) => !/^FUN_/.test(f.name) ? f.name : docs.get(f.entry)?.name ?? 'unknown';
  const self = new Map<number, number>(), incl = new Map<number, number>(), buckets = new Map<string, number>();
  let gpuLeaf = 0;
  const keys: { t: number; bucket: string; key: string }[] = [];
  for (const s of samples) {
    const ts3 = s.frames.filter(f => f.mod === 'TS3W.exe').map(f => lookup(fns, f.a)).filter((f): f is Fn => !!f);
    if (GPU_MOD.test(s.frames[0]?.mod ?? '')) gpuLeaf++;
    if (ts3[0]) self.set(ts3[0].entry, (self.get(ts3[0].entry) ?? 0) + 1);
    for (const e of new Set(ts3.map(f => f.entry))) incl.set(e, (incl.get(e) ?? 0) + 1);
    const firstTs3 = s.frames.findIndex(f => f.mod === 'TS3W.exe'); // a GPU-module frame above the first TS3W frame: inside DXVK (submit, Present, pacing wait)
    let b: string | undefined = s.frames.slice(0, firstTs3 < 0 ? undefined : firstTs3).some(f => GPU_MOD.test(f.mod)) ? 'present/D3D' : undefined;
    if (!b) for (const f of ts3) { b = bucketOf(f.entry); if (b) break; const d = docs.get(f.entry); if (d && DOC_BUCKET[d.doc]) { b = DOC_BUCKET[d.doc]; break; } }
    b ??= 'other';
    buckets.set(b, (buckets.get(b) ?? 0) + 1);
    const named = ts3.find(f => !/^FUN_/.test(f.name) || docs.has(f.entry)) ?? ts3[0];
    keys.push({ t: s.t, bucket: b, key: named ? `0x${named.entry.toString(16).toUpperCase()} ${nameOf(named)}` : '(no TS3W frame)' });
  }
  const runs: { t: number; n: number; key: string; bucket: string }[] = [];
  for (let i = 0; i < keys.length;) {
    let j = i + 1;
    while (j < keys.length && keys[j].key === keys[i].key && keys[j].t - keys[j - 1].t <= 450) j++;
    if (j - i >= 3 && keys[i].bucket !== 'idle' && !keys[i].key.includes('App_IdleIfNotForeground')) runs.push({ t: keys[i].t, n: j - i, key: keys[i].key, bucket: keys[i].bucket });
    i = j;
  }
  return { n: samples.length, self, incl, buckets, gpuLeaf, runs, keys, nameOf };
}

function phases(tag: string): { sec: number; step: string }[] {
  const log = join(ROOT, 'probe-out', 'queue', `${tag}.log`);
  if (!existsSync(log)) return [];
  return [...readFileSync(log, 'utf8').matchAll(/^(\d\d):(\d\d):(\d\d) repro \S+ step (\w+)/gm)].map(m => ({ sec: +m[1] * 3600 + +m[2] * 60 + +m[3], step: m[4] }));
}
const pct = (a: number, n: number) => (100 * a / n).toFixed(1) + '%';

function report(files: string[], top: number) {
  const fns = loadFunctions(readFileSync(join(ROOT, 'game-copy', 'analysis', 'functions.csv'), 'utf8'),
    existsSync(join(ROOT, 'game-copy', 'analysis', 'ghidra-user-symbols.tsv')) ? readFileSync(join(ROOT, 'game-copy', 'analysis', 'ghidra-user-symbols.tsv'), 'utf8') : '');
  const docs = docNames(join(ROOT, 'docs'));
  const all: Sample[] = [];
  const order = ['idle', 'present/D3D', 'UI draw', 'scene/culling', 'render-job wait', 'sim-render consume', 'resource', 'texture compositor', 'swarm (effects)', 'CAS SimService', 'job pump', 'allocator', 'other'];
  console.log(`| Run | Samples | ${order.join(' | ')} | leaf in GPU module |`);
  console.log(`|---|---|${order.map(() => '---|').join('')}---|`);
  const perPhase = new Map<string, Map<string, number>>();
  for (const f of files) {
    const s = parse(readFileSync(f, 'utf8')); all.push(...s);
    const r = analyze(s, fns, docs), tag = basename(f).replace(/-mainstack\.jsonl$/, '');
    console.log(`| ${tag} | ${r.n} | ${order.map(b => pct(r.buckets.get(b) ?? 0, r.n)).join(' | ')} | ${pct(r.gpuLeaf, r.n)} |`);
    const ph = phases(tag);
    for (const k of r.keys) {
      const d = new Date(k.t), sec = d.getUTCHours() * 3600 + d.getUTCMinutes() * 60 + d.getUTCSeconds();
      const step = ph.length ? (ph.filter(p => p.sec <= sec).at(-1)?.step ?? 'before steps') : 'no repro';
      const m = perPhase.get(step) ?? new Map(); m.set(k.bucket, (m.get(k.bucket) ?? 0) + 1); m.set('_n', (m.get('_n') ?? 0) + 1); perPhase.set(step, m);
    }
    for (const run of r.runs.sort((a, b) => b.n - a.n).slice(0, 6)) {
      const d = new Date(run.t), sec = d.getUTCHours() * 3600 + d.getUTCMinutes() * 60 + d.getUTCSeconds();
      console.log(`  long block ${tag} ${d.toISOString().slice(11, 19)}Z ${run.n} samples (${ph.filter(p => p.sec <= sec).at(-1)?.step ?? '-'}): ${run.bucket} / ${run.key}`);
    }
  }
  const r = analyze(all, fns, docs);
  const other = new Map<string, number>(); for (const k of r.keys) if (k.bucket === 'other') other.set(k.key, (other.get(k.key) ?? 0) + 1);
  console.log('\nTop innermost named frames in other: ' + [...other].sort((a, b) => b[1] - a[1]).slice(0, 10).map(([k, c]) => `${k} ${pct(c, r.n)}`).join('; '));
  console.log(`\nPooled ${r.n} samples. By repro step (share of that step's samples):`);
  console.log(`| Step | Samples | ${order.join(' | ')} |`);
  for (const [step, m] of perPhase) console.log(`| ${step} | ${m.get('_n')} | ${order.map(b => pct(m.get(b) ?? 0, m.get('_n')!)).join(' | ')} |`);
  for (const [title, map] of [['Self', r.self], ['Inclusive', r.incl]] as const) {
    console.log(`\n| # | ${title} | Address | Name | Bucket |\n|---|---|---|---|---|`);
    [...map].sort((a, b) => b[1] - a[1]).slice(0, top).forEach(([e, c], i) => {
      const f = lookup(fns, e)!, d = docs.get(e);
      console.log(`| ${i + 1} | ${pct(c, r.n)} | 0x${e.toString(16).toUpperCase()} | ${r.nameOf(f)}${d && /^FUN_/.test(f.name) ? ` (docs/${d.doc})` : ''} | ${bucketOf(e) ?? (d && DOC_BUCKET[d.doc]) ?? ''} |`);
    });
  }
}

function selfCheck() {
  const fns: Fn[] = [{ entry: 0x50b270, end: 0x50b400, name: 'FUN_0050b270' }, { entry: 0x5887a0, end: 0x588800, name: 'FUN_005887a0' }, { entry: 0x700000, end: 0x700100, name: 'FUN_00700000' }];
  const docs = new Map([[0x5887a0, { name: 'App_IdleIfNotForeground', doc: '41' }]]);
  const line = (t: number, st: string[]) => JSON.stringify({ t, thread: 1, role: 'main', stack: st });
  const s = parse([line(0, ['0x77000000 ntdll.dll+0x1', '0x5887b0 TS3W.exe!FUN_005887a0+0x10']), line(200, ['0x700010 TS3W.exe+0x300010', '0x50b280 TS3W.exe+0x10b280']),
    line(400, ['0x700010 TS3W.exe+0x300010']), line(600, ['0x700020 TS3W.exe+0x300020']), JSON.stringify({ t: 1, role: null, stack: ['0x1 x'] })].join('\n'));
  const r = analyze(s, fns, docs);
  if (r.n !== 4 || r.buckets.get('idle') !== 1 || r.buckets.get('UI draw') !== 1 || r.buckets.get('other') !== 2) throw new Error(`buckets ${JSON.stringify([...r.buckets])}`);
  if (r.self.get(0x700000) !== 3 || r.incl.get(0x50b270) !== 1 || r.runs.length !== 1 || r.runs[0].n !== 3) throw new Error('self/incl/runs');
  if (r.keys[0].key !== '0x5887A0 App_IdleIfNotForeground') throw new Error(r.keys[0].key);
  console.log('self-check ok');
}

if (resolve(process.argv[1] ?? '') === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2), ti = args.indexOf('--top'), top = ti < 0 ? 20 : Number(args.splice(ti, 2)[1]);
  if (args[0] === '--self-check') selfCheck();
  else {
    const dir = join(ROOT, 'probe-out', 'profile');
    report(args.length ? args : readdirSync(dir).filter(f => f.endsWith('-mainstack.jsonl') && statSync(join(dir, f)).size > 0).map(f => join(dir, f)), top);
  }
}
