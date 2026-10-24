// Installs a CC-free lot .Sims3Pack the way the launcher does, so the lot shows in Edit Town's lot bin without the launcher.
// Usage: node tools/install-lot.ts <file.Sims3Pack> [--docs <The Sims 3 Documents dir>] [--dry-run] [--force]
//        node tools/install-lot.ts --self-check [--docs <dir>]   (read-only on <dir>: rebuilds every launcher-installed lot
//        found in <dir>\Downloads into a temp dir and compares it with the launcher's Library\ebf_*.package and DCBackup copy)
// Writes Library\ebf_0x<id>.package (embedded lot package + the launcher's 0x0679B9B8 marker) and DCBackup\0x<PackageId>.package
// (byte copy of the embedded package). Does not touch DCCache\dcdb0.dbc (launcher's installed-content list). See docs/181 §6.
import { readFileSync, writeFileSync, existsSync, mkdirSync, readdirSync, mkdtempSync, rmSync } from 'node:fs';
import { join, basename } from 'node:path';
import { homedir, tmpdir } from 'node:os';
import { readIndex, refpack } from './extract-assemblies.ts';
import { fnv64 } from './dbpf.ts';

const TYPE_LOT = 0x34e5247c; // the lot record; the marker uses its instance
const TYPE_MARKER = 0x0679b9b8; // added by the launcher: group 0x02000000, data 01 00
const MARKER_GROUP = 0x02000000;

export function readSims3Pack(buf: Buffer) {
  const ml = buf.readUInt32LE(0);
  if (buf.toString('latin1', 4, 4 + ml) !== 'TS3Pack') throw new Error('not a Sims3Pack (no TS3Pack magic)');
  let p = 4 + ml + 2;
  const xl = buf.readUInt32LE(p); p += 4;
  const xml = buf.toString('utf8', p, p + xl);
  const base = p + xl;
  const tag = (s: string, t: string) => s.match(new RegExp(`<${t}>([^<]*)</${t}>`))?.[1];
  const files = [...xml.matchAll(/<PackagedFile>([\s\S]*?)<\/PackagedFile>/g)].map(m => {
    const name = tag(m[1], 'Name')!, len = Number(tag(m[1], 'Length')), off = Number(tag(m[1], 'Offset'));
    if (base + off + len > buf.length) throw new Error(`${name} runs past the end of the file`);
    return { name, data: buf.subarray(base + off, base + off + len) };
  });
  return { type: xml.match(/<Sims3Package Type="([^"]*)"/)?.[1], packageId: tag(xml, 'PackageId'), displayName: tag(xml, 'DisplayName'), files };
}

// Launcher transform: embedded package + one uncompressed 2-byte marker resource appended before the index.
export function buildEbf(pkg: Buffer): Buffer {
  const indexPos = pkg.readUInt32LE(0x40), indexSize = pkg.readUInt32LE(0x2c), count = pkg.readUInt32LE(0x24);
  // shortcut: only flat indexes (flags 0, as the game writes lots) and index-at-end layout; others are refused, not converted
  if (pkg.readUInt32LE(indexPos) !== 0 || indexPos + indexSize !== pkg.length || indexSize !== 4 + count * 32)
    throw new Error('unexpected package layout (index flags or position): not written by the game');
  const lots = readIndex(pkg).filter(e => e.type === TYPE_LOT);
  if (lots.length !== 1) throw new Error(`expected one lot record 0x34E5247C, found ${lots.length}`);
  if (readIndex(pkg).some(e => e.type === TYPE_MARKER)) throw new Error('package already has the 0x0679B9B8 marker');
  const data = Buffer.from([1, 0]);
  const row = Buffer.alloc(32);
  row.writeUInt32LE(TYPE_MARKER, 0);
  row.writeUInt32LE(MARKER_GROUP, 4);
  row.writeUInt32LE(Number(lots[0].inst >> 32n), 8);
  row.writeUInt32LE(Number(lots[0].inst & 0xffffffffn), 12);
  row.writeUInt32LE(indexPos, 16);
  row.writeUInt32LE((data.length | 0x80000000) >>> 0, 20);
  row.writeUInt32LE(data.length, 24);
  row.writeUInt16LE(0, 28); // uncompressed, as the launcher stores it
  row.writeUInt16LE(1, 30);
  const head = Buffer.from(pkg.subarray(0, 96));
  head.writeUInt32LE(count + 1, 0x24);
  head.writeUInt32LE(indexSize + 32, 0x2c);
  head.writeUInt32LE(indexPos + data.length, 0x40);
  return Buffer.concat([head, pkg.subarray(96, indexPos), data, pkg.subarray(indexPos), row]);
}

export function planInstall(sims3pack: Buffer) {
  const s = readSims3Pack(sims3pack);
  if (s.type !== 'lot') throw new Error(`Sims3Pack type "${s.type}" is not a lot`);
  const pkgs = s.files.filter(f => f.name.toLowerCase().endsWith('.package'));
  // CC-free = the lot package only; bundled CC needs the launcher's DCCache merge, which we do not do
  if (pkgs.length !== 1) throw new Error(`${pkgs.length} .package files inside: lot with bundled CC, use the launcher`);
  const id = s.packageId!.toLowerCase();
  if (pkgs[0].name.toLowerCase() !== `${id}.package`) throw new Error(`embedded package ${pkgs[0].name} does not match PackageId ${id}`);
  // the launcher's ebf id is not derived from anything in the pack (docs/181 §6); ours is the PackageId hash so reruns are stable
  const ebfName = `ebf_0x${fnv64(id).toString(16).padStart(16, '0')}.package`;
  return { name: s.displayName, packageId: id, embedded: pkgs[0].data, ebfName, ebf: buildEbf(pkgs[0].data), backupName: `${id}.package` };
}

function install(file: string, docs: string, dry: boolean, force: boolean) {
  const plan = planInstall(readFileSync(file));
  const backup = join(docs, 'DCBackup', plan.backupName), ebf = join(docs, 'Library', plan.ebfName);
  console.log(`${plan.name} (${plan.packageId})\n  ${ebf} (${plan.ebf.length} B)\n  ${backup} (${plan.embedded.length} B)`);
  if (existsSync(backup) && !force) throw new Error(`already installed (${backup} exists); --force to write again`);
  if (dry) return console.log('  dry run: nothing written');
  for (const d of ['Library', 'DCBackup']) mkdirSync(join(docs, d), { recursive: true });
  writeFileSync(backup, plan.embedded);
  writeFileSync(ebf, plan.ebf);
  console.log('  installed; start the game and look in Edit Town > lot bin');
}

// Resource-level compare of our ebf with the launcher's. Launcher-only differences seen on 2026-10-24: its own RefPack
// re-encode, resources stored uncompressed, data order, and some 0x044AE110 presets re-serialized with <value> lines reordered.
export function compareEbf(ours: Buffer, theirs: Buffer) {
  const load = (b: Buffer) => new Map(readIndex(b).map(e => {
    const raw = b.subarray(e.offset, e.offset + e.fileSize);
    return [`${e.type.toString(16).padStart(8, '0')}:${e.group.toString(16)}:${e.inst.toString(16).padStart(16, '0')}`,
      { raw, data: e.compressed === 0xffff ? refpack(raw) : raw }];
  }));
  const a = load(ours), b = load(theirs);
  const r = { byteIdentical: ours.equals(theirs), resources: a.size, rawEqual: 0, dataEqual: 0, presetReordered: 0, bad: [] as string[] };
  const lines = (d: Buffer) => d.toString('utf16le').split('\r\n').sort().join('\n');
  for (const k of new Set([...a.keys(), ...b.keys()])) {
    const x = a.get(k), y = b.get(k);
    if (!x || !y) r.bad.push(`${k} only in ${x ? 'ours' : 'launcher'}`);
    else if (x.raw.equals(y.raw)) r.rawEqual++;
    else if (x.data.equals(y.data)) r.dataEqual++;
    else if (k.startsWith('044ae110') && x.data.length === y.data.length && lines(x.data) === lines(y.data)) r.presetReordered++;
    else r.bad.push(`${k} content differs`);
  }
  return r;
}

function selfCheck(docs: string) {
  const tmp = mkdtempSync(join(tmpdir(), 'install-lot-'));
  let checked = 0, failed = 0;
  try {
    const ebfs = readdirSync(join(docs, 'Library')).filter(n => /^ebf_0x[0-9a-f]+\.package$/i.test(n))
      .map(n => ({ n, b: readFileSync(join(docs, 'Library', n)) }));
    for (const f of readdirSync(join(docs, 'Downloads')).filter(n => n.toLowerCase().endsWith('.sims3pack'))) {
      let plan;
      try { plan = planInstall(readFileSync(join(docs, 'Downloads', f))); } catch (e) { console.log(`skip ${f}: ${(e as Error).message}`); continue; }
      const realBackup = join(docs, 'DCBackup', plan.backupName);
      if (!existsSync(realBackup)) { console.log(`skip ${f}: not installed by the launcher`); continue; }
      // install into a temp Documents tree, then compare with the launcher's files
      const tdocs = join(tmp, String(checked));
      install(join(docs, 'Downloads', f), tdocs, false, false);
      const ourBackup = readFileSync(join(tdocs, 'DCBackup', plan.backupName)), ourEbf = readFileSync(join(tdocs, 'Library', plan.ebfName));
      const lotKey = readIndex(ourEbf).find(e => e.type === TYPE_MARKER)!.inst;
      const theirs = ebfs.find(e => readIndex(e.b).some(x => x.type === TYPE_MARKER && x.inst === lotKey));
      checked++;
      const backupOk = ourBackup.equals(readFileSync(realBackup));
      if (!theirs) { failed++; console.log(`FAIL ${f}: no launcher ebf with lot ${lotKey.toString(16)}`); continue; }
      const r = compareEbf(ourEbf, theirs.b);
      const ok = backupOk && r.bad.length === 0;
      if (!ok) failed++;
      console.log(`${ok ? 'OK  ' : 'FAIL'} ${f} vs ${theirs.n}: DCBackup byte-identical ${backupOk}; ebf byte-identical ${r.byteIdentical} ` +
        `(${ourEbf.length} vs ${theirs.b.length} B); ${r.resources} resources: ${r.rawEqual} same bytes, ${r.dataEqual} same data ` +
        `(launcher re-encoded), ${r.presetReordered} presets same lines reordered${r.bad.length ? `; differ: ${r.bad.slice(0, 5).join(', ')}` : ''}`);
    }
  } finally { rmSync(tmp, { recursive: true, force: true }); }
  // synthetic check of the transform itself, so the self-check also runs without a game install
  const lot = Buffer.alloc(96 + 4 + 4 + 32);
  lot.write('DBPF', 0, 'latin1'); lot.writeUInt32LE(2, 4); lot.writeUInt32LE(1, 0x24); lot.writeUInt32LE(36, 0x2c); lot.writeUInt32LE(3, 0x3c); lot.writeUInt32LE(100, 0x40);
  lot.writeUInt32LE(TYPE_LOT, 104); lot.writeUInt32LE(0x11223344, 112); lot.writeUInt32LE(0x55667788, 116); lot.writeUInt32LE(96, 120);
  lot.writeUInt32LE(0x80000004, 124); lot.writeUInt32LE(4, 128); lot.writeUInt16LE(1, 134);
  const out = readIndex(buildEbf(lot));
  console.assert(out.length === 2 && out[1].type === TYPE_MARKER && out[1].inst === 0x1122334455667788n && out[0].offset === 96, 'synthetic transform');
  console.log(`self-check: ${checked} installs compared, ${failed} failed; synthetic transform ok`);
  if (failed) process.exitCode = 1;
}

if (process.argv[1]?.endsWith('install-lot.ts')) {
  const args = process.argv.slice(2);
  const opt = (n: string) => { const i = args.indexOf(n); return i >= 0 ? args.splice(i, 2)[1] : undefined; };
  const flag = (n: string) => { const i = args.indexOf(n); return i >= 0 && !!args.splice(i, 1); };
  const docs = opt('--docs') ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3');
  const dry = flag('--dry-run'), force = flag('--force');
  try {
    if (flag('--self-check')) selfCheck(docs);
    else if (args.length === 1) install(args[0], docs, dry, force);
    else { console.error('usage: node tools/install-lot.ts <file.Sims3Pack> [--docs <dir>] [--dry-run] [--force] | --self-check [--docs <dir>]'); process.exitCode = 2; }
  } catch (e) { console.error(`${basename(args[0] ?? 'install-lot')}: ${(e as Error).message}`); process.exitCode = 1; }
}
