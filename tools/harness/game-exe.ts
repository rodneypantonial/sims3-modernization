// Which game edition the harness drives. Defaults = the Steam edition; the EA app edition needs only environment settings:
//   TS3_BIN          Game\Bin folder (default: Steam library path)
//   TS3_EXE          game process/file name (default TS3W.exe; EA app: TS3.exe or Sims3Launcher.exe)
//   TS3_LAUNCH       direct (default: start <TS3_BIN>\<TS3_EXE>) | ea (EALauncher.exe with TS3_EA_URI, then wait for the TS3_EXE process)
//   TS3_EA_URI, TS3_EA_LAUNCHER   EA launch URI and EALauncher.exe path (defaults below)
//   TS3_MACHINE      machine tag (default main): result rows/files of any other machine carry it and never mix with the main machine's (docs/TESTING.md)
//   node tools/harness/game-exe.ts --self-check   no process is started
//   node tools/harness/game-exe.ts --installed    base game + EP/SP folders found under the game root
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

export const MACHINE = process.env.TS3_MACHINE || 'main';
export const machineTag = (m = MACHINE) => (m === 'main' ? '' : m); // '' on the main machine: its file names and labels stay unchanged

export const GAME_EXE = process.env.TS3_EXE ?? 'TS3W.exe';
export const GAME_NAME = GAME_EXE.replace(/\.exe$/i, ''); // PowerShell Get-Process / Windows counters name
export const BIN = process.env.TS3_BIN ?? 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\The Sims 3\\Game\\Bin';
// Installed products from the game root (<TS3_BIN>/../..), same on Steam and the EA app: base game + every EPnn/SPnn folder that holds GameData.
// The "Installed" line in ScriptError xml is written by the game itself (not by the harness) from its own check; the HKLM Uninstall keys list the same set.
export const gameRoot = (bin = BIN) => resolve(bin, '..', '..');
export const installedProducts = (bin = BIN): string[] => {
  const root = gameRoot(bin);
  if (!existsSync(root)) return [];
  return ['BaseGame', ...readdirSync(root).filter(d => /^(EP|SP)\d+$/i.test(d) && existsSync(join(root, d, 'GameData'))).sort()];
};
export const LAUNCH = process.env.TS3_LAUNCH ?? 'direct';
export const EA_URI = process.env.TS3_EA_URI ?? 'origin2://game/launch/?offerIds=sims3_dd,1002441,70384_CE,70384_Unrevised';
export const EA_LAUNCHER = process.env.TS3_EA_LAUNCHER ?? 'C:\\Program Files\\Electronic Arts\\EA Desktop\\EA Desktop\\EALauncher.exe';
export const isSteamGame = (exe = GAME_EXE) => exe.toLowerCase() === 'ts3w.exe';
// Edition from the game exe itself (PE TimeDateStamp + sha256), never from TS3_LAUNCH/TS3_EXE alone: mod mode (queue.ts) installs
// the asi + ini of the edition this says. EA app 1.69.47: TS3.exe and Sims3Launcher.exe are the same file (docs/232).
export const EXE_STAMPS: Record<string, number> = { steam: 0x52DEC247, ea169: 0x6707155C };
export const EA169_EXE_SHA256 = ['7352dd6e599f4475f812bbca19bbc7e5d8e84bbd5b6d21acaac9b1c5236adfe4'];
export const peStamp = (b: Buffer): number | undefined => { try { return b.readUInt16LE(0) === 0x5a4d ? b.readUInt32LE(b.readUInt32LE(0x3c) + 8) : undefined; } catch { return undefined; } };
// why = '' when usable. The Steam set-up (TS3W.exe, TS3_LAUNCH direct) with an unknown or missing exe stays 'steam', as before.
export function editionOf(exe: Buffer | null, launch = LAUNCH, gameExe = GAME_EXE): { edition: 'steam' | 'ea169'; why: string } {
  const steamSetup = launch !== 'ea' && isSteamGame(gameExe), st = exe ? peStamp(exe) : undefined;
  const hex = st === undefined ? 'none' : `0x${st.toString(16).toUpperCase().padStart(8, '0')}`;
  if (st === EXE_STAMPS.ea169) {
    const sha = createHash('sha256').update(exe!).digest('hex');
    return { edition: 'ea169', why: EA169_EXE_SHA256.includes(sha) ? '' : `${gameExe} has the EA 1.69 PE stamp but sha256 ${sha.slice(0, 12)} is not on the 1.69.47 list (EA169_EXE_SHA256 in game-exe.ts)` };
  }
  if (steamSetup) return { edition: 'steam', why: '' };
  return { edition: 'ea169', why: !exe ? `${gameExe} not found in TS3_BIN` : st === EXE_STAMPS.steam ? `TS3_LAUNCH=${launch} / TS3_EXE=${gameExe} but the exe is the Steam TS3W.exe (PE stamp ${hex})` : `${gameExe} PE stamp ${hex} is no known edition (Steam 1.67 or EA app 1.69.47)` };
}
// EA mod mode: TS3_NATIVE_DIR holds the EA CI build (game-copy/ci-ea169-<sha7>/ts3native-ea169/out-ea169): the asi and the
// shipped-default EA ini (ts3native-ea169-rc.ini, tools/ea-ini.ts). The asi must be an EA 1.69.47 build (its own host check string).
export const EA_ASI = 'ts3native-ea169.asi', EA_INI = 'ts3native-ea169-rc.ini', EA_ASI_MARK = 'host build: EA app 1.69.47';
export function nativeDirProblem(dir: string | undefined): string {
  if (!dir) return `TS3_NATIVE_DIR is not set: point it at the EA build folder holding ${EA_ASI} + ${EA_INI} (game-copy/ci-ea169-<sha7>/ts3native-ea169/out-ea169)`;
  for (const f of [EA_ASI, EA_INI]) if (!existsSync(join(dir, f))) return `TS3_NATIVE_DIR ${dir} has no ${f}`;
  if (!readFileSync(join(dir, EA_ASI)).toString('latin1').includes(EA_ASI_MARK)) return `TS3_NATIVE_DIR ${dir}: ${EA_ASI} is not an EA 1.69.47 build (no "${EA_ASI_MARK}" string; Steam asi?)`;
  return '';
}
// after start-up: the EA asi logs this line only when it accepted the exe (else `SKIP all: host is not EA app 1.69.47`)
export const eaLogProblem = (log: string) => (log.includes(EA_ASI_MARK) ? '' : `ts3native.log has no "${EA_ASI_MARK}" line (the EA asi did not accept this exe)`);
// PIDs of GAME_EXE seen by gameRunning() in this process: crash pickup takes only reports whose dump names one of them (crash-ledger.ts --pids),
// so an old xcpt or another process's crash is never blamed on this run. Callers clear it when their run starts.
export const seenPids = new Set<number>();
// Game processes the harness did not launch or track. The old rule "a GAME_EXE with -test is EA's crash relaunch" was wrong:
// every xcpt on the EA laptop on 2026-11-02 shows `-test`, the harness's own launches too (docs/236 #20). An untracked game
// process is logged with its command line, never killed and never assumed to be a relaunch. Steam: no-op.
export const untrackedPids = (psOut: string, known: Set<number>): { pid: number; cmd: string }[] =>
  [...psOut.matchAll(/^(\d+) (.*)$/gm)].map(m => ({ pid: Number(m[1]), cmd: m[2].trim() })).filter(p => !known.has(p.pid));
export async function logUntrackedGame(log: (l: string) => void, seconds = 150) {
  if (LAUNCH !== 'ea') return;
  const logged = new Set<number>();
  for (let t = 0; t < seconds; t += 5) {
    await new Promise(r => setTimeout(r, 5000));
    let out = '';
    try { out = execFileSync('powershell', ['-NoProfile', '-Command', `Get-CimInstance Win32_Process -Filter "Name='${GAME_EXE}'" | % { "$($_.ProcessId) $($_.CommandLine)" }`], { timeout: 20000 }).toString(); } catch { continue; }
    for (const p of untrackedPids(out, seenPids)) if (!logged.has(p.pid)) { logged.add(p.pid); log(`untracked ${GAME_EXE} PID ${p.pid} ${t + 5} s after the game left: ${p.cmd} (not launched or seen by this run; not killed, not assumed a relaunch)`); }
  }
}
export const csvPids = (csv: string): number[] => [...csv.matchAll(/^"[^"]*","(\d+)"/gm)].map(m => Number(m[1]));
export const gameRunning = () => {
  try {
    const out = execFileSync('tasklist', ['/NH', '/FO', 'CSV', '/FI', `IMAGENAME eq ${GAME_EXE}`]).toString();
    for (const p of csvPids(out)) seenPids.add(p);
    return out.includes(GAME_EXE);
  } catch { return true; } // tasklist failed: no verdict
};

// After a failed or hung launch: the game and, on EA, the per-launch helpers go. Never the EA app itself (EADesktop.exe: the sign-in and
// the entitlement the next launch needs). Returns what was running, for the log (docs/236 round 4: launcher-stage hangs).
export const LEFTOVER_EXES = (launch = LAUNCH, game = GAME_EXE) => [game, ...(launch === 'ea' ? ['EALauncher.exe', 'TSLHelper.exe'] : [])];
export function killLeftovers(): string[] {
  const found: string[] = [];
  for (const n of LEFTOVER_EXES()) {
    let out = '';
    try { out = execFileSync('tasklist', ['/NH', '/FO', 'CSV', '/FI', `IMAGENAME eq ${n}`]).toString(); } catch { continue; }
    const pids = csvPids(out);
    if (!pids.length) continue;
    found.push(`${n} ${pids.join(',')}`);
    try { execFileSync('taskkill', ['/F', '/T', '/IM', n], { stdio: 'ignore' }); } catch { /* gone meanwhile */ }
  }
  return found;
}

if (import.meta.main) {
  if (process.argv[2] === '--self-check') {
    const assert = (await import('node:assert')).strict;
    assert.deepEqual(LEFTOVER_EXES('ea', 'Sims3Launcher.exe'), ['Sims3Launcher.exe', 'EALauncher.exe', 'TSLHelper.exe']);
    assert.deepEqual(LEFTOVER_EXES('direct', 'TS3W.exe'), ['TS3W.exe']);
    assert.ok(!LEFTOVER_EXES('ea').some(n => /EADesktop/i.test(n)), 'never the EA app');
    assert.ok(isSteamGame('TS3W.exe') && isSteamGame('ts3w.EXE') && !isSteamGame('TS3.exe') && !isSteamGame('Sims3Launcher.exe'));
    assert.ok(GAME_EXE.length > 0 && GAME_NAME === GAME_EXE.replace(/\.exe$/i, '') && !/\.exe$/i.test(GAME_NAME));
    assert.ok(['direct', 'ea'].includes(LAUNCH), `TS3_LAUNCH "${LAUNCH}": use direct or ea`);
    assert.match(EA_URI, /^origin2:\/\/game\/launch\/\?offerIds=/);
    assert.equal(machineTag('main'), ''); assert.equal(machineTag('ea-laptop'), 'ea-laptop');
    // docs/236 #20: only PIDs the run did not see are reported, whatever their command line; -test alone means nothing
    const ps = '28624 "C:/EA Games/The Sims 3/Game/Bin/Sims3Launcher.exe"  -test\r\n100 "C:/x/Sims3Launcher.exe" -test\r\n\r\n';
    assert.deepEqual(untrackedPids(ps, new Set([100])), [{ pid: 28624, cmd: '"C:/EA Games/The Sims 3/Game/Bin/Sims3Launcher.exe"  -test' }]);
    assert.deepEqual(untrackedPids(ps, new Set([100, 28624])), []);
    assert.deepEqual(untrackedPids('', new Set()), []);
    // the running game is found by GAME_EXE/GAME_NAME only (7d: a soak loop watched TS3W on the EA laptop, where the game is Sims3Launcher.exe)
    const hard = /Get-Process\s+'?TS3W\b|IMAGENAME eq TS3W|frida\.attach\('TS3W|'-n', 'TS3W|\/IM', 'TS3W|Name='TS3W/;
    for (const f of readdirSync(import.meta.dirname).filter(f => /\.(ts|mjs|js)$/.test(f) && f !== 'game-exe.ts')) {
      const bad = readFileSync(join(import.meta.dirname, f), 'utf8').split('\n').findIndex(l => !/^\s*\/\//.test(l) && hard.test(l));
      assert.equal(bad, -1, `${f}:${bad + 1}: hard-coded TS3W process name; use GAME_EXE / GAME_NAME from game-exe.ts`);
    }
    assert.deepEqual(csvPids('"Sims3Launcher.exe","14068","Console","1","2,000 K"\r\n"Sims3Launcher.exe","7","Console","1","1 K"\r\n'), [14068, 7]);
    assert.deepEqual(csvPids('INFO: No tasks are running which match the specified criteria.\r\n'), []);
    // edition from the exe (PE stamp + sha), never from the env alone; the Steam set-up stays 'steam' whatever the exe says unless it is EA's
    const pe = (stamp: number, tail = '') => { const b = Buffer.alloc(0x60 + tail.length); b.write('MZ', 0, 'latin1'); b.writeUInt32LE(0x40, 0x3c); b.writeUInt32LE(stamp, 0x48); b.write(tail, 0x60, 'latin1'); return b; };
    assert.equal(peStamp(pe(0x6707155C)), 0x6707155C); assert.equal(peStamp(Buffer.from('xx')), undefined);
    assert.deepEqual(editionOf(pe(EXE_STAMPS.steam), 'direct', 'TS3W.exe'), { edition: 'steam', why: '' });
    assert.deepEqual(editionOf(null, 'direct', 'TS3W.exe'), { edition: 'steam', why: '' }, 'Steam set-up without the exe: as before');
    assert.deepEqual(editionOf(pe(0x12345678), 'direct', 'TS3W.exe'), { edition: 'steam', why: '' }, 'Steam set-up, unknown exe: as before');
    assert.match(editionOf(pe(EXE_STAMPS.ea169), 'ea', 'Sims3Launcher.exe').why, /not on the 1\.69\.47 list/);
    const okExe = pe(EXE_STAMPS.ea169, 'ok'); EA169_EXE_SHA256.push(createHash('sha256').update(okExe).digest('hex'));
    assert.deepEqual(editionOf(okExe, 'ea', 'Sims3Launcher.exe'), { edition: 'ea169', why: '' });
    assert.deepEqual(editionOf(okExe, 'direct', 'TS3W.exe'), { edition: 'ea169', why: '' }, 'the exe decides, not the env');
    EA169_EXE_SHA256.pop();
    assert.match(editionOf(pe(EXE_STAMPS.steam), 'ea', 'Sims3Launcher.exe').why, /Steam TS3W\.exe/);
    assert.match(editionOf(pe(0x12345678), 'ea', 'TS3.exe').why, /0x12345678 is no known edition/);
    assert.match(editionOf(null, 'ea', 'TS3.exe').why, /not found/);
    const nd = mkdtempSync(join(tmpdir(), 'ts3-native-'));
    try {
      assert.match(nativeDirProblem(undefined), /not set/); assert.match(nativeDirProblem(nd), /has no ts3native-ea169\.asi/);
      writeFileSync(join(nd, EA_ASI), 'ts3native 0.36.152 %04d SKIP all: host is not TS3W.exe'); assert.match(nativeDirProblem(nd), /has no ts3native-ea169-rc\.ini/);
      writeFileSync(join(nd, EA_INI), '[GC]\nEnabled=1\n'); assert.match(nativeDirProblem(nd), /not an EA 1\.69\.47 build/);
      writeFileSync(join(nd, EA_ASI), `ts3native 0.36.152 %04d ${EA_ASI_MARK}`); assert.equal(nativeDirProblem(nd), '');
    } finally { rmSync(nd, { recursive: true, force: true }); }
    assert.equal(eaLogProblem(`ts3native 0.36.152 x\nhost Sims3Launcher.exe base 0x00400000\n${EA_ASI_MARK}\n`), '');
    assert.match(eaLogProblem('ts3native 0.36.152 x\nSKIP all: host is not EA app 1.69.47 (pe 52DEC247 size 0x1)\n'), /did not accept/);
    const tmp = mkdtempSync(join(tmpdir(), 'ts3-game-')); // Steam (<lib>/steamapps/common/The Sims 3) and EA (Program Files/EA Games/The Sims 3) layouts: same shape under different roots
    try {
      for (const root of ['steam/The Sims 3', 'ea/The Sims 3']) {
        mkdirSync(join(tmp, root, 'Game', 'Bin'), { recursive: true });
        for (const d of ['EP01', 'EP11', 'SP08']) mkdirSync(join(tmp, root, d, 'GameData'), { recursive: true });
        mkdirSync(join(tmp, root, 'EP02'), { recursive: true }); mkdirSync(join(tmp, root, 'Support'), { recursive: true }); // no GameData / not a pack
        assert.deepEqual(installedProducts(join(tmp, root, 'Game', 'Bin')), ['BaseGame', 'EP01', 'EP11', 'SP08'], root);
      }
      assert.deepEqual(installedProducts(join(tmp, 'none', 'Game', 'Bin')), [], 'missing root');
    } finally { rmSync(tmp, { recursive: true, force: true }); }
    console.log('self-check ok');
  } else if (process.argv[2] === '--installed') console.log(`${gameRoot()}: ${installedProducts().join(' ')}`);
  else { console.error('usage: node tools/harness/game-exe.ts --self-check | --installed'); process.exit(2); }
}
