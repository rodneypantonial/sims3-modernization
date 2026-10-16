// Self-check of the G11 accounting in agent.js (the block between "--- g11 begin" and "--- g11 end"), no game:
//   node tools/probe/g11-check.mjs
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const src = readFileSync(join(import.meta.dirname, 'agent.js'), 'utf8');
const block = src.slice(src.indexOf('// --- g11 begin'), src.indexOf('// --- g11 end'));
assert.ok(block.includes('function g11Tracker'), 'g11 block not found in agent.js');
const g11Tracker = new Function(`${block}; return g11Tracker;`)();

const SIM = 1, W = 2;
const g = g11Tracker(0);
// second 0: plan 0-4 ms (its own gate entry and lock hold do not end the window), then the sim thread's next touch at 10 ms
g.planEnter(true, 0);
g.gateEnter(SIM, true, 'goalStep', 1); g.gateLeave(SIM, 2);
g.lockEnter(SIM, true, 2); g.lockHeld(SIM, 2); assert.equal(g.lockRelease(SIM, 3), true);
g.planLeave(true, 4);
g.lockEnter(SIM, true, 10); g.lockHeld(SIM, 10);           // gap 6 ms, plan 4 ms: min(4, 6) = 4
g.gateEnter(SIM, true, 'qReset', 11); g.gateLeave(SIM, 12); // under the lock: not unlocked
assert.equal(g.lockRelease(SIM, 12), true);
// a worker-thread entry without the lock counts as unlocked; nested gate entries only once as outer
g.gateEnter(W, false, 'lotTableFind', 100); g.gateEnter(W, false, 'qReset', 101); g.gateLeave(W, 102); g.gateLeave(W, 103);
// second 1: plan 1000-1010, gap closed by an unlocked gate entry at 1012 -> min(10, 2) = 2
g.planEnter(true, 1000); g.planLeave(true, 1010);
g.gateEnter(SIM, true, 'connectedRooms', 1012); g.gateLeave(SIM, 1013);
// second 2: plan 2000-2001, next plan at 2050 closes it -> min(1, 49) = 1; that plan stays open (not counted)
g.planEnter(true, 2000); g.planLeave(true, 2001);
g.planEnter(true, 2050); g.planLeave(true, 2060);
// a worker plan is not a sim-thread plan; a release without an acquire seen is ignored
g.planEnter(false, 2100); g.planLeave(false, 2200);
assert.equal(g.lockRelease(W, 2300), false);

const r = g.report(3000);
assert.equal(r.plans, 4);
assert.equal(r.movableMsPerSec, 2.333); // (4 + 2 + 1) / 3 s
assert.deepEqual(r.series.movableMs, [4, 2, 1]);
assert.equal(r.open, true);
assert.deepEqual(r.sites.qReset, { entries: 2, unlocked: 1, unlockedSim: 0, unlockedOuter: 0, unlockedPerSec: 0 });
assert.deepEqual(r.sites.goalStep, { entries: 1, unlocked: 1, unlockedSim: 1, unlockedOuter: 1, unlockedPerSec: 0.333 });
assert.equal(r.sites.lotTableFind.unlockedOuter, 1);
assert.deepEqual(r.series.unlockedGate, [2, 1, 0]);
// busy: 1-3 (gate 1-2 then lock 2-3), 10-12, 100-103, 1012-1013 = 2 + 2 + 3 + 1 = 8 ms of 3000
assert.equal(r.freeShare, Math.round((1 - 8 / 3000) * 1000) / 1000);
assert.deepEqual(r.series.busyMs, [7, 1, 0]);
assert.equal(r.movablePerPlan.max, 4);
console.log('g11 self-check ok');
