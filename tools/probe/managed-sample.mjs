// Statistical C# profile of the sim thread through the probe's /managedstack (interpreter frame chain).
// Usage: node tools/probe/managed-sample.mjs [seconds=180] [hz=5] [out.jsonl]; prints self, inclusive and task rankings.
import { appendFileSync } from 'node:fs';
const [secs = 180, hz = 5] = process.argv.slice(2, 4).map(Number);
const out = process.argv[4];
const get = async p => (await fetch('http://127.0.0.1:47891/' + p, { signal: AbortSignal.timeout(8000) })).json();
const self = new Map(), incl = new Map(), task = new Map();
const bump = (m, k) => m.set(k, (m.get(k) ?? 0) + 1);
let n = 0, idle = 0;
const t0 = Date.now();
while (Date.now() - t0 < secs * 1000) {
  const t = Date.now();
  try {
    const r = await get('managedstack?frames=64');
    if (r.methods) {
      n++;
      if (out) appendFileSync(out, JSON.stringify(r.methods) + '\n');
      if (!r.methods.length) idle++;
      else {
        bump(self, r.methods[0]);
        for (const m of new Set(r.methods)) bump(incl, m);
        const i = r.methods.lastIndexOf('ScriptCore.ScriptProxy.Simulate');
        bump(task, i > 0 ? r.methods[i - 1] : r.methods.at(-1)); // the wrapped object's Simulate, else the task's base
      }
    }
  } catch { /* probe busy: skip this sample */ }
  const w = 1000 / hz - (Date.now() - t); if (w > 0) await new Promise(res => setTimeout(res, w));
}
const show = (title, m, k = 25) => { console.log(`\n${title}`); for (const [name, c] of [...m].sort((a, b) => b[1] - a[1]).slice(0, k)) console.log(`${(100 * c / n).toFixed(1).padStart(5)}%  ${name}`); };
console.log(`samples ${n}, no script running ${(100 * idle / n).toFixed(1)}%`);
show('SELF (innermost C# method)', self);
show('TASK (object whose Simulate runs)', task);
show('INCLUSIVE (anywhere on the stack)', incl, 40);
