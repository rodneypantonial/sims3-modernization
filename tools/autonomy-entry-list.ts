// PLAN gap (12) step 12c: rank interaction definitions by sampled autonomy scoring time.
// Usage: node tools/autonomy-entry-list.ts [--samples probe-out/profile] [--src game-copy/decompiled] [--csv docs/data/autonomy-entry-list.csv] [--summary]
// Reads every *-managed.jsonl / managed-*.jsonl (one C# stack per line, innermost first) and the decompiled
// script sources (tools/extract-assemblies.ts + ILSpy layout: one file per top-level type, tab indentation).
// A scoring sample is a stack with Autonomy.CalculateScore (docs/59 C2). It is charged to the outermost concrete
// definition frame inside C2; nested type frames carry only the short name ("Definition.Test"), so candidates are
// narrowed by the next callee frame and by the search context (meta, social). Samples with only base-class frames
// stay in "base path" buckets. Self-check runs first.
import { readFileSync, readdirSync, writeFileSync, statSync } from 'node:fs';
import { join, relative, sep } from 'node:path';

type Cls = { full: string; key: string; asm: string; bases: string[]; raw: string[]; baseText: string; parent?: Cls; methods: Map<string, string>; body: string };

const arg = (n: string, d: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const SAMPLES = arg('--samples', 'probe-out/profile');
const SRC = arg('--src', 'game-copy/decompiled');
const CSV = arg('--csv', 'docs/data/autonomy-entry-list.csv');

const genericKey = (name: string) => { const m = /^(\w+)\s*<([^<>]*(?:<[^<>]*>[^<>]*)*)>/.exec(name); return m ? `${m[1]}\`${m[2].split(',').length}` : name; };

const splitTop = (s: string) => { const out: string[] = []; let d = 0, cur = ''; for (const ch of s) { if (ch === '<') d++; if (ch === '>') d--; if (ch === ',' && !d) { out.push(cur); cur = ''; } else cur += ch; } out.push(cur); return out; };

// Parse one ILSpy file by indentation: class headers at depth d own methods at depth d+1.
export function parseFile(text: string, asm: string): Cls[] {
  const lines = text.split(/\r?\n/);
  const ns = /^namespace ([\w.]+);/m.exec(text)?.[1] ?? '';
  const out: Cls[] = [];
  const stack: { cls: Cls; depth: number }[] = [];
  const CLS = /^(\t*)(?:(?:public|private|protected|internal|abstract|sealed|static|partial|new|unsafe)\s+)*(?:class|struct)\s+(\w+(?:<[^{:]*?>)?)\s*(?::\s*(.*?))?\s*(?:where .*)?$/;
  const METH = /^(\t*)(?:(?:public|private|protected|internal|override|virtual|static|sealed|new|abstract|extern|unsafe)\s+)+[\w<>\[\],.? ]+?\s(\w+)\s*(?:<[^()]*>)?\(/;
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const depth = /^\t*/.exec(line)![0].length;
    while (stack.length && depth <= stack[stack.length - 1].depth && line.trim() && !/^\t*[{}]/.test(line)) stack.pop();
    const c = CLS.exec(line);
    if (c) {
      const name = c[2].replace(/\s+/g, '');
      const parent = stack[stack.length - 1]?.cls;
      const key = genericKey(name);
      const raw = splitTop(c[3] ?? '').map((b) => b.trim().replace(/<.*$/s, (g) => genericKey('x' + g).slice(1))).filter(Boolean);
      const bases = raw.map((b) => b.replace(/^[\w.]*\./, ''));
      const cls: Cls = { full: parent ? `${parent.full}+${key}` : (ns ? `${ns}.${key}` : key), key, asm, bases, raw, baseText: c[3] ?? '', parent, methods: new Map(), body: '' };
      out.push(cls); stack.push({ cls, depth });
      continue;
    }
    const top = stack[stack.length - 1];
    if (!top) continue;
    top.cls.body += line + '\n';
    const m = METH.exec(line);
    if (m && m[1].length === top.depth + 1) {
      let j = i + 1; const end = `${m[1]}}`; let body = line + '\n';
      if (lines[j]?.trim() === '{') { for (; j < lines.length && lines[j] !== end; j++) body += lines[j] + '\n'; }
      top.cls.methods.set(m[2], (top.cls.methods.get(m[2]) ?? '') + body);
    }
  }
  return out;
}

function walk(dir: string, acc: string[] = []): string[] {
  for (const n of readdirSync(dir)) { const p = join(dir, n); if (statSync(p).isDirectory()) walk(p, acc); else if (n.endsWith('.cs')) acc.push(p); }
  return acc;
}

// One-level scan of the definition's own Test/score bodies, docs/59 §2.4 classes. A guess, not a closure.
export function pureReadGuess(src: string): string {
  if (!src) return '';
  if (/Simulator\.Sleep|YieldIfPossible|\.Yield\(/.test(src)) return 'Y';
  if (/RandomUtil\.|RandomGen\.|EventTracker\.|AddExitReason|CreateObject|PlantObjectHere|\bthis\.\w+\s*=[^=]|^\s*m[A-Z]\w*\s*=[^=]|\bOwner\s*=[^=]/m.test(src)) return 'W';
  if (/Queries\.|GetObjects|CountObjects|FindClosest|Slots\.|World\.|Terrain|GetHoursOfOperation|FindCallbox/.test(src)) return 'P';
  if (/LotCurrent|\.Parent\b|\.Position\b|IsOutside|HasBeenDestroyed|IsOnRoof|InUse/.test(src)) return 'H';
  return 'M';
}

function selfCheck(): void {
  const t = 'namespace A.B;\n\npublic class Outer : Interaction<Sim, Lot>\n{\n\tpublic class Definition : MetaInteractionScoredByVenue<Sim, Lot, Outer>\n\t{\n\t\tpublic override bool Test(Sim a, Lot t, bool au, ref GreyedOutTooltipCallback cb)\n\t\t{\n\t\t\treturn Outer.Shared(a);\n\t\t}\n\t}\n\n\tpublic static bool Shared(Sim a)\n\t{\n\t\treturn true;\n\t}\n}\n';
  const cs = parseFile(t, 'X');
  const d = cs.find((c) => c.key === 'Definition');
  const o = cs.find((c) => c.key === 'Outer');
  if (!d || d.full !== 'A.B.Outer+Definition' || d.bases[0] !== 'MetaInteractionScoredByVenue`3' || !d.methods.get('Test')?.includes('Shared(')) throw new Error('selfcheck: nested class');
  if (!o || !o.methods.has('Shared') || o.methods.has('Test')) throw new Error('selfcheck: method owner');
  if (pureReadGuess('return RandomUtil.CoinFlip();') !== 'W' || pureReadGuess('return a.LotCurrent == t;') !== 'H' || pureReadGuess('return true;') !== 'M') throw new Error('selfcheck: guess');
}
selfCheck();
if (process.argv.includes('--self-check')) { console.log('autonomy-entry-list self-check ok'); process.exit(0); }

// ---- index
const byKey = new Map<string, Cls[]>();
for (const f of walk(SRC)) {
  const asm = relative(SRC, f).split(sep)[0];
  for (const c of parseFile(readFileSync(f, 'utf8'), asm)) { const l = byKey.get(c.key) ?? []; l.push(c); byKey.set(c.key, l); }
}
// Base type text -> classes. Short nested names are ambiguous ("Definition"), so: qualified "X.Y" matches the
// nested path; "class Definition : Definition" means the outer class's base's nested Definition; else prefer siblings.
function baseClasses(c: Cls, raw: string): Cls[] {
  const parts = raw.split('.'); const key = parts[parts.length - 1];
  const all = byKey.get(key) ?? [];
  if (parts.length > 1) { const tail = parts.slice(-2); const re = new RegExp(`[.+]${tail[0].replace(/`/g, '.')}[.+]${tail[1].replace(/`/g, '.')}$`); return all.filter((x) => re.test(x.full)); }
  if (key === c.key) return c.parent ? c.parent.raw.flatMap((r) => baseClasses(c.parent!, r)).flatMap((p) => all.filter((x) => x.full === `${p.full}+${key}`)) : [];
  if (all.length <= 3) return all;
  const sib = all.filter((x) => c.parent && x.full === `${c.parent.full}+${key}`);
  if (sib.length) return sib;
  const top = all.filter((x) => !x.parent);
  return top.length <= 3 ? top : [];
}
const ancMemo = new Map<Cls, string[]>();
const ancestors = (c: Cls, seen = new Set<Cls>()): string[] => {
  if (ancMemo.has(c)) return ancMemo.get(c)!;
  seen.add(c);
  const out: string[] = [];
  for (const r of c.raw) { out.push(r.split('.').pop()!); for (const p of baseClasses(c, r)) if (!seen.has(p)) out.push(...ancestors(p, seen)); }
  ancMemo.set(c, out);
  return out;
};
const ROOT = /^(Meta|SoloSim|Immediate|Actorless|Sub|New|Social)?InteractionDefinition(`\d+)?$|^IInteractionDefinition$|^InteractionDefinition$/;
const isDef = (c: Cls) => ROOT.test(c.key) || ancestors(c).some((a) => ROOT.test(a));
const isMeta = (c: Cls) => ancestors(c).some((a) => /^MetaInteraction/.test(a));
const isSocial = (c: Cls) => /Social/.test(c.full) || ancestors(c).some((a) => /Social/.test(a));
// Generic base frames: every pair passes through them, so they say nothing about the type.
const BASE = new Set(['InteractionDefinition`3', 'SoloSimInteractionDefinition`1', 'MetaInteractionDefinition`3', 'ImmediateInteractionDefinition`3', 'ActorlessInteractionDefinition`3', 'InteractionDefinition`4']);

const frameCls = (f: string) => { const p = f.split('.'); return { key: p[p.length - 2] ?? '', method: p[p.length - 1], full: p.slice(0, -1).join('.') }; };
const calls = (body: string, callee: string) => {
  const { key, method } = frameCls(callee);
  if (BASE.has(key) || /^\.ctor$/.test(method)) return body.includes('base.') || body.includes(key.replace(/`.*/, ''));
  const name = method.replace(/^(get_|set_)/, '');
  return new RegExp(`\\b${name}\\b`).test(body);
};

function resolve(frame: string, inner: string | undefined, ctx: string): { label: string; cls?: Cls; how: string; n: number } {
  const { key, method, full } = frameCls(frame);
  let cands = (byKey.get(key) ?? []).filter((c) => isDef(c) && c.methods.has(method));
  if (full.includes('.') && full !== key) { const ex = cands.filter((c) => c.full === full); if (ex.length) cands = ex; }
  let how = cands.length === 1 ? 'name' : 'callee';
  if (cands.length > 1 && inner) { const f = cands.filter((c) => calls(c.methods.get(method)!, inner)); if (f.length) cands = f; }
  const narrow = (ok: (c: Cls) => boolean, tag: string) => { if (cands.length > 1) { const f = cands.filter(ok); if (f.length) { cands = f; how += tag; } } };
  narrow(ctx === 'meta' ? isMeta : (c) => !isMeta(c), ctx === 'meta' ? '+meta' : '+nonmeta');
  narrow(ctx === 'social' ? isSocial : () => true, '+social');
  if (cands.length === 1) return { label: cands[0].full, cls: cands[0], how, n: 1 };
  const ik = inner ? frameCls(inner) : undefined;
  const tail = !ik ? 'self' : BASE.has(ik.key) ? 'base Test' : `${ik.key}.${ik.method.replace(/^get_/, '')}`;
  if (cands.length > 1 && cands.length <= 4) return { label: cands.map((c) => c.full).sort().join(' | '), how: 'ambiguous', n: cands.length };
  return { label: `${key}.${method} [unresolved: ${cands.length} candidates; callee ${tail}; ${ctx}]`, how: 'unresolved', n: cands.length };
}

// ---- samples
const files = readdirSync(SAMPLES).filter((f) => /-managed\.jsonl$|^managed-.*\.jsonl$/.test(f));
let total = 0, stacked = 0, scoring = 0, autonomy = 0;
const rows = new Map<string, { kind: string; samples: number; pre: number; post: number; cls?: Cls; how: string; callee: Map<string, number>; ctx: Map<string, number> }>();
const ctxCount = new Map<string, number>();
for (const f of files) {
  const post = statSync(join(SAMPLES, f)).mtime >= new Date('2026-10-16T00:00:00+08:00');
  for (const line of readFileSync(join(SAMPLES, f), 'utf8').split('\n')) {
    if (!line.trim()) continue;
    let s: string[]; try { s = JSON.parse(line); } catch { continue; }
    total++; if (s.length) stacked++;
    if (s.some((x) => x.startsWith('Sims3.Gameplay.Autonomy.Autonomy.') || x.includes('AutonomyManager.'))) autonomy++;
    const c = s.indexOf('Sims3.Gameplay.Autonomy.Autonomy.CalculateScore');
    if (c < 0) continue;
    scoring++;
    const outer = s.slice(c + 1).join(' ');
    const ctx = /ScoreInteractionsForMetaObject|ScoreInteractionsForMetaAutonomy/.test(outer) ? 'meta' : /ScoreSocialsOnSims|FindBestSocial/.test(outer) ? 'social' : /ScoreSoloSimInteractions/.test(outer) ? 'solo' : /ScoreInteractionsForCommodity/.test(outer) ? 'commodity' : /FindTransitionInteraction/.test(outer) ? 'posture' : 'other';
    ctxCount.set(ctx, (ctxCount.get(ctx) ?? 0) + 1);
    let label = '', kind = '', how = '', cls: Cls | undefined, callee = '';
    // outermost concrete definition frame inside C2
    for (let i = c - 1; i >= 0 && !label; i--) {
      const { key, method } = frameCls(s[i]);
      if (BASE.has(key)) continue;
      const known = (byKey.get(key) ?? []).some((k) => isDef(k));
      if (!known) continue;
      const r = resolve(s[i], s[i - 1], ctx);
      label = r.label; cls = r.cls; how = r.how;
      kind = cls && /`/.test(cls.key) && cls.methods.has(method) && !cls.full.includes('+') ? 'family' : method === 'CalculateScore' ? 'score-override' : 'definition';
      callee = s[i - 1] ? frameCls(s[i - 1]).key + '.' + frameCls(s[i - 1]).method : '(self)';
      // the venue family scores by target: split so the row maps to fewer concrete types
      if (kind === 'family' && key === 'MetaInteractionScoredByVenue`3') label += ` (target ${/^(\w+)\.HowMuchItWantsSimToCome/.exec(callee)?.[1] ?? 'unknown'})`;
    }
    if (!label) {
      const sf = s.findIndex((x) => x.includes('ScoringFunctionDelegate.Invoke'));
      if (sf > 0 && sf < c) { const t = frameCls(s[sf - 1]); label = `scoring function ${t.key}.${t.method}`; kind = 'scoring-function'; how = 'name'; }
    }
    if (!label) {
      const inner = s.slice(0, c).reverse().map((x) => frameCls(x)).find((x) => !BASE.has(x.key) && !/TestAndCalculateScore|InteractionInstanceParameters/.test(x.key + '.' + x.method));
      label = `base path: ${inner ? inner.key + '.' + inner.method : 'C2 self'}`; kind = 'base'; how = '-';
    }
    const r = rows.get(label) ?? { kind, samples: 0, pre: 0, post: 0, cls, how, callee: new Map(), ctx: new Map() };
    r.samples++; post ? r.post++ : r.pre++;
    r.callee.set(callee, (r.callee.get(callee) ?? 0) + 1); r.ctx.set(ctx, (r.ctx.get(ctx) ?? 0) + 1);
    rows.set(label, r);
  }
}

// Concrete types behind a venue-family row: subclasses whose TTarget is the target class or derives from it.
const allCls = [...byKey.values()].flat();
const venueTypes = (target: string) => allCls.filter((c) => {
  const t = /MetaInteractionScoredByVenue<\s*\w+\s*,\s*(\w+)/.exec(c.baseText)?.[1];
  if (!t || t === 'TTarget') return false;
  return target === 'unknown' || t === target || (byKey.get(t) ?? []).some((x) => ancestors(x).includes(target));
}).map((c) => c.full);
const typesOf = (label: string, r: { kind: string; how: string; cls?: Cls }): string[] | null => {
  if (r.kind === 'base' || r.how === 'unresolved') return null;
  const fam = / \(target (\w+)\)$/.exec(label);
  if (fam) return venueTypes(fam[1]);
  return r.how === 'ambiguous' ? label.split(' | ') : [label];
};

const top = (m: Map<string, number>) => [...m].sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k} ${v}`).slice(0, 3).join('; ');
const sorted = [...rows].sort((a, b) => b[1].samples - a[1].samples);
const q = (v: string) => (/[",;]/.test(v) ? `"${v.replace(/"/g, '""')}"` : v);
const csv = ['rank,definition_type,kind,assembly,samples,share_of_scoring,samples_pre_10_16,samples_post_10_16,resolution,test_overridden,score_overridden,pure_read_guess,context,top_callees,concrete_types'];
let rank = 0;
for (const [label, r] of sorted) {
  const c = r.cls;
  const testOv = c ? (c.methods.has('Test') ? 'yes' : 'no') : '';
  const scoreOv = c ? (c.methods.has('CalculateScore') || c.bases.includes('IOverridesCalculateScore') ? 'yes' : ancestors(c).some((a) => a === 'IOverridesCalculateScore' || (byKey.get(a) ?? []).some((p) => p.methods.has('CalculateScore') && isDef(p))) ? 'inherited' : 'no') : '';
  const guess = c ? pureReadGuess((c.methods.get('Test') ?? '') + (c.methods.get('CalculateScore') ?? '')) : '';
  const fam = r.kind === 'family' ? typesOf(label, r)!.join(' | ') : '';
  csv.push([++rank, label, r.kind, c?.asm ?? '', r.samples, (r.samples / scoring).toFixed(4), r.pre, r.post, r.how, testOv, scoreOv, guess, top(r.ctx), top(r.callee), fam].map((v) => q(String(v))).join(','));
}
writeFileSync(CSV, csv.join('\n') + '\n');
console.log(JSON.stringify({ files: files.length, total, stacked, autonomy, scoring, ctx: Object.fromEntries(ctxCount), rows: rows.size }));
// Coverage: rows in rank order, union of concrete types; base-path and unresolved rows cannot be targeted.
const own = sorted.filter(([l, r]) => typesOf(l, r));
const ownTotal = own.reduce((a, [, r]) => a + r.samples, 0);
const types = new Set<string>(); let cum = 0; const marks = [0.5, 0.8, 0.9, 0.95, 1]; let mi = 0;
for (const [l, r] of own) {
  for (const t of typesOf(l, r)!) types.add(t);
  cum += r.samples;
  while (mi < marks.length && cum / ownTotal >= marks[mi] - 1e-9) console.log(`coverage ${marks[mi] * 100}% of resolvable own time: ${types.size} types (${cum} samples, ${(cum / scoring * 100).toFixed(1)}% of scoring)`), mi++;
}
const kinds = new Map<string, number>(); for (const [l, r] of sorted) { const k = r.kind === 'base' ? 'base' : r.how === 'unresolved' ? 'unresolved' : 'resolvable'; kinds.set(k, (kinds.get(k) ?? 0) + r.samples); }
console.log(JSON.stringify(Object.fromEntries(kinds)));
