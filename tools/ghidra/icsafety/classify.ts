// node classify.ts scan6.tsv overrides.json out.csv  -> per-IC verdict CSV (one row per binding)
import { readFileSync, writeFileSync } from 'node:fs';
import { parse } from './stats.ts';
const [scan, ovr, outCsv] = process.argv.slice(2);
const ps = readFileSync(scan, 'utf8').trim().split(/\r?\n/).map(l => parse(l.split('\t')));
const over: Record<string, { verdict?: string; confidence?: string; note?: string }> = JSON.parse(readFileSync(ovr, 'utf8'));
const q = (s: string) => /[",]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
const short = (s: string) => s.replace(/^0x/, '').split(' ').filter(Boolean);
const NAMED: Record<string, string> = { '11da098': 'audioSys', '11e9400': 'camera', '11d4bbc': 'scriptHost', '11ee514': 'scriptHost', '11b1378': 'msgServer' };
function glist(s: string | undefined, max = 6) {
  const xs = (s || '').split(' ').filter(Boolean);
  const more = xs.length && xs[xs.length - 1].startsWith('+') ? xs.pop() : '';
  const out = xs.slice(0, max).map(x => { const [a, n] = x.split('='); return n ? n : NAMED[a] ? `0x${a.toUpperCase()}(${NAMED[a]})` : '0x' + a.toUpperCase(); });
  const extra = xs.length - max + (more ? parseInt(more.slice(1)) : 0);
  return out.join(' ') + (extra > 0 ? ` +${extra}` : '');
}
const rows: string[] = ['ic,name,address,reads,writes,locks,threadcheck,posts,callsManaged,allocs,verdict,confidence,note'];
const count: Record<string, number> = {};
const conf: Record<string, number> = {};
for (const p of ps) {
  const fl: Record<string, string> = p.flags || {};
  const keys = Object.keys(fl);
  const has = (k: string) => keys.filter(x => x.startsWith(k));
  const H = (p.H || '[]').slice(1, -1).split(', ').filter(Boolean);
  const [indRoot, indAll] = (p.ind || '0/0').split('/').map(Number);
  const n = parseInt(p.n); const cap = String(p.n).endsWith('+');
  const trunc = Number(p.tr || 0), open = Number(p.open || 0); // undisassembled tail (already in indAll) / callees cut by depth
  const locks = [...new Set((p.L || '[]').slice(1, -1).split(', ').filter(Boolean).map((s: string) => { const [a] = s.split('@'); return a === 'member' ? 'member-lock@' + s.split('@')[1] : a.replace(/^0x[0-9a-f]+=/, ''); }))];
  if (has('res:').length) locks.push('g_ResourceLock (ResourceMan)');
  const thread = has('thread:').map(k => k.split(':')[1]);
  const posts = [...has('post:'), ...has('wait:')].map(k => k.replace(/^(post|wait):/, k.startsWith('wait') ? 'wait:' : ''));
  const managed = [...has('managed:'), ...has('yield:'), ...has('task:')].map(k => k.replace(/^managed:/, ''));
  const allocs = has('alloc:').map(k => k.split(':')[1]);
  const mono = has('mono:').length > 0;
  const d3dAll = has('d3d:').map(k => k.split(':')[1]); const d3d = d3dAll.filter(x => x !== 'GraphicsDevice_Get');
  const handleLookup = H.includes('cc'); const handleCreate = H.includes('c8');
  const reads = [glist(p.R), handleLookup ? 'object via script handle' : '', indAll ? `${indAll} unresolved virtual calls` : '', trunc ? `${trunc} undisassembled body tail(s)` : '', open ? `${open} callees past depth not scanned` : ''].filter(Boolean).join('; ');
  const writes = [glist(p.W), handleCreate ? 'script handle table (create)' : ''].filter(Boolean).join('; ');
  let verdict = 'PROXY', why = '';
  const ownerChecked = thread.includes('Device_IsOwnerThread');
  if (p.fn === 'NOFUNC') why = 'no Ghidra function at the IC address: body not scanned'; // was SAFE by default (UIManager_GetWorldMetadataImpl)
  else if (has('yield:').length || keys.includes('managed:ScriptHost_RunTask')) { verdict = 'UNSAFE'; why = 'yields or waits on the calling script task'; }
  else if (has('managed:').length) { verdict = 'UNSAFE'; why = 'calls back into managed code synchronously (' + has('managed:').map(k => k.split(':')[1]).join(' ') + ')'; }
  else if (has('task:').length) { verdict = 'UNSAFE'; why = 'reads the current script task id (host +0x118): sim-thread task context'; }
  else if (d3d.length && !ownerChecked) { verdict = 'MAIN'; why = 'device work on the calling thread (' + d3d.join(' ') + ')'; }
  else {
    const w = (p.W || '').trim();
    const onlyBenign = keys.every(k => /^(nalloc|atomic|alloc:|mono:)/.test(k));
    const hwAll = Number((p.hw || '0/0').split('/')[1]);
    if (onlyBenign && !w && hwAll === 0 && indAll === 0 && open === 0 && !cap && !handleLookup && !handleCreate && !locks.length && !posts.length) { verdict = 'SAFE'; why = 'no global writes, no unresolved virtual calls, no handle lookup'; }
    else if (posts.some(x => /PostRenderMessage|RenderMessageBuffer/.test(x))) why = 'posts to the render message buffer (sim-thread producer, ordered with the tick)';
    else if (handleLookup) why = 'acts on a script-handle object (handle table locked, object not)';
    else if (w || hwAll) why = 'writes shared native state (globals or object fields) without a covering lock';
    else if (trunc) why = 'closure ends in code Ghidra did not disassemble (unresolved, may hide a virtual call)';
    else if (open || cap) why = 'closure not seen whole (cut by depth or size cap)';
    else why = 'reads or calls into sim-owned native objects through virtual calls';
  }
  if (verdict === 'UNSAFE' && d3d.length && !ownerChecked) why += '; also device work on the calling thread (' + d3d.join(' ') + '): MAIN hazard too';
  if (d3dAll.includes('GraphicsDevice_Get') && !d3d.length) why += '; reads the device pointer (GraphicsDevice_Get), no device call found';
  if (allocs.length && verdict === 'SAFE') why += '; managed alloc needs docs/30 P3';
  if (mono && verdict === 'SAFE') why += '; Mono metadata calls';
  let confidence = cap || p.fn === 'NOFUNC' ? 'low' : indAll === 0 && n <= 40 ? 'high' : indAll <= 4 ? 'medium' : 'low';
  if (verdict === 'UNSAFE' || verdict === 'MAIN') confidence = confidence === 'low' ? 'medium' : 'high'; // a found path is positive evidence
  for (const nm of p.names) {
    const o = over[nm] || over[p.addr] || over[nm.split('::')[0]] || {};
    const v = o.verdict || verdict, c = o.confidence || confidence;
    const note = [o.note, o.verdict && o.verdict !== verdict ? `scan said ${verdict}` : '', why].filter(Boolean).join('; ') + (p.names.length > 1 ? `; body shared by ${p.names.length} ICs` : '');
    count[v] = (count[v] || 0) + 1; conf[c] = (conf[c] || 0) + 1;
    rows.push(['ScriptCore.' + nm, nm.split('::')[1], '0x' + p.addr.toUpperCase(), reads, writes, locks.join(' '), thread.join(' ') || 'none', posts.join(' '), managed.join(' '), allocs.join(' ') + (mono ? (allocs.length ? ' ' : '') + 'mono-runtime' : ''), v, c, note].map(x => q(String(x))).join(','));
  }
}
writeFileSync(outCsv, rows.join('\n') + '\n');
console.log(rows.length - 1, count, conf);
