import {test} from 'node:test';
import assert from 'node:assert/strict';
import {arrayOptions,arraySvg,arrayDefaults,availableEffects} from '../web/modules/led-array.js';
import {occupiedPeripheralPins} from '../web/modules/peripheral-gpio-defaults.js';
test('effect choices respect the compiled device binding and retain legacy defaults',()=>{
 assert.deepEqual(availableEffects({effects:['solid','rainbow','invalid']}),['solid','rainbow']);
 assert.equal(availableEffects().length,10);
 assert.deepEqual(availableEffects({effects:[]}),[]);
});
test('all layouts draw exactly their configured pixel count',()=>{
 for(const layout of ['strip','ring','panel']){
  const options=arrayOptions({LED_LAYOUT:layout,LED_COUNT:13,LED_ROWS:3,LED_COLUMNS:5});
  assert.equal(options.count,layout==='panel'?15:13);
  assert.equal((arraySvg(options).match(/<circle /g)||[]).length,options.count);
 }
});
test('invalid dimensions and chip budgets are rejected',()=>{
 for(const value of [null,false,0,-1,1.5,'x',Infinity]){
  if(value===null)continue; // missing persisted values receive the documented default
  assert.throws(()=>arrayOptions({LED_LAYOUT:'panel',LED_ROWS:value},32));
 }
 assert.throws(()=>arrayOptions({LED_LAYOUT:'panel',LED_ROWS:8,LED_COLUMNS:8},32));
});
test('layout metadata never reserves GPIOs',()=>{
 const pins=occupiedPeripheralPins({roles:[],bindings:{'control:0':{DIN:'4',LED_COUNT:16,LED_ROWS:2,LED_COLUMNS:8,LED_SERPENTINE:true}}});
 assert.deepEqual([...pins],[4]);
});
test('serpentine changes alternate rows, not count',()=>{
 const opts={LED_LAYOUT:'panel',LED_ROWS:2,LED_COLUMNS:3};
 const snake=arraySvg(arrayOptions({...opts,LED_SERPENTINE:true}));
 const straight=arraySvg(arrayOptions({...opts,LED_SERPENTINE:false}));
 const xs=s=>[...s.matchAll(/<circle cx="([^"]+)"/g)].map(m=>Number(m[1]));
 assert.deepEqual(xs(snake),[32,56,80,80,56,32]);
 assert.deepEqual(xs(straight),[32,56,80,32,56,80]);
});

test('array defaults validate and preserve configured brightness',()=>{
 assert.equal(arrayDefaults().brightness,20);
 assert.deepEqual(arrayDefaults({LED_DEFAULT_EFFECT:'rainbow',LED_DEFAULT_BRIGHTNESS:37}),{effect:'rainbow',brightness:37,red:255,green:128,blue:0,effectSpeed:50});
 for(const value of [true,-1,101,NaN,Infinity])assert.throws(()=>arrayDefaults({LED_DEFAULT_BRIGHTNESS:value}));
 assert.throws(()=>arrayDefaults({LED_DEFAULT_EFFECT:'unknown'}));
});


test('mixed chains retain individual defaults and sync uses first without erasing them',()=>{
 const values={LED_LAYOUT:'ring',LED_COUNT:32,LED_DEFAULT_EFFECT:'scan',LED_ARRAYS:2,LED_ARRAY_ITEMS:[{LED_LAYOUT:'strip',LED_COUNT:16,LED_DEFAULT_EFFECT:'breathe'}]};
 let chain=arrayOptions(values,128);assert.equal(chain.count,48);assert.equal(chain.segments[1].defaults.effect,'breathe');
 values.LED_SYNC=true;chain=arrayOptions(values,128);assert.equal(chain.segments[1].defaults.effect,'scan');
 values.LED_SYNC=false;chain=arrayOptions(values,128);assert.equal(chain.segments[1].defaults.effect,'breathe');
 const svg=arraySvg(chain);assert.equal((svg.match(/<circle /g)||[]).length,48);assert.equal((svg.match(/data-chain-link/g)||[]).length,1);
 assert.throws(()=>arrayOptions({...values,LED_ARRAYS:8},128));
});
