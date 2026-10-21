// docs/141: every managed caller of the native route planner (Route_Plan / PlanFromPoint / Replan / ReplanFromPoint ICs via
// ScriptCore.Route and the Sims3.SimIFace.Route PlanTo* helpers) with the execution contexts it can run in, found by walking
// the docs/78 call graph upward to an entry point (interaction Run, task Simulate, alarm, event listener, one-shot task,
// UI callback, native callback, static constructor).
// Usage: node tools/route-plan-callers.ts --asm game-copy/assemblies --index game-copy/analysis/callgraph/index.json [--csv out.csv]
//        [--why <caller id prefix>]   (prints one shortest chain per context class for that caller)
// Static only. Inferred parts: delegates not handed to a known registration API are taken as called synchronously by their
// creator; the own/foreign and plan-size columns are name heuristics (docs/141 §2).
import { readFileSync, writeFileSync } from 'node:fs';
import { typeModel, overrideIds } from './managed-callgraph.ts';

const arg = (n: string, d: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const ix: { kinds: string[]; methods: { k: string; c: number[] }[] } = JSON.parse(readFileSync(arg('--index', 'game-copy/analysis/callgraph/index.json'), 'utf8'));
const tm = typeModel(arg('--asm', 'game-copy/assemblies'), []);
const M = ix.methods;
const MAX_OV = Number(arg('--max-overrides', '12')), DEPTH = Number(arg('--depth', '12'));
const byId = new Map<string, any>();
for (const a of tm.asms) for (const t of a.types) for (const m of t.methods) byId.set(m.id, m);
const isA = (t: any, base: string) => { for (let c = t; c; c = tm.baseOf(c)) if (c.full === base) return true; return false; };
const idx = new Map(M.map((r, i) => [r.k, i]));

// Funnels: the four ScriptCore.Route entries (each calls its IC) and every Sims3.SimIFace.Route Plan*/Replan* helper.
const FUNNEL = /^(ScriptCore\.Route::(Plan|PlanFromPoint|Replan|ReplanFromPoint)\(|Sims3\.SimIFace\.Route::(Plan|Replan))/;
const funnels = new Set(M.flatMap((r, i) => (FUNNEL.test(r.k) ? [i] : [])));

// Reverse edges: plain calls (all kinds but dlg) and delegate creations (dlg) kept apart.
const rev: number[][] = M.map(() => []), dlgRev: number[][] = M.map(() => []);
M.forEach((r, i) => { for (let k = 0; k < r.c.length; k += 2) (ix.kinds[r.c[k + 1]] === 'dlg' ? dlgRev : rev)[r.c[k]].push(i); });
const callees = (i: number) => { const s = new Set<string>(); for (let k = 0; k < M[i].c.length; k += 2) s.add(M[M[i].c[k]].k); return s; };

// Base/interface declarations a method overrides: their callers reach it through virtual dispatch.
const ovCount = new Map<number, number>();
const nOv = (d: number) => ovCount.get(d) ?? ovCount.set(d, overrideIds(d).length).get(d)!;
const bases = (i: number) => rawBases(i).filter((d) => nOv(d) <= MAX_OV);
function rawBases(i: number): number[] {
  const m = byId.get(M[i].k); if (!m || !(m.flags & tm.VIRT)) return [];
  const out: number[] = [];
  // BCL declarations (IDisposable.Dispose, object.ToString, ...) are called from everywhere: not climbed.
  const bcl = (t: any) => /^(mscorlib|System|System\.Xml)$/.test(t.asm.name);
  for (let b = tm.baseOf(m.type); b && !bcl(b); b = tm.baseOf(b)) { const x = b.methods.find((y: any) => y.flags & tm.VIRT && tm.sigEq(y, m)); if (x && idx.has(x.id)) out.push(idx.get(x.id)!); }
  for (const it of tm.ifaceClosure(m.type)) { if (bcl(it)) continue; const x = it.methods.find((y: any) => tm.sigEq(y, m)); if (x && idx.has(x.id)) out.push(idx.get(x.id)!); }
  return out;
}
// bases() drops a declaration with many overrides (IGameObject members, GetLocalizedName, ...): it links every call site
// to every override, CHA noise rather than a call path. shortcut: count cap instead of receiver typing (docs/127); refine
// with receiverTypes if a chain through such a declaration matters.

// Registration API a delegate's creator hands it to (first match wins).
const REG: [string, RegExp][] = [
  ['alarm', /^Sims3\.Gameplay\.Utilities\.AlarmManager::AddAlarm/],
  ['event', /^Sims3\.Gameplay\.EventSystem\.(EventTracker::AddListener|EventListener::\.ctor)/],
  ['oneshot-task', /^Sims3\.(Gameplay|UI)\.OneShotFunction(Task|WithParams)::\.ctor/],
  ['oneshot', /^Sims3\.Gameplay\.OneShotFunction::\.ctor/],
  ['ui', /^Sims3\.(UI|Gameplay\.UI)\..*::add_/],
];
const regOf = (creator: number) => { const cs = [...callees(creator)]; for (const [c, re] of REG) if (cs.some((x) => re.test(x))) return c; return undefined; };

// Entry class of a method, or undefined when the walk goes on through its callers.
const entryCache = new Map<number, string | undefined>();
function entry(i: number): string | undefined {
  if (entryCache.has(i)) return entryCache.get(i);
  const m = byId.get(M[i].k); const name = M[i].k.slice(M[i].k.indexOf('::') + 2).replace(/\(.*$/, '');
  let e: string | undefined;
  if (name === '.cctor') e = 'cctor';
  else if (m && (name === 'Run' || name === 'RunFromInventory') && isA(m.type, 'Sims3.Gameplay.Interactions.InteractionInstance')) e = 'interaction';
  else if (name === 'Simulate' && m && isA(m.type, 'Sims3.Gameplay.Actors.Sim')) e = 'sim-task';
  else if (name === 'Simulate' && m && !rev[i].length) e = 'object-task';
  // Hubs that would reach every root (docs/141 §2): their own context instead of a walk through all their callers.
  else if (m && name === 'Cleanup' && isA(m.type, 'Sims3.Gameplay.Interactions.InteractionInstance')) e = 'interaction-cleanup';
  else if (name === 'ProcessEvent' || (m && isA(m.type, 'Sims3.Gameplay.EventSystem.EventListener'))) e = 'event';
  else if (m && name === 'Test' && isA(m.type, 'Sims3.Gameplay.Interactions.InteractionDefinition')) e = 'definition-test';
  else if (/^(ConfigureInteraction|CreateInstance|CreateInstanceFromInventory)$/.test(name)) e = 'interaction-setup';
  else if (m && /^(OnAddition|OnRemoval|OnTimeout)$/.test(name) && isA(m.type, 'Sims3.Gameplay.ActorSystems.Buff')) e = 'buff-callback';
  else if (m && name === 'Init' && isA(m.type, 'Sims3.Gameplay.Autonomy.Situation')) e = 'situation-state';
  else if (m && isA(m.type, 'Sims3.Gameplay.Socializing.SocialCallback')) e = 'social-callback';
  else if (/^(OnReset|OnResetTargeted|ResetSim)$/.test(name)) e = 'reset';
  else if (/^(OnLoad|PostLoad|OnPreLoad|PostWorldLoad|OnWorldLoadFinished\w*|OnLoadFixup|PersistPostLoad|PostLoadFixUp)$/.test(name)) e = 'load';
  else if (/^(OnCreation|OnStartup|Init|Startup|Shutdown)$/.test(name)) e = 'object-init';
  else if (/^(Dispose|Destroy|OnDestroy|OnDispose)$/.test(name)) e = 'dispose';
  else if (!rev[i].length && !bases(i).length && rawBases(i).length) e = 'virtual-hub';
  else if (!rev[i].length && !bases(i).length) {
    const regs = dlgRev[i].map(regOf).filter(Boolean) as string[];
    if (regs.length) e = regs[0];
    else if (!dlgRev[i].length) e = /^(On[A-Z]|Handle)|Callback|Handler|EventCallback/.test(name) ? 'native-callback' : 'no-managed-caller';
  }
  entryCache.set(i, e); return e;
}
// Predecessors in the upward walk: callers, base declarations, and (unregistered) delegate creators.
const preds = (i: number) => [...rev[i], ...bases(i), ...(dlgRev[i].some((c) => regOf(c)) ? [] : dlgRev[i])];

// Context class: a = own interaction/Sim task, b = another yielding task, c = no yielding task (sync only),
// x = inherits the context of whoever calls the hub (any class), u = no managed caller found (mod-only, reflection,
// unresolved dispatch, or dead). definition-test = autonomy (a) or pie menu (c); social-callback = called by name from the
// social interaction's Run (a) [inferred].
export const CLASS: Record<string, string> = {
  'interaction': 'a', 'sim-task': 'a', 'interaction-cleanup': 'a', 'social-callback': 'a', 'definition-test': 'ac',
  'object-task': 'b', 'oneshot-task': 'b', 'oneshot': 'b', 'alarm': 'b',
  'event': 'c', 'ui': 'c', 'reset': 'c', 'dispose': 'c', 'load': 'c', 'native-callback': 'c', 'cctor': 'c',
  'interaction-setup': 'x', 'buff-callback': 'x', 'situation-state': 'x', 'object-init': 'x', 'virtual-hub': 'x', 'depth-limit': 'x',
  'no-managed-caller': 'u',
};

function walk(start: number, cap = 40000) {
  const par = new Map<number, number>([[start, -1]]); let q = [start]; const roots = new Map<string, number[]>(); let capped = false;
  for (let depth = 0; q.length && !capped; depth++) {
    if (depth > DEPTH) { roots.set('depth-limit', q); break; }
    const nx: number[] = [];
    for (const i of q) {
      const e = i === start ? undefined : entry(i);
      if (e) { (roots.get(e) ?? roots.set(e, []).get(e)!).push(i); continue; }
      const ps = preds(i).filter((p) => !funnels.has(p));
      if (!ps.length && i === start) { const e0 = entry(i) ?? 'no-managed-caller'; (roots.get(e0) ?? roots.set(e0, []).get(e0)!).push(i); }
      for (const p of ps) if (!par.has(p)) { par.set(p, i); nx.push(p); if (par.size > cap) capped = true; }
    }
    q = nx;
  }
  if (!roots.size && !capped) roots.set('no-managed-caller', [start]);
  return { roots, par, capped, size: par.size };
}
const chain = (par: Map<number, number>, r: number) => { const out: string[] = []; for (let i = r; i >= 0; i = par.get(i)!) out.push(M[i].k.replace(/\(.*$/, '')); return out.join(' > '); };

// Heuristics (docs/141 §2) [inferred]: route owned by another object, and how far the plan can reach.
const FOREIGN = /Social|Group|Follower|Push|Vehicle|Car[A-Z]|Car::|Boat|Driver|Elevator|Dance|JigB|Carry|Horse|Situation|Stroller|Rider|Passenger|Gangway|Npc/;
const WORLD = /Lot|Home|Travel|Car[A-Z]|Car::|Vehicle|Boat|RabbitHole|Community|Venue|Street|World|GoHere|Outside|OffScreen|Offscreen|Leave|Spawn|Teleport|Stuck|Routable|Gangway|Destination|Taxi|Bike|Horse/;
const LOCAL = /Slot|Jig|Turn|FailPath|Near|Adjacent|Radial|Radius|Chair|Seat|Bed|Counter|Footprint|InFrontOf|Facing/;

const direct = new Map<number, Set<string>>();
for (const f of funnels) for (const c of [...rev[f]]) if (!funnels.has(c)) (direct.get(c) ?? direct.set(c, new Set()).get(c)!).add(M[f].k.replace(/^.*::/, '').replace(/\(.*$/, ''));

const why = arg('--why', '');
// kind=caller: one row per direct caller (contexts its callers reach; hub = the walk passed 40k methods, i.e. a shared
// routing helper reachable from every context). kind=entry: one row per entry point that reaches a planner, with the
// shortest chain (entry > ... > direct caller > plan API).
const rows: string[] = ['kind,method,assembly,context,class,plan_api,chain,foreign_hint,plan_size,note'];
const asmOf = (k: string) => byId.get(k)?.asm.name ?? '';
const q = (s: string) => JSON.stringify(s ?? '');
const size = (s: string, api: string) => (WORLD.test(s) ? 'world' : LOCAL.test(s) || /Radial|Radius|Slot/.test(api) ? 'local-or-object' : 'any');
const counts: Record<string, number> = {};
for (const [c, apis] of [...direct].sort((x, y) => M[x[0]].k.localeCompare(M[y[0]].k))) {
  const w = walk(c);
  if (why && M[c].k.startsWith(why)) for (const [e, rs] of w.roots) console.log(`${e} (${rs.length}): ${chain(w.par, rs[0])}`);
  const ctx = [...w.roots.keys()].sort();
  const cls = [...new Set(ctx.flatMap((e) => [...CLASS[e]]))].sort().join('');
  counts['caller:' + cls] = (counts['caller:' + cls] ?? 0) + 1;
  const cRoot = ctx.filter((e) => CLASS[e].includes('c')).map((e) => w.roots.get(e)![0])[0];
  const k = M[c].k, apiStr = [...apis].join('|');
  rows.push(['caller', q(k), asmOf(k), q(ctx.join('|')), cls, q(apiStr), q(cRoot !== undefined ? chain(w.par, cRoot) : ''),
    FOREIGN.test(k) ? 'foreign' : '', size(k, apiStr), q((w.capped ? 'hub; ' : '') + ctx.map((e) => `${e}:${w.roots.get(e)!.length}`).join('|'))].join(','));
}
// One upward BFS from every direct caller at once: each entry point once, with its shortest chain.
{
  const par = new Map<number, number>(); let qq = [...direct.keys()]; for (const c of qq) par.set(c, -1);
  const found: number[] = [];
  for (let depth = 0; qq.length && depth <= DEPTH; depth++) {
    const nx: number[] = [];
    for (const i of qq) {
      const ps = preds(i).filter((p) => !funnels.has(p));
      if (!entry(i) && !ps.length) entryCache.set(i, 'no-managed-caller');
      if (entry(i)) { found.push(i); continue; }
      for (const p of ps) if (!par.has(p)) { par.set(p, i); nx.push(p); }
    }
    qq = nx;
  }
  for (const i of found) {
    const e = entry(i)!; const ch = chain(par, i); let d = i; while (par.get(d)! >= 0) d = par.get(d)!;
    const apiStr = [...direct.get(d)!].join('|');
    counts['entry:' + e] = (counts['entry:' + e] ?? 0) + 1;
    rows.push(['entry', q(M[i].k), asmOf(M[i].k), e, CLASS[e], q(apiStr), q(ch), FOREIGN.test(ch) ? 'foreign' : '', size(ch, apiStr), ''].join(','));
  }
}
const out = arg('--csv', '');
if (out) writeFileSync(out, rows.join('\n') + '\n');
console.log(`funnels ${funnels.size}, direct callers ${direct.size}; ${JSON.stringify(counts)}`);
