import {ONBOARD_BOARDS} from './onboard-boards.js';
export function createRs485Tab({request}) {
 const tab=document.querySelector('[data-tab="rs485"]'),panel=document.getElementById('tab-rs485');
 const form=panel.querySelector('form'),notice=panel.querySelector('[data-notice]'),result=panel.querySelector('[data-values]'),monitor=panel.querySelector('[data-monitor]'),pause=panel.querySelector('[data-pause]');
 let active=false,available=false,busy=false,dirty=false,timer,lastSequence=0,lines=[];
 form.addEventListener('input',()=>dirty=true);
 async function poll(){clearTimeout(timer);if(!active||!available||busy||document.hidden)return;busy=true;
  try{const d=await request('/api/rs485');notice.textContent=d.message||'Busy';result.textContent=(d.values||[]).map((v,i)=>`${d.address+i}: ${v}`).join('\n');form.querySelectorAll('button').forEach(b=>b.disabled=!!d.busy);
   if(!dirty&&!form.contains(document.activeElement))for(const key of ['baud','parity','stops','unit','address','count','function'])if(d[key]!==undefined)form.elements[key].value=d[key];
   if(!pause.checked){if(d.frames?.length&&d.frames.at(-1).sequence<lastSequence){lastSequence=0;lines=[];}
    for(const f of d.frames||[])if(f.sequence>lastSequence){lines.push(`${(f.atMs/1000).toFixed(3)}s ${f.direction} ${f.info}\n${f.hex}`);lastSequence=f.sequence;}
    lines=lines.slice(-200);const text=lines.join('\n');if(monitor.textContent!==text){monitor.textContent=text||'Listening for frames...';monitor.scrollTop=monitor.scrollHeight;}}
  }catch(e){notice.textContent=e.message;}finally{busy=false;if(active&&available)timer=setTimeout(poll,500);}}
 form.onsubmit=async e=>{e.preventDefault();form.querySelectorAll('button').forEach(b=>b.disabled=true);const args=Object.fromEntries(new FormData(form));for(const key of ['baud','unit','address','count','function','stops'])args[key]=Number(args[key]);args.action=e.submitter?.value||'read';
  try{await request('/api/rs485',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(args)});notice.textContent='Queued';dirty=false;}
  catch(e){notice.textContent=e.message;form.querySelectorAll('button').forEach(b=>b.disabled=false);}poll();};
 panel.querySelector('[data-clear]').onclick=()=>{lines=[];monitor.textContent='Log cleared. Listening...';};
 document.addEventListener('visibilitychange',()=>{if(!document.hidden)poll();});
 return {setActive(value){active=value;clearTimeout(timer);if(active)poll();},update(settings,locked){available=!!ONBOARD_BOARDS[settings?.ui?.gpioBoardSelection]?.rs485&&!locked;tab.hidden=panel.hidden=!available;if(!available)clearTimeout(timer);else if(active&&!busy)poll();}};
}
