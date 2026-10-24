import assert from 'node:assert/strict';
import {requestLogics} from '../web/modules/logic-http.js';
const sleep=async()=>{};
const json=value=>new Response(JSON.stringify(value),{status:200});
let calls=[];
let result=await requestLogics('/api/logics',{group:{id:'g',mode:'playing'}},{sleep,fetchImpl:async(url,options)=>{calls.push([url,options]);return calls.length===1?new Response('Memory busy; retry shortly.',{status:503}):json({mode:'playing',groups:[{id:'g',mode:'playing'}]});}});
assert.equal(result.groups[0].mode,'playing');assert.equal(calls.filter(([,o])=>o.method==='POST').length,1);assert.match(calls[1][0],/live=1/);
calls=[];
const graph={nodes:[{id:'n',ports:[{}],binding:{x:1},parameters:{value:1}}],connections:[],devices:[{}]};
result=await requestLogics('/api/logics',{graph,mode:'playing'},{deduplicate:true,sleep,fetchImpl:async(url,options)=>{calls.push(options.body);return calls.length===1?new Response('Memory busy',{status:503}):json({saved:true,mode:'playing'});}});
assert.equal(result.saved,true);assert.equal(calls[0],calls[1]);let sent=JSON.parse(calls[0]);assert.ok(sent.request.client);assert.ok(sent.request.sequence>0);assert.equal(sent.graph.devices,undefined);assert.equal(sent.graph.nodes[0].ports,undefined);assert.equal(graph.nodes[0].ports.length,1);
for(const payload of [{graph,mode:'playing'},{test:{nodeId:'n',action:'toggle'}}]){
 let count=0;await assert.rejects(requestLogics('/api/logics',payload,{sleep,fetchImpl:async()=>{count++;return new Response('Memory busy; retry shortly.',{status:503});}}),/Memory busy/);assert.equal(count,1);
}
let attempts=0;await assert.rejects(requestLogics('/api/logics',null,{sleep,fetchImpl:async()=>{attempts++;return new Response('Memory busy',{status:503});}}),/Memory busy/);assert.equal(attempts,3);
console.log('PASS applied controls reconcile without replay; deduplicated graph saves retry byte-identically; legacy saves/tests never retry; busy errors remain readable');
let releaseFirst;const order=[];
const fetchImpl=async(url,options)=>{let body=JSON.parse(options.body);order.push(body.group.mode);if(order.length===1)await new Promise(resolve=>{releaseFirst=resolve;});return json({mode:'playing',groups:[{id:'g',mode:body.group.mode}]});};
const first=requestLogics('/api/logics',{group:{id:'g',mode:'playing'}},{fetchImpl});
const second=requestLogics('/api/logics',{group:{id:'g',mode:'stopped'}},{fetchImpl});
await new Promise(resolve=>setImmediate(resolve));assert.deepEqual(order,['playing']);releaseFirst();await Promise.all([first,second]);assert.deepEqual(order,['playing','stopped']);
console.log('PASS mutations serialize across saves and controls; no overlapping requests or stale control responses');
let finishRead;
const stale=requestLogics('/api/logics?live=1',null,{fetchImpl:async()=>{await new Promise(resolve=>finishRead=resolve);return json({groups:[{id:'g',mode:'stopped'}]});}});
const staleCheck=assert.rejects(stale,error=>error.stale===true);
await new Promise(resolve=>setImmediate(resolve));
await requestLogics('/api/logics',{group:{id:'g',mode:'playing'}},{fetchImpl:async()=>json({mode:'playing',groups:[{id:'g',mode:'playing'}]})});
finishRead();await staleCheck;
console.log('PASS an older in-flight poll cannot overwrite a newer command state');
