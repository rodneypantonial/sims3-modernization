// docs/206 §3: managed read set of the [AutShadow] worker. Every instance and static field the pilot closure reads
// (allow-list v0 + the 7 HowMuchItWantsSimToCome implementors + the meta base path, docs/126 pilot union; the CHA run adds
// every game override within --hops calls of a root, docs/127), with the writers of each field across the whole call-graph
// index and a write class: no-IL-writer (set only by native code, reflection or the persist loader), ctor-only, load-time
// (writers are constructors, class constructors or load/parse/startup methods by name [inferred]), mutable (anything
// else: what a cut-time copy or a write barrier has to cover). Also counts array element loads (IL ldelem*) and
// collection reads (calls into System.Collections non-mutators) in the closure.
// Usage: node tools/worker-read-set.ts --asm <repo>/game-copy/assemblies --index <repo>/game-copy/analysis/callgraph/index.json
//          [--hops 5] > docs/data/worker-read-set.csv   (summary to stderr). Static only.
//        node tools/worker-read-set.ts --self-check
type Rec = { k: string; a: number; c: number[]; fr?: number[]; fw?: number[]; sr?: number[]; sw?: number[] };
type Ix = { asms: { name: string }[]; kinds: string[]; methods: Rec[]; fields: string[] };
export type Row = { field: string; type: string; isStatic: boolean; run: 'base' | 'cha'; readers: number; writers: string[]; cls: string };

const LOAD = /::(\.cctor|\.ctor|Parse\w*|Load\w*|OnPreLoad|OnPostLoad|OnWorldLoad\w*|OnStartup\w*|Startup\w*|Init\w*|Initialize\w*|\w*FromXml|\w*Xml\w*|ImportContent|Import|Instantiate)\(/;
export function writeClass(writers: string[]): string {
  if (!writers.length) return 'no-IL-writer';
  if (writers.every((w) => /::\.c?ctor\(/.test(w))) return 'ctor-only';
  if (writers.every((w) => LOAD.test(w))) return 'load-time';
  return 'mutable';
}
const typeOf = (f: string) => f.slice(0, f.lastIndexOf('::'));

// Rows for the fields read by the closure ids; writers from the whole index (fw/sw). chaOnly = ids reached only in the CHA run.
export function readSet(ix: Ix, base: Iterable<number>, cha: Iterable<number>): Row[] {
  const wr = new Map<number, number[]>();
  ix.methods.forEach((m, i) => { for (const f of [...(m.fw ?? []), ...(m.sw ?? [])]) (wr.get(f) ?? wr.set(f, []).get(f)!).push(i); });
  const rows = new Map<number, Row>();
  const add = (ids: Iterable<number>, run: 'base' | 'cha') => {
    for (const i of ids) {
      const m = ix.methods[i];
      for (const [list, st] of [[m.fr ?? [], false], [m.sr ?? [], true]] as const) for (const f of list) {
        let r = rows.get(f);
        if (!r) { const w = (wr.get(f) ?? []).map((j) => ix.methods[j].k); r = { field: ix.fields[f], type: typeOf(ix.fields[f]), isStatic: st, run, readers: 0, writers: w, cls: writeClass(w) }; rows.set(f, r); }
        r.readers++;
      }
    }
  };
  const b = new Set(base); add(b, 'base'); add([...cha].filter((i) => !b.has(i)), 'cha');
  return [...rows.values()];
}

const READ_COLL = /^System\.Collections\.[^:]*::(get_Item|get_Count|TryGetValue|ContainsKey|ContainsValue|Contains|GetEnumerator|MoveNext|get_Current|IndexOf|Find\w*|Exists|ToArray|get_Keys|get_Values|CopyTo)\(/;
const esc = (s: string) => (/[",]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s);
const short = (k: string) => k.replace(/\(.*$/s, '').replace(/^.*[.+]([^.+]+::)/, '$1');

function selfCheck(): void {
  const ok = (c: boolean, m: string) => { if (!c) { console.error(`FAIL ${m}`); process.exit(1); } };
  ok(writeClass([]) === 'no-IL-writer', 'no writer');
  ok(writeClass(['A.B::.ctor()', 'A.B::.cctor()']) === 'ctor-only', 'ctor only');
  ok(writeClass(['A.B::.ctor()', 'A.B::ParseTuning(X)']) === 'load-time', 'load-time');
  ok(writeClass(['A.B::.ctor()', 'A.B::Update(float)']) === 'mutable', 'mutable');
  const ix: Ix = { asms: [{ name: 'x' }], kinds: [], fields: ['T::a', 'T::s', 'U::b'], methods: [
    { k: 'T::Read()', a: 0, c: [], fr: [0], sr: [1] }, { k: 'T::Over()', a: 0, c: [], fr: [2] },
    { k: 'T::.ctor()', a: 0, c: [], fw: [0] }, { k: 'T::Tick()', a: 0, c: [], sw: [1] } ] };
  const r = readSet(ix, [0], [0, 1]);
  const by = (f: string) => r.find((x) => x.field === f)!;
  ok(r.length === 3, 'three fields');
  ok(by('T::a').cls === 'ctor-only' && !by('T::a').isStatic && by('T::a').run === 'base', 'instance ctor-only field');
  ok(by('T::s').cls === 'mutable' && by('T::s').isStatic, 'static mutable field');
  ok(by('U::b').run === 'cha' && by('U::b').cls === 'no-IL-writer', 'cha-only field');
  console.error('worker-read-set self-check ok (7 checks)');
}

async function main(): Promise<void> {
  if (process.argv.includes('--self-check')) return selfCheck();
  const arg = (n: string, d: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
  const t2 = await import('./autonomy-tier2.ts');
  const mcg = await import('./managed-callgraph.ts');
  const ix = t2.ix as unknown as Ix;
  // Pilot union roots as docs/126 (autonomy-tier2.ts pilotRoots): v0 definitions, implementors, the pilot base path.
  const BASE = ['Sims3.Gameplay.Autonomy.Autonomy::CheckAvailability', 'Sims3.Gameplay.Interactions.InteractionDefinitionUtilities::CommonTests',
    'Sims3.Gameplay.Autonomy.Autonomy::CalculateScoreAndAddToCandidates', 'Sims3.Gameplay.Interactions.MetaInteractionDefinitionUtilities::SpecialCaseTests',
    'Sims3.Gameplay.Abstracts.GameObject::get_Parent'];
  const roots = [...[...t2.inV0].flatMap((t) => mcg.defRoots(ix as never, t)), ...t2.IMPL.flatMap((t) => t2.starts(`${t}::HowMuchItWantsSimToCome`)), ...BASE.flatMap((b) => t2.starts(b))];
  const base = t2.closure(roots), cha = t2.closure(roots, undefined, Number(arg('--hops', '5')));
  const rows = readSet(ix, base.ids, cha.ids);
  // Array element loads and collection reads in the closure (CHA run).
  let ldelem = 0, ldlen = 0, ilMethods = 0; const coll = new Map<string, number>();
  for (const i of cha.ids) {
    const il = mcg.methodIl(i);
    if (il) { ilMethods++; for (const x of il.ins) { if ((x.op >= 0x90 && x.op <= 0x9a) || x.op === 0xa3 || x.op === 0x8f) ldelem++; else if (x.op === 0x8e) ldlen++; } }
    const m = ix.methods[i];
    for (let k = 0; k < m.c.length; k += 2) { const t = ix.methods[m.c[k]].k; if (READ_COLL.test(t)) coll.set(short(t), (coll.get(short(t)) ?? 0) + 1); }
  }
  const out = ['field,declaring_type,static,run,closure_readers,writer_methods,write_class,example_writers'];
  rows.sort((a, b) => a.cls.localeCompare(b.cls) || a.type.localeCompare(b.type) || a.field.localeCompare(b.field));
  for (const r of rows) out.push([r.field, r.type, r.isStatic ? 'yes' : '', r.run, String(r.readers), String(r.writers.length), r.cls,
    [...new Set(r.writers.map(short))].slice(0, 3).join('; ')].map(esc).join(','));
  console.log(out.join('\n'));
  // Summary.
  const tally = (f: (r: Row) => boolean) => { const t = new Map<string, number>(); for (const r of rows.filter(f)) t.set(r.cls, (t.get(r.cls) ?? 0) + 1); return [...t].map(([k, n]) => `${k} ${n}`).join(', '); };
  console.error(`closures: base ${base.size} methods, cha ${cha.size}; IL bodies read ${ilMethods}`);
  console.error(`fields read: ${rows.length} (base ${rows.filter((r) => r.run === 'base').length}); instance: ${tally((r) => !r.isStatic)}; static: ${tally((r) => r.isStatic)}`);
  const mut = rows.filter((r) => r.cls === 'mutable');
  const byType = new Map<string, Row[]>(); for (const r of mut) (byType.get(r.type) ?? byType.set(r.type, []).get(r.type)!).push(r);
  console.error(`mutable fields by declaring type (${byType.size} types), top 30:`);
  for (const [t, rs] of [...byType].sort((a, b) => b[1].length - a[1].length).slice(0, 30)) console.error(`  ${rs.length}\t${t}${rs.some((r) => r.isStatic) ? ` (static ${rs.filter((r) => r.isStatic).length})` : ''}`);
  const barrier = new Set<string>(); const mutIds = new Set(mut.map((r) => r.field));
  for (const m of ix.methods) if ([...(m.fw ?? []), ...(m.sw ?? [])].some((f) => mutIds.has(ix.fields[f]))) barrier.add(m.k);
  const byAsm = new Map<string, number>(); for (const m of ix.methods) if (barrier.has(m.k)) { const a = ix.asms[m.a]?.name ?? '?'; byAsm.set(a, (byAsm.get(a) ?? 0) + 1); }
  console.error(`methods that write a mutable read-set field (barrier sites, whole index): ${barrier.size} (${[...byAsm].map(([a, n]) => `${a} ${n}`).join(', ')})`);
  console.error(`array element loads (ldelem*) ${ldelem}, ldlen ${ldlen}; collection read call sites ${[...coll.values()].reduce((a, b) => a + b, 0)}, top: ${[...coll].sort((a, b) => b[1] - a[1]).slice(0, 8).map(([k, n]) => `${k} ${n}`).join(', ')}`);
}
await main();
