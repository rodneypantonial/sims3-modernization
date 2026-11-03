// [ImageGuard] repro package (docs/232 Run 8d, repros.json `imgguard-oversized-png`): one PNG resource whose header claims
// a huge image, so the game's decode asks for (bpp / 8) * w * h bytes. Only the header matters: the guard acts before rows.
//   node tools/imgguard-repro.ts --out <scratch Mods copy>/ImageGuardRepro.package [--key 0x2F7D0004:0x0:0x<instance>]
//        [--w 30000 --h 30000]   defaults: 30000 x 30000 RGBA = 3.6 GB, wraps in 32 bits (REJECT, overflow);
//        --w 16000 --h 16000 = 1 GB (REJECT, over MaxMB 512); --w 6000 --h 6000 = 144 MB (logged only)
//   node tools/imgguard-repro.ts --self-check
// The key must be one the game decodes on the way to the main menu (or in the step that the repro names), so the package
// overrides it; the default instance is a placeholder: take a real one from an IMAGEGUARD line or s3pe (repros.json note).
import { crc32, deflateSync } from 'node:zlib';
import { writeFileSync } from 'node:fs';
import { parseArgs } from 'node:util';
import { writePackage } from './dbpf.ts';

const chunk = (type: string, data: Buffer) => {
  const len = Buffer.alloc(4), crc = Buffer.alloc(4), td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  len.writeUInt32BE(data.length);
  crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
};

// PNG with IHDR w x h, 8-bit RGBA, and one tiny IDAT (decoding the rows would fail; the guard acts before that)
export function hugePng(w: number, h: number): Buffer {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0), ihdr.writeUInt32BE(h, 4), ihdr[8] = 8, ihdr[9] = 6;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(Buffer.alloc(1))), chunk('IEND', Buffer.alloc(0))]);
}

export function parseKey(s: string) {
  const [t, g, i] = s.split(':');
  if (i === undefined) throw new Error('--key type:group:instance');
  return { type: Number(t) >>> 0, group: Number(g) >>> 0, instance: BigInt(i) };
}

const { values: v } = parseArgs({ options: { out: { type: 'string' }, key: { type: 'string', default: '0x2F7D0004:0x0:0x1A6E6E7E00000001' },
  w: { type: 'string', default: '30000' }, h: { type: 'string', default: '30000' }, 'self-check': { type: 'boolean' } } });
if (v['self-check']) {
  const p = hugePng(30000, 30000);
  if (p.readUInt32BE(16) !== 30000 || p.readUInt32BE(20) !== 30000 || p.toString('ascii', 12, 16) !== 'IHDR' || p[25] !== 6) throw new Error('png header');
  if (crc32(p.subarray(12, 29)) !== p.readUInt32BE(29)) throw new Error('IHDR crc');
  const k = parseKey('0x2F7D0004:0x0:0x10');
  if (k.type !== 0x2f7d0004 || k.group !== 0 || k.instance !== 16n) throw new Error('key');
  const pkg = writePackage([{ ...k, data: p }]);
  if (pkg.toString('ascii', 0, 4) !== 'DBPF') throw new Error('package');
  console.log('imgguard-repro self-check: ok');
} else {
  if (!v.out) throw new Error('--out <file.package> (a scratch Mods copy, never the live Mods folder of a timing machine)');
  const w = Number(v.w), h = Number(v.h);
  writeFileSync(v.out, writePackage([{ ...parseKey(v.key!), data: hugePng(w, h) }]));
  console.log(`wrote ${v.out}: PNG ${w} x ${h} RGBA = ${(4 * w * h / 2 ** 20).toFixed(0)} MB requested by the decode, key ${v.key}`);
}
