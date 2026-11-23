// CC image scan: lists every image resource in mod packages (DDS, PNG, JPG, RLE2/RLES, found by magic bytes, so any
// type id) with its header size, format, mips and the raster bytes a decoder would request. Read-only. Hunts the
// oversized-raster request behind a main-menu crash (image decoder 0x445E80 on 1.69 asking for ~1.9 GB).
//   node tools/cc-image-scan.ts [--csv out.csv] [<folder or .package>...]   default <Documents>\...\The Sims 3\Mods
//   node tools/cc-image-scan.ts --self-check
// Columns: native = bytes in the stored format with all mips; raster = w x h x 4 (ARGB32 decode of the top level),
// rasterMips = the same over all mips. Flags: huge (raster >= 256 MB), watch (>= 64 MB), dim (side 0 or > 8192),
// giant (side >= 16384), short (data shorter than the header implies), corrupt (bad header / RefPack / memSize).
import assert from 'node:assert/strict';
import { closeSync, mkdtempSync, openSync, readSync, rmSync, writeFileSync } from 'node:fs';
import { homedir, tmpdir } from 'node:os';
import { join } from 'node:path';
import { writePackage } from './dbpf.ts';
import { body, entries, packages } from './cc-mesh-scan.ts';

const MB = 1 << 20;
export type Img = { kind: string; w: number; h: number; fmt: string; mips: number; native: number; raster: number; rasterMips: number; flags: string[] };

// numeric D3DFMT fourccs (float formats): bytes per pixel
const D3DFMT_BYTES: Record<number, number> = { 111: 2, 112: 4, 113: 8, 114: 4, 115: 8, 116: 16 };
const blockBytes = (fmt: string) => (fmt === 'DXT1' || fmt === 'BC1' || fmt === 'ATI1' || fmt === 'BC4U' ? 8 : 16);
function levels(w: number, h: number, mips: number, per: (w: number, h: number) => number) {
  let t = 0;
  for (let i = 0; i < Math.max(1, mips); i++) t += per(Math.max(1, w >> i), Math.max(1, h >> i));
  return t;
}
function finish(kind: string, w: number, h: number, fmt: string, mips: number, native: number, have: number | null, flags: string[]): Img {
  const raster = w * h * 4, rasterMips = levels(w, h, mips, (a, b) => a * b * 4);
  if (raster >= 256 * MB) flags.push('huge'); else if (raster >= 64 * MB) flags.push('watch');
  if (!w || !h || w > 8192 || h > 8192) flags.push('dim');
  if (w >= 16384 || h >= 16384) flags.push('giant');
  if (have !== null && have < native) flags.push(`short(${have}<${native})`);
  return { kind, w, h, fmt, mips, native, raster, rasterMips, flags };
}

// null = not an image
export function sniff(b: Buffer): Img | null {
  const s4 = b.toString('latin1', 0, 4);
  if (s4 === 'DDS ') {
    if (b.length < 128 || b.readUInt32LE(4) !== 124) return { kind: 'DDS', w: 0, h: 0, fmt: '?', mips: 0, native: 0, raster: 0, rasterMips: 0, flags: ['corrupt(header)'] };
    const h = b.readUInt32LE(12), w = b.readUInt32LE(16), mips = b.readUInt32LE(28), pfFlags = b.readUInt32LE(80);
    let fmt = b.toString('latin1', 84, 88), data = 128, native: number;
    if (pfFlags & 4) { // FOURCC
      if (fmt === 'DX10') { fmt = `DX10:${b.readUInt32LE(128)}`; data = 148; }
      const bb = blockBytes(fmt);
      native = fmt.startsWith('DXT') || fmt.startsWith('ATI') || fmt.startsWith('BC')
        ? levels(w, h, mips, (a, c) => Math.ceil(a / 4) * Math.ceil(c / 4) * bb)
        : levels(w, h, mips, (a, c) => a * c * (D3DFMT_BYTES[b.readUInt32LE(84)] ?? 4));
      if (!/^[ -~]{4}$/.test(fmt)) fmt = `D3DFMT${b.readUInt32LE(84)}`;
    } else {
      const bpp = b.readUInt32LE(88);
      fmt = `RGB${bpp}`;
      native = levels(w, h, mips, (a, c) => (a * c * bpp) / 8);
    }
    return finish('DDS', w, h, fmt, mips, native, b.length - data, mips > 16 ? ['corrupt(mips)'] : []);
  }
  if (s4 === '\x89PNG') {
    if (b.length < 26 || b.toString('latin1', 12, 16) !== 'IHDR') return finish('PNG', 0, 0, '?', 1, 0, null, ['corrupt(IHDR)']);
    const w = b.readUInt32BE(16), h = b.readUInt32BE(20);
    return finish('PNG', w, h, `d${b[24]}c${b[25]}`, 1, 0, null, []);
  }
  if (b[0] === 0xff && b[1] === 0xd8 && b[2] === 0xff) {
    for (let p = 2; p + 9 < b.length;) {
      if (b[p] !== 0xff) return finish('JPG', 0, 0, '?', 1, 0, null, ['corrupt(marker)']);
      const m = b[p + 1];
      if (m === 0xff) { p++; continue; }
      if (m >= 0xc0 && m <= 0xcf && m !== 0xc4 && m !== 0xc8 && m !== 0xcc)
        return finish('JPG', b.readUInt16BE(p + 7), b.readUInt16BE(p + 5), `SOF${m - 0xc0}`, 1, 0, null, []);
      if (m === 0xd9 || m === 0xda) break;
      p += 2 + (m >= 0xd0 && m <= 0xd8 ? 0 : b.readUInt16BE(p + 2));
    }
    return finish('JPG', 0, 0, '?', 1, 0, null, ['corrupt(no SOF)']);
  }
  const v = b.length >= 16 ? b.toString('latin1', 4, 8) : '';
  if (v === 'RLE2' || v === 'RLES') { // s3pi RLEResource: fourcc, version, width u16, height u16, mip count u16
    const w = b.readUInt16LE(8), h = b.readUInt16LE(10), mips = b.readUInt16LE(12);
    const native = levels(w, h, mips, (a, c) => Math.ceil(a / 4) * Math.ceil(c / 4) * 16);
    return finish(v, w, h, b.toString('latin1', 0, 4), mips, native, null, mips > 16 ? ['corrupt(mips)'] : []);
  }
  return null;
}

type Row = { pkg: string; type: number; group: number; inst: bigint; size: number; img: Img };
export function scanPackage(p: string): { n: number; rows: Row[]; bad: Row[] } {
  const fd = openSync(p, 'r'), rows: Row[] = [], bad: Row[] = [];
  const blank = (f: string): Img => ({ kind: '-', w: 0, h: 0, fmt: '', mips: 0, native: 0, raster: 0, rasterMips: 0, flags: [f] });
  try {
    const es = entries(fd);
    for (const e of es) {
      const base = { pkg: p, type: e.type, group: e.group, inst: e.inst, size: e.memSize };
      let b: Buffer;
      try {
        if (e.compressed === 0xffff) {
          const head = Buffer.alloc(6); readSync(fd, head, 0, 6, e.offset);
          const big = head[0] & 0x80, at = head[0] & 1 ? (big ? 6 : 5) : 2;
          const hb = Buffer.alloc(at + 4); readSync(fd, hb, 0, hb.length, e.offset);
          const n = big ? hb.readUInt32BE(at) : hb.readUIntBE(at, 3);
          if (n !== e.memSize) { bad.push({ ...base, img: blank(`corrupt(RefPack size ${n} != memSize ${e.memSize})`) }); if (n > 512 * MB) continue; }
        }
        b = body(fd, e);
      } catch (x) { bad.push({ ...base, img: blank(`corrupt(${(x as Error).message})`) }); continue; }
      const img = sniff(b);
      if (img) rows.push({ ...base, size: b.length, img });
    }
    return { n: es.length, rows, bad };
  } finally { closeSync(fd); }
}

const hx = (v: number | bigint, n: number) => v.toString(16).toUpperCase().padStart(n, '0');
const csvRow = (r: Row) => [r.pkg, hx(r.type, 8), hx(r.group, 8), hx(r.inst, 16), r.size, r.img.kind, r.img.w, r.img.h, r.img.fmt, r.img.mips,
  r.img.native, r.img.raster, r.img.rasterMips, r.img.flags.join(' ')].map(v => /[",]/.test(String(v)) ? `"${String(v).replace(/"/g, '""')}"` : v).join(',');

function selfCheck() {
  const dds = (w: number, h: number, fourcc: string, mips: number, data: number) => {
    const b = Buffer.alloc(128 + data); b.write('DDS ', 0, 'latin1'); b.writeUInt32LE(124, 4);
    b.writeUInt32LE(h, 12); b.writeUInt32LE(w, 16); b.writeUInt32LE(mips, 28); b.writeUInt32LE(4, 80); b.write(fourcc, 84, 'latin1');
    return b;
  };
  const ok = dds(8, 8, 'DXT5', 2, 64 + 16);
  assert.deepEqual(sniff(ok), { kind: 'DDS', w: 8, h: 8, fmt: 'DXT5', mips: 2, native: 80, raster: 256, rasterMips: 320, flags: [] });
  assert.deepEqual(sniff(dds(24000, 20000, 'DXT1', 1, 16))!.flags, ['huge', 'dim', 'giant', 'short(16<240000000)']);
  const png = Buffer.alloc(33); png.write('\x89PNG\r\n\x1a\n', 0, 'latin1'); png.writeUInt32BE(13, 8); png.write('IHDR', 12, 'latin1');
  png.writeUInt32BE(5000, 16); png.writeUInt32BE(4000, 20); png[24] = 8; png[25] = 6;
  assert.deepEqual([sniff(png)!.raster, sniff(png)!.flags], [80_000_000, ['watch']]);
  const jpg = Buffer.from([0xff, 0xd8, 0xff, 0xe0, 0, 4, 0, 0, 0xff, 0xc0, 0, 11, 8, 0, 32, 0, 64, 3, 0, 0, 0]);
  assert.deepEqual([sniff(jpg)!.w, sniff(jpg)!.h, sniff(jpg)!.fmt], [64, 32, 'SOF0']);
  const rle = Buffer.alloc(16); rle.write('DXT5RLE2', 0, 'latin1'); rle.writeUInt16LE(256, 8); rle.writeUInt16LE(128, 10); rle.writeUInt16LE(1, 12);
  assert.deepEqual([sniff(rle)!.kind, sniff(rle)!.native], ['RLE2', 64 * 32 * 16]);
  const f16 = dds(4, 4, String.fromCharCode(113, 0, 0, 0), 1, 128); // fourcc 113 = A16B16G16R16F, 8 bytes per pixel
  assert.deepEqual([sniff(f16)!.native, sniff(f16)!.fmt, sniff(f16)!.flags], [128, 'D3DFMT113', []]);
  assert.equal(sniff(Buffer.from('not an image at all')), null);
  const dir = mkdtempSync(join(tmpdir(), 'ccimg-'));
  try {
    const p = join(dir, 'x.package');
    writeFileSync(p, writePackage([{ type: 0x00b2d882, group: 0, instance: 1n, data: ok }, { type: 0x12345678, group: 0, instance: 2n, data: png },
      { type: 0x1, group: 0, instance: 3n, data: Buffer.from('plain') }]));
    const r = scanPackage(p);
    assert.deepEqual([r.n, r.rows.map(x => x.img.kind), r.bad.length], [3, ['DDS', 'PNG'], 0]);
  } finally { rmSync(dir, { recursive: true, force: true }); }
  console.log('cc-image-scan self-check ok');
}

function main(argv: string[]) {
  if (argv[0] === '--self-check') return selfCheck();
  let csv = '';
  if (argv[0] === '--csv') { csv = argv[1]; argv = argv.slice(2); }
  const roots = argv.length ? argv : [join(process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3'), 'Mods')];
  const all: Row[] = [], bad: Row[] = [];
  let pk = 0, res = 0;
  for (const root of roots) for (const p of packages(root)) {
    pk++;
    try { const r = scanPackage(p); res += r.n; all.push(...r.rows); bad.push(...r.bad); }
    catch (x) { bad.push({ pkg: p, type: 0, group: 0, inst: 0n, size: 0, img: { kind: '-', w: 0, h: 0, fmt: '', mips: 0, native: 0, raster: 0, rasterMips: 0, flags: [`corrupt(package: ${(x as Error).message})`] } }); }
  }
  if (csv) writeFileSync(csv, ['package,type,group,instance,size,kind,width,height,format,mips,native,raster,rasterMips,flags', ...[...all, ...bad].map(csvRow)].join('\n') + '\n');
  all.sort((a, b) => b.img.rasterMips - a.img.rasterMips);
  const near = [...all].sort((a, b) => Math.abs(a.img.raster - 2e9) - Math.abs(b.img.raster - 2e9))[0];
  console.log(`packages ${pk}, resources ${res}, images ${all.length}, unreadable ${bad.length}`);
  console.log('top 10 by raster with mips:'); for (const r of all.slice(0, 10)) console.log('  ' + csvRow(r));
  console.log('flagged:'); for (const r of [...all.filter(r => r.img.flags.length), ...bad]) console.log('  ' + csvRow(r));
  if (near) console.log(`closest to 2e9 raster: ${csvRow(near)} (${near.img.raster >= 1.8e9 && near.img.raster <= 2.2e9 ? 'WITHIN' : 'outside'} +-10%)`);
}
if (process.argv[1]?.endsWith('cc-image-scan.ts')) main(process.argv.slice(2));
