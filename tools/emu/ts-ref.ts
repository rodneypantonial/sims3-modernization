// Reference outputs from the repo's own TypeScript code, for tools/emu specs.
// stdin: JSON array of {fn: 'refpack', hex} | {fn: 'fnv64', text}; stdout: JSON array of {hex} | {error}.
import { readFileSync } from 'node:fs';
import { refpack } from '../extract-assemblies.ts';
import { fnv64 } from '../dbpf.ts';

const reqs: { fn: string; hex?: string; text?: string }[] = JSON.parse(readFileSync(0, 'utf8'));
const out = reqs.map(r => {
  try {
    if (r.fn === 'refpack') return { hex: refpack(Buffer.from(r.hex!, 'hex')).toString('hex') };
    if (r.fn === 'fnv64') return { hex: fnv64(r.text!).toString(16).padStart(16, '0') };
    return { error: `unknown fn ${r.fn}` };
  } catch (e) {
    return { error: String((e as Error).message) };
  }
});
process.stdout.write(JSON.stringify(out));
