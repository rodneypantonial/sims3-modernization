// Send one agent command through the probe server and print the full JSON reply.
// Usage: node tools/harness/ag.mjs <cmd> [json args]   e.g. node tools/harness/ag.mjs patches '{"gohome":false}'
const [cmd, args] = process.argv.slice(2);
const q = new URLSearchParams({ cmd, timeout: '10000' }); if (args) q.set('args', args);
const r = await (await fetch('http://127.0.0.1:47891/agent?' + q, { signal: AbortSignal.timeout(30000) })).json();
console.log(JSON.stringify(r));
