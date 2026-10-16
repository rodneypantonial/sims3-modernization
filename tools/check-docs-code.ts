// Flags decompiled-looking C/C# in docs (AGENTS.md: no decompiled code in the repo).
// Usage: node tools/check-docs-code.ts [dir]   (default: docs). Prints file:line per hit, exit 1 on hits.
// Our own examples: put `<!-- docs-code: ok -->` on the line before a fence, inside it, or on the flagged line.
// Self-check runs first on every call (exit 2 if it fails).
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';

const MARK = '<!-- docs-code: ok -->';
// Ghidra/IDA output that never appears in our own prose.
// Checked on every line, prose included.
const DECOMP = [
  /\bundefined[1-8]\b|\bundefined\s*\*/, // Ghidra types; plain "undefined" is an English word
  /\*\s*\(\s*(?:int|uint|char|byte|short|ushort|float|double|void|undefined\d?|code|DWORD|BYTE)\s*\*+\s*\)/, // *(int *)
];
// Inside fences only: a lone `FUN_xxxxxxxx(arg)` in prose is an address reference, not code.
const FUNCALL = /\bFUN_[0-9a-fA-F]{8}\s*\(/;
// Decompiler locals: only inside fences (prose may name a `DAT_` address label).
const LOCALS = /\b(?:puVar|piVar|pcVar|uVar|iVar|bVar|local_)[0-9a-fA-F]+\b/;
const NON_C = new Set(['sh', 'bash', 'ps1', 'powershell', 'pwsh', 'json', 'jsonl', 'ini', 'text', 'txt', 'ts', 'typescript', 'js', 'javascript', 'yaml', 'yml', 'md', 'markdown', 'diff', 'asm', 'nasm', 'csv', 'toml', 'xml', 'mermaid', 'console']);

export function check(text: string): number[] {
  const lines = text.split(/\r?\n/);
  const hits: number[] = [];
  let fence: { start: number; lang: string; ok: boolean; body: string[] } | null = null;
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const m = /^\s*(```|~~~)\s*([\w#+-]*)/.exec(line);
    if (m && !fence) {
      fence = { start: i, lang: m[2].toLowerCase(), ok: i > 0 && lines[i - 1].includes(MARK), body: [] };
      continue;
    }
    if (m && fence) {
      if (!fence.ok && !fence.body.some((l) => l.includes(MARK)) && looksLikeC(fence.lang, fence.body)) hits.push(fence.start + 1);
      fence = null;
      continue;
    }
    if (fence) { fence.body.push(line); continue; }
    if (!line.includes(MARK) && DECOMP.some((r) => r.test(line))) hits.push(i + 1);
  }
  if (fence && !fence.ok && looksLikeC(fence.lang, fence.body)) hits.push(fence.start + 1); // unclosed fence
  return hits;
}

function looksLikeC(lang: string, body: string[]): boolean {
  if (body.some((l) => LOCALS.test(l) || FUNCALL.test(l) || DECOMP.some((r) => r.test(l)))) return true;
  if (NON_C.has(lang)) return false;
  // shortcut: structure heuristic only (a brace block with statements, or an if/for/while with a `;` body); widen if a hit slips through.
  const open = body.findIndex((l) => /[{]\s*(\/\/.*)?$/.test(l));
  const close = open >= 0 && body.slice(open + 1).some((l) => /^\s*}/.test(l));
  const stmts = body.filter((l) => /;\s*(\/\/.*)?$/.test(l)).length;
  const ctrl = body.some((l) => /^\s*(if|for|while|switch|return)\b.*[({;]/.test(l));
  return (close && stmts >= 1) || (ctrl && stmts >= 2);
}

function selfCheck(): void {
  const bad = [
    '```c\nif (x) {\n  FUN_00e4a320();\n  return 1;\n}\n```',
    '```\nwhile (p) {\n  p = p->next;\n}\n```',
    'Prose with `*(int *)(ecx + 0x38)` inline.',
    'Inline `undefined4 local_8;` in prose.',
    '```cs\npublic void Init() {\n  Foo();\n}\n```',
  ];
  const good = [
    'Plain prose about `GC_malloc` `0xE4BE30` and `ecx+0x38`.',
    '```sh\nnode tools/check-docs-code.ts\n```',
    '```ini\n[GC]\nHeapMB=128\n```',
    `${MARK}\n\`\`\`c\nint f(void) {\n  return 0;\n}\n\`\`\``,
    '```\nTS3W.exe -> TS3W.exe.orig\n```',
    '| `0xE4BE30` | `GC_malloc` | `8B 44 24 04` |',
    '```ts\nfunction f() {\n  return 1;\n}\n```',
    'Prose: the timer mode of `FUN_004f30e0(2)`; an undefined block at `0xF49820`.',
  ];
  bad.forEach((t, i) => { if (check(t).length === 0) throw new Error(`self-check: bad sample ${i} not flagged`); });
  good.forEach((t, i) => { if (check(t).length !== 0) throw new Error(`self-check: good sample ${i} flagged`); });
}

function walk(dir: string): string[] {
  return readdirSync(dir).flatMap((n) => {
    const p = join(dir, n);
    return statSync(p).isDirectory() ? walk(p) : n.endsWith('.md') ? [p] : [];
  });
}

try { selfCheck(); } catch (e) { console.error((e as Error).message); process.exit(2); }
const root = process.argv[2] ?? 'docs';
let total = 0;
for (const f of walk(root)) {
  for (const n of check(readFileSync(f, 'utf8'))) { console.log(`${relative('.', f).replaceAll('\\', '/')}:${n}`); total++; }
}
console.log(total ? `${total} hit(s): rewrite as prose or allowlist with ${MARK}` : 'ok: no decompiled-looking code');
process.exit(total ? 1 : 0);
