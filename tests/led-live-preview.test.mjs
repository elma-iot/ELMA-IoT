import test from 'node:test';
import assert from 'node:assert/strict';
import {runtimeFrame} from '../web/modules/led-effects.js';
test('runtime phase follows elapsed milliseconds, not frame count',()=>{
 const s={phase:1000,age:10,received:200,running:true,on:true,effect:'scan',brightness:37};
 assert.equal(runtimeFrame(s,233).phase,1043);
 assert.equal(runtimeFrame(s,1200).phase,2010);
 assert.equal(runtimeFrame(s,5000).phase,4010);
 assert.equal(runtimeFrame({...s,running:false},1200).phase,1000);
 assert.equal(runtimeFrame(s,233).brightness,37);
 assert.equal(runtimeFrame(null,1000),null);
});

import {displayPixel,animateArray} from '../web/modules/led-effects.js';
test('display transfer preserves off/full intensity and represents linear 20% light',()=>{
 assert.deepEqual(displayPixel([0,51,255]),[0,124,255]);
});
test('redraw paints actual pixels immediately without a placeholder or phase reset',()=>{
 const previous={document:globalThis.document,raf:globalThis.requestAnimationFrame,cancel:globalThis.cancelAnimationFrame,performance:globalThis.performance};
 try{
  globalThis.document={hidden:false};globalThis.performance={now:()=>1000};
  globalThis.requestAnimationFrame=()=>7;let cancelled=0;globalThis.cancelAnimationFrame=id=>cancelled=id;
  const pixels=Array.from({length:3},()=>({fill:'placeholder',setAttribute(_,value){this.fill=value;}}));
  const view={isConnected:true,querySelectorAll:()=>pixels};
  const options=()=>({count:3}),defaults=()=>({effect:'chase',effectSpeed:100,brightness:20,red:255,green:0,blue:0});
  const stop=animateArray(view,options,defaults);
  // 1000 / 25 ms = step 40, index 1; rebuilding must not jump to index 0.
  assert.deepEqual(pixels.map(p=>p.fill),['rgb(0,0,0)','rgb(124,0,0)','rgb(0,0,0)']);stop();assert.equal(cancelled,7);
  const stopLive=animateArray(view,options,defaults,()=>null);
  assert.ok(pixels.every(p=>p.fill==='rgb(0,0,0)'));stopLive();
 }finally{globalThis.document=previous.document;globalThis.requestAnimationFrame=previous.raf;globalThis.cancelAnimationFrame=previous.cancel;globalThis.performance=previous.performance;}
});
