// ModTheSims listing images (release/mts/CHECKLIST.md): thumbnail + 3 info images, drawn as SVG from
// release/mts/mts-data.json and converted to PNG. No EA artwork, no game assets, no personal names or paths.
//   node tools/mts-images.ts                 write release/mts/images/*.png (TO FILL slots drawn dashed)
//   node tools/mts-images.ts --final         same, but exit 1 while any v0.2.0 number or feature list is still null
//   node tools/mts-images.ts --svg-only      write the SVGs only (release/mts/out/svg), no converter needed
//   node tools/mts-images.ts --self-check    offline checks, writes nothing
// PNG converter: inkscape (1.x) or rsvg-convert on PATH (Windows, Ubuntu, macOS all have packages for both).
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { execFileSync } from 'node:child_process';

const ROOT = resolve(import.meta.dirname, '..');
const DATA = join(ROOT, 'release', 'mts', 'mts-data.json');
const PNG_DIR = join(ROOT, 'release', 'mts', 'images');
const SVG_DIR = join(ROOT, 'release', 'mts', 'out', 'svg');

type Num = number | null;
type Metric = { id: string; title: string; unit: string; nofixes: Num[]; v014: Num[]; v020: Num[] };
type Data = {
  version: string; worlds: string[]; metrics: Metric[]; method: string;
  thumbnail: { claim: string; sub: string };
  features: { ts3fixes: string[]; ts3native: string[]; v020: string[] | null };
};

// Light surface; series = dataviz reference palette slots 1-2 (validated pair) + neutral gray for the baseline.
const C = { bg: '#fcfcfb', ink: '#0b0b0b', ink2: '#52514e', muted: '#8a8984', grid: '#e4e3df', base: '#a3a29c', s1: '#2a78d6', s2: '#eb6834', panel: '#f1f0ec', dark: '#14213d' };
const FONT = "Segoe UI, DejaVu Sans, Helvetica, Arial, sans-serif";

const esc = (s: string) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const text = (x: number, y: number, s: string, size: number, fill = C.ink, extra = '') =>
  `<text x="${x}" y="${y}" font-family="${FONT}" font-size="${size}" fill="${fill}" ${extra}>${esc(s)}</text>`;
const svg = (w: number, h: number, body: string) =>
  `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}">\n<rect width="${w}" height="${h}" fill="${C.bg}"/>\n${body}\n</svg>\n`;
// Bar with 4px rounded top, square at the baseline.
const bar = (x: number, y: number, w: number, h: number, fill: string) => {
  if (h <= 0) return '';
  const r = Math.min(4, h, w / 2);
  return `<path d="M${x},${y + h} V${y + r} Q${x},${y} ${x + r},${y} H${x + w - r} Q${x + w},${y} ${x + w},${y + r} V${y + h} Z" fill="${fill}"/>`;
};

export function thumbnail(d: Data): string {
  const v = d.version;
  return svg(320, 240, [
    `<rect width="320" height="240" fill="${C.dark}"/>`,
    text(20, 46, 'Sims 3', 30, '#ffffff', 'font-weight="700"'),
    text(20, 80, 'Modernization', 30, '#ffffff', 'font-weight="700"'),
    `<rect x="20" y="96" width="${16 + v.length * 11}" height="26" rx="4" fill="${C.s2}"/>`,
    text(28, 115, `v${v}`, 17, '#ffffff', 'font-weight="700"'),
    text(20, 160, d.thumbnail.claim, 21, '#ffffff', 'font-weight="600"'),
    text(20, 184, d.thumbnail.sub, 14, '#c9cfdc'),
    text(20, 222, 'Steam 1.67 · script mod + ASI plugin', 13, '#c9cfdc'),
  ].join('\n'));
}

export function features(d: Data): string {
  const W = 1200, H = 900, colW = 530;
  const col = (x: number, title: string, file: string, items: string[]) => [
    `<rect x="${x}" y="150" width="${colW}" height="430" rx="10" fill="${C.panel}"/>`,
    text(x + 28, 200, title, 30, C.ink, 'font-weight="700"'),
    text(x + 28, 234, file, 20, C.ink2, 'font-family="Consolas, DejaVu Sans Mono, monospace"'),
    ...items.map((s, i) => `<circle cx="${x + 36}" cy="${284 + i * 56}" r="6" fill="${C.s1}"/>` + text(x + 56, 292 + i * 56, s, 24)),
  ].join('\n');
  const v020 = d.features.v020;
  const newBlock = v020 && v020.length
    ? v020.slice(0, 6).map((s, i) => { // shortcut: 2 columns x 3 rows, longer lists are cut; keep items under ~38 chars
        const x = 76 + (i % 2) * 540, y = 680 + Math.floor(i / 2) * 50;
        return `<circle cx="${x}" cy="${y}" r="6" fill="${C.s2}"/>` + text(x + 20, y + 8, s, 24);
      }).join('\n')
    : `<rect x="60" y="650" width="1080" height="90" rx="10" fill="none" stroke="${C.muted}" stroke-width="2" stroke-dasharray="10 8"/>` + text(84, 705, 'TO FILL: the v0.2.0 features that passed their A/B (release-features.ts)', 24, C.muted);
  return svg(W, H, [
    text(60, 80, 'What it does', 44, C.ink, 'font-weight="700"'),
    text(60, 120, 'Two parts. Each works on its own; together they give the most.', 24, C.ink2),
    col(60, 'Script mod', 'Mods\\Packages\\ts3fixes.package', d.features.ts3fixes),
    col(610, 'Native plugin', 'Game\\Bin\\ts3native.asi + .ini', d.features.ts3native),
    text(60, 632, `New in v${d.version}`, 28, C.ink, 'font-weight="700"'),
    newBlock,
    text(60, 840, 'Same game code, same results: work nobody can see is skipped or moved off the main thread.', 22, C.ink2),
  ].join('\n'));
}

export function benchmark(d: Data): string {
  const W = 1200, H = 900;
  const series: [keyof Metric & ('nofixes' | 'v014' | 'v020'), string, string][] = [['nofixes', 'No fixes', C.base], ['v014', 'v0.1.4', C.s1], ['v020', `v${d.version}`, C.s2]];
  const panel = (m: Metric, top: number) => {
    const ph = 250, px = 60, pw = 1080, base = top + 60 + ph;
    const vals = [...m.nofixes, ...m.v014, ...m.v020].filter((v): v is number => v != null);
    const max = Math.max(...vals) * 1.15;
    const out = [text(px, top + 30, m.title, 24, C.ink, 'font-weight="600"'), `<line x1="${px}" y1="${base}" x2="${px + pw}" y2="${base}" stroke="${C.ink2}" stroke-width="1"/>`];
    const gw = pw / d.worlds.length, bw = 70, gap = 2;
    d.worlds.forEach((world, wi) => {
      const gx = px + wi * gw + (gw - (bw * 3 + gap * 2)) / 2;
      series.forEach(([key, , color], si) => {
        const v = m[key][wi], x = gx + si * (bw + gap);
        if (v == null) {
          const h = ph * 0.35;
          out.push(`<rect x="${x + 1}" y="${base - h}" width="${bw - 2}" height="${h}" fill="none" stroke="${color}" stroke-width="2" stroke-dasharray="6 5"/>`);
          out.push(text(x + bw / 2, base - h - 10, 'TO FILL', 15, C.muted, 'text-anchor="middle"'));
        } else {
          const h = (v / max) * ph;
          out.push(bar(x, base - h, bw, h, color));
          out.push(text(x + bw / 2, base - h - 10, String(v), 18, C.ink, 'text-anchor="middle"'));
        }
      });
      out.push(text(px + wi * gw + gw / 2, base + 32, world, 22, C.ink2, 'text-anchor="middle"'));
    });
    return out.join('\n');
  };
  const legend = series.map(([, label, color], i) => `<rect x="${60 + i * 190}" y="112" width="18" height="18" rx="3" fill="${color}"/>` + text(86 + i * 190, 128, label, 20, C.ink2)).join('\n');
  return svg(W, H, [
    text(60, 70, 'Measured in game', 44, C.ink, 'font-weight="700"'),
    legend,
    panel(d.metrics[0], 150),
    panel(d.metrics[1], 500),
    text(60, 880, d.method, 15, C.muted),
  ].join('\n'));
}

export function install(): string {
  const W = 1200, H = 900, mono = 'font-family="Consolas, DejaVu Sans Mono, monospace"';
  const box = (x: number, y: number, w: number, h: number, title: string, lines: string[], accent: string) => [
    `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="10" fill="${C.panel}"/>`,
    `<rect x="${x}" y="${y}" width="8" height="${h}" rx="4" fill="${accent}"/>`,
    text(x + 30, y + 44, title, 26, C.ink, 'font-weight="700"'),
    ...lines.map((s, i) => text(x + 30, y + 86 + i * 34, s, 21, i % 2 ? C.ink : C.ink2, i % 2 ? mono : '')),
  ].join('\n');
  const arrow = (x1: number, y1: number, x2: number, y2: number) =>
    `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${C.ink2}" stroke-width="3"/><path d="M${x2},${y2} l-14,-8 v16 Z" fill="${C.ink2}" transform="rotate(${Math.atan2(y2 - y1, x2 - x1) * 180 / Math.PI} ${x2} ${y2})"/>`;
  return svg(W, H, [
    text(60, 70, 'Install in two places', 44, C.ink, 'font-weight="700"'),
    text(60, 110, 'Close the game and back up Documents\\Electronic Arts\\The Sims 3 first.', 22, C.ink2),
    box(60, 160, 340, 330, 'The download', ['Mods\\Packages\\', 'ts3fixes.package', 'Game\\Bin\\', 'ts3native.asi', 'Game\\Bin\\', 'ts3native.ini', 'and README.txt'], C.base),
    arrow(400, 260, 500, 230),
    arrow(400, 400, 500, 500),
    box(500, 140, 640, 230, '1. Script mod', ['Documents\\Electronic Arts\\The Sims 3\\', 'Mods\\Packages\\ts3fixes.package', 'plus Mods\\Resource.cfg (see README.txt)'], C.s1),
    box(500, 400, 640, 270, '2. Native plugin', ['Steam: right-click The Sims 3, Manage, Browse local files', 'Game\\Bin\\ts3native.asi + ts3native.ini', 'needs an ASI loader (not included):', 'wininet.dll from Ultimate ASI Loader (Win32)', 'or the one S3SS already installed'], C.s2),
    text(60, 740, 'Then start the game from Steam and allow script mods.', 24, C.ink),
    text(60, 780, 'Check it works: Game\\Bin\\ts3native.log appears after the first start.', 22, C.ink2),
    text(60, 840, 'Steam version 1.67 only. Uninstall: delete the files you added; saves are not changed.', 22, C.ink2),
  ].join('\n'));
}

export function render(d: Data): Record<string, string> {
  return { 'thumbnail.png': thumbnail(d), '1-features.png': features(d), '2-benchmark.png': benchmark(d), '3-install.png': install() };
}

export function missing(d: Data): string[] {
  const out: string[] = [];
  d.metrics.forEach((m) => m.v020.forEach((v, i) => { if (v == null) out.push(`metrics.${m.id}.v020[${i}] (${d.worlds[i]})`); }));
  if (!d.features.v020 || !d.features.v020.length) out.push('features.v020');
  if (/v0\.1\.4/.test(d.thumbnail.sub)) out.push('thumbnail.sub still names v0.1.4');
  return out;
}

function converter(): (svgFile: string, pngFile: string, w: number, h: number) => void {
  const has = (cmd: string, args: string[]) => { try { execFileSync(cmd, args, { stdio: 'ignore', timeout: 60000 }); return true; } catch { return false; } };
  if (has('inkscape', ['--version'])) return (s, p, w, h) => execFileSync('inkscape', [s, '--export-type=png', `--export-filename=${p}`, `--export-width=${w}`, `--export-height=${h}`], { stdio: 'ignore', timeout: 120000 });
  if (has('rsvg-convert', ['--version'])) return (s, p, w, h) => execFileSync('rsvg-convert', ['-w', String(w), '-h', String(h), '-o', p, s], { stdio: 'ignore', timeout: 120000 });
  throw new Error('no SVG converter: install Inkscape 1.x or rsvg-convert (librsvg), or use --svg-only');
}

function selfCheck() {
  const d: Data = JSON.parse(readFileSync(DATA, 'utf8'));
  const out = render(d);
  for (const [name, s] of Object.entries(out)) {
    const opens = (s.match(/<(?!\/)[a-z]+[^>]*[^/]>/g) || []).length, closes = (s.match(/<\/[a-z]+>/g) || []).length;
    if (opens !== closes) throw new Error(`self-check: ${name} tags not balanced (${opens} open, ${closes} close)`);
    if (/undefined|NaN/.test(s)) throw new Error(`self-check: ${name} has undefined/NaN`);
    if (/[A-Z]:\\Users\\|\/home\/|\/Users\//i.test(s)) throw new Error(`self-check: ${name} has a personal path`);
  }
  if (!/width="320" height="240"/.test(out['thumbnail.png'])) throw new Error('self-check: thumbnail size');
  if (!out['2-benchmark.png'].includes('TO FILL') && missing(d).length) throw new Error('self-check: missing numbers not drawn as TO FILL');
  const full: Data = structuredClone(d);
  full.metrics.forEach((m) => { m.v020 = m.v014.map((v) => v); });
  full.features.v020 = ['a', 'b'];
  full.thumbnail.sub = 'v0.2.0, measured';
  if (missing(full).length) throw new Error('self-check: missing() on a full data set');
  if (render(full)['2-benchmark.png'].includes('TO FILL')) throw new Error('self-check: TO FILL drawn with full data');
  if (esc('a<&>"') !== 'a&lt;&amp;&gt;&quot;') throw new Error('self-check: esc');
  console.log('mts-images self-check ok');
}

const args = process.argv.slice(2);
if (args.includes('--self-check')) selfCheck();
else {
  const d: Data = JSON.parse(readFileSync(DATA, 'utf8'));
  const gaps = missing(d);
  if (gaps.length) console.log(`TO FILL (${gaps.length}): ${gaps.join(', ')}`);
  mkdirSync(SVG_DIR, { recursive: true });
  const conv = args.includes('--svg-only') ? null : converter();
  if (conv) mkdirSync(PNG_DIR, { recursive: true });
  for (const [name, s] of Object.entries(render(d))) {
    const svgFile = join(SVG_DIR, name.replace(/\.png$/, '.svg'));
    writeFileSync(svgFile, s);
    const [, w, h] = /width="(\d+)" height="(\d+)"/.exec(s)!;
    if (conv) { conv(svgFile, join(PNG_DIR, name), +w, +h); console.log(`wrote release/mts/images/${name} (${w}x${h})`); }
    else console.log(`wrote release/mts/out/svg/${name.replace(/\.png$/, '.svg')}`);
  }
  if (args.includes('--final') && gaps.length) { console.error('--final: fill release/mts/mts-data.json first'); process.exit(1); }
}
