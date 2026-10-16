// Rule-based IC safety verdicts from tools/ghidra/IcSafetyDepth4.java output (docs/data/ic-safety-a.md).
// node tools/ic-safety-classify.ts <IcSafety.tsv> <decompiled ScriptCore dir (return types, read locally)> <out.csv> <overrides.json>
import { readFileSync, readdirSync, writeFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';

const [tsv, scDir, outCsv, ovPath] = process.argv.slice(2);
const ret = new Map<string, string>();
for (const f of readdirSync(scDir)) if (f.endsWith('.cs')) {
  for (const m of readFileSync(join(scDir, f), 'utf8').matchAll(/extern\s+([\w.\[\]<>, ]+?)\s+(\w+)\s*\(/g)) ret.set(m[2], m[1].trim());
}
const ov: Record<string, any> = ovPath && existsSync(ovPath) ? JSON.parse(readFileSync(ovPath, 'utf8')) : {};

// writable globals that are set once at init or are self-locked infrastructure (not sim-mutated state)
const IMMUTABLE = new Set(['11d4bbc', '11cb864', '11c78d8', '114c900', '11ce2cc', '11d8a8c', '11d8a88', '11ee518', '11ee51c', '1195b7c', '11959dc', '1195b60', 'fb2cd0', '1004124']);
const NAMED_STATE: Record<string, string> = {
  '11dc350': 'ObjectService', '11ecbc4': 'WorldManager', '11ecf08': 'lot/object layer 11ECF08', '11d40c4': 'RouteHandleTable',
  '11ebae0': 'ObjectDesignerService', '11ce6a0': 'SimService', '11cf490': 'PhysicsWorld', '1021cf0': 'RoutingWorld', '11dc2a8': 'object event handlers',
  '11d9140': 'LocomotionEnabled', '11b1378': 'MessageServer', '11ed034': 'catalog',
};
const HANDLE_SLOTS: Record<string, string> = { '0xcc': 'script handle table (+0xCC, locked)', '0xc8': 'script handle table (+0xC8)', '0xd8': 'script handle table (+0xD8, locked)', '0xc0': 'host object maps (+0xC0)', '0xc4': 'host object maps (+0xC4)' };
const READ = /^(Get|Is|Has|Can|Count|Find|Test|Are|Query|Does|Was|Should|Exists|Clip|Calc|Compute|Lookup|Try|Pick|Num|Load|Debug|Did|Valid|Check|Contains|Retrieve|Peek|Read)/;

const rows: string[][] = [];
const counts: Record<string, number> = {};
for (const line of readFileSync(tsv, 'utf8').split(/\r?\n/)) {
  if (!line.trim()) continue;
  const c = line.split('\t');
  const [full, addr, fname] = c;
  const method = full.split('::')[1];
  const kv = (k: string) => (c.find(x => x.startsWith(k + '=')) ?? '').slice(k.length + 1);
  const n = +kv('n'), vc = +kv('vc'), gw = +kv('gw');
  const hitsRaw = c.find(x => x.startsWith('{')) ?? '{}';
  const hits = new Map<string, string>();
  for (const m of hitsRaw.slice(1, -1).matchAll(/([a-z0]+:[^=,]+(?:=[A-Za-z_0-9]+)?)=(\d+):([^,}]*)/g)) hits.set(m[1], `${m[2]}:${m[3]}`);
  const glob = kv('glob').trim().split(' ').filter(Boolean).map(g => { const i = g.lastIndexOf(':'); const k = g.slice(0, i); const a = k.includes('=') ? k.split('=')[1] : k; return { name: k.includes('=') ? k.split('=')[0] : '', a, m: g.slice(i + 1) }; });
  const named = kv('named');
  // host +0x10 (MonoScriptHost_Init) is a misresolved vcall (init is never called from an IC): drop everything found through it
  for (const [k, v] of [...hits]) if (k.includes('MonoScriptHost_Init') || v.includes('MonoScriptHost_Init')) hits.delete(k);
  const keys = [...hits.keys()];
  const has = (p: string) => keys.filter(k => k.startsWith(p));
  const short = (k: string) => { const v = hits.get(k)!; const d = v.split(':')[0]; const path = v.slice(d.length + 1).split('>'); return `${k.replace(/^[a-z0]+:/, '')}(d${d}${path.length > 2 ? ' via ' + path[path.length - 2] : ''})`; };

  const stateR = new Set<string>(), stateW = new Set<string>();
  for (const g of glob) {
    if (g.name.startsWith('g_CompBit_')) { stateR.add('object components'); continue; }
    if (IMMUTABLE.has(g.a) || g.name.startsWith('PTR_')) continue;
    const label = NAMED_STATE[g.a] ?? (g.name || '0x' + g.a.toUpperCase());
    if (g.m.includes('W')) stateW.add(label); if (g.m.includes('R') || g.m.includes('A')) stateR.add(label);
  }
  for (const k of has('host:')) { const s = k.match(/\+(0x[0-9a-f]+)/)![1]; if (HANDLE_SLOTS[s]) stateR.add(HANDLE_SLOTS[s]); }
  if (/ObjectService_FindById|ObjectId_Lookup/.test(named)) stateR.add('ObjectService');
  if (/RouteHandle_Lookup/.test(named) || keys.some(k => hits.get(k)!.includes('RouteHandle_Lookup'))) stateR.add('RouteHandleTable');
  if (/Object_GetComponent/.test(named)) stateR.add('object components');
  if (/RoutingWorld_|LotTable_|RoomGraph_|Portal_/.test(named)) stateR.add('RoutingWorld lot planners');
  if (/\bLot_|WorldManager_|World_Get/.test(named)) stateR.add('lots/WorldManager');
  for (const k of has('host:')) { const s = k.match(/\+(0x[0-9a-f]+)/)![1]; if (!HANDLE_SLOTS[s] && !/^0x(13[48]|58|17c|6[8c]|7[048c]|8[48c]|90|a8)$/.test(s)) stateR.add(`script host (+${s.toUpperCase().replace('0X', '0x')})`); }
  const sz = +kv('sz');

  const locks = has('lock:').map(short);
  const thr = has('thr:').map(short);
  const posts = has('post:').map(short);
  const mgd = [...has('mgd:'), ...has('evt:'), ...keys.filter(k => /host0?:\+0x13[48]=/.test(k) || /host0:\+0x(4|14)=/.test(k))].map(short);
  const allocs = has('alloc:').map(short);
  const dev = has('dev:').map(short);
  const muts = has('mut:').map(short);
  const tail = method.replace(/Impl$/, '').split('_').slice(1);
  const verb = tail.length > 1 && /^(ObjectDesigner|DesignMode|HandTool|Lot|Object|Room|Terrain)$/.test(tail[0]) ? tail.slice(1).join('_') : tail.join('_');
  const isRead = READ.test(verb) && !/^(Get|Find)\w*(AndSet|Create)/.test(verb);
  const rtype = ret.get(method) ?? '?';
  const writes = new Set<string>([...stateW, ...muts]);
  if (!isRead && (stateR.size || writes.size)) writes.add('sim object/world state (inferred from name)');

  let verdict: string, conf = 'inferred', note: string[] = [];
  const routingLocked = locks.some(l => /Route_PlanLocked|ObstacleQuery/.test(l));
  if (sz <= 8 && n === 1 && vc === 0 && stateR.size === 0) { verdict = 'SAFE'; note.push('empty stub (no state)'); }
  else if (dev.length) { verdict = 'MAIN'; note.push('reaches D3D device code on the calling thread'); }
  else if (mgd.length) { verdict = 'PROXY'; note.push('calls back into managed code (events/delegates); must run on the Mono thread'); }
  else if (!isRead && (writes.size || stateR.size)) { verdict = 'PROXY'; note.push(rtype === 'void' ? 'void command: can be queued to the sim thread' : `returns ${rtype}: needs a synchronous proxy`); }
  else if (!isRead && +kv('heap') > 0) { verdict = 'PROXY'; writes.add('fields reached through an argument or host pointer'); note.push('command that stores through a pointer argument'); }
  else if (!isRead && vc > 0) { verdict = 'PROXY'; note.push('command whose target is reached through virtual calls'); }
  else if (stateR.size === 0 && stateW.size === 0 && vc > 0) { verdict = 'UNSAFE'; note.push('target state reached only through unresolved virtual calls (conservative)'); }
  else if (stateR.size === 0 && stateW.size === 0) { verdict = 'SAFE'; note.push('no sim-mutated global, handle or object lookup in the direct-call closure'); }
  else if (routingLocked && [...stateR].every(s => /RoutingWorld|RouteHandle/.test(s))) { verdict = 'SAFE'; note.push('reads under the routing lock'); }
  else { verdict = 'UNSAFE'; note.push('unlocked read of sim-mutated state'); }
  if (!allocs.length && /^(string|.*\[\]|Array|XmlDocument|object)$/.test(rtype)) allocs.push(`managed ${rtype} return (inferred from the return type)`);
  if (allocs.length) note.push('allocates managed memory (needs GC thread registration, docs/30)');
  if (posts.length) note.push('posts work to another thread');
  if (vc > 0) note.push(`${vc} virtual call(s) at depth<=1 not followed`);
  if (n >= 1500) note.push('closure capped at 1500 functions');
  if (vc === 0 && n <= 30) conf = 'verified';
  const o = ov[method];
  if (o) { if (o.verdict) verdict = o.verdict; if (o.confidence) conf = o.confidence; if (o.note) note.unshift(o.note); }
  counts[verdict] = (counts[verdict] ?? 0) + 1;
  const j = (a: Iterable<string>) => [...a].join('; ');
  rows.push([full.replace('ScriptCore.', ''), method, addr, j(stateR), j(writes), j(locks), j(thr), j(posts), mgd.length ? 'yes: ' + j(mgd.slice(0, 3)) : 'no', allocs.length ? 'yes: ' + j(allocs.slice(0, 2)) : 'no', verdict, conf, `ret=${rtype}; n=${n}; ` + note.join('; ')]);
}
const esc = (s: string) => /[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
writeFileSync(outCsv, ['ic,name,address,reads,writes,locks,threadcheck,posts,callsManaged,allocs,verdict,confidence,note', ...rows.map(r => r.map(esc).join(','))].join('\n') + '\n');
const byClass: Record<string, Record<string, number>> = {};
for (const r of rows) { const k = r[0].split('::')[0]; (byClass[k] ??= {})[r[10]] = (byClass[k][r[10]] ?? 0) + 1; }
console.log(rows.length, JSON.stringify(counts));
console.log(JSON.stringify(byClass));
console.log('verified', rows.filter(r => r[11] === 'verified').length, 'ret?', rows.filter(r => r[12].startsWith('ret=?')).length);
