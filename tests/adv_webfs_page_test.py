#!/usr/bin/env python3
"""Exercise the embedded page with a small DOM/fetch stub; no browser dependency."""
import json
import re
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
 constructor(tag){this.tagName=tag;this.children=[];this.disabled=false;this.files=[];this.value='';this.textContent='';this.hidden=false;}
 append(...nodes){this.children.push(...nodes);}
 replaceChildren(){this.children=[];}
}
const elements=new Map();
for(const [tag,id,hidden] of pageElements){const node=new Element(tag);node.hidden=hidden;elements.set(id,node);}
global.document={getElementById:id=>{assert(elements.has(id),'unknown DOM id '+id);return elements.get(id);},createElement:tag=>new Element(tag),querySelectorAll:selector=>{
 const found=new Set();function visit(node){if(selector.split(',').includes(node.tagName))found.add(node);node.children.forEach(visit);}
 elements.forEach(visit);return Array.from(found);
}};
let listingEntries=[],requests=[],confirms=[],answer=true,promptAnswer=null,pendingMutation=false,failMutation=false,failRead=false,failList=false,fileText='SSID=café\nPW=a=b\n';
global.confirm=message=>{confirms.push(message);return answer;};
global.prompt=()=>promptAnswer;
global.fetch=async(url,options={})=>{
 assert(!pendingMutation,'mutations must be sequential');requests.push({url,...options});
 if((options.method||'GET')==='GET'&&url.startsWith('/api/file?')){
  await new Promise(resolve=>setImmediate(resolve));
  return {ok:!failRead,json:async()=>({error:'Volume unavailable'}),blob:async()=>new Blob([fileText])};
 }
 if(options.method==='GET') {if(failList)throw Error('Wi-Fi disconnected');return {ok:true,json:async()=>({total:100,used:10,free:90,entries:listingEntries.map(x=>({...x}))})};}
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
 assert.equal(rowFor('<unsafe>&.txt').children[0].children[0].href,undefined);
 const button=(name,label)=>rowFor(name).children[2].children.find(b=>b.textContent===label);

 const label=name=>rowFor(name).children[0].children[0];
 const click=name=>label(name).onclick({preventDefault(){}});
 for(const name of ['setting.txt','alias.txt']){
  for(const size of [0,65536,65537]){
   listingEntries.push({name,type:'file',size});await browse('/flash');
   assert.equal(label(name).tagName,size<=65536?'a':'span');
   assert.equal(typeof label(name).onclick,size<=65536?'function':'undefined');
   assert.deepEqual(rowFor(name).children[2].children.map(x=>x.textContent),['Download','Rename','Delete']);
   listingEntries.pop();
  }
 }
 listingEntries.push(...['setting.txt','alias.txt','setting.txt.bak','SETTING.TXT','station.txt'].map(name=>({name,type:'file',size:30})));
 await browse('/flash');
 for(const name of ['old.txt','<unsafe>&.txt','setting.txt.bak','SETTING.TXT','station.txt'])assert.equal(label(name).tagName,'span');
 for(const entry of listingEntries.filter(x=>x.type==='file')){
  assert.deepEqual(rowFor(entry.name).children[2].children.map(x=>x.textContent),['Download','Rename','Delete']);
  assert.equal(button(entry.name,'Download').href,'/api/file?path='+encodeURIComponent('/flash/'+entry.name));
  assert.equal(button(entry.name,'Download').download,entry.name);
 }
 requests=[];const opening=click('setting.txt');assert.equal(el('save').disabled,true);await opening;
 assert.equal(requests[0].url,'/api/file?path=%2Fflash%2Fsetting.txt');
 assert.equal(requests[0].headers['X-WebFS-Read'],'1');
 assert.equal(el('edittext').value,fileText);assert.equal(el('editpath').textContent,'/flash/setting.txt');
 assert.equal(el('editor').hidden,false);assert.equal(el('browser').hidden,true);
 el('edittext').value='PW=changed & café\n';requests=[];
 const saving=el('save').onclick();assert.equal(el('edittext').disabled,true);
 el('cancel').onclick();await el('save').onclick();await saving;
 assert.deepEqual(requests.map(r=>r.method),['PUT','GET']);assert.equal(requests[0].body,'PW=changed & café\n');
 assert.equal(requests[0].url,'/api/file?path=%2Fflash%2Fsetting.txt');
 assert.equal(el('editor').hidden,true);assert.equal(el('browser').hidden,false);
 await click('alias.txt');el('edittext').value='unsaved';requests=[];el('cancel').onclick();
 assert.equal(requests.length,0);assert.equal(el('editor').hidden,true);assert.equal(el('browser').hidden,false);assert.equal(el('edittext').value,'');
 failRead=true;await click('setting.txt');assert(el('error').textContent.includes('Volume unavailable'));assert.equal(el('editor').hidden,true);failRead=false;
 const original=fileText;fileText='é'.repeat(32769);await click('setting.txt');assert(el('error').textContent.includes('64 KiB'));assert.equal(el('editor').hidden,true);fileText=original;
 await click('setting.txt');el('edittext').value='retain this';failMutation=true;await el('save').onclick();failMutation=false;
 assert.equal(el('edittext').value,'retain this');assert.equal(el('editor').hidden,false);assert(el('editerror').textContent.includes('Directory is not empty'));
 el('edittext').value='é'.repeat(32769);requests=[];await el('save').onclick();assert.equal(requests.length,0);assert(el('editerror').textContent.includes('64 KiB'));
 el('edittext').value='é'.repeat(32768);requests=[];await el('save').onclick();assert.equal(new Blob([requests[0].body]).size,65536);
 await click('alias.txt');el('edittext').value='';requests=[];failList=true;await el('save').onclick();failList=false;
 assert.equal(requests[0].body,'');assert.equal(el('editor').hidden,true);assert(el('error').textContent.includes('File saved, but'));
 await browse('/flash');
 await click('folder/');assert.equal(path,'/flash/folder');await browse('/flash');
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
    nodes = [(tag, ident, 'hidden' in attrs) for tag, ident, attrs in re.findall(r'<(\w+)[^>]*?id="([^"]+)"([^>]*)>', page)]
    js.write_text('const pageElements='+json.dumps(nodes)+';\n'+stub+script+tests)
    subprocess.run(['node','--check',str(js)],check=True)
    subprocess.run(['node',str(js)],check=True)
