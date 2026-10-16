// List every TS3W.exe code address named in our native sources (ts3native), for the hook conflict map (docs/68).
//
//   node tools/hook-sites.ts              one line per address: module, address, file:line, label
//   node tools/hook-sites.ts --check      also list addresses that docs/68-hook-conflict-map.md does not mention (exit 1 if any)
//
// Static text scan only: it finds code-range literals (.text is 0x00401000-0x00F94FFF on Steam 1.67), not whether the
// module patches, detours or only calls that address. docs/68 holds the curated classification; --check keeps it complete.
// shortcut: addresses built at run time (base + offset) are not found; add them to docs/68 by hand.
import { readFileSync, readdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const repo = join(dirname(fileURLToPath(import.meta.url)), "..");
const src = join(repo, "native", "ts3native");
const doc = join(repo, "docs", "68-hook-conflict-map.md");
const TEXT_LO = 0x00401000, TEXT_HI = 0x00f94fff;

type Hit = { module: string; addr: number; where: string; label: string };

export function scan(text: string, module: string): Hit[] {
    const hits: Hit[] = [];
    text.split(/\r?\n/).forEach((line, i) => {
        const code = line.replace(/\/\/.*$/, "");  // comments name callers and docs, not sites
        const label = (code.match(/"([^"]{2,60})"/) ?? code.match(/\b([A-Z][A-Z0-9_]{2,})\s*=/) ?? [, ""])[1];
        for (const m of code.matchAll(/\b0x([0-9A-Fa-f]{6,8})\b/g)) {
            const addr = parseInt(m[1], 16);
            if (addr >= TEXT_LO && addr <= TEXT_HI) hits.push({ module, addr, where: `${module}.cpp:${i + 1}`, label });
        }
    });
    return hits;
}

const hex = (a: number) => "0x" + a.toString(16).toUpperCase().padStart(8, "0");

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
    // self-check: code literal found, comment and data-range literals ignored
    const t = scan('static const DWORD A_B = 0x004E16F0; // 0x00ABCDEF\nx = 0x011F30D0;', "t");
    if (t.length !== 1 || t[0].addr !== 0x4e16f0 || t[0].label !== "A_B") throw new Error("hook-sites self-check failed");

    const files = readdirSync(src).filter((f) => f.endsWith(".cpp") && !/test/.test(f));
    const hits = files.flatMap((f) => scan(readFileSync(join(src, f), "utf8"), f.replace(/\.cpp$/, "")));
    const seen = new Map<number, Hit[]>();
    for (const h of hits) seen.set(h.addr, [...(seen.get(h.addr) ?? []), h]);
    const addrs = [...seen.keys()].sort((a, b) => a - b);
    for (const a of addrs) for (const h of seen.get(a)!) console.log(`${hex(a)}  ${h.module.padEnd(12)} ${h.where.padEnd(22)} ${h.label}`);

    if (process.argv.includes("--check")) {
        const text = readFileSync(doc, "utf8").toUpperCase().replace(/0X0*/g, "0X");
        const missing = addrs.filter((a) => !text.includes("0X" + a.toString(16).toUpperCase()));
        console.log(`\n${addrs.length} addresses, ${missing.length} not in docs/68${missing.length ? ": " + missing.map(hex).join(" ") : ""}`);
        if (missing.length) process.exitCode = 1;
    }
}
