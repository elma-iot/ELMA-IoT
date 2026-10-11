import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const source=readFileSync(new URL('../web/app.js',import.meta.url),'utf8');
const start=source.indexOf('async function request(path, options = {})');
const implementation=source.slice(start,source.indexOf('\n}',start)+2);
function client(responses){let calls=0;const messages=[];const request=new Function('fetch','isApiPath','markDeviceReachability','setStorageStatus',implementation+';return request')(
 async()=>{calls++;const response=responses.shift();if(response instanceof Error)throw response;return{ok:response===200,status:response,text:async()=>JSON.stringify({error:'occupied'}),headers:{get:()=> 'application/json'},json:async()=>({ok:true})};},()=>true,()=>{},message=>messages.push(message));return{request,messages,count:()=>calls};}
let c=client([423,423,200]);assert.deepEqual(await c.request('/api/storage/delete?target=sd',{method:'POST',body:'{"path":"/web/test.txt"}'}),{ok:true});assert.equal(c.count(),3);assert.equal(c.messages.length,2);assert(c.messages.every(s=>s.includes('Waiting for storage')));
c=client([400]);await assert.rejects(c.request('/api/storage/delete'),/occupied/);assert.equal(c.count(),1);
c=client([423]);await assert.rejects(c.request('/api/storage/delete',{storageWaitingSince:Date.now()-121000}),/Finish it/);assert.equal(c.count(),1);
c=client([423]);await assert.rejects(c.request('/api/settings'),/occupied/);assert.equal(c.count(),1);
console.log('PASS automatic delete retries, waiting status, bounded wait, permanent error handling and other API isolation');
