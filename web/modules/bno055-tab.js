export function createBno055Tab({request}) {
 const tab=document.querySelector('[data-tab="bno055"]'),panel=document.getElementById('tab-bno055');
 const canvas=panel.querySelector('canvas'),context=canvas.getContext('2d'),notice=panel.querySelector('[data-notice]');
 const enabled=panel.querySelector('[data-enabled]'),mode=panel.querySelector('[data-mode]');
 let active=false,available=false,busy=false,saving=false,timer;
 function draw(data) {
  context.clearRect(0,0,480,300);context.fillStyle='#aebbc9';context.strokeStyle='#aebbc9';context.font='16px sans-serif';
  context.beginPath();context.arc(100,110,72,0,Math.PI*2);context.stroke();context.fillText('N',94,29);
  if(!data?.ready){context.fillText('No live readings',205,110);return;}
  if(data.compass){const a=data.heading*Math.PI/180;context.strokeStyle='#f59e0b';context.lineWidth=4;context.beginPath();context.moveTo(100,110);context.lineTo(100+Math.sin(a)*64,110-Math.cos(a)*64);context.stroke();context.lineWidth=1;context.fillText(`${data.heading.toFixed(1)}° magnetic`,30,208);}
  else context.fillText('IMU mode',55,208);
  for(const [row,key,unit] of [[0,'accel','m/s²'],[1,'gyro','°/s'],[2,'mag','µT']]){
   context.fillText(`${key.toUpperCase()} (${unit})`,210,45+row*75);
   (data[key]||[]).forEach((v,i)=>{context.fillStyle=['#ff7777','#7bdd99','#78b9ff'][i];context.fillText(`${'XYZ'[i]} ${v.toFixed(2)}`,210+i*88,72+row*75);const x=246+i*88,y=83+row*75,width=Math.max(-32,Math.min(32,v/[20,250,100][row]*32));context.fillRect(x+Math.min(width,0),y,Math.max(1,Math.abs(width)),6);context.fillRect(x,y-2,1,10);});context.fillStyle='#aebbc9';
  }
  context.fillText(`Roll ${data.roll.toFixed(1)}° · Pitch ${data.pitch.toFixed(1)}°`,30,268);
 }
 async function poll(){clearTimeout(timer);if(!active||!available||document.hidden||busy||saving)return;busy=true;
  try{const d=await request('/api/bno055');if(!saving){enabled.checked=d.enabled;mode.value=d.mode;}draw(d.ready&&d.ageMs<2000?d:null);
   notice.textContent=d.error||(!d.enabled?'Sampling paused':d.ready?`Calibration 0–3: system ${d.calibration.system}, gyro ${d.calibration.gyro}, accel ${d.calibration.accel}, compass ${d.calibration.mag}`:'Waiting for sensor');
  }catch(e){draw(null);notice.textContent=e.message;}finally{busy=false;if(active&&available)timer=setTimeout(poll,250);}}
 async function save(extra={}){if(saving)return;saving=true;enabled.disabled=mode.disabled=true;
  try{await request('/api/bno055',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:enabled.checked,mode:mode.value,...extra})});}
  catch(e){notice.textContent=e.message;}finally{saving=false;enabled.disabled=mode.disabled=false;poll();}}
 enabled.onchange=()=>save();mode.onchange=()=>save();panel.querySelector('button').onclick=()=>save({reinitialize:true});
 document.addEventListener('visibilitychange',()=>{if(!document.hidden)poll();});draw(null);
 return {setActive(value){active=value;clearTimeout(timer);if(value)poll();},update(settings,locked){let profiles=settings?.ui?.peripheralProfiles||{};if(typeof profiles==='string'){try{profiles=JSON.parse(profiles);}catch{profiles={};}}available=profiles.sensors?.includes('bno055')&&!locked;tab.hidden=panel.hidden=!available;if(!available){clearTimeout(timer);draw(null);}else if(active&&!busy)poll();}};
}
