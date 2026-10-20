// Extracts .NET assemblies (S3SA resources, type 0x073FAA07) from Sims 3 DBPF .package files.
// Usage: node tools/extract-assemblies.ts <outDir> <file.package>...
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, basename } from 'node:path';

const S3SA = 0x073faa07;

// RefPack (EA QFS) decompression as used by DBPF 2.0 resources.
export function refpack(src: Buffer): Buffer {
  let p = 0;
  const flags = src[p++];
  if (src[p++] !== 0xfb) throw new Error('not RefPack');
  const sizeBytes = flags & 0x80 ? 4 : 3;
  if (flags & 0x01) p += sizeBytes; // compressed-size field
  let size = 0;
  for (let i = 0; i < sizeBytes; i++) size = size * 256 + src[p++];
  const out = Buffer.alloc(size);
  let o = 0;
  for (;;) {
    const b0 = src[p++];
    let plain: number, copy = 0, off = 0;
    if (b0 < 0x80) {
      const b1 = src[p++];
      plain = b0 & 3; copy = ((b0 & 0x1c) >> 2) + 3; off = ((b0 & 0x60) << 3) + b1 + 1;
    } else if (b0 < 0xc0) {
      const b1 = src[p++], b2 = src[p++];
      plain = b1 >> 6; copy = (b0 & 0x3f) + 4; off = ((b1 & 0x3f) << 8) + b2 + 1;
    } else if (b0 < 0xe0) {
      const b1 = src[p++], b2 = src[p++], b3 = src[p++];
      plain = b0 & 3; copy = ((b0 & 0x0c) << 6) + b3 + 5; off = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1;
    } else if (b0 < 0xfc) {
      plain = ((b0 & 0x1f) << 2) + 4;
    } else {
      plain = b0 & 3;
      src.copy(out, o, p, p + plain); o += plain;
      break;
    }
    src.copy(out, o, p, p + plain); o += plain; p += plain;
    for (let i = 0; i < copy; i++, o++) out[o] = out[o - off]; // byte-wise: ranges may overlap
  }
  if (o !== size) throw new Error(`RefPack size mismatch ${o} != ${size}`);
  return out;
}

type Entry = { type: number; group: number; inst: bigint; offset: number; fileSize: number; memSize: number; compressed: number };

export function readIndex(pkg: Buffer): Entry[] {
  if (pkg.toString('latin1', 0, 4) !== 'DBPF') throw new Error('not DBPF');
  const count = pkg.readUInt32LE(36);
  let p = pkg.readUInt32LE(64);
  const flags = pkg.readUInt32LE(p); p += 4;
  const cType = flags & 1 ? pkg.readUInt32LE((p += 4) - 4) : 0;
  const cGroup = flags & 2 ? pkg.readUInt32LE((p += 4) - 4) : 0;
  const cHi = flags & 4 ? pkg.readUInt32LE((p += 4) - 4) : 0;
  const entries: Entry[] = [];
  for (let i = 0; i < count; i++) {
    const type = flags & 1 ? cType : pkg.readUInt32LE((p += 4) - 4);
    const group = flags & 2 ? cGroup : pkg.readUInt32LE((p += 4) - 4);
    const hi = flags & 4 ? cHi : pkg.readUInt32LE((p += 4) - 4);
    const lo = pkg.readUInt32LE(p);
    const offset = pkg.readUInt32LE(p + 4);
    const fileSize = pkg.readUInt32LE(p + 8) & 0x7fffffff;
    const memSize = pkg.readUInt32LE(p + 12);
    const compressed = pkg.readUInt16LE(p + 16);
    p += 20;
    entries.push({ type, group, inst: (BigInt(hi) << 32n) | BigInt(lo), offset, fileSize, memSize, compressed });
  }
  return entries;
}

// S3SA body: header + XOR-chained 512-byte blocks. Port of s3pi ScriptResource.Parse/decrypt
// (github.com/Destrospean/s3pi-modern, Wrappers/ScriptResource/ScriptResource.cs).
export function decryptS3SA(body: Buffer): Buffer {
  let p = 0;
  const version = body[p++];
  if (version > 1) p += body.readInt32LE(p) * 2 + 4; // UTF-16 game version string
  p += 4 + 64; // unknown2, md5sum
  const count = body.readUInt16LE(p); p += 2;
  const table = body.subarray(p, p + count * 8); p += count * 8;
  const data = body.subarray(p, p + count * 512);
  let sum = 0n;
  for (let i = 0; i < table.length; i += 8) sum = (sum + table.readBigUInt64LE(i)) & 0xffffffffffffffffn;
  let seed = Number(BigInt(table.length - 1) & sum);
  const out = Buffer.alloc(count * 512);
  let r = 0;
  for (let i = 0, blk = 0; i < table.length; i += 8, blk++) {
    if (table[i] & 1) continue; // flagged block stays zero and consumes no input, as in s3pi
    for (let j = 0; j < 512; j++, r++) {
      const v = data[r];
      out[blk * 512 + j] = v ^ table[seed];
      seed = (seed + v) % table.length;
    }
  }
  return out;
}

// Pulls the PE image out of a decrypted S3SA body.
export function peImage(body: Buffer): Buffer | null {
  for (let i = body.indexOf('MZ'); i >= 0; i = body.indexOf('MZ', i + 1)) {
    if (i + 0x40 > body.length) break;
    const pe = body.readUInt32LE(i + 0x3c);
    if (i + pe + 4 <= body.length && body.toString('latin1', i + pe, i + pe + 4) === 'PE\0\0') return body.subarray(i);
  }
  return null;
}

// Module name from .NET metadata (Module table row 0), e.g. "UI.dll".
export function moduleName(img: Buffer): string | null {
  const pe = img.readUInt32LE(0x3c);
  const nSec = img.readUInt16LE(pe + 6), optSize = img.readUInt16LE(pe + 20), opt = pe + 24;
  const secs = opt + optSize;
  const rva2off = (rva: number) => {
    for (let i = 0; i < nSec; i++) {
      const s = secs + i * 40, va = img.readUInt32LE(s + 12), size = img.readUInt32LE(s + 8);
      if (rva >= va && rva < va + size) return rva - va + img.readUInt32LE(s + 20);
    }
    throw new Error(`rva ${rva} outside sections`);
  };
  const cliRva = img.readUInt32LE(opt + 96 + 14 * 8); // PE32 data directory 14
  if (!cliRva) return null;
  const md = rva2off(img.readUInt32LE(rva2off(cliRva) + 8));
  const verLen = img.readUInt32LE(md + 12);
  let p = md + 16 + verLen + 2;
  const nStreams = img.readUInt16LE(p); p += 2;
  let tables = 0, strings = 0;
  for (let i = 0; i < nStreams; i++) {
    const off = img.readUInt32LE(p), end = img.indexOf(0, p + 8);
    const name = img.toString('latin1', p + 8, end);
    if (name === '#~') tables = md + off;
    if (name === '#Strings') strings = md + off;
    p = p + 8 + ((end - (p + 8) + 4) & ~3);
  }
  const wideStrings = img[tables + 6] & 1;
  const valid = img.readBigUInt64LE(tables + 8);
  const rows = tables + 24 + 4 * [...valid.toString(2)].filter(b => b === '1').length;
  const nameIdx = wideStrings ? img.readUInt32LE(rows + 2) : img.readUInt16LE(rows + 2); // Module row: Generation u16, Name
  return img.toString('utf8', strings + nameIdx, img.indexOf(0, strings + nameIdx));
}

if (import.meta.main ?? process.argv[1]?.endsWith('extract-assemblies.ts')) {
  const [outDir, ...pkgs] = process.argv.slice(2);
  if (!outDir || !pkgs.length) { console.error('usage: node tools/extract-assemblies.ts <outDir> <file.package>...'); process.exit(2); }
  mkdirSync(outDir, { recursive: true });
  for (const file of pkgs) {
    const pkg = readFileSync(file);
    const entries = readIndex(pkg).filter(e => e.type === S3SA);
    for (const e of entries) {
      let body = pkg.subarray(e.offset, e.offset + e.fileSize);
      if (e.compressed === 0xffff) body = refpack(body);
      const img = peImage(body) ?? peImage(decryptS3SA(body));
      const id = e.inst.toString(16).padStart(16, '0');
      if (!img) { console.log(`${basename(file)} ${id}: no plain PE (encrypted?) ${body.length} B`); continue; }
      const name = moduleName(img) ?? `${id}.dll`;
      writeFileSync(join(outDir, name), img);
      console.log(`${basename(file)} ${id}: ${name} ${img.length} B`);
    }
  }
}
