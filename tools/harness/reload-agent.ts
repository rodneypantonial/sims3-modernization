// Hot-reloads the agent's commands in the running game: builds a uniquely named plugin assembly, streams it
// through the mailbox in base64 chunks (requests are capped at 64 KB) and activates it; no game restart.
// Only src/plugin changes reload this way; src/loader changes still need build-agent.ts + a restart.
// Usage: node tools/harness/reload-agent.ts [--port 47891]
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';

const { values } = parseArgs({ options: { port: { type: 'string', default: '47891' } } });
const ROOT = resolve(import.meta.dirname, '..', '..');
const base = `http://127.0.0.1:${values.port}`;

async function agent(cmd: string, args?: object): Promise<any> {
  const q = new URLSearchParams({ cmd, timeout: '15000' });
  if (args) q.set('args', JSON.stringify(args));
  const r = await (await fetch(`${base}/agent?${q}`, { signal: AbortSignal.timeout(60000) })).json();
  if (!r.ok) throw new Error(`${cmd}: ${r.error}`);
  return r.result;
}

execFileSync(process.execPath, [join(ROOT, 'tools', 'harness', 'build-agent.ts'), '--hot'], { stdio: 'inherit' });
const dll = readFileSync(join(ROOT, 'build', 'hot-plugin.txt'), 'utf8').trim();
const b64 = readFileSync(dll).toString('base64');
const CHUNK = 40000; // base64 chars per request; well under the mailbox's 64 KB request cap with JSON overhead
const chunks = Math.ceil(b64.length / CHUNK);
const t0 = Date.now();
await agent('plugin_begin', { chunks });
for (let i = 0; i < chunks; i++) await agent('plugin_chunk', { index: i, data: b64.slice(i * CHUNK, (i + 1) * CHUNK) });
const status = await agent('plugin_load');
const ping = await agent('ping');
console.log(`reloaded ${status.plugin} (reload #${status.reloads}, ${chunks} chunks, ${Date.now() - t0} ms); ping reports plugin ${ping.plugin}`);
