// Reports whether the game window is in front. It never focuses it (Rodney 2026-10-16: no focusing or moving the window on
// start; he places it himself). Unfocused, the game drops below 60 fps, so callers log a "not comparable" note when false.
//   node tools/harness/focus-game.ts    (also called by cycle.ts --save and profile-world.ts)
// Windows-only by nature (TS3W.exe window).
import { execFileSync } from 'node:child_process';

export function focusGame(): boolean {
  const ps = "$p = Get-Process TS3W -ErrorAction SilentlyContinue | Select-Object -First 1; if (-not $p) { 'no game'; exit }" +
    "; Add-Type 'using System; using System.Runtime.InteropServices; public static class Fg { [DllImport(\"user32.dll\")] public static extern IntPtr GetForegroundWindow();" +
    " [DllImport(\"user32.dll\")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p); }'" +
    "; $id = 0; [void][Fg]::GetWindowThreadProcessId([Fg]::GetForegroundWindow(), [ref]$id); [bool]($id -eq $p.Id)";
  try {
    return execFileSync('powershell', ['-NoProfile', '-NonInteractive', '-Command', ps], { timeout: 15000 }).toString().trim() === 'True';
  } catch { return false; }
}

if (import.meta.main) console.log(focusGame() ? 'in front' : 'not in front');
