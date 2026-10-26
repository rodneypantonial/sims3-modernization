// Writes Sims 3 DBPF 2.0 .package files (uncompressed) and wraps assemblies as S3SA resources.
// Port of Sims3-MCP tools/dbpf.py (MIT, Copyright (c) 2026 Critical-Reynolds).
// Self-test: node tools/dbpf.ts (also covers extract-assemblies.ts readIndex, decryptS3SA, peImage)
import assert from 'node:assert/strict';
import { readIndex, decryptS3SA, peImage } from './extract-assemblies.ts';

export const TYPE_S3SA = 0x073faa07;
export const TYPE_VIDEO = 0xb1cc1af6;

// FNV-1 64-bit of the lowercase name: the instance id the game derives from a resource name.
export function fnv64(text: string): bigint {
  let h = 0xcbf29ce484222325n;
  for (const b of Buffer.from(text.toLowerCase(), 'ascii')) {
    h = (h * 0x100000001b3n) & 0xffffffffffffffffn;
    h ^= BigInt(b);
  }
  return h;
}

// Version-1 S3SA with an all-zero key table: every XOR key is 0, so blocks are the padded cleartext (as s3pe writes).
export function s3saEncode(assembly: Buffer): Buffer {
  const count = Math.ceil(assembly.length / 512);
  const head = Buffer.alloc(1 + 4 + 64 + 2 + count * 8);
  head[0] = 1;
  head.writeUInt32LE(0x2bc4f79f, 1);
  head.writeUInt16LE(count, 69);
  const body = Buffer.alloc(count * 512);
  assembly.copy(body);
  return Buffer.concat([head, body]);
}

// memSize set: data is already RefPack-compressed (copied as is from a game package), memSize = its decompressed size
export type Resource = { type: number; group: number; instance: bigint; data: Buffer; memSize?: number };

export function writePackage(resources: Resource[]): Buffer {
  const HEADER = 96;
  const offsets: number[] = [];
  let pos = HEADER;
  for (const r of resources) { offsets.push(pos); pos += r.data.length; }
  const index = Buffer.alloc(4 + resources.length * 32); // flags 0: no constant fields
  resources.forEach((r, i) => {
    const o = 4 + i * 32;
    index.writeUInt32LE(r.type, o);
    index.writeUInt32LE(r.group, o + 4);
    index.writeUInt32LE(Number(r.instance >> 32n), o + 8);
    index.writeUInt32LE(Number(r.instance & 0xffffffffn), o + 12);
    index.writeUInt32LE(offsets[i], o + 16);
    index.writeUInt32LE((r.data.length | 0x80000000) >>> 0, o + 20);
    index.writeUInt32LE(r.memSize ?? r.data.length, o + 24);
    index.writeUInt16LE(r.memSize === undefined ? 0 : 0xffff, o + 28); // 0xffff = RefPack
    index.writeUInt16LE(1, o + 30); // committed
  });
  const header = Buffer.alloc(HEADER);
  header.write('DBPF', 0, 'latin1');
  header.writeUInt32LE(2, 4);
  header.writeUInt32LE(resources.length, 0x24);
  header.writeUInt32LE(index.length, 0x2c);
  header.writeUInt32LE(3, 0x3c);
  header.writeUInt32LE(pos, 0x40);
  return Buffer.concat([header, ...resources.map(r => r.data), index]);
}

if (process.argv[1]?.endsWith('dbpf.ts')) {
  const asm = Buffer.from('MZ fake assembly '.repeat(70));
  const pkg = writePackage([{ type: TYPE_S3SA, group: 0, instance: fnv64('Test'), data: s3saEncode(asm) }]);
  const [e] = readIndex(pkg);
  assert.ok(e.type === TYPE_S3SA && e.inst === fnv64('Test'), 'index round trip');
  const back = decryptS3SA(pkg.subarray(e.offset, e.offset + e.fileSize)).subarray(0, asm.length);
  assert.ok(back.equals(asm), 'S3SA round trip');
  assert.equal(fnv64(''), 0xcbf29ce484222325n, 'fnv64 offset basis');
  // a minimal PE: 'MZ', e_lfanew 0x40 at +0x3C, 'PE\0\0' at +0x40, found behind leading bytes
  const pe = Buffer.alloc(0x48); pe.write('MZ', 0, 'latin1'); pe.writeUInt32LE(0x40, 0x3c); pe.write('PE\0\0', 0x40, 'latin1');
  assert.equal(peImage(Buffer.concat([Buffer.from('xxMZ'), pe]))?.length, pe.length, 'peImage finds the PE after a false MZ');
  assert.equal(peImage(asm), null, 'peImage: no PE header');
  console.log('dbpf self-test done');
}
