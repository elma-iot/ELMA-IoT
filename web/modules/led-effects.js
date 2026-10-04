export const EFFECTS=['solid','blink','breathe','chase','rainbow','color-wipe','theater-chase','theater-chase-rainbow','colorloop','scan'];
export function effectPixel(effect,i,count,elapsed,speed,brightness,red,green,blue,audio=null){
 if(effect.startsWith('stream-')||effect.startsWith('mic-')){
  const kind=effect.split('-')[1],level=audio?.audioLevel||0;
  const gain=kind==='spectrum'?(audio?.audioBands?.[Math.min(7,Math.floor(i*8/count))]||0):kind==='vu'?(i/count<level?1:0):level;
  brightness=Math.round(brightness*gain);effect=kind==='spectrum'?'rainbow':'solid';
 }
 const stepMs=300-Math.floor(speed*275/100),step=Math.floor(elapsed/stepMs),cycle=12000-speed*100;
 let gain=brightness/100;
 if(effect==='blink')gain*=Math.floor(elapsed/(1000-speed*8))%2===0;
 else if(effect==='breathe')gain*=.5-.5*Math.cos(2*Math.PI*(elapsed%cycle)/cycle);
 else if(effect==='chase')gain*=i===step%count;
 else if(effect==='color-wipe'){const phase=step%(count*2);gain*=phase<count?i<=phase:i>phase-count;}
 else if(effect==='theater-chase'||effect==='theater-chase-rainbow')gain*=(i+step)%3===0;
 else if(effect==='scan'){const length=count>1?2*(count-1):1;let pos=step%length;if(pos>=count)pos=length-pos;gain*=i===pos;}
 if(['rainbow','theater-chase-rainbow','colorloop'].includes(effect)){
  const hue=(Math.floor((elapsed%cycle)*1536/cycle)+(effect==='colorloop'?0:Math.floor(i*1536/count)))%1536,x=hue%256;
  [red,green,blue]=[[255,x,0],[255-x,255,0],[0,255,x],[0,255-x,255],[x,0,255],[255,0,255-x]][Math.floor(hue/256)];
 }
 return [red,green,blue].map(v=>Math.floor(v*gain+.5));
}
// LED PWM values are linear intensity; CSS rgb() is sRGB encoded.
// Transfer function: https://www.w3.org/Graphics/Color/srgb
export function displayPixel(rgb){
 return rgb.map(value=>{const x=Math.min(255,Math.max(0,value))/255;return Math.round(255*(x<=.0031308?12.92*x:1.055*x**(1/2.4)-.055));});
}
// A runtime sample is optional for offline designer previews.
export function runtimeFrame(sample,now){
 if(!sample)return null;
 const age=Math.max(0,now-sample.received);
 const delta=sample.running?Math.min(age,3000)+(sample.age||0):0;
 return {...sample,bootPhase:(sample.bootPhase||0)+delta,phase:(sample.phase+delta)>>>0,...(sample.segments?{segments:sample.segments.map(segment=>({...segment,phase:(segment.phase+delta)>>>0}))}:{})};
}
export function animateArray(preview,options,defaults,live){
 let raf=0,stopped=false;
 function frame(now){
  if(stopped)return;
  if(preview.isConnected&&!document.hidden){
   const o=options(),sample=live?runtimeFrame(live(),now):null,d=sample||defaults();
   const phase=sample?sample.phase:now,brightness=live?sample&&sample.on?sample.brightness:0:d.brightness;
   let offset=0;const segments=o.segments?.length?o.segments:[o];
   const states=segments.map((segment,index)=>{const entry={...segment,offset};offset+=segment.count;return entry;});
   preview.querySelectorAll('circle').forEach((pixel,i)=>{
    if(sample?.bootIndicator){const level=sample.bootIndicator===2?51:Math.round(51*(.5-.5*Math.cos(2*Math.PI*(sample.bootPhase%2000)/2000)));pixel.setAttribute('fill',`rgb(${displayPixel(sample.bootIndicator===2?[0,level,0]:[0,0,level]).join(',')})`);return;}
    const index=states.findIndex(segment=>i>=segment.offset&&i<segment.offset+segment.count),segment=states[index]||states[0];
    const runtime=sample?.segments?.[index],settings=runtime||(sample?d:(segment.defaults||d));
    const level=live?sample&&sample.on?settings.brightness:0:settings.brightness;
    pixel.setAttribute('fill',`rgb(${displayPixel(effectPixel(settings.effect,i-segment.offset,runtime?.count||segment.count,Math.floor(runtime?.phase??phase),settings.effectSpeed,level,settings.red,settings.green,settings.blue,runtime||sample)).join(',')})`);
   });
  }
  raf=requestAnimationFrame(frame);
 }
 frame(performance.now());return ()=>{stopped=true;cancelAnimationFrame(raf);};
}
