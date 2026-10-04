import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

test('compact API queue includes response consumption and recovers after failure',async()=>{
 const source=readFileSync(new URL('../web/esp8266/app.js',import.meta.url),'utf8');
 const start=source.indexOf('let requestQueue='),end=source.indexOf('\nfunction showError',start);
 let active=0,peak=0;const seen=[];
 const context=vm.createContext({AbortSignal,fetch:async path=>{
  ++active;peak=Math.max(peak,active);seen.push(path);
  return {ok:path!=='bad',status:path==='bad'?503:200,text:async()=>{
   await new Promise(resolve=>setTimeout(resolve,5));--active;
   return path==='bad'?'{"error":"busy"}':'{"ok":true}';
  }};
 }});
 const request=vm.runInContext(source.slice(start,end)+'\nrequest;',context);
 const results=await Promise.allSettled([request('status'),request('bad'),request('logics')]);
 assert.equal(peak,1);assert.deepEqual(seen,['status','bad','logics']);
 assert.equal(results[1].status,'rejected');assert.equal(results[2].status,'fulfilled');
 assert.match(source,/createLogicsTab\(\{request:/);
});
