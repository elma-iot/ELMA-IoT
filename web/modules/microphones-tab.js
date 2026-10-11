import {attachMedia} from './spk-media.js';
export function createMicrophonesTab({request}){
 const tab=document.querySelector('[data-tab="microphones"]'),panel=document.getElementById('tab-microphones');
 const enabled=panel.querySelector('[data-mic-enabled]'),gain=panel.querySelector('[data-mic-gain]'),notice=panel.querySelector('[data-mic-status]');
 const format=panel.querySelector('[data-mic-format]');
 const left=panel.querySelector('[data-mic-left]'),right=panel.querySelector('[data-mic-right]');
 const media=attachMedia({request,panel,notice});
 let active=false,available=false,busy=false,saving=false,timer;
 async function poll(){
  clearTimeout(timer);if(!active||!available||document.hidden||busy)return;busy=true;
  try{const data=await request('/api/microphones');if(!saving){enabled.checked=data.enabled;gain.value=data.gain;format.value=data.captureFormat;format.closest('label').hidden=data.clockPin!==39||data.dataPin!==38;}
   left.value=data.left*100;right.value=data.right*100;
   notice.textContent=!data.enabled?'Microphones disabled':data.ready?`${data.channels>1?'Stereo':'Mono'} ${data.captureFormat==='pdm'?'PDM':'I2S'} · ${data.sampleRate/1000} kHz`:'Microphones unavailable — check device logs';
  }catch(error){notice.textContent=error.message;}finally{busy=false;if(active&&available)timer=setTimeout(poll,250);}
 }
 async function save(){saving=true;enabled.disabled=gain.disabled=format.disabled=true;
  try{await request('/api/microphones',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({enabled:Number(enabled.checked),gain:gain.value,captureFormat:format.value}).toString()});}
  catch(error){notice.textContent=error.message;}finally{saving=false;enabled.disabled=gain.disabled=format.disabled=false;poll();}
 }
 enabled.addEventListener('change',save);gain.addEventListener('change',save);format.addEventListener('change',save);
 document.addEventListener('visibilitychange',()=>{if(!document.hidden)poll();});
 return {setActive(value){media.setActive(value);active=value;clearTimeout(timer);if(value)poll();},update(status){media.update(status);available=!!status?.firmware?.microphones&&!status?.system?.webUiLocked;tab.hidden=!available;if(!available)clearTimeout(timer);else if(active&&!busy)poll();}};
}
