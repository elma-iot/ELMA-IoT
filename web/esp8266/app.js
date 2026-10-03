import {animateArray} from '../modules/led-effects.js';
import {arrayOptions,arraySvg,arrayDefaults,arrayControls} from '../modules/led-array.js';
import {createLogicsTab} from '../modules/logic-editor.js';

const logic=createLogicsTab();

const $=s=>document.querySelector(s);

const request=async(path,data)=>{const r=await fetch(path,{cache:'no-store',signal:AbortSignal.timeout(12000),...(data?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)}:{})});const text=await r.text();let result;try{result=text?JSON.parse(text):{};}catch{throw Error(r.ok?'Device returned incomplete data; retry shortly':`Device request failed (${r.status})`);}if(!r.ok)throw Error(result.error||`Device busy or unavailable (${r.status}); retry shortly`);if(!text)throw Error('Device returned an empty response; retry shortly');return result;};

function showError(message){$('#status').textContent=message;$('#status').hidden=!message;}

const tabs=[...document.querySelectorAll('[data-tab]')];

function selectTab(b){for(const p of document.querySelectorAll('.tab-panel'))p.classList.toggle('active',p.id==='tab-'+b.dataset.tab);for(const tab of tabs){tab.setAttribute('aria-selected',String(tab===b));tab.tabIndex=tab===b?0:-1;}if(b.dataset.tab==='logics')logic.load();}

for(const b of tabs){b.onclick=()=>selectTab(b);b.onkeydown=e=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(e.key))return;e.preventDefault();const visible=tabs.filter(t=>!t.hidden),i=visible.indexOf(b);const next=visible[e.key==='Home'?0:e.key==='End'?visible.length-1:(i+(e.key==='ArrowRight'?1:-1)+visible.length)%visible.length];next.focus();selectTab(next);};}

selectTab(tabs[0]);

const menu=$('#headerActionsMenu'),menuButton=$('#headerActionsButton');

function closeMenu(){menu.dataset.open='false';menu.setAttribute('aria-hidden','true');menuButton.setAttribute('aria-expanded','false');}

menuButton.onclick=()=>{const open=menu.dataset.open!=='true';menu.dataset.open=String(open);menu.setAttribute('aria-hidden',String(!open));menuButton.setAttribute('aria-expanded',String(open));};

document.addEventListener('click',e=>{if(!e.target.closest('.hero-actions'))closeMenu();});document.addEventListener('keydown',e=>{if(e.key==='Escape')closeMenu();});

$('#headerRefreshButton').onclick=()=>location.reload();

const form=$('#settings'),wifiForm=$('#wifi-settings'),deviceForm=$('#device-settings');

let saving=false;
async function save(data){saving=true;try{const accepted=await request('/api/settings',data);if(accepted.pending){$('#message').textContent='Applying configuration…';let complete=false;for(let attempt=0;attempt<30;attempt++){await new Promise(resolve=>setTimeout(resolve,500));const result=await request('/api/config-operation');if(result.operation!==accepted.operation)throw Error('Configuration operation changed; reload to verify saved values');if(!result.pending){if(result.error)throw Error(result.error);complete=true;break;}}if(!complete)throw Error('Configuration was not confirmed; reload to verify saved values');}$('#message').textContent='Saved';}finally{saving=false;}}

let configurationReady=false;
function loadConfiguration(){return Promise.resolve().then(async()=>[await request('/api/board'),await request('/api/settings')]).then(([board,s])=>{
 document.querySelectorAll('[data-led-array-preview]').forEach(p=>p.remove());

 $('#board-name').textContent=board.name||board.id;
 const parse=v=>typeof v==='string'?JSON.parse(v):v||{};
 const profiles=parse(s.ui?.peripheralProfiles),bindings=parse(s.ui?.peripheralHelperBindings);
 for(const [i,p] of (profiles.controls||[]).entries())if(p==='ws2812-neopixel-led-strip'){
  const panel=document.createElement('section');panel.dataset.ledArrayPreview='';panel.className='array-configuration';
  const limit=board.chip==='esp32c2'?32:['esp8266','esp8285'].includes(board.chip)?128:256;
  const values={...bindings[`control:${i}`]},controls=arrayControls(values,limit,next=>{bindings[`control:${i}`]=next;renderDiagram(next);});
  const pinLabel=document.createElement('label');pinLabel.textContent='DI / Data GPIO';const pin=document.createElement('select');
  for(const gpio of board.pins)pin.append(new Option(`GPIO${gpio}`,gpio));pin.value=values.DIN;pin.onchange=()=>{values.DIN=pin.value;bindings[`control:${i}`]={...values};renderDiagram(values);};pinLabel.append(pin);
  const saveButton=document.createElement('button');saveButton.textContent='Save and apply LED array';saveButton.type='button';saveButton.onclick=async()=>{saveButton.disabled=true;try{await save({ui:{peripheralHelperBindings:bindings}});showError('');}catch(e){showError(e.message);}finally{saveButton.disabled=false;}};
  const left=document.createElement('div');left.append(pinLabel,controls,saveButton);
  const diagram=document.createElement('div');diagram.className='array-wiring';
  function renderDiagram(next){
   const options=arrayOptions(next,limit),positions=parse(s.ui?.peripheralDiagramPositions),boardKey='BOARD_'+board.id.toUpperCase().replaceAll('-','_');
   const savedBoard=positions[boardKey]||{},savedArray=positions[`control-${i}`]||{};
   const bx=Number(savedBoard.x??440),by=Number(savedBoard.y??170),ax=Number(savedArray.x??30),ay=Number(savedArray.y??40);
   const width=190,height=236,aw=154,ah=140,minX=Math.min(bx,ax)-80,minY=Math.min(by,ay)-50;
   const svg=arraySvg(options).replace('<svg ',`<svg x="${ax}" y="${ay}" width="${aw}" height="${ah}" `),gpio=Number(next.DIN);
   const contact=key=>{const point=board.contacts?.[key]?.[0];return point?[bx+point[0]*width,by+point[1]*height]:null;};
   let wires='';for(const [key,label,color,row] of [[String(gpio),`DI → GPIO${gpio}`,'#0cba90',25],['GND','GND','#d9ccac',60],['5V','5V','#fa5063',95]]){
    const end=contact(key);if(!end)continue;const x=ax+aw,y=ay+row,mid=(x+end[0])/2;wires+=`<path d="M${x} ${y} H${mid} V${end[1]} H${end[0]}" fill="none" stroke="${color}" stroke-width="3"/><circle cx="${end[0]}" cy="${end[1]}" r="4" fill="${color}"/><text x="${x+8}" y="${y-7}" fill="currentColor" font-size="12">${label}</text>`;
   }
   diagram.innerHTML=`<svg viewBox="${minX} ${minY} ${Math.max(bx+width,ax+aw)-minX+80} ${Math.max(by+height,ay+ah)-minY+70}" role="img" aria-label="LED array wiring"><image href="/board.svg" x="${bx}" y="${by}" width="${width}" height="${height}" preserveAspectRatio="none"/>${wires}<g data-array-pixels>${svg}</g><text x="${ax}" y="${ay+ah+20}" fill="currentColor" font-size="12">DO → next array only</text></svg>`;
  }
  renderDiagram(values);const stop=animateArray({get isConnected(){return diagram.isConnected;},querySelectorAll:selector=>diagram.querySelectorAll(`[data-array-pixels] ${selector}`)},()=>arrayOptions(values,limit),()=>arrayDefaults(values));
  const watch=new MutationObserver(()=>{if(!panel.isConnected){stop();watch.disconnect();}});queueMicrotask(()=>watch.observe(document.body,{childList:true,subtree:true}));
  const hint=document.createElement('p');hint.textContent='Animated preview uses configured defaults. A later Logics action can override them. Save applies these values to the active array; the wiring diagram uses the saved Windows positions and selected board pin contacts.';
  panel.append(left,diagram,hint);form.after(panel);
 }

 const image=$('[data-board-image]');let imageRetries=0;
 image.onerror=()=>{if(imageRetries++<2)setTimeout(()=>{image.src='/board.svg?retry='+imageRetries;},1000);};
 image.src='/board.svg';

 $('#deviceTitle').textContent=s.device.friendlyName||s.device.deviceName||'ELMA IoT';deviceForm.elements.name.value=s.device.friendlyName||s.device.deviceName||'ELMA IoT';

 for(const key of ['pin','green','blue']){form.elements[key].replaceChildren();for(const pin of [-1,...board.pins])form.elements[key].append(new Option(pin===-1?'Disabled':'GPIO'+pin,pin));}

 form.elements.green.value=s.device.statusLedGreenPin??-1;form.elements.blue.value=s.device.statusLedBluePin??-1;

 wifiForm.elements.ssid.value=s.wifi?.ssid||'';form.elements.pin.value=s.device.statusLedPin;form.elements.kind.value=s.device.statusLedType;form.elements.activeLow.checked=s.device.statusLedActiveLow;form.elements.kind.dispatchEvent(new Event('change'));



 configurationReady=true;
}).catch(e=>{showError(e.message);setTimeout(loadConfiguration,3000);});}
loadConfiguration();

form.elements.kind.onchange=()=>{for(const field of document.querySelectorAll('[data-rgb]'))field.hidden=form.elements.kind.value!=='rgb';};

form.onsubmit=async e=>{e.preventDefault();try{await save({device:{statusLedPin:Number(form.elements.pin.value),statusLedType:form.elements.kind.value,statusLedActiveLow:form.elements.activeLow.checked,statusLedGreenPin:Number(form.elements.green.value),statusLedBluePin:Number(form.elements.blue.value)}});}catch(e){showError(e.message);}};

wifiForm.onsubmit=async e=>{e.preventDefault();const wifi={ssid:wifiForm.elements.ssid.value};if(wifiForm.elements.password.value)wifi.password=wifiForm.elements.password.value;try{await save({wifi});wifiForm.elements.password.value='';}catch(e){showError(e.message);}};

deviceForm.onsubmit=async e=>{e.preventDefault();try{await save({device:{friendlyName:deviceForm.elements.name.value}});$('#deviceTitle').textContent=deviceForm.elements.name.value;}catch(e){showError(e.message);}};

for(const message of document.querySelectorAll('[data-unavailable]'))message.textContent='This feature is unavailable in this firmware profile.';

const security=$('[data-tab="security"]');for(const key of ['aria-label','title','data-tooltip'])security.setAttribute(key,'Security');

function renderStatus(s){
 const plotButton=$('[data-tab="plots"]');plotButton.hidden=!s.plotsAvailable;
 if(plotButton.hidden&&$('#tab-plots').classList.contains('active'))selectTab(tabs[0]);

 $('#deviceTitle').textContent=s.device?.friendlyName||s.device?.deviceName||'ELMA IoT';$('#heroFirmwareVersion').textContent=s.firmware?.version||'-';

 const rssi=s.wifi.rssi,level=!s.wifi.connected?0:rssi>=-55?4:rssi>=-67?3:rssi>=-80?2:1;

 $('#wifi-quality').textContent=!s.wifi.connected?'Connecting':level>=3?'Good':level===2?'Fair':'Weak';

 $('#wifi-address').textContent=s.wifi.ip+' - '+rssi+' dBm';document.querySelectorAll('.compact-signal i').forEach((bar,i)=>bar.classList.toggle('on',i<level));

 const hardware=$('#hardware');hardware.replaceChildren();for(const [label,value] of [['Chip',s.hardware.chipModel],['Flash size',s.hardware.flashSize+' bytes'],['Free heap RAM',s.hardware.freeHeap+' bytes'],['Largest free block',s.hardware.largestFreeBlock+' bytes'],['Uptime',(s.system?.uptime??0)+' s']]){const dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=label;dd.textContent=value;hardware.append(dt,dd);}showError(s.error||'');

}

let busy=false,since=0,boot=0;const plots=new Map();

setInterval(async()=>{if(busy||saving||document.hidden||!configurationReady)return;busy=true;try{const s=await request('/api/status');renderStatus(s);if(document.querySelector('#tab-plots').classList.contains('active')){const p=await request('/api/plots?since='+since+'&boot='+boot);if(boot&&boot!==p.boot){plots.clear();document.querySelector('#plots').replaceChildren();}boot=p.boot;since=p.sequence;for(const v of p.samples){const key=v.plot+' / '+v.series;let plot=plots.get(key);if(!plot){const box=document.createElement('div'),title=document.createElement('h3'),canvas=document.createElement('canvas');title.textContent=key+' ('+v.unit+')';canvas.className='plot';canvas.width=800;canvas.height=180;box.append(title,canvas);document.querySelector('#plots').append(box);plot={canvas,values:[]};plots.set(key,plot);}plot.values.push(v.value);if(plot.values.length>300)plot.values.shift();const c=plot.canvas.getContext('2d'),lo=Math.min(...plot.values),hi=Math.max(...plot.values);c.clearRect(0,0,800,180);c.strokeStyle='#f2a313';c.beginPath();plot.values.forEach((y,i)=>{const x=i*800/Math.max(1,plot.values.length-1),v=165-(y-lo)*150/(hi-lo||1);i?c.lineTo(x,v):c.moveTo(x,v);});c.stroke();}}}catch(e){showError(e.message);}finally{busy=false;}},1000);

