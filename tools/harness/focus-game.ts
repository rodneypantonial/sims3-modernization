// Brings the game window to the front: unfocused, the game drops below 60 fps and runs are not comparable (Rodney 2026-10-13).
//   node tools/harness/focus-game.ts    (also called by cycle.ts --save and profile-world.ts)
// Windows-only by nature (TS3W.exe window).
import { execFileSync } from 'node:child_process';

export function focusGame(): boolean {
  const ps = "$p = Get-Process TS3W -ErrorAction SilentlyContinue | Select-Object -First 1; if (-not $p) { 'no game'; exit }" +
    "; $w = New-Object -ComObject WScript.Shell; $w.SendKeys('%'); [bool]$w.AppActivate($p.Id)"; // the Alt press lifts Windows' foreground lock
  try {
    return execFileSync('powershell', ['-NoProfile', '-NonInteractive', '-Command', ps], { timeout: 15000 }).toString().trim() === 'True';
  } catch { return false; }
}

if (import.meta.main) console.log(focusGame() ? 'focused' : 'not focused');
