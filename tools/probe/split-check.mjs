// Self-check of the address-space split in agent.js (the block between "--- split begin" and "--- split end"), no game:
//   node tools/probe/split-check.mjs
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const src = readFileSync(join(import.meta.dirname, 'agent.js'), 'utf8');
const block = src.slice(src.indexOf('// --- split begin'), src.indexOf('// --- split end'));
assert.ok(block.includes('function bucketOf'), 'split block not found in agent.js');
const MEM_IMAGE = 0x1000000, MEM_MAPPED = 0x40000, MEM_PRIVATE = 0x20000;
const bucketOf = new Function('k32', 'MEM_IMAGE', 'MEM_MAPPED', `${block}; return bucketOf;`)(null, MEM_IMAGE, MEM_MAPPED);
const bs = String.fromCharCode(92);
const dev = p => p.split('/').join(bs);
assert.equal(bucketOf(MEM_IMAGE, 'd3d9.dll', null), 'driverImg');
assert.equal(bucketOf(MEM_IMAGE, 'igvk32.dll', null), 'driverImg');
assert.equal(bucketOf(MEM_IMAGE, 'nvoglv32.dll', null), 'driverImg');
assert.equal(bucketOf(MEM_IMAGE, 'vulkan-1.dll', null), 'driverImg');
assert.equal(bucketOf(MEM_IMAGE, 'kernel32.dll', null), 'imageOther');
assert.equal(bucketOf(MEM_IMAGE, 'Sims3Launcher.exe', null), 'imageOther');
assert.equal(bucketOf(MEM_IMAGE, null, null), 'imageOther');
assert.equal(bucketOf(MEM_MAPPED, null, null), 'anonMapped');
assert.equal(bucketOf(MEM_MAPPED, null, dev('/Device/HarddiskVolume3/Users/x/Documents/Electronic Arts/The Sims 3/Mods/Packages/cc.package')), 'ccPkg');
assert.equal(bucketOf(MEM_MAPPED, null, dev('/Device/HarddiskVolume3/Program Files/EA Games/The Sims 3/GameData/Shared/Packages/FullBuild0.package')), 'gamePkg');
assert.equal(bucketOf(MEM_MAPPED, null, dev('/Device/HarddiskVolume3/Windows/Fonts/arial.ttf')), 'otherMapped');
assert.equal(bucketOf(MEM_PRIVATE, null, null), 'private');
console.log('split-check: ok');
