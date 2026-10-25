// docs/141 §8 / docs/184 P1: type -> source file map for the NRaas source (names only, no code), so tool outputs over
// the mod assemblies (route-plan-callers.ts --source-map) cite a readable repo path.
// Usage: node tools/nraas-source-map.ts --src game-copy/nraas-src [--csv docs/data/nraas-source-map.csv]
//        node tools/nraas-source-map.ts --self-check
// Scans NRaas*/ folders (not Sims3/, which holds EA projects, nor Obsolete/); per .cs file the namespace and every
// top-level type (class, struct, interface, enum, delegate) declared in it. Nested types are not listed: look up the outer
// type (route-plan-callers strips "+Nested"). A type defined in several files (each mod builds its own copy of NRaas
// Common) gets one row per file.
import { readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { join, relative } from 'node:path';

const arg = (n: string, d: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };

// Top-level types of one C# file: brace depth 1 inside a namespace (file-scoped namespaces are not used by NRaas).
export function typesOf(text: string): string[] {
  const s = text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '').replace(/"(?:\\.|[^"\\])*"/g, '""');
  const out: string[] = []; const ns: { name: string; depth: number }[] = []; let depth = 0;
  const re = /\bnamespace\s+([\w.]+)|\b(?:class|struct|interface|enum)\s+(\w+)|\bdelegate\s+[\w<>\[\],. ]+?\s+(\w+)\s*[(<]|[{}]/g;
  for (let m; (m = re.exec(s));) {
    if (m[0] === '{') { depth++; continue; }
    if (m[0] === '}') { depth--; if (ns.length && depth < ns[ns.length - 1].depth) ns.pop(); continue; }
    if (m[1]) { ns.push({ name: m[1], depth: depth + 1 }); continue; }
    const name = m[2] ?? m[3]; const top = ns.length ? ns[ns.length - 1].depth : 0;
    if (depth === top) out.push((ns.length ? ns.map((n) => n.name).join('.') + '.' : '') + name);
  }
  return out;
}

function selfCheck(): void {
  const t = typesOf(`using X;\nnamespace NRaas.A.B\n{\n  // class Commented\n  public class Outer : Base { class Inner { } string s = "class Fake"; }\n  public delegate void D(int x);\n  interface I { }\n}\nnamespace NRaas { public abstract class Common { public class AlarmTask { } } }\n`);
  const want = ['NRaas.A.B.Outer', 'NRaas.A.B.D', 'NRaas.A.B.I', 'NRaas.Common'];
  console.assert(JSON.stringify(t) === JSON.stringify(want), `typesOf: ${JSON.stringify(t)}`);
  if (JSON.stringify(t) !== JSON.stringify(want)) process.exit(1);
  console.log('nraas-source-map self-check ok');
}

if (process.argv.includes('--self-check')) selfCheck();
else {
  const src = arg('--src', 'game-copy/nraas-src');
  const rows: string[] = [];
  const walk = (d: string) => {
    for (const n of readdirSync(d).sort()) {
      const p = join(d, n);
      if (statSync(p).isDirectory()) { if (n !== 'Obsolete' && n !== '.git') walk(p); }
      else if (n.endsWith('.cs')) for (const t of typesOf(readFileSync(p, 'utf8'))) rows.push(`${t},${relative(src, p).replace(/\\/g, '/')}`);
    }
  };
  for (const top of readdirSync(src).sort()) if (/^NRaas/.test(top) && statSync(join(src, top)).isDirectory()) walk(join(src, top));
  rows.sort();
  const out = arg('--csv', '');
  if (out) writeFileSync(out, 'type,source\n' + rows.map((r) => (r.includes(' ') ? r.replace(/,(.*)$/, ',"$1"') : r)).join('\n') + '\n');
  console.log(`${rows.length} type rows, ${new Set(rows.map((r) => r.split(',')[0])).size} types`);
}
