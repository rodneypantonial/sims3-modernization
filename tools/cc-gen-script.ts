// Scripted synthetic CC for memory stress tests (docs/200 §7, docs/191): every item = a cloned object (base game or expansion /
// stuff pack OBJD, via tools/cc-gen.ts cloneItem) that runs its OWN script assembly, as real script-heavy CC does. The
// assembly is a renamed copy of the original object's script class chain, cloned at IL level (ildasm -> rename -> ilasm):
// the OBJK script class up to the nearest class that declares interactions (nested InteractionDefinition types), with every
// nested interaction, helper and compiler-generated type. Per item it also gets its own copies of the animation data the code
// names: jazz state machines (JAZZ, instance = FNV64(name)) acquired by a string literal and every CLIP they reference, plus
// clips named directly by a literal; its own interaction tuning (ITUN, names rewritten) and class tuning XML. One S3SA per
// item (unique assembly name, fresh MVID), packed with the object's resources in the item's .package; manifest.json lists
// mode (il | subclass), classes, interactions, animation copies, assembly size and MVID.
//
//   node tools/cc-gen-script.ts --count 100 --seed 1 --ep-share 0.5 [--out game-copy/cc-synth/cc-script-100]
//   node tools/cc-gen-script.ts --list-sources [--count 20]      candidate classes (origin, chain, interactions, IL lines)
//   node tools/cc-gen-script.ts --self-check                     fixture IL + fixture package (no EA bytes); ilasm step only on Windows
// Game folder: --game or TS3_GAME (default Steam path). Script DLLs: game-copy/assemblies (tools/extract-assemblies.ts).
// Tools: ildasm (Windows SDK NETFX tools, or ILDASM=<path>) and ilasm (.NET Framework 4, ships with Windows).
//
// Output holds EA-derived IL and resources: only under game-copy/cc-synth/ (git-ignored), never committed or published.
// IL text, per-item .il/.dll in game-copy/cc-synth/.il/ (cache, git-ignored as part of game-copy/).
//
// [verified static] Mono 1.2.3 in TS3W.exe does no member or type visibility checks, so a clone in a new assembly can use
// EA internal/private members: the only MethodAccessException raise site (ref to the class name at 0xE7CED8) is the
// reflection invoke "Cannot invoke constructor of an abstract class." path; no FieldAccessException class name and no
// "inaccessible"/visibility message strings in the exe (searched 2026-10-26). Subclass fallback (an empty class deriving from
// the original leaf class: the object runs EA's code and shares EA's animations) is used only when an item cannot be cloned:
// field data labels in the cloned classes, internalcall/pinvoke methods, an incomplete <PrivateImplementationDetails> copy
// (pidSubset), or an ilasm error; the reason goes in the manifest.
// [inferred, checked by the scripted-cc load smoke] the game binds OBJK scriptClass to a type in a mod assembly by full name
// (community scripted-object CC does this); a cloned JAZZ under a new instance is found by the new name although its
// internal name hashes stay the original's (actor/state hashes must stay, the code names actors by the same literals);
// cloned CLIPs are found by key; cloned ITUNs load with every other ITUN (AutonomyTuning.LoadAllInteractions = KeySearch over
// type 0x03B33DDF [verified static]).
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve, sep } from 'node:path';
import { parseArgs } from 'node:util';
import { Index, T, cloneItem, closure, englishNames, keyOf, scanRefs, scriptClass, verifyPackage, writeStbl } from './cc-gen.ts';
import { TYPE_S3SA, fnv64, s3saEncode, writePackage, type Resource } from './dbpf.ts';
import { readIndex } from './extract-assemblies.ts';

type Entry = Index['byKey'] extends Map<string, infer E> ? E : never;
export const TS = { ITUN: 0x03b33ddf, JAZZ: 0x02d5df13, CLIP: 0x6b20c4f3, XML: 0x0333406c };
const ROOT = resolve(import.meta.dirname, '..');
const IL_DIR = join(ROOT, 'game-copy', 'cc-synth', '.il');
const SCRIPT_DLLS = ['Sims3GameplayObjects', 'Sims3GameplaySystems', 'Sims3StoreObjects'];
const ILASM = 'C:/Windows/Microsoft.NET/Framework/v4.0.30319/ilasm.exe';
const hex = (n: number | bigint) => n.toString(16);

// ---------------------------------------------------------------- IL text model (ildasm output)
export type IlClass = { name: string; asm: string; text: string; ext: string; extBare: boolean; abstract: boolean; sealed: boolean;
  defs: string[]; nested: string[]; blockers: string[] };
export type IlModule = { asm: string; header: string; classes: Map<string, IlClass>; data: Map<string, string> }; // data: label -> `.data` text

// name in a `.class` line: after the flag words, quoted, or up to the generic parameter list
const FLAGS = /^(public|private|auto|ansi|beforefieldinit|abstract|sealed|interface|sequential|explicit|serializable|specialname|rtspecialname|import|nested|assembly|family|famandassem|famorassem|unicode|autochar)$/;
export function classLineName(line: string): string {
  const rest = line.replace(/^\s*\.class\s+/, '');
  let p = 0;
  for (;;) { const w = /^(\S+)\s+/.exec(rest.slice(p)); if (!w || !FLAGS.test(w[1])) break; p += w[0].length; }
  const t = rest.slice(p);
  return t[0] === "'" ? t.slice(0, t.indexOf("'", 1) + 1) : /^[^\s<]+/.exec(t)![0];
}
// top-level `.class` at column 0 up to `} // end of class <name>` [ildasm 4.8 layout: full dotted names, nested classes inside]
export function parseIl(text: string, asm: string): IlModule {
  const classes = new Map<string, IlClass>();
  const first = text.search(/^\.class /m);
  const header = first < 0 ? text : text.slice(0, first);
  for (const m of text.matchAll(/^\.class ([^\r\n]*)/gm)) {
    const name = classLineName(m[0]), start = m.index!, flags = m[1].slice(0, m[1].indexOf(name));
    const endTag = `\n} // end of class ${name}`, e = text.indexOf(endTag, start);
    if (e < 0) continue;
    const end = text.indexOf('\n', e + endTag.length);
    const block = text.slice(start, end < 0 ? text.length : end + 1);
    const ext = /^\s+extends\s+([^\r\n]+)/m.exec(block.slice(0, block.indexOf('\n{')))?.[1].trim() ?? '';
    const extName = ext.replace(/^(class|valuetype)\s+/, '');
    const defs: string[] = [], nested: string[] = [], stack: string[] = [];
    const lines = block.split('\n');
    for (let i = 1; i < lines.length; i++) {
      const l = lines[i];
      if (/^\s+\.class /.test(l)) {
        stack.push(classLineName(l).replace(/^'|'$/g, ''));
        nested.push(stack.join('/'));
        const nx = /^\s+extends\s+([^\r\n]+)/.exec(lines[i + 1] ?? '')?.[1] ?? '';
        if (/InteractionDefinition/.test(nx)) defs.push(stack.join('/'));
      } else if (/^\s+} \/\/ end of class /.test(l)) stack.pop();
    }
    const blockers = [
      /\binternalcall\b/.test(block) && 'internalcall method',
      /\bpinvokeimpl\b/.test(block) && 'pinvoke method',
      /^\s*\.data\b| at D_[0-9A-F]+/m.test(block) && 'field data label',
    ].filter((x): x is string => !!x);
    classes.set(name, { name, asm, text: block, ext: extName, extBare: !!extName && !extName.startsWith('['), abstract: /\babstract\b/.test(flags),
      sealed: /\bsealed\b/.test(flags), defs, nested, blockers });
  }
  // `.data cil <label> = bytearray (<hex lines>)` or `= int8[N]`; a comment after the hex can hold ')', so cut comments first
  const data = new Map<string, string>(), lines = text.split('\n');
  for (let i = 0; i < lines.length; i++) {
    const m = /^\.data cil (\w+) = /.exec(lines[i]);
    if (!m) continue;
    let j = i;
    if (/=\s*bytearray\s*\(/.test(lines[i])) while (j < lines.length - 1 && !lines[j].replace(/\/\/[^\r\n]*/, '').includes(')')) j++;
    data.set(m[1], lines.slice(i, j + 1).map(l => l.replace(/\s*\/\/[^\r\n]*/, '').trimEnd()).join('\n'));
  }
  return { asm, header, classes, data };
}

// The cloned code's refs to the source module's '<PrivateImplementationDetails>{GUID}' (array-init data for ldtoken +
// InitializeArray, switch-on-string dictionaries) must not cross assemblies: the GUID is per compile, so a recompiled
// replacement of the EA assembly (simler90's core mod ships its own Sims3GameplayObjects/Systems) has another name, the
// ref resolves to NULL and InitializeArray faults at 0xE7A349 (docs/200 §8.1, docs/67 §19). Copy the referenced fields, their nested
// size types and their `.data` blobs into an own type of the same name in the clone (refs stay unprefixed = own module).
export function pidSubset(mod: IlModule, pid: string, body: string): { text: string; fields: number; problems: string[] } {
  const esc = pid.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), k = mod.classes.get(pid), problems: string[] = [];
  const names = new Set([...body.matchAll(new RegExp(`${esc}::('[^']+')`, 'g'))].map(m => m[1]));
  if (!k || !names.size) return { text: '', fields: 0, problems };
  const fields = k.text.split('\n').filter(l => /^\s+\.field /.test(l) && names.has(/('[^']+')(?:\s+at\s+\w+)?\s*$/.exec(l)?.[1] ?? ''));
  if (fields.length !== names.size) problems.push(`${pid}: ${names.size - fields.length} referenced fields not found`);
  const nested = new Map<string, string>(); // quoted nested name -> its class block
  for (const l of fields) for (const m of l.matchAll(new RegExp(`${esc}/('[^']+')`, 'g'))) {
    const n = m[1].replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const b = new RegExp(`^  \\.class [^\\n]*nested[^\\n]*${n}\\s[\\s\\S]*?^  } // end of class ${n}`, 'm').exec(k.text)?.[0];
    if (b) nested.set(m[1], b); else problems.push(`${pid}: nested type ${m[1]} missing`);
  }
  const blocks = [...nested.values()];
  const datas = fields.map(l => {
    const label = /\sat\s+(\w+)\s*$/.exec(l)?.[1];
    if (!label) return '';
    const t = [...nested.keys()].find(n => l.includes(`/${n}`)), size = Number(/\.size (\d+)/.exec(t ? nested.get(t)! : '')?.[1]);
    const d = mod.data.get(label);
    if (!d) { problems.push(`${pid}: no .data ${label}`); return ''; }
    const bytes = /bytearray/.test(d) ? (d.replace(/^[^(]*\(/, '').replace(/\).*$/s, '').match(/[0-9A-F]{2}/gi) ?? []).length : Number(/int8\[(\d+)\]/.exec(d)?.[1]);
    if (bytes !== size) problems.push(`${pid}: .data ${label} has ${bytes} bytes, field type ${size}`);
    return d;
  });
  const text = `\n.class private auto ansi ${pid}\n       extends [mscorlib]System.Object\n{\n${blocks.join('\n')}\n${fields.join('\n')}\n} // end of class ${pid}\n\n${datas.filter(Boolean).join('\n')}\n`;
  return { text, fields: fields.length, problems };
}

// OBJK class -> classes to clone: the class, then its same-module bases until one declares interactions (at most 4 classes).
// Never the engine bases (Sims3.Gameplay.Abstracts: GameObject, SpawnerBase ...): game code casts every object to them, and
// GameObject's own nested types are only its DEBUG interactions.
export const NO_CLONE = /^Sims3\.Gameplay\.Abstracts\./;
export function cloneChain(mod: IlModule, cls: string): IlClass[] | undefined {
  const out: IlClass[] = [];
  for (let c = mod.classes.get(cls); c && out.length < 4 && !NO_CLONE.test(c.name); c = c.extBare ? mod.classes.get(c.ext.replace(/<.*$/, '')) : undefined) {
    out.push(c);
    if (c.defs.length) return out;
  }
  return undefined;
}

// Rename cloned types, give every other same-module type an [asm] prefix. Strings, comments, quoted names and member names
// (after :: or .) are left alone. map: original full name -> new full name.
export function rewriteIl(text: string, map: Map<string, string>, moduleTypes: Set<string>, asm: string): { text: string; prefixed: number; renamed: number } {
  let prefixed = 0, renamed = 0;
  const out = text.replace(/"(?:[^"\\\r\n]|\\.)*"|\/\/[^\n]*|'(?:[^'\\\r\n]|\\.)*'|[A-Za-z_][\w`$]*(?:\.[A-Za-z_][\w`$]*)+/g, (tok, at: number, s: string) => {
    const c = tok[0];
    if (c === '"' || c === '/') return tok;
    if (/[\]\w.:'/`$]/.test(s[at - 1] ?? ' ')) return tok;
    // quoted: only a whole quoted top-level type (<PrivateImplementationDetails>{GUID}: switch-on-string dictionaries, array init data)
    if (c === "'") { if (moduleTypes.has(tok)) { prefixed++; return `[${asm}]${tok}`; } return tok; }
    const nw = map.get(tok);
    if (nw) { renamed++; return nw; }
    if (moduleTypes.has(tok)) { prefixed++; return `[${asm}]${tok}`; }
    return tok;
  });
  return { text: out, prefixed, renamed };
}

// ldstr literals: animation names. Jazz only where an Acquire/StateMachine call follows within 4 instructions (the same literal
// can be an actor name); clips wherever the literal is a clip name.
export function animationLiterals(text: string, isJazz: (s: string) => boolean, isClip: (s: string) => boolean) {
  const lines = text.split('\n'), jazz = new Set<string>(), clips = new Set<string>(), jazzNoCall = new Set<string>();
  lines.forEach((l, i) => {
    const m = /\bldstr\s+"([^"\\]*)"/.exec(l);
    if (!m || !m[1]) return;
    const s = m[1];
    if (isClip(s)) clips.add(s);
    if (!isJazz(s)) return;
    const next = lines.slice(i + 1, i + 12).filter(x => /^\s*IL_[0-9a-f]+:/.test(x)).slice(0, 4);
    if (next.some(x => /\b(call|callvirt|newobj)\b.*(Acquire|StateMachine)/.test(x))) jazz.add(s); else jazzNoCall.add(s);
  });
  for (const j of jazz) jazzNoCall.delete(j);
  return { jazz, clips, jazzNoCall };
}
// rewrite ldstr "<old>" -> "<new>" only at the lines animationLiterals accepted (same rule)
export function renameLiterals(text: string, jazz: Map<string, string>, clips: Map<string, string>): string {
  const lines = text.split('\n');
  return lines.map((l, i) => {
    const m = /\bldstr\s+"([^"\\]*)"/.exec(l);
    if (!m) return l;
    const s = m[1];
    if (clips.has(s)) return l.replace(`"${s}"`, `"${clips.get(s)}"`);
    if (!jazz.has(s)) return l;
    const next = lines.slice(i + 1, i + 12).filter(x => /^\s*IL_[0-9a-f]+:/.test(x)).slice(0, 4);
    return next.some(x => /\b(call|callvirt|newobj)\b.*(Acquire|StateMachine)/.test(x)) ? l.replace(`"${s}"`, `"${jazz.get(s)}"`) : l;
  }).join('\n');
}

// .assembly extern blocks of the source module + the source module itself + the new assembly with the attributes EA's
// gameplay assemblies carry ([Tunable]: TuningControl.TuneAssemblies skips assemblies without it; [PersistableStatic])
export function assemblyHeader(src: IlModule, name: string): string {
  const externs = [...src.header.matchAll(/^\.assembly extern [\s\S]*?^\}/gm)].map(m => m[0]).join('\n');
  const ver = /^\.assembly (?!extern)\S+\s*\{[\s\S]*?\.ver ([\d:]+)/m.exec(src.header)?.[1] ?? '1:0:0:0';
  return `${externs}\n.assembly extern ${src.asm}\n{\n  .ver ${ver}\n}\n.assembly ${name}\n{\n` +
    `  .custom instance void [SimIFace]Sims3.SimIFace.TunableAttribute::.ctor() = ( 01 00 00 00 )\n` +
    `  .custom instance void [SimIFace]Sims3.SimIFace.PersistableStaticAttribute::.ctor() = ( 01 00 00 00 )\n  .ver 1:0:0:0\n}\n.module ${name}.dll\n\n`;
}
const ensureSimIFace = (h: string) => /\.assembly extern SimIFace\b/.test(h) ? h : `.assembly extern SimIFace\n{\n  .ver 1:0:0:50\n}\n${h}`;

// fallback: an empty public class deriving from the original leaf class (EA's code and animations, own type + assembly)
export function subclassIl(src: IlModule, leaf: string, nw: string): string {
  return `.class public auto ansi beforefieldinit ${nw}\n       extends [${src.asm}]${leaf}\n{\n` +
    `  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed\n  {\n    .maxstack 8\n` +
    `    ldarg.0\n    call instance void [${src.asm}]${leaf}::.ctor()\n    ret\n  }\n} // end of class ${nw}\n`;
}

// ildasm prints IEEE specials as inf / -inf / nan / -nan(ind), which ilasm cannot read back: write their bit patterns
const SPECIAL: Record<string, [string, string]> = { inf: ['0x7F800000', '0x7FF0000000000000'], '-inf': ['0xFF800000', '0xFFF0000000000000'],
  nan: ['0x7FC00000', '0x7FF8000000000000'], '-nan': ['0xFFC00000', '0xFFF8000000000000'] };
export function fixFloats(il: string): string {
  const v = (s: string, r8: boolean) => SPECIAL[s.replace(/\(.*$/, '')][r8 ? 1 : 0];
  return il.replace(/\b(ldc\.r[48])(\s+)(-?(?:inf|nan)(?:\([a-z]+\))?)(?=\s)/g, (_, op, sp, x) => `${op}${sp}float${op.endsWith('8') ? 64 : 32}(${v(x, op.endsWith('8'))})`)
    .replace(/\bfloat(32|64)\((-?(?:inf|nan)(?:\([a-z]+\))?)\)/g, (_, w, x) => `float${w}(${v(x, w === '64')})`);
}

export function ilasm(il: string, out: string): string | undefined { // error text, or undefined when built
  const src = out.replace(/\.dll$/, '.il');
  writeFileSync(src, fixFloats(il));
  try { execFileSync(ILASM, ['-dll', '-quiet', '-nologo', `-output=${out}`, src], { stdio: 'pipe', timeout: 120_000 }); return undefined; }
  catch (e: any) { return `${e.stdout ?? ''}${e.stderr ?? ''}`.split(/\r?\n/).filter(l => /error/i.test(l)).slice(0, 3).join(' | ') || String(e.message).slice(0, 200); }
}

// MVID = the first #GUID entry (the Module row's Mvid index is 1 in csc/ilasm output). shortcut: no full table walk; read the Module row if a tool ever emits more GUIDs first.
export function mvid(dll: Buffer): string {
  const pe = dll.readUInt32LE(0x3c), nsec = dll.readUInt16LE(pe + 6), opt = pe + 24, secs = pe + 24 + dll.readUInt16LE(pe + 20);
  const off = (rva: number) => { for (let i = 0; i < nsec; i++) { const s = secs + i * 40, va = dll.readUInt32LE(s + 12), sz = dll.readUInt32LE(s + 16); if (rva >= va && rva < va + Math.max(sz, dll.readUInt32LE(s + 8))) return rva - va + dll.readUInt32LE(s + 20); } throw new Error('rva'); };
  const cli = off(dll.readUInt32LE(opt + 208)), md = off(dll.readUInt32LE(cli + 8));
  const vlen = dll.readUInt32LE(md + 12); let p = md + 16 + vlen + 2;
  const n = dll.readUInt16LE(p); p += 2;
  for (let i = 0; i < n; i++) {
    const o = dll.readUInt32LE(p), nameEnd = dll.indexOf(0, p + 8), name = dll.toString('latin1', p + 8, nameEnd);
    if (name === '#GUID') return dll.subarray(md + o, md + o + 16).toString('hex');
    p = (nameEnd + 4) & ~3;
  }
  throw new Error('no #GUID stream');
}

// OBJK component data "scriptClass": u32 key length + key, u8 type 0 (string), u32 length + text [verified: base-game OBJKs]
export function setScriptClass(objk: Buffer, cls: string): Buffer {
  const at = objk.indexOf('\x0b\x00\x00\x00scriptClass\x00', 0, 'latin1');
  if (at < 0) throw new Error('OBJK has no scriptClass');
  const p = at + 4 + 11 + 1, len = objk.readUInt32LE(p);
  const nb = Buffer.from(cls, 'latin1'), l = Buffer.alloc(4); l.writeUInt32LE(nb.length);
  return Buffer.concat([objk.subarray(0, p), l, nb, objk.subarray(p + 4 + len)]);
}

// ITUN index: Interaction / Object names [verified static: InteractionTuning reads <Interaction name> + <Object name>]
type Itun = { e: Entry; xml: string; inter: string; obj: string };
function itunIndex(ix: Index): Itun[] {
  const out: Itun[] = [];
  for (const e of ix.byKey.values()) {
    if (e.type !== TS.ITUN) continue;
    const xml = ix.body(e).toString('utf8');
    out.push({ e, xml, inter: /<Interaction name="([^"]+)"/.exec(xml)?.[1] ?? '', obj: /<Object name="([^"]+)"/.exec(xml)?.[1] ?? '' });
  }
  return out;
}

// ---------------------------------------------------------------- per item
export type ScriptItem = { i: number; kind: 'scripted'; mode: 'il' | 'subclass'; fallback?: string; origin: string; source: string; name: string;
  package: string; objd: string; objkClass: string; scriptClass: string; cloned: { orig: string; new: string }[]; assembly: string; mvid: string; assemblyBytes: number;
  ilLines: number; prefixedRefs: number; interactions: string[]; interactionsOrig: string[]; jazz: { orig: string; new: string; clips: number }[];
  jazzNotRenamed: string[]; objdJazzRefs: number; clips: number; clipBytes: number; ituns: number; classTunings: number; resources: number; diskBytes: number; problems: string[] };
export type Ctx = { ix: Index; names: Map<bigint, string>; mods: IlModule[]; moduleTypes: Map<string, Set<string>>; ituns: Itun[]; work: string; seed: string; build: boolean };

// pack of an entry: its EP/SP folder; a DeltaPackages/pNN patch belongs to the pack whose FullBuild is pNN (EP1's is FullBuildep1 = p02)
const PRODUCT = new Map<string, string>([['p02', 'EP1']]);
export function setProducts(files: string[]) {
  for (const f of files) { const m = /[\\/]((?:EP|SP)\d+)[\\/]GameData[\\/]Shared[\\/]Packages[\\/]FullBuild\d*_(p\d+)\.package$/i.exec(f); if (m) PRODUCT.set(m[2].toLowerCase(), m[1].toUpperCase()); }
}
export const originOf = (e: { file: string }) => {
  const d = /[\\/]DeltaPackages[\\/](p\d+)[\\/]/i.exec(e.file)?.[1].toLowerCase();
  return d ? PRODUCT.get(d) ?? d : /[\\/]((?:EP|SP)\d+)[\\/]/i.exec(e.file)?.[1].toUpperCase() ?? 'base';
};
const findClass = (c: Ctx, cls: string) => c.mods.find(m => m.classes.has(cls));
const instOf = (c: Ctx, t: number, name: string) => (c.ix.byInst.get(fnv64(name)) ?? []).find(e => e.type === t);

export function buildItem(c: Ctx, root: Entry, i: number): { res: Resource[]; item: ScriptItem; oldInsts: Set<bigint> } {
  const pad = String(i).padStart(5, '0'), asmName = `CCGenS${c.seed}I${pad}`, ns = `CCGenScript.S${c.seed}.I${pad}`;
  const objkClass = scriptClass(c.ix, root);
  if (!objkClass) throw new Error('no OBJK scriptClass');
  const mod = findClass(c, objkClass);
  const chain = mod && cloneChain(mod, objkClass);
  if (!mod || !chain) throw new Error(`${objkClass}: no cloneable class chain`);
  const { res, item: base } = cloneItem(c.ix, root, i, `s${c.seed}`, c.names, 'object');
  const short = (n: string) => n.split('.').pop()!;
  const map = new Map(chain.map(k => [k.name, `${ns}.${short(k.name)}`]));
  const newLeaf = map.get(objkClass)!;
  let mode: 'il' | 'subclass' = 'il', fallback = chain.flatMap(k => k.blockers).join('; ') || undefined;
  // animation copies
  const isJazz = (s: string) => /^[\w.-]{2,80}$/.test(s) && !!instOf(c, TS.JAZZ, s), isClip = (s: string) => /^[\w.-]{3,120}$/.test(s) && !!instOf(c, TS.CLIP, s);
  let body = chain.map(k => k.text).join('\n');
  const lit = animationLiterals(body, isJazz, isClip);
  const jazzMap = new Map([...lit.jazz].map(s => [s, `ccgs${c.seed}_${pad}_${s.toLowerCase()}`])) /* FNV64 lowercases: "Stove" = "stove" */, clipMap = new Map([...lit.clips].map(s => [s, `ccgs${c.seed}_${pad}_${s}`]));
  const anim: Resource[] = [], clipNew = new Map<string, bigint>(), jazzOut: ScriptItem['jazz'] = [];
  const oldInsts = new Set([...closure(c.ix, root).values()].map(n => n.e.inst));
  const cloneClip = (e: Entry, inst: bigint) => { anim.push({ type: e.type, group: e.group, instance: inst, data: c.ix.raw(e), ...(e.compressed === 0xffff ? { memSize: e.memSize } : {}) }); };
  for (const [s, nw] of clipMap) { const e = instOf(c, TS.CLIP, s)!; const k = keyOf(e.type, e.group, e.inst); if (!clipNew.has(k)) { clipNew.set(k, fnv64(nw)); cloneClip(e, fnv64(nw)); } }
  for (const [s, nw] of jazzMap) {
    if (jazzOut.some(j => j.new === nw)) continue;
    const je = instOf(c, TS.JAZZ, s)!, jb = Buffer.from(c.ix.body(je));
    let n = 0;
    for (const r of scanRefs(jb, new Set([TS.JAZZ, TS.CLIP]), k => c.ix.byKey.has(k))) {
      if (r.type === TS.JAZZ) { if (r.inst === je.inst) jb.writeBigUInt64LE(fnv64(nw), r.at); continue; }
      let ni = clipNew.get(r.key);
      if (ni === undefined) { ni = fnv64(`cc-gen-script/${c.seed}/${i}/clip/${hex(r.inst)}`); clipNew.set(r.key, ni); cloneClip(c.ix.byKey.get(r.key)!, ni); n++; }
      jb.writeBigUInt64LE(ni, r.at);
    }
    anim.push({ type: TS.JAZZ, group: je.group, instance: fnv64(nw), data: jb });
    jazzOut.push({ orig: s, new: nw, clips: n });
    oldInsts.add(je.inst);
  }
  // IL clone (or subclass fallback)
  let il = '', rw = { prefixed: 0 };
  const header = ensureSimIFace(assemblyHeader(mod, asmName));
  if (!fallback) {
    body = renameLiterals(body, jazzMap, clipMap);
    const types = c.moduleTypes.get(mod.asm)!, pid = [...types].find(n => n.startsWith("'<PrivateImplementationDetails>"));
    const r = rewriteIl(body, map, pid ? new Set([...types].filter(n => n !== pid)) : types, mod.asm);
    const own = pid ? pidSubset(mod, pid, r.text) : undefined;
    if (own?.problems.length) fallback = own.problems.join('; ');
    rw = r; il = header + r.text + (own?.text ?? '');
  }
  let dllPath = join(c.work, `${asmName}.dll`);
  let dll = Buffer.alloc(0);
  if (c.build) {
    if (!fallback) { const err = ilasm(il, dllPath); if (err) fallback = `ilasm: ${err}`; }
    if (fallback) {
      if (chain[0].sealed || chain[0].abstract) throw new Error(`${objkClass}: fallback impossible (${chain[0].sealed ? 'sealed' : 'abstract'}): ${fallback}`);
      mode = 'subclass';
      dllPath = join(c.work, `${asmName}.sub.dll`); // keeps the failed clone's .il for a look
      const err = ilasm(header + subclassIl(mod, objkClass, newLeaf), dllPath);
      if (err) throw new Error(`subclass ilasm: ${err}`);
    }
    dll = readFileSync(dllPath);
  } else if (fallback) mode = 'subclass';
  const scriptCls = newLeaf; // both modes: the new leaf type
  // tunings: ITUN for the cloned definitions / object types, class tuning XML at FNV64(full name) [verified static for both]
  const extra: Resource[] = [];
  const renameRt = (n: string) => { for (const [o, nw] of map) if (n === o || n.startsWith(`${o}+`)) return nw + n.slice(o.length); return n; };
  let ituns = 0, classTunings = 0;
  if (mode === 'il') {
    for (const t of c.ituns) {
      const ni = renameRt(t.inter), no = renameRt(t.obj);
      if (ni === t.inter && no === t.obj) continue;
      const xml = t.xml.replace(`<Interaction name="${t.inter}"`, `<Interaction name="${ni}"`).replace(`<Object name="${t.obj}"`, `<Object name="${no}"`);
      extra.push({ type: TS.ITUN, group: t.e.group, instance: fnv64(`cc-gen-script/${c.seed}/${i}/itun/${hex(t.e.inst)}`), data: Buffer.from(xml, 'utf8') });
      ituns++;
    }
    for (const k of chain) for (const n of [k.name, ...k.nested.map(x => `${k.name}+${x.replace(/\//g, '+')}`)]) {
      const e = c.ix.byKey.get(keyOf(TS.XML, 0, fnv64(n)));
      if (!e) continue;
      extra.push({ type: TS.XML, group: 0, instance: fnv64(renameRt(n)), data: c.ix.body(e) });
      classTunings++;
    }
  }
  let objdJazzRefs = 0;
  // object resources: OBJK -> new class, catalogue name "CCGenScript <name> #i"
  const out = res.map(r => {
    if (r.type === T.OBJK) return { ...r, data: setScriptClass(r.data, scriptCls) };
    // some OBJDs name their state machine by bare instance (SwingSet, Sprinkler [verified: the jazz instance, no type next to it]): point it at the copy
    if (r.type === T.OBJD && mode === 'il') { const b = Buffer.from(r.data); for (const j of jazzOut) objdJazzRefs += replaceInst(b, fnv64(j.orig), fnv64(j.new)); return { ...r, data: b }; }
    if (r.type === T.STBL) return { ...r, data: writeStbl(new Map([...readStblLocal(r.data)].map(([k, s]) => [k, s.replace(/^CCGen /, 'CCGenScript ')]))) };
    return r;
  });
  if (c.build) out.push({ type: TYPE_S3SA, group: 0, instance: fnv64(asmName), data: s3saEncode(dll) });
  out.push(...extra, ...anim);
  const defs = mode === 'il' ? chain.flatMap(k => k.defs.map(d => `${map.get(k.name)}+${d.replace(/\//g, '+')}`)) : [];
  const item: ScriptItem = { i, kind: 'scripted', mode, ...(fallback ? { fallback } : {}), origin: originOf(root), source: base.source, name: base.name, package: `ccgs-${c.seed}-${pad}.package`,
    objd: base.root, objkClass, scriptClass: scriptCls, cloned: mode === 'il' ? chain.map(k => ({ orig: k.name, new: map.get(k.name)! })) : [], assembly: asmName,
    mvid: dll.length ? mvid(dll) : '', assemblyBytes: dll.length, ilLines: il.split('\n').length, prefixedRefs: rw.prefixed,
    interactions: defs, interactionsOrig: chain.flatMap(k => k.defs.map(d => `${k.name}+${d.replace(/\//g, '+')}`)),
    jazz: mode === 'il' ? jazzOut : [], jazzNotRenamed: [...lit.jazzNoCall], objdJazzRefs, clips: mode === 'il' ? clipNew.size : 0,
    clipBytes: mode === 'il' ? anim.filter(r => r.type === TS.CLIP).reduce((s, r) => s + r.data.length, 0) : 0, ituns, classTunings,
    resources: 0, diskBytes: 0, problems: [] };
  const final = mode === 'il' ? out : out.filter(r => r.type !== TS.CLIP && r.type !== TS.JAZZ); // fallback: EA's animations
  item.resources = final.length; item.diskBytes = final.reduce((s, r) => s + r.data.length, 0);
  return { res: final, item, oldInsts };
}
export function replaceInst(b: Buffer, from: bigint, to: bigint): number {
  let n = 0;
  for (let p = 0; p + 8 <= b.length; p++) if (b.readBigUInt64LE(p) === from) { b.writeBigUInt64LE(to, p); n++; p += 7; }
  return n;
}
// cc-gen's readStbl wants an exact-size buffer; re-export would touch cc-gen, so parse here (same format)
function readStblLocal(b: Buffer): Map<bigint, string> {
  const m = new Map<bigint, string>(), n = b.readUInt32LE(7); let p = 17;
  for (let i = 0; i < n; i++) { const ch = b.readUInt32LE(p + 8); m.set(b.readBigUInt64LE(p), b.toString('utf16le', p + 12, p + 12 + ch * 2)); p += 12 + ch * 2; }
  return m;
}

// candidates: OBJDs whose class chain clones, one per cloned top class, deterministic order per seed; base and EP/SP mixed
export function pickScripted(c: Ctx, count: number, epShare: number, log?: (s: string) => void): Entry[] {
  const want = { base: Math.round(count * (1 - epShare)), ep: count - Math.round(count * (1 - epShare)) };
  const got = { base: [] as Entry[], ep: [] as Entry[] }, tops = new Set<string>();
  // FNV-1's high bits follow the prefix (similar instances cluster): mix before sorting
  const mix = (x: bigint) => { x ^= x >> 33n; x = (x * 0xff51afd7ed558ccdn) & 0xffffffffffffffffn; return x ^ (x >> 33n); };
  const pools = new Map<string, Entry[]>();
  for (const e of c.ix.byKey.values()) if (e.type === T.OBJD) { const o = originOf(e); (pools.get(o) ?? pools.set(o, []).get(o)!).push(e); } // EP/SP OBJDs sit in group 0x08000000 etc.
  for (const l of pools.values()) l.sort((x, y) => { const a = mix(fnv64(`${c.seed}/${hex(x.inst)}`)), b = mix(fnv64(`${c.seed}/${hex(y.inst)}`)); return a < b ? -1 : a > b ? 1 : 0; });
  const take = (o: string): boolean => { // next cloneable source of one pack
    for (let l = pools.get(o)!, e = l.shift(); e; e = l.shift()) {
      const cls = scriptClass(c.ix, e);
      const mod = cls && findClass(c, cls), chain = mod && cloneChain(mod, cls!);
      if (!chain || tops.has(chain.at(-1)!.name) || chain[0].abstract) continue;
      // shortcut: Buy-catalogue OBJDs carry the catalogue block (3-6 KB); spawners, rabbit holes and relics are ~0.5 KB [inferred: 12 picks]; parse the OBJD flags if the load smoke's product count falls short
      if (c.ix.body(e).length < 1000) continue;
      try { const { item } = cloneItem(c.ix, e, 0, 'probe', c.names, 'object'); if (item.diskBytes < 20_000 || item.diskBytes > 4_000_000) continue; }
      catch { continue; } // no catalogue name
      tops.add(chain.at(-1)!.name);
      got[o === 'base' ? 'base' : 'ep'].push(e);
      log?.(`${o === 'base' ? 'base' : 'ep'} ${o} ${hex(e.inst)} ${cls} -> ${chain.map(k => k.name.split('.').pop()).join(' < ')}: ${chain.reduce((s, k) => s + k.defs.length, 0)} defs, ${chain.reduce((s, k) => s + k.text.split('\n').length, 0)} IL lines${chain.some(k => k.blockers.length) ? ' (fallback: ' + chain.flatMap(k => k.blockers).join('; ') + ')' : ''}`);
      return true;
    }
    return false;
  };
  while (got.base.length < want.base && pools.has('base') && take('base'));
  const packs = [...pools.keys()].filter(o => o !== 'base').sort((a, b) => a.localeCompare(b, 'en', { numeric: true }));
  for (let live = packs.slice(); got.ep.length < want.ep && live.length;) live = live.filter(o => got.ep.length < want.ep && take(o)); // one per pack per round
  if (got.base.length + got.ep.length < count) log?.(`only ${got.base.length} base + ${got.ep.length} EP/SP sources (wanted ${want.base} + ${want.ep}); items cycle over them`);
  const out: Entry[] = [];
  for (let k = 0; k < Math.max(got.base.length, got.ep.length); k++) { if (got.base[k]) out.push(got.base[k]); if (got.ep[k]) out.push(got.ep[k]); }
  return out;
}

export function generateScripted(c: Ctx, o: { count: number; out: string; sources: Entry[] }) {
  if (!/[\\/]game-copy[\\/]cc-synth[\\/][^\\/]/i.test(resolve(o.out))) throw new Error(`--out must be under game-copy/cc-synth/ (got ${o.out})`);
  if (!o.sources.length) throw new Error('no scripted sources');
  mkdirSync(o.out, { recursive: true }); mkdirSync(c.work, { recursive: true });
  const items: ScriptItem[] = [], problems: string[] = [], seen = new Map<string, string>(), mvids = new Set<string>();
  for (let i = 0; i < o.count; i++) {
    const root = o.sources[i % o.sources.length];
    let built;
    try { built = buildItem(c, root, i); } catch (e: any) { problems.push(`item ${i} ${hex(root.inst)}: ${e.message}`); continue; }
    const { res, item, oldInsts } = built;
    const file = join(o.out, item.package);
    writeFileSync(file, writePackage(res));
    const v = verifyPackage(readFileSync(file), c.ix, oldInsts);
    item.problems = v.problems;
    for (const k of v.keys) { if (seen.has(k)) problems.push(`${item.package}: key ${k} also in ${seen.get(k)}`); else seen.set(k, item.package); }
    if (item.mvid) { if (mvids.has(item.mvid)) problems.push(`${item.package}: MVID ${item.mvid} not unique`); mvids.add(item.mvid); }
    problems.push(...v.problems.map(p => `${item.package}: ${p}`));
    items.push(item);
  }
  const sum = (f: (x: ScriptItem) => number) => items.reduce((s, x) => s + f(x), 0);
  const manifest = { tool: 'tools/cc-gen-script.ts', seed: c.seed, count: items.length, time: new Date().toISOString(),
    modes: { il: items.filter(x => x.mode === 'il').length, subclass: items.filter(x => x.mode === 'subclass').length },
    origins: Object.fromEntries([...new Set(items.map(x => x.origin))].sort().map(k => [k, items.filter(x => x.origin === k).length])),
    totalDiskBytes: sum(x => x.diskBytes), totalAssemblyBytes: sum(x => x.assemblyBytes), totalClipBytes: sum(x => x.clipBytes),
    uniqueKeys: seen.size, problems, items };
  writeFileSync(join(o.out, 'manifest.json'), JSON.stringify(manifest, null, 1));
  return manifest;
}

// ildasm text of a script DLL, cached in game-copy/cc-synth/.il
function ildasmPath(): string {
  if (process.env.ILDASM) return process.env.ILDASM;
  const sdk = 'C:/Program Files (x86)/Microsoft SDKs/Windows';
  const found = existsSync(sdk) ? readdirSync(sdk).sort().reverse().flatMap(v => existsSync(join(sdk, v, 'bin')) ? readdirSync(join(sdk, v, 'bin')).sort().reverse().map(t => join(sdk, v, 'bin', t, 'ildasm.exe')) : []).find(existsSync) : undefined;
  if (!found) throw new Error('ildasm.exe not found (Windows SDK NETFX tools); set ILDASM=<path>');
  return found;
}
export function loadIl(asm: string): IlModule {
  const il = join(IL_DIR, `${asm}.il`), dll = join(ROOT, 'game-copy', 'assemblies', `${asm}.dll`);
  if (!existsSync(il)) {
    if (!existsSync(dll)) throw new Error(`missing ${dll}: run tools/extract-assemblies.ts first`);
    mkdirSync(IL_DIR, { recursive: true });
    execFileSync(ildasmPath(), ['-nobar', '-utf8', `-out=${il}`, dll], { stdio: 'pipe', timeout: 300_000 });
  }
  return parseIl(readFileSync(il, 'utf8'), asm);
}
// base game, then each EP/SP (Full before Delta inside a pack: later wins), thumbnails, and Game/Bin (jazz, ITUN, class tuning)
export function gamePackages(game: string): string[] {
  const base = ['FullBuild0', 'FullBuild1', 'FullBuild2', 'DeltaBuild0', 'DeltaBuild1', 'DeltaBuild2'].map(p => join(game, 'GameData/Shared/Packages', `${p}.package`));
  const packs = readdirSync(game).filter(d => /^(EP|SP)\d+$/i.test(d)).sort((a, b) => a.localeCompare(b, 'en', { numeric: true }));
  const list = (d: string) => existsSync(d) ? readdirSync(d, { recursive: true }).map(String).filter(f => f.endsWith('.package')).sort().map(f => join(d, f)) : [];
  return [...base, ...packs.flatMap(p => [...list(join(game, p, 'GameData/Shared/Packages')), ...list(join(game, p, 'GameData/Shared/DeltaPackages')), ...list(join(game, p, 'Thumbnails'))]),
    join(game, 'Thumbnails/AllThumbnails.package'), ...['Game/Bin/gameplay.package', 'Game/Bin/Gameplay/GameplayData.package', 'Game/Bin/Jazz/JazzData.package'].map(p => join(game, p))].filter(existsSync);
}

// ---------------------------------------------------------------- self-check: own fixture IL + package, no EA bytes
function selfCheck() {
  const src = `// fixture
.assembly extern mscorlib
{
  .publickeytoken = (B7 7A 5C 56 19 34 E0 89 )
  .ver 2:0:0:0
}
.assembly extern SimIFace
{
  .ver 1:0:0:50
}
.assembly Fix
{
  .ver 1:0:0:0
}
.module Fix.dll

.class public auto ansi beforefieldinit Fix.Objects.ToyBase
       extends [mscorlib]System.Object
{
  .class auto ansi nested public beforefieldinit Play
         extends [mscorlib]System.Object
  {
    .class auto ansi sealed nested public beforefieldinit Definition
           extends class [Gp]Gp.InteractionDefinition\`3<class [Gp]Gp.Sim,class Fix.Objects.ToyBase,class Fix.Objects.ToyBase/Play>
    {
    } // end of class Definition
  } // end of class Play
  .field public static class Fix.Objects.Helper sHelper
  .method public hidebysig instance void Run() cil managed
  {
    .maxstack 8
    IL_0000:  ldstr      "toyjazz"
    IL_0005:  call       void Fix.Objects.Helper::AcquireStateMachine(string)
    IL_000a:  ldstr      "toyjazz"
    IL_000f:  call       void Fix.Objects.Helper::SetActor(string)
    IL_0014:  ldstr      "a_toy_play_x"
    IL_0019:  call       void Fix.Objects.Helper::Fix.Objects.ToyBase.Odd(string)
    IL_001e:  ldstr      "Fix.Objects.Helper"   // not a type ref
    IL_0023:  pop
    IL_0024:  ldsfld     object '<PrivateImplementationDetails>{G}'::'$$m-1'
    IL_0029:  pop
    IL_002a:  ldtoken    field valuetype '<PrivateImplementationDetails>{G}'/'__StaticArrayInitTypeSize=4' '<PrivateImplementationDetails>{G}'::'$$m-2'
    IL_002f:  pop
    IL_0030:  ret
  }
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed
  {
    .maxstack 8
    IL_0000:  ldarg.0
    IL_0001:  call       instance void [mscorlib]System.Object::.ctor()
    IL_0006:  ret
  }
} // end of class Fix.Objects.ToyBase

.class public auto ansi beforefieldinit Fix.Objects.ToyBear
       extends Fix.Objects.ToyBase
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed
  {
    .maxstack 8
    IL_0000:  ldarg.0
    IL_0001:  call       instance void Fix.Objects.ToyBase::.ctor()
    IL_0006:  ret
  }
} // end of class Fix.Objects.ToyBear

.class public auto ansi beforefieldinit Fix.Objects.Helper
       extends [mscorlib]System.Object
{
  .method public hidebysig static void AcquireStateMachine(string s) cil managed
  {
    .maxstack 8
    IL_0000:  ret
  }
  .method public hidebysig static void SetActor(string s) cil managed
  {
    .maxstack 8
    IL_0000:  ret
  }
  .method public hidebysig static void 'Fix.Objects.ToyBase.Odd'(string s) cil managed
  {
    .maxstack 8
    IL_0000:  ret
  }
} // end of class Fix.Objects.Helper

.class private auto ansi '<PrivateImplementationDetails>{G}'
       extends [mscorlib]System.Object
{
  .class explicit ansi sealed nested private '__StaticArrayInitTypeSize=4'
         extends [mscorlib]System.ValueType
  {
    .pack 1
    .size 4
  } // end of class '__StaticArrayInitTypeSize=4'

  .class explicit ansi sealed nested private '__StaticArrayInitTypeSize=40'
         extends [mscorlib]System.ValueType
  {
    .pack 1
    .size 40
  } // end of class '__StaticArrayInitTypeSize=40'

  .field static assembly object '$$m-1'
  .field static assembly valuetype '<PrivateImplementationDetails>{G}'/'__StaticArrayInitTypeSize=4' '$$m-2' at I_00000010
  .field static assembly valuetype '<PrivateImplementationDetails>{G}'/'__StaticArrayInitTypeSize=40' '$$m-3' at I_00000020
} // end of class '<PrivateImplementationDetails>{G}'

.data cil I_00000010 = bytearray (
                 01 02 03 29) // ...)
.data cil I_00000014 = int8[4]
`.replace(/\n/g, '\r\n'); // ildasm writes CRLF
  const mod = parseIl(src, 'Fix');
  assert.deepEqual([...mod.classes.keys()], ['Fix.Objects.ToyBase', 'Fix.Objects.ToyBear', 'Fix.Objects.Helper', "'<PrivateImplementationDetails>{G}'"]);
  const base = mod.classes.get('Fix.Objects.ToyBase')!;
  assert.deepEqual(base.defs, ['Play/Definition']); assert.deepEqual(base.nested, ['Play', 'Play/Definition']);
  assert.equal(mod.classes.get('Fix.Objects.ToyBear')!.ext, 'Fix.Objects.ToyBase');
  const chain = cloneChain(mod, 'Fix.Objects.ToyBear')!;
  assert.deepEqual(chain.map(k => k.name), ['Fix.Objects.ToyBear', 'Fix.Objects.ToyBase'], 'mimic leaf + the class with interactions');
  assert.equal(cloneChain(mod, 'Fix.Objects.Helper'), undefined, 'no interactions: not cloneable');
  assert.equal(cloneChain(parseIl(src.replace(/Fix\.Objects\.ToyBase/g, 'Sims3.Gameplay.Abstracts.ToyBase'), 'Fix'), 'Fix.Objects.ToyBear'), undefined, 'engine bases are never cloned');
  assert.equal(fixFloats('IL_0:  ldc.r4     -inf\r\nIL_1:  ldc.r8     -nan(ind)\r\n.field static float32 x = float32(inf)'),
    'IL_0:  ldc.r4     float32(0xFF800000)\r\nIL_1:  ldc.r8     float64(0xFFF8000000000000)\r\n.field static float32 x = float32(0x7F800000)');
  assert.equal(classLineName(".class public abstract auto ansi beforefieldinit Sims3.X`2<class (Sims3.I) T>"), 'Sims3.X`2', 'generic class name');
  const ob = Buffer.alloc(24); ob.writeBigUInt64LE(0x1122334455667788n, 3); assert.equal(replaceInst(ob, 0x1122334455667788n, 9n), 1); assert.equal(ob.readBigUInt64LE(3), 9n);
  const lit = animationLiterals(chain.map(k => k.text).join('\n'), s => s === 'toyjazz', s => s === 'a_toy_play_x');
  assert.deepEqual([[...lit.jazz], [...lit.clips], [...lit.jazzNoCall]], [['toyjazz'], ['a_toy_play_x'], []], 'jazz by the acquire call, clip anywhere');
  let body = renameLiterals(chain.map(k => k.text).join('\r\n'), new Map([['toyjazz', 'ccgs1_00000_toyjazz']]), new Map([['a_toy_play_x', 'ccgs1_00000_a_toy_play_x']]));
  assert.match(body, /ldstr\s+"ccgs1_00000_toyjazz"\r?\n\s+IL_0005:\s+call\s+void Fix.Objects.Helper::AcquireStateMachine/, 'jazz literal renamed at the acquire');
  assert.match(body, /ldstr\s+"toyjazz"\r?\n\s+IL_000f:\s+call\s+void Fix.Objects.Helper::SetActor/, 'actor literal kept');
  const map = new Map([['Fix.Objects.ToyBear', 'CCGenScript.S1.I00000.ToyBear'], ['Fix.Objects.ToyBase', 'CCGenScript.S1.I00000.ToyBase']]);
  const rw = rewriteIl(body, map, new Set(mod.classes.keys()), 'Fix');
  body = rw.text;
  assert.match(body, /^\.class public auto ansi beforefieldinit CCGenScript\.S1\.I00000\.ToyBase\r?$/m);
  assert.match(body, /extends CCGenScript\.S1\.I00000\.ToyBase/, 'leaf extends the cloned base');
  assert.match(body, /class CCGenScript\.S1\.I00000\.ToyBase,class CCGenScript\.S1\.I00000\.ToyBase\/Play>/, 'generic args + nested ref renamed');
  assert.match(body, /class \[Fix\]Fix\.Objects\.Helper sHelper/, 'other same-module type prefixed');
  assert.match(body, /\[Fix\]Fix\.Objects\.Helper::Fix\.Objects\.ToyBase\.Odd/, 'member name after :: untouched');
  assert.match(body, /ldstr\s+"Fix\.Objects\.Helper"/, 'string untouched');
  assert.match(body, /\[Gp\]Gp\.Sim/, 'foreign refs untouched');
  assert.match(body, /ldsfld\s+object \[Fix\]'<PrivateImplementationDetails>\{G\}'::'\$\$m-1'/, 'quoted module type prefixed, quoted member kept');
  assert.equal(rw.renamed, 6); // 2 .class names, extends, generic args x2, ToyBase::.ctor (comments skipped)
  // own <PrivateImplementationDetails>: refs stay unprefixed, only referenced fields + their size type + data blob copied
  const pid = "'<PrivateImplementationDetails>{G}'";
  assert.equal(mod.data.get('I_00000010'), '.data cil I_00000010 = bytearray (\n                 01 02 03 29)', 'bytearray up to the ) outside the comment');
  const rwOwn = rewriteIl(renameLiterals(chain.map(k => k.text).join('\r\n'), new Map(), new Map()), map, new Set([...mod.classes.keys()].filter(n => n !== pid)), 'Fix');
  assert.doesNotMatch(rwOwn.text, /\[Fix\]'<Private/, 'no cross-assembly PID refs');
  const own = pidSubset(mod, pid, rwOwn.text);
  assert.deepEqual(own.problems, []); assert.equal(own.fields, 2);
  assert.match(own.text, /'__StaticArrayInitTypeSize=4'/); assert.doesNotMatch(own.text, /=40'|\$\$m-3/, 'unreferenced field and size type left out');
  assert.match(own.text, /'\$\$m-2' at I_00000010[\s\S]*\.data cil I_00000010 = bytearray/);
  assert.match(pidSubset({ ...mod, data: new Map([['I_00000010', '.data cil I_00000010 = int8[2]']]) }, pid, rwOwn.text).problems.join(), /has 2 bytes, field type 4/);
  assert.equal(originOf({ file: 'G/SP3/GameData/Shared/DeltaPackages/p02/DeltaBuild_p02.package' }), 'EP1', 'delta patch -> its pack');
  setProducts(['G/EP2/GameData/Shared/Packages/FullBuild_p04.package']);
  assert.equal(originOf({ file: 'G\\EP9\\GameData\\Shared\\DeltaPackages\\p04\\DeltaBuild_p04.package' }), 'EP2', 'backslash paths');
  assert.equal(originOf({ file: 'G/SP1/GameData/Shared/Packages/FullBuild_p03.package' }), 'SP1'); assert.equal(originOf({ file: 'G/GameData/Shared/Packages/FullBuild0.package' }), 'base');
  // OBJK scriptClass rewrite (own bytes in the base-game layout)
  const objk = Buffer.concat([Buffer.from([1, 0, 0, 0, 1]), Buffer.from('\x0b\x00\x00\x00scriptClass\x00\x13\x00\x00\x00Fix.Objects.ToyBear\x01\x00\x00\x00', 'latin1')]);
  const nk = setScriptClass(objk, 'CCGenScript.S1.I00000.ToyBear');
  assert.equal(nk.toString('latin1', 25, 25 + 29), 'CCGenScript.S1.I00000.ToyBear'); assert.equal(nk.readUInt32LE(21), 29);
  assert.deepEqual([...nk.subarray(nk.length - 4)], [1, 0, 0, 0], 'bytes after the string kept');
  // header: externs + source as extern + [Tunable]
  const h = assemblyHeader(mod, 'CCGenS1I00000');
  assert.match(h, /\.assembly extern Fix\s*\{\s*\.ver 1:0:0:0/); assert.match(h, /TunableAttribute/); assert.match(h, /\.module CCGenS1I00000\.dll/);
  // end to end with ilasm (Windows only): clone and fallback assemble, MVIDs differ, OBJK + S3SA in the package
  if (process.platform === 'win32' && existsSync(ILASM)) {
    const dir = mkdtempSync(join(tmpdir(), 'cc-gen-script-'));
    try {
      const e1 = ilasm(h + body, join(dir, 'a.dll')); assert.equal(e1, undefined, `clone ilasm: ${e1}`);
      const e2 = ilasm(h + subclassIl(mod, 'Fix.Objects.ToyBear', 'CCGenScript.S1.I00001.ToyBear'), join(dir, 'b.dll')); assert.equal(e2, undefined, `subclass ilasm: ${e2}`);
      const e3 = ilasm(h + body, join(dir, 'c.dll')); assert.equal(e3, undefined);
      const e4 = ilasm(h + rwOwn.text + own.text, join(dir, 'e.dll')); assert.equal(e4, undefined, `own PID ilasm: ${e4}`);
      const [ma, mc] = [mvid(readFileSync(join(dir, 'a.dll'))), mvid(readFileSync(join(dir, 'c.dll')))];
      assert.match(ma, /^[0-9a-f]{32}$/); assert.notEqual(ma, mc, 'fresh MVID per assembly');
      assert.ok(ilasm(h + body.replace('extends CCGenScript', 'extendz CCGenScript'), join(dir, 'd.dll')), 'ilasm errors are reported');
      const pkg = writePackage([{ type: TYPE_S3SA, group: 0, instance: fnv64('CCGenS1I00000'), data: s3saEncode(readFileSync(join(dir, 'a.dll'))) }]);
      assert.equal(readIndex(pkg)[0].type, TYPE_S3SA);
    } finally { rmSync(dir, { recursive: true, force: true }); }
  } else console.log('cc-gen-script: ilasm step skipped (Windows only)');
  console.log('cc-gen-script self-check done');
}

if (process.argv[1]?.endsWith('cc-gen-script.ts')) {
  const { values: v } = parseArgs({ options: { count: { type: 'string', default: '10' }, seed: { type: 'string', default: '1' }, out: { type: 'string' },
    'ep-share': { type: 'string', default: '0.5' }, game: { type: 'string' }, 'list-sources': { type: 'boolean' }, 'self-check': { type: 'boolean' } } });
  if (v['self-check']) { selfCheck(); process.exit(0); }
  const t0 = Date.now(), count = Number(v.count), seed = v.seed!;
  const game = v.game ?? process.env.TS3_GAME ?? 'C:/Program Files (x86)/Steam/steamapps/common/The Sims 3';
  const ix = new Index();
  const pkgs = gamePackages(game);
  for (const p of pkgs) ix.add(p);
  setProducts(pkgs);
  const mods = SCRIPT_DLLS.map(loadIl);
  const c: Ctx = { ix, names: englishNames(ix), mods, moduleTypes: new Map(mods.map(m => [m.asm, new Set(m.classes.keys())])), ituns: itunIndex(ix),
    work: join(IL_DIR, `build-s${seed}`), seed, build: !v['list-sources'] };
  const sources = pickScripted(c, count, Number(v['ep-share']), v['list-sources'] ? console.log : undefined);
  if (v['list-sources']) process.exit(0);
  const out = v.out ?? join(ROOT, 'game-copy', 'cc-synth', `cc-script-${count}`);
  const m = generateScripted(c, { count, out, sources });
  ix.close();
  const mb = (n: number) => (n / 1e6).toFixed(1);
  console.log(`cc-gen-script: ${m.count} packages in ${out.split(sep).join('/')} (${((Date.now() - t0) / 1000).toFixed(0)} s): il ${m.modes.il}, subclass ${m.modes.subclass}; origins ${JSON.stringify(m.origins)}; ` +
    `${mb(m.totalDiskBytes)} MB disk (assemblies ${mb(m.totalAssemblyBytes)} MB, clips ${mb(m.totalClipBytes)} MB); assembly ${(Math.min(...m.items.map(x => x.assemblyBytes)) / 1e3).toFixed(0)}-${(Math.max(...m.items.map(x => x.assemblyBytes)) / 1e3).toFixed(0)} KB; problems ${m.problems.length}`);
  for (const p of m.problems.slice(0, 10)) console.log(`  ${p}`);
  process.exit(m.problems.length ? 1 : 0);
}
