import fs from 'fs'; import path from 'path';
const root = process.argv[2]; const names = new Set(process.argv.slice(3));
function walk(d){ return fs.readdirSync(d,{withFileTypes:true}).flatMap(e=> e.isDirectory()? walk(path.join(d,e.name)) : e.name.endsWith('.cpp')? [path.join(d,e.name)]:[]); }
for (const f of walk(root)) { const t = fs.readFileSync(f,'utf8'); const re=/RegisterHandler\(TEXT\("([a-z0-9_]+)"\)/g; let m;
 while((m=re.exec(t))){ if(names.size && !names.has(m[1])) continue; let i=m.index, depth=0, j=i; for(;j<t.length;j++){ if(t[j]=='(')depth++; else if(t[j]==')'){depth--; if(depth==0)break;} }
 const block=t.slice(i,j+1); const params=[...block.matchAll(/MCPParam::(Required|Optional)\(TEXT\("(\w+)"\)/g)].map(p=>(p[1]=='Required'?'*':'')+p[2]); const helpers=[...block.matchAll(/\b([A-Z]\w+)\(\)/g)].map(h=>h[1]);
 console.log(m[1]+' ['+path.basename(f)+']: '+params.join(' ')+(helpers.length?'  {'+[...new Set(helpers)].join(',')+'}':'')); } }
