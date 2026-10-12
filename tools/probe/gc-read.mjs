// GC counters from the live game via the probe (addresses from docs/13): heap size, largest free, collections per
// second over 30 s, bytes allocated since the last GC, per-frame budget. Usage: node tools/probe/gc-read.mjs
const rd = async (a, n = 4) => { const r = await (await fetch(`http://127.0.0.1:47891/read?addr=${a}&len=${n}`)).json(); return Buffer.from(r.hex, 'hex').readUInt32LE(0); };
const snap = async () => ({ t: Date.now(), heap: await rd('0x12225A0'), free: await rd('0x12225B4'), words: await rd('0x12225C4'), gcno: await rd('0x11F2F90'), budget: await rd('0x11922A4') });
const a = await snap(); await new Promise(r => setTimeout(r, 30000)); const b = await snap();
const s = (b.t - a.t) / 1000, MB = 1048576;
console.log(`heap ${(b.heap / MB).toFixed(1)} MB, large free ${(b.free / MB).toFixed(1)} MB, collections ${b.gcno - a.gcno} in ${s.toFixed(0)} s (${((b.gcno - a.gcno) / s).toFixed(2)}/s), allocated since last GC ${(b.words * 4 / MB).toFixed(1)} MB, budget ${b.budget}`);
