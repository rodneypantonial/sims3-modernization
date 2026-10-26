// CC library lots by file rewrite (docs/200 §10): takes an installed lot (a downloaded .Sims3Pack or a Library ebf_*.package,
// read only), points every EA object and Build product it uses at a tools/cc-gen.ts clone of the SAME product (the set
// manifests record each clone's source key), and writes Library-ready ebf packages. Output holds EA-derived bytes: only under
// game-copy/cc-synth/ (git-ignored), never committed.
//
//   node tools/cc-lot-gen.ts [--count 6] [--seed 1] [--source a.Sims3Pack,b.package] [--sets game-copy/cc-synth/cc-2k-v2,...]
//                            [--out game-copy/cc-synth/lots-cc] [--top-up game-copy/cc-synth/cc-lots-topup] [--game <Steam TS3 dir>] [--list]
//   node tools/cc-lot-gen.ts --self-check        synthetic fixture (no EA bytes): plan, rewrite, read-back
// Default sources: every lot .Sims3Pack in <Documents>/Downloads (one embedded .package). Lot n uses source n mod S and clone
// pick variant floor(n / S), so lots from the same source get different clones. --list prints the plan per source, writes nothing.
// --top-up <dir>: first clones (cc-gen cloneItem) every product the lots use that no set has, as one more set in <dir>, so every
// product gets a same-source clone; the queue must load that set too.
//
// Format [verified static 2026-10-26 on both installed lots, docs/200 §10]: a lot package is DBPF; the lot record 0x34E5247C
// holds u32 count + a TGI key table (MODL 0x01661233 + OBJD 0x319E4F1D per product, objects point into it) and an ITG table
// of the package's own per-object presets (0x044AE110 complate XML, 0x033A1435 TXTC, 0x0341ACC9); the reference store
// 0x05ED1226 holds TGI keys of the Build products (CWAL patterns, CFEN fences, CSTR stairs, CRAL railings, CTPT terrain
// paints) next to IMG / complate keys; presets name textures as text "key:00B2D882:00000000:<inst>"; 0x73E93EEB is the
// launcher manifest (keylist = every key the lot uses, lotThumbs by lot instance, lotName); 0xD063545B holds the lot name
// (u32 chars + UTF-16). Every own-resource reference is a 16-byte key (no bare instance found in either lot), no checksum
// field was seen [inferred: the first game load is the check]. Rewrite = same-length key swaps in place, own resources
// renamed per output lot (new lot instance = new library entry), name spliced, launcher marker added (install-lot.ts).
// Changed resources are stored uncompressed (no RefPack encoder; the launcher stores some that way too, docs/181 §6); the
// rest keep their bytes. Mapping per product: a clone of the same source (geometry, footprint and slots match) -> its root
// key, and every closure key of that source (MODL, IMG, ...) -> the clone's copy (cc-gen cloneInst), dropped when two picks
// claim the same EA key; else a same-category clone (OBJD: equal footprint bytes and same door / window / object kind;
// CWAL: wall or floor; fence / stairs / railing / terrain: same type [inferred compatible]); else the EA product stays.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { homedir, tmpdir } from 'node:os';
import { basename, join, resolve, sep } from 'node:path';
import { parseArgs } from 'node:util';
import { readIndex, refpack } from './extract-assemblies.ts';
import { fnv64, writePackage, type Resource } from './dbpf.ts';
import { Index, T, cloneItem, closure, englishNames, keyOf, kindOf, scanRefs, verifyPackage, type Kind } from './cc-gen.ts';
import { buildEbf, readSims3Pack } from './install-lot.ts';

const ROOT = resolve(import.meta.dirname, '..');
const LOT = 0x34e5247c, REFS = 0x05ed1226, PRESET = 0x044ae110, NAME = 0xd063545b, MANIFEST = 0x73e93eeb, MARKER = 0x0679b9b8;
const PRODUCT = new Map<number, string>([[T.OBJD, 'objd'], [T.CWAL, 'cwal'], [T.CFEN, 'cfen'], [T.CSTR, 'cstr'], [T.CRAL, 'cral'], [T.CTPT, 'ctpt']]);
const hex = (n: number | bigint) => n.toString(16);
// cc-gen cloneItem's instance rule (generateLots checks it against every manifest root before use)
export const cloneInst = (seed: string, kind: Kind, i: number, old: bigint) => fnv64(`cc-gen/${seed}/${kind}/${i}/${hex(old)}`);
const GAME_PKGS = ['FullBuild0', 'FullBuild1', 'FullBuild2', 'DeltaBuild0', 'DeltaBuild1', 'DeltaBuild2'].map(p => `GameData/Shared/Packages/${p}`);

type Res = { type: number; group: number; inst: bigint; raw: Buffer; compressed: number; memSize: number };
const body = (r: Res) => r.compressed === 0xffff ? refpack(r.raw) : r.raw;
export type Lot = { file: string; name: string; res: Res[]; lotInst: bigint };
export type Clone = { set: string; seed: string; kind: Kind; i: number; source: string; root: string; name: string; pkg: string };
type Pick = { clone: Clone; how: 'same' | 'fallback' };

export function readLot(file: string, buf = readFileSync(file)): Lot {
  let pkg = buf;
  if (buf.toString('latin1', 4, 11) === 'TS3Pack') {
    const s = readSims3Pack(buf), p = s.files.filter(f => f.name.toLowerCase().endsWith('.package'));
    if (s.type !== 'lot' || p.length !== 1) throw new Error(`${file}: not a CC-free lot pack (type ${s.type}, ${p.length} packages)`);
    pkg = p[0].data;
  }
  const res = readIndex(pkg).filter(e => e.type !== MARKER)
    .map(e => ({ type: e.type, group: e.group, inst: e.inst, raw: pkg.subarray(e.offset, e.offset + e.fileSize), compressed: e.compressed, memSize: e.memSize }));
  const lots = res.filter(r => r.type === LOT);
  if (lots.length !== 1) throw new Error(`${file}: ${lots.length} lot records 0x34E5247C`);
  const man = res.find(r => r.type === MANIFEST), name = man && /<lotName>([^<]*)<\/lotName>/.exec(body(man).toString('utf8'))?.[1];
  if (!name) throw new Error(`${file}: no <lotName> in 0x73E93EEB`);
  return { file, name, res, lotInst: lots[0].inst };
}

// kinds whose clones follow cc-gen's plain instance rule; 'presets' clones give each material its own instance (not mapped), CAS kinds never sit in a lot
const LOT_KINDS = new Set(['object', 'inv', 'door', 'window', 'wall', 'floor', 'fence', 'terrain', 'railing', 'stairs']);
export function readSets(dirs: string[]): Clone[] {
  return dirs.flatMap(d => {
    const m = JSON.parse(readFileSync(join(d, 'manifest.json'), 'utf8')) as { seed: string; items: any[] };
    return m.items.filter(i => LOT_KINDS.has(i.kind)).map(i => ({ set: basename(d), seed: String(m.seed), kind: i.kind, i: i.i, source: i.source, root: i.root, name: i.name, pkg: join(d, i.package ?? '') }));
  });
}

// category for the same-category fallback: OBJD by internal model name + footprint bytes, CWAL by wall / floor
function categoryOf(ix: Index, names: Map<bigint, string>, key: string, cache: Map<string, string>) {
  if (cache.has(key)) return cache.get(key)!;
  const e = ix.byKey.get(key);
  let c = '';
  if (e && e.type === T.OBJD) {
    const k = kindOf(e.type, ix.body(e), names), ft = [...closure(ix, e).values()].filter(n => n.e.type === T.FTPT).map(n => n.body!.toString('hex')).sort();
    c = ft.length ? `objd/${k === 'inv' ? 'object' : k}/${createHash('sha1').update(ft.join()).digest('hex')}` : ''; // no footprint: no fallback
  } else if (e && e.type === T.CWAL) c = `cwal/${kindOf(e.type, ix.body(e), names) ?? ''}`;
  else if (e) c = PRODUCT.get(e.type) ?? '';
  cache.set(key, c);
  return c;
}

// product keys a lot uses (lot record key table + reference store), in first-seen order
export function lotProducts(ix: Index, lot: Lot) {
  const out = new Set<string>();
  for (const r of lot.res) if (r.type === LOT || r.type === REFS) for (const x of refsAt(body(r), new Set(PRODUCT.keys()), k => ix.byKey.has(k))) out.add(x.key);
  return [...out];
}

export function planLot(ix: Index, names: Map<bigint, string>, lot: Lot, clones: Clone[], variant: number, cache = new Map<string, string>()) {
  const bySource = new Map<string, Clone[]>(), used = new Set<string>(), picks = new Map<string, Pick>();
  for (const c of clones) (bySource.get(c.source) ?? bySource.set(c.source, []).get(c.source)!).push(c);
  const rot = <X>(l: X[], n: number) => l.map((_, j) => l[(j + n) % l.length]);
  lotProducts(ix, lot).forEach((key, ord) => {
    const same = rot(bySource.get(key) ?? [], variant * 7 + ord).find(c => !used.has(c.root));
    let pick: Pick | undefined = same && { clone: same, how: 'same' };
    if (!pick) {
      const cat = categoryOf(ix, names, key, cache);
      const fb = cat ? rot(clones.filter(c => categoryOf(ix, names, c.source, cache) === cat), variant * 7 + ord).find(c => !used.has(c.root)) : undefined;
      if (fb) pick = { clone: fb, how: 'fallback' };
    }
    if (pick) { used.add(pick.clone.root); picks.set(key, pick); }
  });
  // closure keys of same-source picks -> the clone's copy; an EA key two picks claim stays EA's (the lot shares it)
  const sub = new Map<string, { inst: bigint; clone: Clone }>(), claimed = new Map<string, number>();
  for (const [key, p] of picks) {
    if (p.how !== 'same') continue;
    for (const n of closure(ix, ix.byKey.get(key)!).values()) {
      if (n.e === ix.byKey.get(key)) continue;
      const k = keyOf(n.e.type, n.e.group, n.e.inst);
      claimed.set(k, (claimed.get(k) ?? 0) + 1);
      sub.set(k, { inst: cloneInst(p.clone.seed, p.clone.kind, p.clone.i, n.e.inst), clone: p.clone });
    }
  }
  const conflicts = [...claimed].filter(([, n]) => n > 1).map(([k]) => k);
  for (const k of conflicts) sub.delete(k);
  return { picks, sub, conflicts: conflicts.length };
}

// scanRefs tries 4 key orders, so one key can match twice at the same instance offset (an inst followed by the next key's
// type + group reads as ITG): one hit per offset
const refsAt = (b: Buffer, types: Set<number>, has: (k: string) => boolean) => [...new Map(scanRefs(b, types, has).map(x => [x.at, x])).values()];
const instOf = (key: string) => BigInt(`0x${key.split(':')[2]}`);
const keyParts = (key: string) => key.split(':').map(x => parseInt(x, 16));

// one output lot: own resources renamed, product / closure keys swapped, name spliced, ebf marker added
export function rewriteLot(ix: Index, lot: Lot, plan: ReturnType<typeof planLot>, tag: string, newName: string) {
  const ren = (i: bigint) => i > 0xffffffffn ? fnv64(`cc-lot-gen/${tag}/${hex(i)}`) : i; // inst 0 (manifest) stays
  const own = new Set(lot.res.map(r => keyOf(r.type, r.group, r.inst)));
  const map = (k: string): bigint | undefined => own.has(k) ? ren(instOf(k)) : plan.picks.has(k) ? instOf(plan.picks.get(k)!.clone.root) : plan.sub.get(k)?.inst;
  const types = new Set([...ix.types, ...lot.res.map(r => r.type)]);
  const swaps: Record<string, number> = {};
  const count = (what: string) => { swaps[what] = (swaps[what] ?? 0) + 1; };
  const what = (k: string) => own.has(k) ? 'own' : plan.picks.has(k) ? `product:${PRODUCT.get(keyParts(k)[0])}:${plan.picks.get(k)!.how}` : `closure:${hex(keyParts(k)[0])}`;
  const textKey = (s: string, re: RegExp, fmt: (t: string, g: string, n: bigint) => string) => s.replace(re, (all, t: string, g: string, i: string) => {
    const k = keyOf(parseInt(t, 16), parseInt(g, 16), BigInt(`0x${i}`)), n = map(k);
    if (n === undefined) return all;
    count(what(k));
    return fmt(t, g, n);
  });
  const out: Resource[] = [];
  for (const r of lot.res) {
    let b = body(r), changed = false;
    if (r.type === PRESET) { // UTF-16 XML after an 8-byte header; same-length swaps keep the header's char count valid
      const s = b.subarray(8).toString('utf16le');
      const t = textKey(s, /key:([0-9A-Fa-f]{8}):([0-9A-Fa-f]{8}):([0-9A-Fa-f]{16})/g, (ty, g, n) => `key:${ty}:${g}:${hex(n).padStart(16, '0').toUpperCase()}`);
      if (t !== s) { b = Buffer.concat([b.subarray(0, 8), Buffer.from(t, 'utf16le')]); changed = true; }
    } else if (r.type === MANIFEST) { // launcher manifest: keylist, lot thumbnails, names, package id
      let s = b.toString('utf8');
      s = s.replace(/<reskey>([01]):([0-9a-f]{8}):([0-9a-f]{8}):([0-9a-f]+)<\/reskey>/g, (all, p: string, t: string, g: string, i: string) => {
        const k = keyOf(parseInt(t, 16), parseInt(g, 16), BigInt(`0x${i}`)), n = map(k);
        if (n === undefined) return all;
        count(what(k));
        return `<reskey>${p}:${t}:${g}:${hex(n)}</reskey>`;
      });
      s = s.replace(/(<lotThumb>[0-9a-f]{8}-[0-9a-f]{8}-)([0-9a-f]{16})(<\/lotThumb>)/g, (all, a, i, z) => BigInt(`0x${i}`) === lot.lotInst ? `${a}${hex(ren(lot.lotInst)).padStart(16, '0')}${z}` : all);
      s = s.split(`>${lot.name}<`).join(`>${newName}<`);
      s = s.replace(/(<packageid>0x)([0-9a-f]{32})(<\/packageid>)/g, (all, a, id, z) => id.startsWith('050cffe8') ? all : `${a}${(hex(fnv64(`cc-lot-gen/${tag}/pkg`)).padStart(16, '0')).repeat(2)}${z}`);
      b = Buffer.from(s, 'utf8'); changed = true;
    } else if (r.type === NAME) { // u32 chars + UTF-16 name, first match of the old name
      const old = Buffer.concat([Buffer.alloc(4), Buffer.from(lot.name, 'utf16le')]); old.writeUInt32LE(lot.name.length);
      const at = b.indexOf(old);
      if (at < 0) throw new Error(`${lot.file}: lot name not found in 0xD063545B`);
      const nw = Buffer.concat([Buffer.alloc(4), Buffer.from(newName, 'utf16le')]); nw.writeUInt32LE(newName.length);
      b = Buffer.concat([b.subarray(0, at), nw, b.subarray(at + old.length)]); changed = true;
    } else {
      const refs = refsAt(b, types, k => own.has(k) || ix.byKey.has(k)).filter(x => map(x.key) !== undefined);
      if (refs.length) {
        b = Buffer.from(b);
        for (const x of refs) { b.writeBigUInt64LE(map(x.key)!, x.at); count(what(x.key)); }
        changed = true;
      }
    }
    out.push({ type: r.type, group: r.group, instance: ren(r.inst), data: changed ? b : r.raw, ...(!changed && r.compressed === 0xffff ? { memSize: r.memSize } : {}) });
  }
  return { ebf: buildEbf(writePackage(out)), swaps, lotInst: ren(lot.lotInst) };
}

// read-back: every body decodes at its size; one lot record + marker; no old own instance left; every product key is EA's or a set clone
export function verifyLot(ix: Index, ebf: Buffer, oldOwn: Set<bigint>, cloneRoots: Set<string>) {
  const problems: string[] = [], products: Record<string, { cc: number; ea: number }> = {};
  const ixe = readIndex(ebf), own = new Set(ixe.map(e => keyOf(e.type, e.group, e.inst)));
  if (own.size !== ixe.length) problems.push('duplicate key');
  const lot = ixe.filter(e => e.type === LOT);
  if (lot.length !== 1) problems.push(`${lot.length} lot records`);
  if (!ixe.some(e => e.type === MARKER && lot[0] && e.inst === lot[0].inst)) problems.push('no launcher marker at the lot instance');
  const has = (k: string) => own.has(k) || ix.byKey.has(k) || cloneRoots.has(k);
  for (const e of ixe) {
    const raw = ebf.subarray(e.offset, e.offset + e.fileSize), b = e.compressed === 0xffff ? refpack(raw) : raw;
    if (b.length !== e.memSize) problems.push(`${keyOf(e.type, e.group, e.inst)}: size ${b.length} != ${e.memSize}`);
    if (oldOwn.has(e.inst)) problems.push(`${keyOf(e.type, e.group, e.inst)}: not renamed`);
    if (e.type === LOT || e.type === REFS) {
      for (const x of refsAt(b, new Set(PRODUCT.keys()), has)) { const p = (products[PRODUCT.get(x.type)!] ??= { cc: 0, ea: 0 }); if (cloneRoots.has(x.key)) p.cc++; else p.ea++; }
      // a product key (TGI, group 0) that is neither EA's nor a set clone = a broken swap
      for (let p = 0; p + 16 <= b.length; p++) if (PRODUCT.has(b.readUInt32LE(p)) && b.readUInt32LE(p + 4) === 0) {
        const k = keyOf(b.readUInt32LE(p), 0, b.readBigUInt64LE(p + 8));
        if (!has(k) && b.readBigUInt64LE(p + 8) > 0xffffffffn) problems.push(`${keyOf(e.type, e.group, e.inst)}: unknown product key ${k} at ${p}`);
      }
    }
    if (e.type === PRESET || e.type === MANIFEST || e.type === NAME) continue;
    for (let p = 0; p + 8 <= b.length; p++) if (oldOwn.has(b.readBigUInt64LE(p))) { problems.push(`${keyOf(e.type, e.group, e.inst)}: old own instance at ${p}`); break; }
  }
  return { problems, products, resources: ixe.length };
}

export function generateLots(ix: Index, names: Map<bigint, string>, lots: Lot[], clones: Clone[], o: { count: number; seed: string; out?: string }) {
  const cloneRoots = new Set(clones.map(c => c.root)), cache = new Map<string, string>(), rows: any[] = [], pkgKeys = new Map<string, Set<string>>();
  for (const c of clones) { // the manifest root must be cc-gen's own instance rule, else the closure mapping is wrong
    const [t, g, i] = keyParts(c.source), want = keyOf(t, g, cloneInst(c.seed, c.kind, c.i, BigInt(`0x${c.source.split(':')[2]}`)));
    if (want !== c.root) throw new Error(`${c.set} item ${c.i}: root ${c.root} != cc-gen rule ${want}`);
  }
  if (o.out) { mkdirSync(o.out, { recursive: true }); for (const f of readdirSync(o.out)) if (/^ebf_0x[0-9a-f]{16}\.package$/.test(f)) rmSync(join(o.out, f)); }
  for (let n = 0; n < o.count; n++) {
    const lot = lots[n % lots.length], variant = Math.floor(n / lots.length), tag = `${o.seed}/${n}`;
    const plan = planLot(ix, names, lot, clones, variant, cache);
    const name = `CCLot ${String(n + 1).padStart(2, '0')} ${lot.name}`;
    const { ebf, swaps, lotInst } = rewriteLot(ix, lot, plan, tag, name);
    const v = verifyLot(ix, ebf, new Set(lot.res.map(r => r.inst).filter(i => i > 0xffffffffn)), cloneRoots);
    const how: Record<string, Record<string, number>> = {};
    for (const key of lotProducts(ix, lot)) { const t = PRODUCT.get(keyParts(key)[0])!, p = plan.picks.get(key); (how[t] ??= { products: 0, same: 0, fallback: 0, ea: 0 }).products++; how[t][p ? p.how : 'ea']++; }
    // clone packages on disk (sets not moved into Mods by a running queue): root and every mapped closure key must be in them
    const want = new Map<string, Set<string>>(); // package -> keys
    for (const p of plan.picks.values()) (want.get(p.clone.pkg) ?? want.set(p.clone.pkg, new Set()).get(p.clone.pkg)!).add(p.clone.root);
    for (const [k, x] of plan.sub) { const [t, g] = keyParts(k); want.get(x.clone.pkg)!.add(keyOf(t, g, x.inst)); }
    let pkgChecked = 0, keysChecked = 0;
    for (const [pkg, keys] of want) {
      if (!existsSync(pkg)) continue;
      const have = pkgKeys.get(pkg) ?? pkgKeys.set(pkg, new Set(readIndex(readFileSync(pkg)).map(e => keyOf(e.type, e.group, e.inst)))).get(pkg)!;
      pkgChecked++;
      for (const k of keys) { keysChecked++; if (!have.has(k)) v.problems.push(`${basename(pkg)}: clone key ${k} missing`); }
    }
    const file = `ebf_0x${hex(fnv64(`cc-lot-gen/${tag}/ebf`)).padStart(16, '0')}.package`;
    if (o.out) writeFileSync(join(o.out, file), ebf);
    rows.push({ n: n + 1, name, file, source: basename(lot.file), sourceName: lot.name, variant, lotInst: hex(lotInst).padStart(16, '0'), bytes: ebf.length,
      resources: v.resources, products: how, refsAfter: v.products, swaps, closureConflicts: plan.conflicts, clonePackagesChecked: `${pkgChecked} of ${want.size} (${keysChecked} keys)`,
      clones: [...plan.picks].map(([k, p]) => ({ ea: k, cc: p.clone.root, set: p.clone.set, how: p.how })), problems: v.problems });
  }
  const m = { tool: 'cc-lot-gen', seed: o.seed, count: o.count, time: new Date().toISOString(), sets: [...new Set(clones.map(c => c.set))], lots: rows,
    problems: rows.flatMap(r => r.problems.map((p: string) => `lot ${r.n}: ${p}`)) };
  if (o.out) writeFileSync(join(o.out, 'manifest.json'), JSON.stringify(m, null, 1));
  return m;
}

// top-up set: one cc-gen clone (cloneItem, unchanged) per product the lots use that no set clones, so the lots are CC
// throughout; a cc-gen-style set (flat dir, manifest.json) the queue loads next to the others
export function topUp(ix: Index, names: Map<bigint, string>, lots: Lot[], clones: Clone[], dir: string, seed: string): Clone[] {
  const have = new Set(clones.map(c => c.source));
  const want = [...new Set(lots.flatMap(l => lotProducts(ix, l)))].filter(k => !have.has(k));
  mkdirSync(dir, { recursive: true });
  for (const f of readdirSync(dir)) if (/^ccgen-.*\.package$/.test(f)) rmSync(join(dir, f));
  const items: any[] = [], problems: string[] = [], skipped: string[] = [];
  for (const k of want) {
    const e = ix.byKey.get(k)!, kind = kindOf(e.type, ix.body(e), names);
    if (!kind) { skipped.push(`${k}: no kind (CWAL without wall / floor text)`); continue; }
    let c: ReturnType<typeof cloneItem>;
    try { c = cloneItem(ix, e, items.length, seed, names, kind); } catch (x) { skipped.push(`${k}: ${String(x).split('\n')[0]}`); continue; } // e.g. no catalogue name: not a buyable product
    const { res, item } = c;
    const oldInsts = new Set([...closure(ix, e).values()].map(n => n.e.inst)); // per item, as cc-gen generate
    const pkg = writePackage(res), file = `ccgen-${seed}-${kind}-${String(item.i).padStart(5, '0')}.package`;
    const bad = verifyPackage(pkg, ix, oldInsts).problems;
    if (bad.length) { skipped.push(`${k}: cc-gen read-back: ${bad[0]}`); continue; } // a reference form cc-gen does not know: product stays EA
    writeFileSync(join(dir, file), pkg);
    items.push({ ...item, package: file });
  }
  writeFileSync(join(dir, 'manifest.json'), JSON.stringify({ tool: 'cc-lot-gen --top-up', seed, count: items.length, time: new Date().toISOString(),
    totalDiskBytes: items.reduce((a, i) => a + i.diskBytes, 0), problems, skipped, items }, null, 1));
  if (problems.length) throw new Error(`top-up: ${problems.length} problems, first: ${problems[0]}`);
  console.log(`top-up: ${skipped.length} products left EA (${skipped.slice(0, 3).join('; ')})`);
  return readSets([dir]);
}

const summary = (r: any) => `${r.name}: ${Object.entries(r.products as Record<string, any>).map(([t, h]) => `${t} ${h.same}+${h.fallback}/${h.products}`).join(', ')}; clone packages checked ${r.clonePackagesChecked}; swaps ${Object.entries(r.swaps).map(([k, v]) => `${k} ${v}`).join(', ')}; ${r.problems.length} problems`;

function selfCheck() {
  const dir = mkdtempSync(join(tmpdir(), 'cc-lot-gen-'));
  try {
    const tgi = (t: number, g: number, i: bigint) => { const b = Buffer.alloc(16); b.writeUInt32LE(t, 0); b.writeUInt32LE(g, 4); b.writeBigUInt64LE(i, 8); return b; };
    const itg = (t: number, g: number, i: bigint) => { const b = Buffer.alloc(16); b.writeBigUInt64LE(i, 0); b.writeUInt32LE(t, 8); b.writeUInt32LE(g, 12); return b; };
    const I = { objd: 0x350n, objd2: 0x351n, objd3: 0x352n, modl: 0x1111222233334444n, ftpt: 0x5555666677778888n, img: 0x0abc0abc0abc0abcn, cwal: 0x0c0a1c0a1c0a1c0an, lot: 0x5ea100703e3948d0n, preset: 0x7e7e7e7e7e7e7e7en, txtc: 0x7c7c7c7c7c7c7c7cn };
    const game = writePackage([ // objd: footprint F; objd2: same footprint F (fallback target); objd3: other footprint
      { type: T.OBJD, group: 0, instance: I.objd, data: Buffer.concat([Buffer.alloc(8), tgi(T.MODL, 1, I.modl), tgi(T.FTPT, 0, I.ftpt), tgi(T.IMG, 0, I.img)]) },
      { type: T.OBJD, group: 0, instance: I.objd2, data: Buffer.concat([Buffer.alloc(8), tgi(T.FTPT, 0, I.ftpt + 1n)]) },
      { type: T.OBJD, group: 0, instance: I.objd3, data: Buffer.concat([Buffer.alloc(8), tgi(T.FTPT, 0, I.ftpt + 2n)]) },
      { type: T.MODL, group: 1, instance: I.modl, data: Buffer.from('model') },
      { type: T.FTPT, group: 0, instance: I.ftpt, data: Buffer.from('footprint A') },
      { type: T.FTPT, group: 0, instance: I.ftpt + 1n, data: Buffer.from('footprint A') },
      { type: T.FTPT, group: 0, instance: I.ftpt + 2n, data: Buffer.from('footprint B') },
      { type: T.IMG, group: 0, instance: I.img, data: Buffer.from('dds') },
      { type: T.CWAL, group: 0, instance: I.cwal, data: Buffer.from('pattern') },
    ]);
    const gp = join(dir, 'game.package'); writeFileSync(gp, game);
    const ix = new Index().add(gp);
    const clone = (set: string, kind: Kind, i: number, src: bigint, t = T.OBJD): Clone => ({ set, seed: '1', kind, i, source: keyOf(t, 0, src), root: keyOf(t, 0, cloneInst('1', kind, i, src)), name: 'x', pkg: join(dir, `${set}-${i}.package`) });
    const clones = [clone('objs', 'object', 0, I.objd), clone('objs', 'object', 1, I.objd), clone('objs', 'object', 2, I.objd2), clone('build', 'wall', 0, I.cwal, T.CWAL)];
    // clone 0's package on disk without its IMG copy: the package check must report exactly that key; the other clones have none (unchecked)
    const ci = (old: bigint) => cloneInst('1', 'object', 0, old);
    writeFileSync(clones[0].pkg, writePackage([[T.OBJD, 0, I.objd], [T.MODL, 1, I.modl], [T.FTPT, 0, I.ftpt]].map(([t, g, i]) => ({ type: t as number, group: g as number, instance: ci(i as bigint), data: Buffer.from('x') }))));
    const name = 'Test Lot', nameRec = Buffer.concat([Buffer.from([0x45, 0, 0, 0, 1, 0, 0, 0, name.length, 0, 0, 0]), Buffer.from(name, 'utf16le'), Buffer.from([4, 0, 0, 0]), Buffer.from('desc', 'utf16le')]);
    const lotRec = Buffer.concat([Buffer.from('head'), Buffer.from([4, 0, 0, 0]), tgi(T.MODL, 1, I.modl), tgi(T.OBJD, 0, I.objd), tgi(T.OBJD, 0, I.objd2), tgi(T.OBJD, 0, I.objd3),
      Buffer.from([2, 0, 0, 0]), itg(PRESET, 0, I.preset), itg(0x033a1435, 0, I.txtc), Buffer.from('tail')]);
    const preset = Buffer.concat([Buffer.from([2, 0, 0, 0, 0, 0, 0, 0]), Buffer.from(`<preset><param value="key:00B2D882:00000000:${hex(I.img).padStart(16, '0').toUpperCase()}"/></preset>`, 'utf16le')]);
    const manifest = `<manifest><packageid>0x${'ab'.repeat(16)}</packageid><dependencylist><packageid>0x050cffe800000000050cffe800000000</packageid></dependencylist><keylist><reskey>0:319e4f1d:00000000:350</reskey><reskey>1:044ae110:00000000:${hex(I.preset)}</reskey></keylist><lotThumbs><lotThumb>d84e7fc7-00000002-${hex(I.lot)}</lotThumb></lotThumbs><lotName>${name}</lotName></manifest>`;
    const pkg = writePackage([
      { type: LOT, group: 0, instance: I.lot, data: lotRec },
      { type: REFS, group: 0, instance: I.lot, data: Buffer.concat([Buffer.from('r'), tgi(T.CWAL, 0, I.cwal), tgi(T.IMG, 0, I.img)]) },
      { type: NAME, group: 0, instance: I.lot, data: nameRec },
      { type: 0x312e7545, group: 0x2e7b1a, instance: I.lot, data: Buffer.from('walls, no keys') },
      { type: PRESET, group: 0, instance: I.preset, data: preset },
      { type: 0x033a1435, group: 0, instance: I.txtc, data: Buffer.concat([Buffer.from('tx'), itg(T.IMG, 0, I.img)]) },
      { type: MANIFEST, group: 0, instance: 0n, data: Buffer.from(manifest, 'utf8') },
    ]);
    const lot = readLot(join(dir, 'lot.package'), pkg);
    assert.equal(lot.name, name);
    assert.deepEqual(lotProducts(ix, lot), [keyOf(T.OBJD, 0, I.objd), keyOf(T.OBJD, 0, I.objd2), keyOf(T.OBJD, 0, I.objd3), keyOf(T.CWAL, 0, I.cwal)]);
    const plan = planLot(ix, englishNames(ix), lot, clones, 0);
    assert.equal(plan.picks.get(keyOf(T.OBJD, 0, I.objd))!.how, 'same');
    assert.equal(plan.picks.get(keyOf(T.OBJD, 0, I.objd2))!.how, 'same');
    assert.ok(!plan.picks.has(keyOf(T.OBJD, 0, I.objd3)), 'other footprint: no fallback, EA stays');
    assert.equal(plan.picks.get(keyOf(T.CWAL, 0, I.cwal))!.how, 'same');
    assert.equal(planLot(ix, englishNames(ix), lot, clones, 1).picks.get(keyOf(T.OBJD, 0, I.objd))!.clone.i, 1, 'variant 1 picks the other clone');
    // fallback: without clones of objd2, objd's spare clone (same footprint bytes) takes it
    const fb = planLot(ix, englishNames(ix), lot, clones.filter(c => c.source !== keyOf(T.OBJD, 0, I.objd2)), 0);
    assert.equal(fb.picks.get(keyOf(T.OBJD, 0, I.objd2))!.how, 'fallback');
    assert.equal(fb.picks.get(keyOf(T.OBJD, 0, I.objd2))!.clone.source, keyOf(T.OBJD, 0, I.objd));
    const out = join(dir, 'game-copy', 'cc-synth', 'lots');
    const m = generateLots(ix, englishNames(ix), [lot], clones, { count: 2, seed: 't', out });
    assert.deepEqual(m.problems, [`lot 1: objs-0.package: clone key ${keyOf(T.IMG, 0, ci(I.img))} missing`], 'package check finds the missing clone texture');
    const r = m.lots[0];
    assert.equal(r.clonePackagesChecked, '1 of 3 (4 keys)');
    assert.deepEqual(r.products.objd, { products: 3, same: 2, fallback: 0, ea: 1 });
    assert.deepEqual(r.refsAfter.objd, { cc: 2, ea: 1 });
    assert.deepEqual(r.refsAfter.cwal, { cc: 1, ea: 0 });
    const ebf = readFileSync(join(out, r.file)), e = readIndex(ebf), get = (t: number) => { const x = e.find(y => y.type === t)!; return ebf.subarray(x.offset, x.offset + x.fileSize); };
    const modlNew = cloneInst('1', 'object', 0, I.modl), imgNew = cloneInst('1', 'object', 0, I.img);
    assert.ok(get(LOT).includes(tgi(T.MODL, 1, modlNew)), 'MODL of a same-source pick -> the clone copy');
    assert.ok(get(LOT).includes(itg(PRESET, 0, fnv64(`cc-lot-gen/t/0/${hex(I.preset)}`))), 'own preset key renamed in the lot record');
    assert.ok(get(PRESET).subarray(8).toString('utf16le').includes(hex(imgNew).padStart(16, '0').toUpperCase()), 'preset texture -> clone texture');
    assert.ok(get(0x033a1435).includes(itg(T.IMG, 0, imgNew)), 'TXTC texture -> clone texture');
    assert.ok(get(NAME).includes(Buffer.from('CCLot 01 Test Lot', 'utf16le')) && get(NAME).includes(Buffer.from('desc', 'utf16le')), 'name spliced, description kept');
    const man = get(MANIFEST).toString('utf8');
    assert.ok(man.includes(`<lotName>CCLot 01 Test Lot</lotName>`) && man.includes(`0:319e4f1d:00000000:${hex(cloneInst('1', 'object', 0, I.objd))}`) && man.includes('050cffe8'), 'manifest names, keylist, base dependency');
    assert.ok(man.includes(`<lotThumb>d84e7fc7-00000002-${hex(fnv64('cc-lot-gen/t/0/' + hex(I.lot))).padStart(16, '0')}</lotThumb>`), 'lot thumbnail key follows the lot instance');
    assert.ok(man.includes(`<reskey>1:044ae110:00000000:${hex(fnv64(`cc-lot-gen/t/0/${hex(I.preset)}`))}</reskey>`), 'own key keeps prefix 1');
    assert.ok(get(0x312e7545).equals(Buffer.from('walls, no keys')), 'untouched resource byte-identical');
    assert.notEqual(m.lots[1].lotInst, r.lotInst, 'each output lot its own instance');
    assert.notEqual(m.lots[1].clones[0].cc, r.clones[0].cc, 'variant 1 uses other clones');
    // a swapped product key that is neither EA's nor a clone: verifyLot reports it (the changed lot record is stored uncompressed)
    const broken = Buffer.from(ebf), at = broken.indexOf(tgi(T.OBJD, 0, I.objd3));
    broken.writeBigUInt64LE(0x1234567890abcdefn, at + 8);
    assert.ok(verifyLot(ix, broken, new Set(), new Set(clones.map(c => c.root))).problems.some(p => /unknown product key 319e4f1d:0:1234567890abcdef/.test(p)), 'unknown product key caught');
    ix.close();
  } finally { rmSync(dir, { recursive: true, force: true }); }
  console.log('cc-lot-gen self-check ok');
}

if (process.argv[1]?.endsWith('cc-lot-gen.ts')) {
  const { values: v } = parseArgs({ options: { count: { type: 'string', default: '6' }, seed: { type: 'string', default: '1' }, source: { type: 'string' },
    sets: { type: 'string', default: 'game-copy/cc-synth/cc-2k-v2,game-copy/cc-synth/cc-build-200' }, out: { type: 'string', default: 'game-copy/cc-synth/lots-cc' },
    game: { type: 'string' }, 'top-up': { type: 'string' }, list: { type: 'boolean' }, 'self-check': { type: 'boolean' } } });
  if (v['self-check']) { selfCheck(); process.exit(0); }
  const docs = process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3');
  const sources = v.source ? v.source.split(',') : readdirSync(join(docs, 'Downloads')).filter(f => /\.sims3pack$/i.test(f)).map(f => join(docs, 'Downloads', f));
  const lots = sources.flatMap(f => { try { return [readLot(f)]; } catch (e) { console.log(`skip ${f}: ${String(e).split('\n')[0]}`); return []; } });
  if (!lots.length) throw new Error('no lot source');
  const out = resolve(ROOT, v.out!);
  if (!out.startsWith(join(ROOT, 'game-copy', 'cc-synth') + sep)) throw new Error('--out must be under game-copy/cc-synth/ (EA-derived output, git-ignored)');
  const gameDir = v.game ?? process.env.TS3_GAME ?? 'C:/Program Files (x86)/Steam/steamapps/common/The Sims 3';
  const ix = new Index();
  for (const p of GAME_PKGS) ix.add(join(gameDir, `${p}.package`));
  const names = englishNames(ix), clones = readSets(v.sets!.split(',').map(s => resolve(ROOT, s)));
  const t0 = Date.now();
  if (v['top-up'] && !v.list) {
    const dir = resolve(ROOT, v['top-up']);
    if (!dir.startsWith(join(ROOT, 'game-copy', 'cc-synth') + sep)) throw new Error('--top-up must be under game-copy/cc-synth/');
    const added = topUp(ix, names, lots, clones, dir, `lots${v.seed}`);
    console.log(`top-up ${v['top-up']}: ${added.length} clones of products no set had (${((Date.now() - t0) / 1000).toFixed(0)} s)`);
    clones.push(...added);
  }
  const m = generateLots(ix, names, lots, clones, { count: v.list ? lots.length : Number(v.count), seed: v.seed!, out: v.list ? undefined : out });
  ix.close();
  for (const r of m.lots) console.log(`  ${r.file} ${(r.bytes / 1e6).toFixed(2)} MB ${summary(r)}`);
  console.log(`cc-lot-gen: ${m.lots.length} lots${v.list ? ' (list only, nothing written)' : ` in ${v.out}`} (${((Date.now() - t0) / 1000).toFixed(0)} s), sets ${m.sets.join(' + ')}; products as same-source + fallback / total; problems ${m.problems.length}`);
  for (const p of m.problems.slice(0, 10)) console.log(`  ${p}`);
  process.exit(m.problems.length ? 1 : 0);
}
