// Offline self-check of unseen_water_watch stranded rule (mods/ts3agent/src/plugin/WaterRule.cs): dry ground below sea level is not water.
// Windows (.NET Framework csc).
//   node tools/selftest/waterrule/run.ts
import { execFileSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';

const ROOT = resolve(import.meta.dirname, '..', '..', '..');
const CSC = 'C:/Windows/Microsoft.NET/Framework/v4.0.30319/csc.exe';
const out = join(ROOT, 'build', 'selftest');
mkdirSync(out, { recursive: true });
const exe = join(out, 'waterrule-test.exe');
execFileSync(CSC, ['/nologo', '/optimize+', '/platform:x86', '/langversion:5', `/out:${exe}`,
  join(import.meta.dirname, 'WaterRuleTest.cs'), join(ROOT, 'mods', 'ts3agent', 'src', 'plugin', 'WaterRule.cs')], { stdio: 'inherit' });
execFileSync(exe, [], { stdio: 'inherit' });
