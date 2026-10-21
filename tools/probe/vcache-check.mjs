// Self-check of the docs/133 callvirt-cache walker in agent.js (the block between "--- vcache begin" and "--- vcache end")
// on synthetic memory, no game: class-cache walk, mint decode, transformed bit, pointer guards, size cap, slices, dry mode.
//   node tools/probe/vcache-check.mjs
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const src = readFileSync(join(import.meta.dirname, 'agent.js'), 'utf8');
const block = src.slice(src.indexOf('// --- vcache begin'), src.indexOf('// --- vcache end'));
assert.ok(block.includes('function vcWalker'), 'vcache block not found in agent.js');
const { vcWalker, vcSelfCheck, vcDecode } = new Function(`${block}; return { vcWalker, vcSelfCheck, vcDecode };`)();
vcSelfCheck();

// synthetic process: u32 words, code regions (u16 words), C strings; everything else is unmapped
const L = new Uint8Array(0x1c1); L[0x06] = 1; L[0xea] = 2; L[0xeb] = 3; L[0xec] = 4; L[0xed] = 5;
function world() {
  const W = new Map(), code = [], str = new Map();
  const set = (a, ...v) => v.forEach((x, i) => W.set(a + 4 * i, x));
  const G_CORLIB = 0x110000, G_SIM = 0x110004, IMG = 0x120000, H = 0x130000, B = 0x131000, S1 = 0x132000, S3 = 0x132100;
  const CLS = 0x140000, ARR = 0x141000, M1 = 0x150000, M2 = 0x150100, M3 = 0x150200, M5 = 0x150300;
  const R1 = 0x160000, R2 = 0x160100, R3 = 0x160200, R5 = 0x160300, C1 = 0x170000, C3 = 0x171000, C5 = 0x180000, BAD = 0x900000;
  set(G_CORLIB, IMG); set(G_SIM, 0);
  set(IMG + 0x298, H); set(H + 8, B, 3);
  set(B, S1, 0, S3);
  set(S1, 1, CLS, 0);
  set(S3, 2, BAD, S3); // a chain that points at itself (rehash race): bounded by the chain guard; its class is unmapped
  set(CLS, IMG); set(CLS + 0x28, 0); set(CLS + 0x34, 0x200000, 0x200010); set(CLS + 0x70, 4); set(CLS + 0x94, ARR);
  str.set(0x200000, 'Foo'); str.set(0x200010, 'Game');
  set(ARR, M1, M2, M5, 0x990000); // the last method pointer is unmapped
  for (const [m, r, n, a] of [[M1, R1, 'Run', 0x200020], [M2, R2, 'Virt', 0x200030], [M3, R3, 'Gen', 0x200040], [M5, R5, 'Big', 0x200050]]) {
    set(m + 8, CLS); set(m + 0x14, r); set(m + 0x18, a); str.set(a, n); set(r + 8, m);
  }
  // R1: callvirt (cache item 0, decl item 2) + call item 3 -> R3 (a method no class cache lists, like an inflated generic)
  code.push({ base: C1, w: Uint16Array.from([0xec, 0, 0, 2, 0xea, 3, 0x06, 0x06]) });
  set(R1, C1); set(R1 + 0x14, C1 + 16); set(R1 + 0x44, 0x100); set(C1 + 16, CLS, R2, R2, R3);
  set(R2 + 0x44, 0); // R2 not transformed yet: skipped as a caller, still a valid target / declared method
  // R3: con.callvirt (5 words) whose cache holds an unmapped class and target R1
  code.push({ base: C3, w: Uint16Array.from([0xed, 0, 0, 0, 2, 0x06, 0x06, 0x06]) });
  set(R3, C3); set(R3 + 0x14, C3 + 16); set(R3 + 0x44, 0x100); set(C3 + 16, BAD, R1, R1);
  // R5: transformed, but its items sit 128 KB after the code: over the 64 KB read cap, never read
  set(R5, C5); set(R5 + 0x14, C5 + 0x20000); set(R5 + 0x44, 0x100);
  let clock = 0, reads = 0;
  const mem = {
    u32(a) { reads++; return W.has(a) ? W.get(a) >>> 0 : null; },
    u16s(a, n) {
      const c = code.find(r => a >= r.base && a < r.base + 2 * r.w.length);
      if (!c) return null;
      assert.ok(n <= 0x10000, `code read of ${n} bytes over the cap`);
      return c.w.slice((a - c.base) / 2, (a - c.base) / 2 + Math.floor(Math.min(n, c.base + 2 * c.w.length - a) / 2));
    },
    cstr: a => str.get(a) ?? null,
    lens: () => L,
    tick: () => Math.floor(clock / 16),
    now: () => clock++,
    sleep(ms) { clock += ms; },
  };
  return { mem, seeds: { corlib: G_CORLIB, simulator: G_SIM }, reads: () => reads };
}
const run = (w, o) => { const x = vcWalker(w.mem, w.seeds); let r, calls = 0; do { r = x.step(o); calls++; } while (!r.done && calls < 100000); return { x, r, calls }; };

// full snapshot in small slices
{
  const w = world(), { x, r, calls } = run(w, { sliceMs: 3, waitMs: 20 });
  assert.equal(r.done, true); assert.equal(r.partial, false);
  assert.ok(calls > 1, 'a snapshot must take several slices');
  assert.ok(r.tickAligned > 0, 'slices wait for the tick');
  const d = x.dump();
  const by = Object.fromEntries(d.sites.map(s => [`${s.caller}@${s.off}`, s]));
  assert.deepEqual(Object.keys(by).sort(), ['Game.Foo::Gen@0', 'Game.Foo::Run@0']);
  assert.deepEqual(by['Game.Foo::Run@0'].targets, { 'Game.Foo::Virt': 1 });
  assert.deepEqual(by['Game.Foo::Run@0'].classes, { 'Game.Foo': 1 });
  assert.equal(by['Game.Foo::Run@0'].decl, 'Game.Foo::Virt');
  assert.deepEqual(by['Game.Foo::Gen@0'].targets, { 'Game.Foo::Run': 1 });
  assert.deepEqual(by['Game.Foo::Gen@0'].classes, { '?': 1 });
  assert.equal(d.stats.tooBig, 1);
  assert.ok(d.stats.notTransformed >= 1);
  assert.equal(r.methods, 3); // R1, R3, R5 (R2 not transformed)
  // a second snapshot reuses the decoded methods and counts each site once more
  const r2 = run(w, { sliceMs: 1000 }).r; // fresh walker: same result in one slice
  assert.equal(r2.sites, 2);
  const again = x.step({ sliceMs: 1000 });
  assert.equal(again.done, true); assert.equal(again.newPairs, 0);
  assert.equal(x.dump().sites.find(s => s.caller === 'Game.Foo::Run').snaps, 2);
}
// a slice never runs much past its time cap (the bp-vres failure was one call holding the agent for > 60 s)
{
  const w = world(), x = vcWalker(w.mem, w.seeds);
  const r = x.step({ sliceMs: 2, waitMs: 0 });
  assert.equal(r.done, false);
  assert.ok(r.workMs <= 40, `slice took ${r.workMs} virtual ms`);
}
// dry mode: N methods, no call / target following (R3 is never reached)
{
  const w = world(), { x, r } = run(w, { dry: 2, sliceMs: 1000 });
  assert.equal(r.done, true); assert.equal(r.dry, 2); assert.equal(r.partial, false); assert.equal(r.methods, 2);
  assert.deepEqual(x.dump().sites.map(s => s.caller), ['Game.Foo::Run']);
}
// wall budget: partial, and the next call starts a new snapshot
{
  const w = world(), x = vcWalker(w.mem, w.seeds);
  const r = x.step({ budgetS: 0.001, sliceMs: 1, waitMs: 0 });
  assert.equal(r.done, true); assert.equal(r.partial, true);
  assert.equal(x.step({ sliceMs: 1000 }).snap, 2);
}
// decoder: a truncated switch and an unknown opcode stop the decode without a site
assert.equal(vcDecode(Uint16Array.from([0xe8]), L).bad, 0);
assert.deepEqual(vcDecode(Uint16Array.from([0xec, 1, 0]), L).sites, []);
console.log('vcache-check ok');
