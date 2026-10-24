import {compactLogicRequest} from './logic-graph-model.js';

// State commands are idempotent, but actions such as Test/Toggle are not.
// A busy response can arrive after a state command was already applied.
let client=globalThis.crypto?.randomUUID?.()||`${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`,sequence=0;
let mutationQueue=Promise.resolve();
let revision=0;
export function requestLogics(url,payload,options={}){
 if(!payload){
  const at=revision;
  return mutationQueue.then(()=>performRequest(url,null,options)).then(result=>{
   if(at!==revision){const error=Error('A newer Logics command replaced this status reply');error.stale=true;throw error;}
   return result;
  });
 }
 revision++;
 const frozen=JSON.parse(JSON.stringify(compactLogicRequest(payload)));
 const next=mutationQueue.then(()=>performRequest(url,frozen,options));
 mutationQueue=next.then(()=>{},()=>{});return next;
}
async function performRequest(url,payload,{signal,deduplicate=false,fetchImpl=fetch,sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms))}={}){
 const stateCommand=payload&&!payload.graph&&!payload.test&&(payload.group||['playing','paused','stopped'].includes(payload.mode));
 const canRetry=!payload||!!stateCommand||!!(deduplicate&&payload.graph);
 if(payload&&deduplicate&&++sequence>0xffffffff){sequence=1;client=`${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;}
 const command=payload?{...compactLogicRequest(payload),...(deduplicate?{request:{client,sequence}}:{})}:null;
 const encoded=command?JSON.stringify(command):null;
 async function call(target,body){
  const response=await fetchImpl(target,{cache:'no-store',signal,...(body?{method:'POST',headers:{'Content-Type':'application/json'},body}:{})});
  const text=await response.text();let result;
  try{result=JSON.parse(text);}catch{result={error:text.trim().slice(0,320)||`Logics request failed (${response.status})`};}
  if(!response.ok||result.error){const error=Error(result.error||`Logics request failed (${response.status})`);error.status=response.status;throw error;}
  return result;
 }
 for(let attempt=0;attempt<3;attempt++){
  try{return await call(url,encoded);}catch(error){
   if(error.status!==503||!canRetry||attempt===2)throw error;
   await sleep(250*(attempt+1));
   if(stateCommand){
    try{
     const live=await call(url.split('?')[0]+'?live=1',null);
     const applied=payload.group?(live.groups||[]).some(g=>g.id===payload.group.id&&g.mode===payload.group.mode)&&(payload.group.mode!=='playing'||live.mode==='playing'):live.mode===payload.mode;
     if(applied)return live;
    }catch(reconcileError){if(reconcileError.status!==503)throw reconcileError;}
   }
  }
 }
}
