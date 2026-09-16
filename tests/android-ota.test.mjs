import assert from 'node:assert/strict';
import {transferOta} from '../../Android/web/ota-transfer.js';
const image=new Uint8Array(20000);
async function test(mode){
 let offset=0,active=false,dropped=false,finished=false,cancelled=false,writes=0;
 const request=async(path,method,body)=>{
  if(path==='/api/status')return {firmware:{chipFamily:mode==='mismatch'?'esp32c3':'esp32'}};
  if(path.endsWith('/status'))return {upload:{active,offset,sessionId:'test'}};
  if(path.endsWith('/start')){active=true;return {ok:true};}
  if(path.includes('/chunk?')){writes++;assert.equal(Number(new URL('http://device'+path).searchParams.get('offset')),offset);offset+=body.length;if(mode==='resume'&&!dropped){dropped=true;throw Error('Disconnected after device wrote chunk');}return {upload:{offset}};}
  if(path.endsWith('/finish')){finished=true;return {ok:true};}
  if(path.endsWith('/cancel')){cancelled=true;return {ok:true};}
 };
 const operation=()=>transferOta({request,image,chip:'esp32',cancelled:()=>mode==='cancel'&&offset>0,progress:()=>{},sleep:async()=>{},sessionId:'test'});
 if(mode==='mismatch'){await assert.rejects(operation,/does not match/);assert.equal(writes,0);}
 else if(mode==='cancel'){await assert.rejects(operation,/cancelled/);assert.equal(cancelled,true);assert.equal(finished,false);}
 else{await operation();assert.equal(offset,image.length);assert.equal(finished,true);assert.equal(cancelled,false);}
}
for(const mode of ['normal','resume','cancel','mismatch'])await test(mode);
console.log('OTA normal transfer, lost-ack resume, cancellation and chip mismatch passed');
