import test from 'node:test';
import assert from 'node:assert/strict';
import {createSdFormatDialog} from '../web/modules/sd-format.js';
function fixture(){
 const live=[];const calls=[];
 const document={body:{append:e=>live.push(e)},createElement:tag=>({tag,dataset:{},style:{},children:[],append(...items){this.children.push(...items)},setAttribute(){},addEventListener(){},showModal(){},remove(){const i=live.indexOf(this);if(i>=0)live.splice(i,1)},querySelectorAll(tag){return this.children.filter(e=>e.tag===tag)}})};
 return {live,calls,ui:createSdFormatDialog({document,request:async(...args)=>calls.push(args)})};
}
test('healthy card never prompts or formats; remote cancel closes prompt',()=>{
 const f=fixture();f.ui.update({sdFormat:{state:0,prompt:false}});assert.equal(f.live.length,0);
 f.ui.update({sdFormat:{state:0,prompt:true}});assert.equal(f.live.length,1);assert.equal(f.calls.length,0);
 f.ui.update({sdFormat:{state:0,prompt:false}});assert.equal(f.live.length,0);
});
test('erase requires explicit click; shared formatting state closes confirmation',async()=>{
 const f=fixture();f.ui.update({sdFormat:{state:0,prompt:true}});
 await f.live[0].querySelectorAll('button')[1].onclick();
 assert.deepEqual(f.calls,[['/api/storage/format?action=confirm&erase=yes',{method:'POST'}]]);
 assert.equal(f.live[0].dataset.mode,'busy');
 assert.equal(f.live[0].children.filter(e=>e.tag==='progress').length,1);
 assert.equal(f.live[0].querySelectorAll('button').length,0);
 // The same transition also happens when confirmation originated on the LCD.
 const g=fixture();g.ui.update({sdFormat:{state:0,prompt:true}});g.ui.update({sdFormat:{state:2,prompt:false}});
 assert.equal(g.live.length,1);assert.equal(g.live[0].dataset.mode,'busy');assert.equal(g.calls.length,0);
});
test('completed and failed operations remove progress popup',()=>{
 const original=globalThis.setTimeout;globalThis.setTimeout=()=>0;
 try{for(const state of [3,4]){const f=fixture();f.ui.update({sdFormat:{state:2,prompt:false}});f.ui.update({sdFormat:{state,prompt:false}});assert.equal(f.live.filter(e=>e.tag==='dialog').length,0);assert.match(f.live[0].textContent,state===3?/ready/:/failed/);}}
 finally{globalThis.setTimeout=original;}
});
test('cancel sends only dismissal and never starts formatting',async()=>{
 const f=fixture();f.ui.update({sdFormat:{state:0,prompt:true}});
 await f.live[0].querySelectorAll('button')[0].onclick();
 assert.deepEqual(f.calls,[['/api/storage/format?action=cancel',{method:'POST'}]]);
 assert.equal(f.live.length,0);
});
