import { readFileSync } from 'node:fs';
const rows = readFileSync(process.argv[2], 'utf8').trim().split('\n').map(l => l.split('\t'));
export function parse(r: string[]) {
  const o: any = { addr: r[0], names: r[1].split(';'), fn: r[2] };
  for (const c of r.slice(3)) {
    const m = c.match(/^(\w+)=(.*)$/s);
    if (c.startsWith('{')) {
      o.flags = {} as Record<string, string>;
      const body = c.slice(1, -1);
      // keys look like kind:Name=depth:path
      for (const m2 of body.matchAll(/(?:^|, )([a-z]+:?[\w+:.-]*|atomic|nalloc)=(\d+):([^,]*)/g)) o.flags[m2[1]] = m2[2] + ':' + m2[3];
    } else if (m) o[m[1]] = m[2];
  }
  return o;
}
if (process.argv[1].endsWith('stats.ts')) {
  const ps = rows.map(parse);
  const kinds: Record<string, number> = {};
  for (const p of ps) for (const k of new Set(Object.keys(p.flags || {}).map(k => k.split(':')[0]))) kinds[k] = (kinds[k] || 0) + 1;
  console.log(ps.length, kinds);
  const hostSlots: Record<string, number> = {};
  for (const p of ps) for (const h of (p.H || '[]').slice(1, -1).split(', ').filter(Boolean)) hostSlots[h] = (hostSlots[h] || 0) + 1;
  console.log('host slots', hostSlots);
  const fl: Record<string, number> = {};
  for (const p of ps) for (const k of Object.keys(p.flags || {})) fl[k] = (fl[k] || 0) + 1;
  console.log(Object.entries(fl).sort((a, b) => b[1] - a[1]).slice(0, 70).map(e => e.join('=')).join(' '));
}
