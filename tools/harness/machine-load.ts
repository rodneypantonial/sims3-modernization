// Machine load beside a profile run (Rodney, 2026-10-17: rule out external factors such as other Claude sessions, IntelliJ
// or thermal throttling). One PowerShell loop per run, a JSON line every 30 s into <pre>-machine.jsonl:
//   other = CPU used by every process except TS3W and Idle, in % of the whole machine (all logical cores)
//   ts3w  = TS3W's own CPU in % of the whole machine; top = the 3 busiest other processes (% of machine)
//   perf  = '% Processor Performance' (CPU clock vs base; > 100 = turbo; a drop during the run = throttling or power limit)
// summarize() gives the run-log line and a verdict: a run with other >= 10% on average, or perf below 80% of its first
// sample, is flagged as not comparable. Windows only, like the rest of the harness.
//   node tools/harness/machine-load.ts [seconds]   self-check: samples every 5 s for N s (default 15) and prints the summary
import { spawn, type ChildProcess } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';

const OTHER_MAX = 10, PERF_DROP = 0.8;

const script = (file: string, everyS: number) => `
$ErrorActionPreference = 'SilentlyContinue'
$n = [Environment]::ProcessorCount
$prev = @{}; foreach ($p in Get-Process) { $prev[$p.Id] = $p.TotalProcessorTime.TotalSeconds }
$t = Get-Date
while ($true) {
  Start-Sleep -Seconds ${everyS}
  $now = Get-Date; $dt = ($now - $t).TotalSeconds; $t = $now
  $cur = @{}; $rows = @()
  foreach ($p in Get-Process) {
    $s = $p.TotalProcessorTime.TotalSeconds; $cur[$p.Id] = $s
    if ($prev.ContainsKey($p.Id) -and $p.Id -ne 0) { $rows += [pscustomobject]@{ n = $p.ProcessName; c = ($s - $prev[$p.Id]) / $dt / $n * 100 } }
  }
  $prev = $cur
  $ts3w = ($rows | Where-Object n -eq 'TS3W' | Measure-Object c -Sum).Sum
  $others = $rows | Where-Object { $_.n -ne 'TS3W' -and $_.n -ne 'Idle' }
  $other = ($others | Measure-Object c -Sum).Sum
  $top = $others | Group-Object n | ForEach-Object { [pscustomobject]@{ n = $_.Name; c = ($_.Group | Measure-Object c -Sum).Sum } } |
    Sort-Object c -Descending | Select-Object -First 3 | ForEach-Object { '{0} {1:N1}' -f $_.n, $_.c }
  $perf = (Get-Counter '\\Processor Information(_Total)\\% Processor Performance').CounterSamples[0].CookedValue
  $o = [ordered]@{ t = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(); other = [math]::Round($other, 1); ts3w = [math]::Round([double]$ts3w, 1); top = @($top); perf = if ($perf) { [math]::Round($perf) } else { $null } }
  Add-Content -Path '${file.replace(/'/g, "''")}' -Value ($o | ConvertTo-Json -Compress)
}`;

export function startMachineLoad(file: string, everyS = 30): () => void {
  const p: ChildProcess = spawn('powershell', ['-NoProfile', '-NonInteractive', '-Command', script(file, everyS)], { stdio: 'ignore' });
  return () => { try { p.kill(); } catch { /* gone */ } };
}

export function summarize(file: string): { line: string; ok: boolean } {
  const rows = existsSync(file) ? readFileSync(file, 'utf8').trim().split(/\r?\n/).filter(Boolean).map(l => JSON.parse(l)) : [];
  if (!rows.length) return { line: 'machine load: no samples', ok: false };
  const mean = (k: string) => rows.reduce((s, r) => s + (r[k] ?? 0), 0) / rows.length;
  const perfs = rows.map(r => r.perf).filter((x: number | null) => x != null);
  const perfMin = perfs.length ? Math.min(...perfs) : null, perf0 = perfs[0] ?? null;
  const busiest = rows.reduce((a, r) => (r.other > a.other ? r : a));
  const why = [mean('other') >= OTHER_MAX && `other CPU ${mean('other').toFixed(1)}% >= ${OTHER_MAX}%`,
    perf0 && perfMin !== null && perfMin < perf0 * PERF_DROP && `clock fell to ${perfMin}% from ${perf0}%`].filter(Boolean);
  const line = `machine load: other CPU mean ${mean('other').toFixed(1)}% max ${busiest.other}% (${busiest.top.join(', ')}), TS3W ${mean('ts3w').toFixed(1)}%, ` +
    `clock ${perf0 ?? '-'}% -> min ${perfMin ?? '-'}% (${rows.length} samples): ${why.length ? `NOT comparable: ${why.join('; ')}` : 'ok'}`;
  return { line, ok: !why.length };
}

if (import.meta.main) {
  const secs = Number(process.argv[2] ?? 15), file = `${process.env.TEMP ?? '.'}/machine-load-selfcheck-${Date.now()}.jsonl`;
  const stop = startMachineLoad(file, 5);
  await new Promise(r => setTimeout(r, (secs + 3) * 1000));
  stop();
  const s = summarize(file);
  console.log(s.line);
  if (!existsSync(file) || !readFileSync(file, 'utf8').trim()) { console.log('self-check FAIL: no samples written'); process.exit(1); }
  const r = JSON.parse(readFileSync(file, 'utf8').trim().split(/\r?\n/)[0]);
  console.assert(typeof r.other === 'number' && Array.isArray(r.top), 'row shape');
  console.log('self-check PASS');
}
