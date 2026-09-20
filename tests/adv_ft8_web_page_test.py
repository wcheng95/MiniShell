#!/usr/bin/env python3
"""Run embedded mirror JavaScript with DOM/fetch stubs."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
root=Path(sys.argv[1]).resolve()
page=''.join(json.loads(line) for line in (root/'platform/adv/adv_ft8_web_page.h').read_text().splitlines() if line.startswith('"'))
assert 'innerHTML' not in page and 'repeat(20,1ch)' in page and 'white-space:pre' in page
script=page.split('<script>')[1].split('</script>')[0]
harness=r'''
const assert=require('node:assert/strict');
class Element{constructor(){this.children=[];this.textContent='';this.value='';this.handlers={};this.inverse=false;this.classList={toggle:(name,value)=>{assert.equal(name,'inverse');this.inverse=value;}};}append(child){this.children.push(child);}addEventListener(name,fn){this.handlers[name]=fn;}}
const elements=new Map(),handlers={};
global.document={getElementById:id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);},createElement:()=>new Element(),addEventListener:(name,fn)=>handlers[name]=fn};
let interval,requests=[],responseBytes=new Uint8Array(284),failKey=false,blockKey=false,unblock;
for(let i=0;i<140;i++){responseBytes[4+i]=32+i%95;responseBytes[144+i]=i%2;}
global.setInterval=(fn,ms)=>{assert.equal(ms,250);interval=fn;};
global.fetch=async(url,options)=>{requests.push({url,...options});if(options.method==='PUT'){if(blockKey)await new Promise(resolve=>unblock=resolve);return {ok:!failKey,text:async()=>'queue full'};}return {ok:true,arrayBuffer:async()=>responseBytes.buffer};};
const settle=()=>new Promise(resolve=>setImmediate(resolve));
'''
tests=r'''
(async()=>{
 await settle();assert.equal(cells.length,140);assert.equal(screen.children.length,140);
 for(let i=0;i<140;i++){assert.equal(cells[i].textContent,String.fromCharCode(32+i%95));assert.equal(cells[i].inverse,Boolean(i%2));}
 responseBytes=new Uint8Array(283);await poll();assert(status.textContent.includes('Incomplete screen'));
 const dispatch=(key,extra={})=>{let prevented=false;handlers.keydown({key,preventDefault:()=>prevented=true,...extra});return prevented;};
 requests=[];assert(dispatch('O',{shiftKey:true}));await settle();assert.equal(requests[0].url,'/api/key?c=79&m=1');assert.equal(requests[0].method,'PUT');
 requests=[];for(const [name,value] of Object.entries(special)){assert(dispatch(name));await settle();assert.equal(requests.at(-1).url,'/api/key?k='+value+'&m=0');}
 requests=[];assert(!dispatch('F5'));assert(!dispatch('é'));assert(!dispatch('a',{isComposing:true}));assert.equal(requests.length,0);
 input.value='a =';input.handlers.input({});await settle();assert.deepEqual(requests.map(r=>r.url),['/api/key?c=97&m=0','/api/key?c=32&m=0','/api/key?c=61&m=0']);assert.equal(input.value,'');
 requests=[];input.handlers.beforeinput({inputType:'deleteContentBackward',preventDefault(){}});await settle();assert.equal(requests[0].url,'/api/key?k=6&m=0');
 const buttons=document.getElementById('buttons').children;assert.equal(buttons.length,10);
 requests=[];for(const button of buttons){button.onclick();await settle();}assert.equal(requests.length,10);
 requests=[];failKey=true;key('c',65);await settle();assert.equal(requests.length,1);assert(status.textContent.includes('not retried'));failKey=false;
 blockKey=true;requests=[];key('c',65);for(let i=0;i<30;i++)key('c',66);assert.equal(pending.length,16);assert(status.textContent.includes('queue full'));
 failKey=true;blockKey=false;unblock();await settle();assert.equal(pending.length,0);assert.equal(requests.length,1);
 console.log('adv_ft8_web_page: PASS');
})().catch(error=>{console.error(error);process.exitCode=1;});
'''
with tempfile.TemporaryDirectory(prefix='ft8-web-page-') as tmp:
    js=Path(tmp)/'test.js';js.write_text(harness+script+tests)
    subprocess.run(['node','--check',str(js)],check=True)
    subprocess.run(['node',str(js)],check=True)
