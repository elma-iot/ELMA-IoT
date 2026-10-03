export const EFFECTS=['solid','blink','breathe','chase','rainbow','color-wipe','theater-chase','theater-chase-rainbow','colorloop','scan'];
export function effectPixel(effect,i,count,elapsed,speed,brightness,red,green,blue){
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
export function animateArray(preview,options,defaults){
 let elapsed=0,last=performance.now(),raf=0,stopped=false;
 function frame(now){if(stopped)return;if(!preview.isConnected){raf=requestAnimationFrame(frame);return;}if(!document.hidden&&now-last>=25){elapsed+=Math.min(now-last,25);last=now;const o=options(),d=defaults();preview.querySelectorAll('circle').forEach((pixel,i)=>pixel.setAttribute('fill',`rgb(${effectPixel(d.effect,i,o.count,Math.floor(elapsed),d.effectSpeed,d.brightness,d.red,d.green,d.blue).join(',')})`));}raf=requestAnimationFrame(frame);}
 raf=requestAnimationFrame(frame);return ()=>{stopped=true;cancelAnimationFrame(raf);};
}
