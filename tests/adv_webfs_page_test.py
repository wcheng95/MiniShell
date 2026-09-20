#!/usr/bin/env python3
"""Exercise the embedded page with a small DOM/fetch stub; no browser dependency."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
root = Path(sys.argv[1]).resolve()
page = ''.join(json.loads(line) for line in (root/'platform/adv/adv_webfs_page.h').read_text().splitlines() if line.startswith('"'))
assert 'innerHTML' not in page
script = page.split('<script>')[1].split('</script>')[0]
stub = r'''
const assert=require('node:assert/strict');
class Element {
 constructor(){this.children=[];this.disabled=false;this.files=[];this.value='';this.textContent='';}
 append(...nodes){this.children.push(...nodes);}
 replaceChildren(){this.children=[];}
}
const elements=new Map();
global.document={getElementById:id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);},createElement:()=>new Element(),querySelectorAll:()=>Array.from(elements.values())};
let listingEntries=[],requests=[],confirms=[],answer=true,promptAnswer=null,pendingMutation=false,failMutation=false;
global.confirm=message=>{confirms.push(message);return answer;};
global.prompt=()=>promptAnswer;
global.fetch=async(url,options)=>{
 assert(!pendingMutation,'mutations must be sequential');requests.push({url,...options});
 if(options.method==='GET')return {ok:true,json:async()=>({total:100,used:10,free:90,entries:listingEntries.map(x=>({...x}))})};
 assert(['PUT','DELETE'].includes(options.method));pendingMutation=true;
 await new Promise(resolve=>setImmediate(resolve));pendingMutation=false;
 return {ok:!failMutation,json:async()=>failMutation?{error:'Directory is not empty'}:{ok:true}};
};
'''
tests = r'''
(async()=>{
 await new Promise(resolve=>setImmediate(resolve));
 listingEntries=[{name:'old.txt',type:'file',size:3},{name:'folder',type:'dir',size:0},{name:'<unsafe>&.txt',type:'file',size:1}];
 await browse('/flash');assert.equal(el('parent').disabled,true);
 const rowFor=name=>el('entries').children.find(row=>row.children[0].children[0].textContent===name);
 assert.equal(rowFor('folder/').children[2].children.length,1,'no directory rename');
 assert.equal(rowFor('<unsafe>&.txt').children[0].children[0].textContent,'<unsafe>&.txt');
 assert.equal(rowFor('<unsafe>&.txt').children[0].children[0].href,'/api/file?path=%2Fflash%2F%3Cunsafe%3E%26.txt');
 const button=(name,label)=>rowFor(name).children[2].children.find(b=>b.textContent===label);
 answer=false;requests=[];await button('old.txt','Delete').onclick();assert.equal(requests.length,0);
 answer=true;await button('old.txt','Delete').onclick();assert.equal(requests[0].method,'DELETE');assert.equal(requests[1].method,'GET');
 requests=[];await button('folder/','Delete').onclick();assert.equal(requests[0].url,'/api/dir?path=%2Fflash%2Ffolder');assert(confirms.at(-1).includes('empty directory'));
 failMutation=true;requests=[];await button('folder/','Delete').onclick();assert(el('error').textContent.includes('Directory is not empty'));failMutation=false;
 promptAnswer='new';requests=[];await el('mkdir').onclick();assert.equal(requests[0].url,'/api/dir?path=%2Fflash%2Fnew');assert.equal(requests[0].method,'PUT');
 promptAnswer='old.txt';answer=false;requests=[];await button('<unsafe>&.txt','Rename').onclick();assert.equal(requests.length,0);
 answer=true;await button('<unsafe>&.txt','Rename').onclick();assert.equal(requests[0].url,'/api/rename?from=%2Fflash%2F%3Cunsafe%3E%26.txt&to=%2Fflash%2Fold.txt');
 promptAnswer='../outside';requests=[];await button('old.txt','Rename').onclick();assert.equal(requests.length,0);
 promptAnswer='folder';await button('old.txt','Rename').onclick();assert.equal(requests.length,0);
 el('files').files=[{name:'old.txt'},{name:'caf\u00e9.txt'}];answer=false;requests=[];await el('upload').onclick();assert.equal(requests.filter(r=>r.method==='PUT').length,1);
 answer=true;requests=[];const run=el('upload').onclick();await el('mkdir').onclick();await run;
 assert.deepEqual(requests.map(r=>r.method),['PUT','GET','PUT','GET']);
 assert.equal(requests[0].body,el('files').files[0]);assert.equal(requests[2].body,el('files').files[1]);
 assert.equal(requests[2].url,'/api/file?path=%2Fflash%2Fcaf%C3%A9.txt');assert.equal(busy,false);
 console.log('adv_webfs_page: PASS');
})().catch(error=>{console.error(error);process.exitCode=1;});
'''
with tempfile.TemporaryDirectory(prefix='webfs-page-') as tmp:
    js = Path(tmp)/'page.js'
    js.write_text(stub+script+tests)
    subprocess.run(['node','--check',str(js)],check=True)
    subprocess.run(['node',str(js)],check=True)
