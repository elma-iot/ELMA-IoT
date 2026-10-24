import test from 'node:test';
import assert from 'node:assert/strict';
import {bindCompactGpioAutosave} from '../web/modules/compact-gpio-autosave.js';
function setup(save){
 const callbacks={},timers=new Map(),errors=[];let id=0,ready=false;
 const form={elements:Object.fromEntries(['pin','green','blue','kind','activeLow'].map(name=>[name,{value:name==='kind'?'neopixel':'-1',checked:false}])),addEventListener:(event,fn)=>callbacks[event]=fn};
 const binding=bindCompactGpioAutosave({form,ready:()=>ready,save,onError:error=>errors.push(error),setTimer:fn=>{timers.set(++id,fn);return id;},clearTimer:id=>timers.delete(id)});
 return {...binding,form,timers,errors,change:callbacks.change,ready:()=>ready=true};
}
test('compact GPIO changes autosave pin zero, coalesce edits and ignore initialization',async()=>{
 const saved=[],s=setup(async data=>saved.push(data));s.change();assert.equal(s.timers.size,0);s.ready();
 s.form.elements.pin.value='0';s.change();s.form.elements.activeLow.checked=true;s.change();
 assert.equal(s.timers.size,1);await s.flush();assert.equal(saved.length,1);assert.equal(saved[0].device.statusLedPin,0);assert.equal(saved[0].device.statusLedActiveLow,true);
});
test('compact autosave serializes slow operations and keeps the newest edit',async()=>{
 const saved=[];let release;const s=setup(async data=>{saved.push(data);if(saved.length===1)await new Promise(resolve=>release=resolve);});s.ready();
 s.form.elements.pin.value='0';s.change();const first=s.flush();
 for(const pin of ['4','5']){s.form.elements.pin.value=pin;s.change();}
 const second=s.flush();release();await Promise.all([first,second]);assert.deepEqual(saved.map(x=>x.device.statusLedPin),[0,5]);
});
test('compact RGB mappings wait for three distinct channels and save on completion',async()=>{
 const saved=[],s=setup(async data=>saved.push(data));s.ready();s.form.elements.kind.value='rgb';
 s.form.elements.pin.value='0';s.form.elements.green.value='0';s.form.elements.blue.value='2';s.change();assert.equal(s.timers.size,0);assert.equal(saved.length,0);assert.equal(s.errors.length,1);
 s.form.elements.green.value='1';s.change();await s.flush();assert.equal(saved[0].device.statusLedGreenPin,1);
});
