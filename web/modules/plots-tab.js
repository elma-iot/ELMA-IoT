// Live data and history stay in browser RAM. Only Save Data writes external storage.
export function createPlotsTab({request,document=globalThis.document,timers=globalThis}) {
 const panel=document.getElementById('tab-plots'),tab=document.querySelector('[data-tab="plots"]'),host=panel.querySelector('[data-plots]'),status=panel.querySelector('[data-plot-status]');
 const syncTime=document.createElement('button');syncTime.type='button';syncTime.textContent='Set device time from browser';syncTime.hidden=true;status.after(syncTime);
 syncTime.onclick=async()=>{try{await request('/api/plots/time',{method:'POST',body:JSON.stringify({epoch:Date.now()})});await poll();}catch(error){status.textContent=error.message;}};
 const cards=new Map(),colors=['#22c55e','#38bdf8','#fb923c','#e879f9','#facc15','#f87171'];
 let active=false,busy=false,configBusy=false,config={plots:[],recordings:[]},cursor=0,boot=0;
 const localDate=t=>{const d=new Date(t);return new Date(t-d.getTimezoneOffset()*60000).toISOString().slice(0,19);};
 const element=(tag,text)=>{const e=document.createElement(tag);if(text)e.textContent=text;return e;};
 function card(name){
  if(cards.has(name))return cards.get(name);
  if(cards.size>=32)return null;
  const box=element('section');box.style.cssText='margin:16px 0;padding:12px;border:1px solid #536174;border-radius:10px';
  box.append(element('h3',name));const controls=element('div');controls.style.cssText='display:flex;flex-wrap:wrap;gap:8px;align-items:center';box.append(controls);
  const from=element('input'),to=element('input');for(const input of [from,to])input.type='datetime-local';from.step=to.step='1';from.value=localDate(Date.now()-3600000);to.value=localDate(Date.now());
  const fromLabel=element('label','From '),toLabel=element('label','To ');fromLabel.append(from);toLabel.append(to);controls.append(fromLabel,toLabel);
  const load=element('button','Show range'),live=element('button','Realtime');load.type=live.type='button';controls.append(load,live);
  const note=element('p');box.append(note);const scroll=element('div');scroll.style.cssText='overflow-x:auto;max-width:100%;border:1px solid #344254';box.append(scroll);
  const canvas=element('canvas');canvas.height=260;canvas.style.height='260px';scroll.append(canvas);host.append(box);
  const item={name,box,from,to,load,live,note,scroll,canvas,history:[],points:[],realtime:true,token:0,loaded:false};cards.set(name,item);
  load.onclick=()=>{item.realtime=false;loadHistory(item);};live.onclick=()=>{item.realtime=true;draw(item);};
  scroll.addEventListener('scroll',()=>{if(item.realtime&&scroll.scrollLeft+scroll.clientWidth<scroll.scrollWidth-20){item.realtime=false;item.note.textContent='Historical view — click Realtime to follow new samples';}});
  return item;
 }
 function draw(c){
  const from=Date.parse(c.from.value),to=Date.parse(c.to.value);
  const points=[...c.history,...c.points].filter(p=>Number.isFinite(p.epoch)&&Number.isFinite(p.value)&& (c.realtime || (p.epoch>=from&&p.epoch<=to)));
  const end=c.realtime?Math.max(Date.now(),...points.map(p=>p.epoch)):to,start=c.realtime?Math.min(end-60000,...points.map(p=>p.epoch)):from;
  if(!Number.isFinite(start)||!Number.isFinite(end)||end<=start)return;
  const width=Math.min(12000,Math.max(c.scroll.clientWidth||500,(end-start)/1000*8));c.canvas.width=width;c.canvas.style.width=width+'px';
  const ctx=c.canvas.getContext('2d');ctx.fillStyle='#19232f';ctx.fillRect(0,0,width,260);ctx.font='12px sans-serif';ctx.fillStyle='#dce6ef';
  if(!points.length){ctx.fillText(window.ElmaFirmwareI18n?.t('No samples in this range')||'No samples in this range',15,40);return;}
  let lo=Math.min(...points.map(p=>p.value)),hi=Math.max(...points.map(p=>p.value));const margin=Math.max((hi-lo)*.08,Math.abs(hi)*.01,1e-6);lo-=margin;hi+=margin;
  for(let i=0;i<5;i++){let y=35+i*45;ctx.strokeStyle='#344254';ctx.beginPath();ctx.moveTo(60,y);ctx.lineTo(width,y);ctx.stroke();ctx.fillStyle='#dce6ef';ctx.fillText((hi-(hi-lo)*i/4).toPrecision(4),2,y);}
  const series=[...new Set(points.map(p=>p.series))];series.forEach((name,i)=>{ctx.strokeStyle=ctx.fillStyle=colors[i%colors.length];const rows=points.filter(p=>p.series===name).sort((a,b)=>a.epoch-b.epoch);ctx.fillText(name+(rows[0].unit?' ('+rows[0].unit+')':''),70+i*160,18);ctx.beginPath();rows.forEach((p,j)=>{const x=60+(p.epoch-start)/(end-start)*(width-80),y=215-(p.value-lo)/(hi-lo)*180;j?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();});
  ctx.fillStyle='#dce6ef';for(let i=0;i<=4;i++)ctx.fillText(new Date(start+(end-start)*i/4).toLocaleString(),60+(width-230)*i/4,248);
  if(c.realtime)c.scroll.scrollLeft=c.scroll.scrollWidth;
 }
 async function loadHistory(c,keepLive=false){
  const from=Date.parse(c.from.value),to=Date.parse(c.to.value);if(!Number.isFinite(from)||!Number.isFinite(to)||from>to){c.note.textContent='Choose a valid date/time range';return;}
  const sources=config.recordings.filter(r=>r.plot===c.name).filter((r,i,a)=>a.findIndex(x=>x.path===r.path)===i);
  if(!sources.length){c.note.textContent='Realtime only. Add Save Data with this plot name to record on external storage.';return;}
  const token=++c.token;c.load.disabled=true;c.history=[];c.note.textContent='Loading recorded samples…';let count=0;
  try {
   for(const source of sources){let offset=0;
    for(let page=0;page<2000;page++){
     if(token!==c.token)return;
     let result;
     for(let retry=0;retry<150;retry++){
      result=await request(`/api/plots/history?id=${encodeURIComponent(source.id)}&offset=${offset}&from=${from}&to=${to}`);
      if(!result.pending)break;
      if(token!==c.token)return;
      await new Promise(resolve=>globalThis.setTimeout(resolve,100));
     }
     if(result.pending)throw Error('Storage history is busy or too slow; retry the range.');
     if(token!==c.token)return;
     for(const sample of result.samples||[])if(Number.isFinite(sample.epoch)&&Number.isFinite(sample.value)){c.history.push(sample);count++;}
     // Bound browser memory with an evenly reduced overview; raw data remains on card.
     if(c.history.length>8000)c.history=c.history.filter((_,i)=>i%2===0);
     c.note.textContent=`Read ${count} recorded samples${count>c.history.length?' (overview reduced)':''}`;
     if(result.eof)break;
     if(!Number.isFinite(result.next)||result.next<=offset)throw Error('Recording scan did not advance');offset=result.next;
     if(page===1999)throw Error('Range scan limit reached; select a smaller recording file/range');
    }
   }
   c.loaded=true;c.realtime=keepLive;draw(c);
  }catch(error){c.note.textContent=error.message;}
  finally{if(token===c.token)c.load.disabled=false;}
 }
 async function discover(){
  if(configBusy||document.hidden)return;configBusy=true;
  try{config=await request('/api/plots/config');const visible=!!config.plots?.length;tab.hidden=!visible;tab.disabled=!visible;panel.hidden=!visible;if(!visible&&panel.classList.contains('active'))document.querySelector('.tab-button:not([hidden]):not([disabled])')?.click();
   const names=new Set((config.plots||[]).map(p=>p.plot));for(const [name,c] of cards)if(!names.has(name)){c.token++;c.box.remove();cards.delete(name);}
   for(const name of names){const c=card(name);if(!c)continue;const recorded=config.recordings.some(r=>r.plot===name);c.load.disabled=!recorded;c.from.disabled=c.to.disabled=!recorded;if(active&&recorded&&!c.loaded)await loadHistory(c,true);}
   if(!visible)status.textContent='Add a Plot node to device Logics to enable this tab.';
  }catch(error){if(active)status.textContent=error.message;}finally{configBusy=false;}
 }
 async function poll(){
  if(!active||document.hidden||busy)return;busy=true;
  try{const data=await request(`/api/plots?after=${cursor}&boot=${boot}`);if(boot&&boot!==data.boot)for(const c of cards.values())c.points=[];boot=data.boot;cursor=data.cursor;
   for(const sample of data.samples||[]){const c=cards.get(sample.plot);if(!c||!Number.isFinite(sample.value))continue;c.points.push({...sample,epoch:sample.epoch||Date.now()});if(c.points.length>2000)c.points.shift();}
   for(const c of cards.values())if(c.realtime)draw(c);
   syncTime.hidden=data.storage?.clockSynced!==false;
   status.textContent=data.storage?.error|| (data.storage?.clockSynced===false?'Clock not synchronized — recording waits for NTP or browser time':'')|| (data.storage?.available?'Live plotting · external recording storage available':'Live plotting · no external recording storage mounted');
  }catch(error){status.textContent=error.message;}finally{busy=false;}
 }
 const discoveryTimer=timers.setInterval(discover,15000),liveTimer=timers.setInterval(poll,300);discover();
 return {setActive(value){active=value;if(value){discover();poll();}else for(const c of cards.values())c.token++;},destroy(){timers.clearInterval(discoveryTimer);timers.clearInterval(liveTimer);for(const c of cards.values())c.token++;},cards};
}
