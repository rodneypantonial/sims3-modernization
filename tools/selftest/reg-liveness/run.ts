// Self-check of tools/reg-liveness.py (docs/237): synthetic caller/callee bytes, then the [LotSinCos] validation case on
// the local Steam exe and 1.69 image when they are there. Needs python 3 with iced-x86 (pip install iced-x86).
//   node tools/selftest/reg-liveness/run.ts
import { execFileSync } from 'node:child_process';
import { join, resolve } from 'node:path';

const ROOT = resolve(import.meta.dirname, '..', '..', '..');
// shell: on Windows `python` is often a pyenv .bat shim, which execFile without a shell skips for the Store alias
execFileSync('python', [JSON.stringify(join(ROOT, 'tools', 'reg-liveness.py')), '--self-check'], { stdio: 'inherit', timeout: 120_000, shell: true });
