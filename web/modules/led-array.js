import {animateArray} from './led-effects.js';
export const effectLabels={"solid": "Solid", "blink": "Blink", "breathe": "Breathe", "chase": "Chase", "rainbow": "Rainbow", "color-wipe": "Color wipe", "theater-chase": "Theater chase", "theater-chase-rainbow": "Theater chase rainbow", "colorloop": "Color loop", "scan": "Scan"};
export function arrayDefaults(values={}){
 const effect=values.LED_DEFAULT_EFFECT??'solid';
 if(!['solid','blink','breathe','chase','rainbow','color-wipe','theater-chase','theater-chase-rainbow','colorloop','scan'].includes(effect))throw Error('Unknown default LED effect');
 const result={effect};
 for(const [key,fallback,max] of [['brightness',20,100],['red',255,255],['green',128,255],['blue',0,255],['effectSpeed',50,100]]){
  const raw=values['LED_DEFAULT_'+key.toUpperCase()]??fallback;
  if(typeof raw!=='number'||!Number.isFinite(raw)||raw<0||raw>max)throw Error('LED default value out of range');
  result[key]=Math.trunc(raw);
 }
 return result;
}
export function arrayOptions(values={},limit=256){
 const layout=values.LED_LAYOUT||'strip';
 if(!['strip','ring','panel'].includes(layout))throw Error('Unknown LED array layout');
 const integer=(key,fallback)=>{const raw=values[key]??fallback,n=Number(raw);if(typeof raw==='boolean'||!Number.isInteger(n)||n<1||n>limit)throw Error('LED dimensions must be positive whole numbers within the chip limit');return n;};
 const rows=integer('LED_ROWS',4),columns=integer('LED_COLUMNS',8),count=layout==='panel'?rows*columns:integer('LED_COUNT',8);
 if(count>limit)throw Error(`Maximum ${limit} LEDs on this chip`);
 return {layout,rows,columns,count,serpentine:values.LED_SERPENTINE!==false};
}
export function arraySvg(options){
 const {layout,count,rows,columns,serpentine}=options;let width,height,radius,points=[];
 if(layout==='ring'){width=height=240;radius=Math.max(1.2,Math.min(9,220/count));for(let i=0;i<count;i++)points.push([120+88*Math.cos(-Math.PI/2+i*2*Math.PI/count),120+88*Math.sin(-Math.PI/2+i*2*Math.PI/count)]);}
 else if(layout==='panel'){width=columns*24+40;height=rows*24+40;radius=7;for(let i=0;i<count;i++){let r=Math.floor(i/columns),c=i%columns;if(serpentine&&r%2)c=columns-1-c;points.push([32+24*c,32+24*r]);}}
 else{width=Math.max(150,count*22+44);height=78;radius=7;for(let i=0;i<count;i++)points.push([28+i*22,34]);}
 return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${width} ${height}" role="img" aria-label="${layout}, ${count} LEDs"><rect x="2" y="2" width="${width-4}" height="${height-4}" rx="12" fill="#203548" stroke="#7092a8"/>${points.map(([x,y],i)=>`<circle cx="${x.toFixed(2)}" cy="${y.toFixed(2)}" r="${radius}" fill="hsl(${i*360/count},80%,65%)"><title>LED ${i+1}</title></circle>`).join('')}<g transform="translate(7,${height-15})" fill="none" stroke="white" stroke-width="1.2"><title>DI</title><path d="M0 0V8H2C8 8 8 0 2 0Z M9 0H13 M11 0V8 M9 8H13"/></g><g transform="translate(${width-24},${height-15})" fill="none" stroke="white" stroke-width="1.2"><title>DO</title><path d="M0 0V8H2C8 8 8 0 2 0Z"/><ellipse cx="11" cy="4" rx="3" ry="4"/></g></svg>`;
}
export function arrayControls(values,limit,onChange){
 const box=document.createElement('fieldset');const legend=document.createElement('legend');legend.textContent='LED array';box.append(legend);
 const preview=document.createElement('div');preview.style.cssText='max-width:420px;max-height:260px;overflow:auto';
 function render(){box.replaceChildren(legend);const o=arrayOptions(values,limit),defaults=arrayDefaults(values);
  const field=(name,label,kind,choices)=>{const row=document.createElement('label');row.style.cssText='display:inline-flex;gap:8px;margin:8px;align-items:center';row.append(document.createTextNode(label));const input=document.createElement(kind==='select'?'select':'input');input.name=name;
   if(kind==='select')for(const [v,text]of choices)input.append(new Option(text,v));else input.type=kind;
   if(kind==='number'){input.min=name.startsWith('LED_DEFAULT_')?0:1;input.max=name.startsWith('LED_DEFAULT_')?(['LED_DEFAULT_BRIGHTNESS','LED_DEFAULT_EFFECTSPEED'].includes(name)?100:255):limit;input.step=1;}
   if(kind==='checkbox')input.checked=o.serpentine;else input.value=values[name]??({LED_LAYOUT:'strip',LED_COUNT:8,LED_ROWS:4,LED_COLUMNS:8,LED_DEFAULT_EFFECT:defaults.effect,LED_DEFAULT_BRIGHTNESS:defaults.brightness,LED_DEFAULT_RED:defaults.red,LED_DEFAULT_GREEN:defaults.green,LED_DEFAULT_BLUE:defaults.blue,LED_DEFAULT_EFFECTSPEED:defaults.effectSpeed}[name]);
   input.onchange=()=>{const next={...values,[name]:kind==='checkbox'?input.checked:kind==='number'?Number(input.value):input.value};try{arrayOptions(next,limit);arrayDefaults(next);onChange(next);Object.assign(values,next);render();}catch(e){input.setCustomValidity(e.message);input.reportValidity();}};
   input.oninput=()=>input.setCustomValidity('');row.append(input);box.append(row);
  };
  field('LED_LAYOUT','Array layout','select',[['strip','Strip'],['ring','Ring'],['panel','Panel']]);
  if(o.layout==='panel'){field('LED_ROWS','Rows','number');field('LED_COLUMNS','Columns','number');field('LED_SERPENTINE','Serpentine wiring','checkbox');}else field('LED_COUNT','LED count','number');
  field('LED_DEFAULT_EFFECT','Default effect','select',['solid','blink','breathe','chase','rainbow','color-wipe','theater-chase','theater-chase-rainbow','colorloop','scan'].map(v=>[v,effectLabels[v]]));
  for(const [key,label]of [['BRIGHTNESS','Default brightness (%)'],['RED','Red'],['GREEN','Green'],['BLUE','Blue'],['EFFECTSPEED','Effect speed (%)']])field('LED_DEFAULT_'+key,label,'number');
  preview.innerHTML=arraySvg(o);preview.firstElementChild.style.cssText="display:block;width:100%;height:240px";box.append(preview);const hint=document.createElement('p');hint.textContent=`${o.count} LEDs. Connect the ESP GPIO to DI. DO feeds the next array, not a second GPIO. Save settings to apply supported array changes; rebuild firmware when adding a new peripheral driver.`;box.append(hint);
 }render();const stop=animateArray(preview,()=>arrayOptions(values,limit),()=>arrayDefaults(values));const observer=new MutationObserver(()=>{if(!box.isConnected){stop();observer.disconnect();}});queueMicrotask(()=>observer.observe(document.body,{childList:true,subtree:true}));return box;
}
