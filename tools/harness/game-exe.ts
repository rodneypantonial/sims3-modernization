// Which game edition the harness drives. Defaults = the Steam edition; the EA app edition needs only environment settings:
//   TS3_BIN          Game\Bin folder (default: Steam library path)
//   TS3_EXE          game process/file name (default TS3W.exe; EA app: TS3.exe or Sims3Launcher.exe)
//   TS3_LAUNCH       direct (default: start <TS3_BIN>\<TS3_EXE>) | ea (EALauncher.exe with TS3_EA_URI, then wait for the TS3_EXE process)
//   TS3_EA_URI, TS3_EA_LAUNCHER   EA launch URI and EALauncher.exe path (defaults below)
//   node tools/harness/game-exe.ts --self-check   no process is started
import { execFileSync } from 'node:child_process';

export const GAME_EXE = process.env.TS3_EXE ?? 'TS3W.exe';
export const GAME_NAME = GAME_EXE.replace(/\.exe$/i, ''); // PowerShell Get-Process / Windows counters name
export const BIN = process.env.TS3_BIN ?? 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\The Sims 3\\Game\\Bin';
export const LAUNCH = process.env.TS3_LAUNCH ?? 'direct';
export const EA_URI = process.env.TS3_EA_URI ?? 'origin2://game/launch/?offerIds=sims3_dd,1002441,70384_CE,70384_Unrevised';
export const EA_LAUNCHER = process.env.TS3_EA_LAUNCHER ?? 'C:\\Program Files\\Electronic Arts\\EA Desktop\\EA Desktop\\EALauncher.exe';
export const isSteamGame = (exe = GAME_EXE) => exe.toLowerCase() === 'ts3w.exe';
export const gameRunning = () => { try { return execFileSync('tasklist', ['/NH', '/FI', `IMAGENAME eq ${GAME_EXE}`]).toString().includes(GAME_EXE); } catch { return true; } }; // tasklist failed: no verdict

if (import.meta.main) {
  if (process.argv[2] === '--self-check') {
    const assert = (await import('node:assert')).strict;
    assert.ok(isSteamGame('TS3W.exe') && isSteamGame('ts3w.EXE') && !isSteamGame('TS3.exe') && !isSteamGame('Sims3Launcher.exe'));
    assert.ok(GAME_EXE.length > 0 && GAME_NAME === GAME_EXE.replace(/\.exe$/i, '') && !/\.exe$/i.test(GAME_NAME));
    assert.ok(['direct', 'ea'].includes(LAUNCH), `TS3_LAUNCH "${LAUNCH}": use direct or ea`);
    assert.match(EA_URI, /^origin2:\/\/game\/launch\/\?offerIds=/);
    console.log('self-check ok');
  } else { console.error('usage: node tools/harness/game-exe.ts --self-check'); process.exit(2); }
}
