// Run after `git merge <agent-branch>` stops on conflicts: resolves PLAN mapping-table rows (prefers the row marked done) and the Ghidra symbols CSV (union, drops later rows that rename an existing address).
const fs=require('fs'),{execSync}=require('child_process');
function res(f,fn){if(!fs.existsSync(f))return;let L=fs.readFileSync(f,'utf8').split(/\r?\n/);let n=0;for(;;){const s=L.findIndex(l=>l.startsWith('<<<<<<<'));if(s<0)break;const m=L.findIndex((l,i)=>i>s&&l==='=======');const e=L.findIndex((l,i)=>i>m&&l.startsWith('>>>>>>>'));L.splice(s,e-s+1,...fn(L.slice(s+1,m),L.slice(m+1,e)));n++}fs.writeFileSync(f,L.join('\n'));if(n)console.log(f,'resolved',n)}
const id=l=>(l.match(/^\s*\| (M-[A-Z0-9]+|N\d+) \|/)||[])[1]??l;
res('PLAN.md',(a,b)=>a.map(la=>{const lb=b.find(x=>id(x)===id(la));return lb&&/\*\*done\*\*/.test(lb)&&!/\*\*done\*\*/.test(la)?lb:la}).concat(b.filter(x=>!a.some(y=>id(y)===id(x)))));
const csv='tools/ghidra/symbols/ts3w-1.67.symbols.csv';
res(csv,(a,b)=>{const s=new Set(a);return [...a,...b.filter(l=>!s.has(l))]});
const L=fs.readFileSync(csv,'utf8').split(/\n/);const by={};L.forEach((l,i)=>{if(i){const k=l.split(',')[0].toLowerCase();if(k)(by[k]??=[]).push(i)}});const drop=new Set();for(const ix of Object.values(by))if(new Set(ix.map(i=>L[i].split(',')[1])).size>1)ix.slice(1).forEach(i=>drop.add(i));fs.writeFileSync(csv,L.filter((_,i)=>!drop.has(i)).join('\n'));if(drop.size)console.log('csv clashes dropped',drop.size);
const left=execSync('git diff --name-only --diff-filter=U').toString().trim();console.log(left?'STILL CONFLICTED: '+left:'clean');
