// Local test packages for the NRaas upstream patches (docs/145 §5, docs/146 §5): builds NRaasOverwatch and NRaasErrorTrap
// from an NRaas clone at a base ref and at the patched refs (Roslyn C# 7.3 against the clone's Sims3/Compiler DLLs) and
// repacks each into a copy of the installed package (read only), only the S3SA assembly swapped. Output (git-ignored):
//   build/nraas-upstream/{base,v2}/NRaas_Overwatch.package, NRaas_ErrorTrap.package
// Installed as the test-only fixes nraas-upstream-base / nraas-upstream-v2 (install-fixes.ts backs up the originals).
// Not in the packages: the patched Overwatch's English strings for "Set Alarm Spacing" (the menu shows the key) and
// ErrorTrapTuning7.xml (the field default 10 applies). Nothing of NRaas is stored in this repo.
//   node tools/setup/build-nraas-upstream.ts <NRaas clone> [overwatchRef=ow] [errortrapRef=et] [baseRef=origin/master]
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { join, resolve } from 'node:path';
import { readIndex, refpack, decryptS3SA, peImage, moduleName } from '../extract-assemblies.ts';
import { TYPE_S3SA, s3saEncode, writePackage, type Resource } from '../dbpf.ts';

const [clone, owRef = 'ow', etRef = 'et', baseRef = 'origin/master'] = process.argv.slice(2);
if (!clone) { console.error('usage: node tools/setup/build-nraas-upstream.ts <NRaas clone> [overwatchRef] [errortrapRef] [baseRef]'); process.exit(2); }
const ROOT = resolve(import.meta.dirname, '..', '..');
const OUT = join(ROOT, 'build', 'nraas-upstream');
const USER = process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3');
const CSC = ['Professional', 'Community', 'Enterprise'].map(e => `C:/Program Files/Microsoft Visual Studio/2022/${e}/Msbuild/Current/Bin/Roslyn/csc.exe`).find(existsSync);
if (!CSC) throw new Error('Roslyn csc.exe (Visual Studio 2022) not found');

function compile(ref: string, proj: string, dll: string) {
  const tree = join(OUT, 'src', ref.replace(/[^\w.-]/g, '_'));
  if (!existsSync(tree)) execFileSync('git', ['-C', clone, 'worktree', 'add', '-f', '--detach', tree, ref], { stdio: 'ignore' });
  const dir = join(tree, proj);
  const csproj = readFileSync(join(dir, proj + '.csproj'), 'utf8');
  const files = [...csproj.matchAll(/<Compile Include="([^"]+)"/g)].map(m => join(dir, m[1].replace(/&amp;/g, '&')));
  const refs = [...csproj.matchAll(/<HintPath>([^<]+)<\/HintPath>/g)].map(m => '/r:' + join(dir, m[1]));
  const rsp = dll + '.rsp';
  writeFileSync(rsp, ['/nologo', '/nostdlib+', '/target:library', '/langversion:7.3', '/define:TRACE', '/warn:0', '/out:' + dll, ...refs, ...files].map(a => `"${a}"`).join('\n'));
  execFileSync(CSC, ['/noconfig', '@' + rsp], { stdio: 'inherit', timeout: 280000 });
}

// the installed package with its S3SA for `module` replaced; other resources copied (decompressed)
function repack(installed: string, module: string, dll: string, out: string) {
  const pkg = readFileSync(installed);
  let swapped = 0;
  const res: Resource[] = readIndex(pkg).map(e => {
    const raw = pkg.subarray(e.offset, e.offset + e.fileSize);
    let data = e.compressed === 0xffff ? refpack(raw) : Buffer.from(raw);
    if (e.type === TYPE_S3SA) {
      const img = peImage(data) ?? peImage(decryptS3SA(data));
      if (img && moduleName(img)?.replace(/\.dll$/i, '').toLowerCase() === module.toLowerCase()) { data = s3saEncode(readFileSync(dll)); swapped++; }
    }
    return { type: e.type, group: e.group, instance: e.inst, data };
  });
  if (swapped !== 1) throw new Error(`${installed}: expected one S3SA named ${module}.dll, swapped ${swapped}`);
  writeFileSync(out, writePackage(res));
  console.log(`built ${out}`);
}

for (const [variant, ow, et] of [['base', baseRef, baseRef], ['v2', owRef, etRef]]) {
  mkdirSync(join(OUT, variant), { recursive: true });
  for (const [proj, ref, pkgName] of [['NRaasOverwatch', ow, 'NRaas_Overwatch.package'], ['NRaasErrorTrap', et, 'NRaas_ErrorTrap.package']]) {
    const dll = join(OUT, variant, proj + '.dll');
    compile(ref, proj, dll);
    repack(join(USER, 'Mods', 'Packages', pkgName), proj, dll, join(OUT, variant, pkgName));
  }
}
for (const r of new Set([baseRef, owRef, etRef])) {
  const tree = join(OUT, 'src', r.replace(/[^\w.-]/g, '_'));
  execFileSync('git', ['-C', clone, 'worktree', 'remove', '--force', tree], { stdio: 'ignore' });
}
rmSync(join(OUT, 'src'), { recursive: true, force: true });
