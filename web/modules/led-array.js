import {animateArray} from './led-effects.js';
export const effectLabels={"solid": "Solid", "blink": "Blink", "breathe": "Breathe", "chase": "Chase", "rainbow": "Rainbow", "color-wipe": "Color wipe", "theater-chase": "Theater chase", "theater-chase-rainbow": "Theater chase rainbow", "colorloop": "Color loop", "scan": "Scan"};
Object.assign(effectLabels,{"stream-spectrum": "Stream spectrum", "stream-vu": "Stream VU meter", "stream-pulse": "Stream pulse", "mic-spectrum": "MIC spectrum", "mic-vu": "MIC VU meter", "mic-pulse": "MIC pulse"});
export function availableEffects(binding={}){
 return (Array.isArray(binding.effects)?binding.effects:Object.keys(effectLabels).slice(0,10)).filter(effect=>Object.hasOwn(effectLabels,effect));
}
export function arrayDefaults(values={}){
 const effect=values.LED_DEFAULT_EFFECT??'solid';
 if(!Object.hasOwn(effectLabels,effect))throw Error('Unknown default LED effect');
 const result={effect};
 for(const [key,fallback,max] of [['brightness',20,100],['red',255,255],['green',128,255],['blue',0,255],['effectSpeed',50,100]]){
  const raw=values['LED_DEFAULT_'+key.toUpperCase()]??fallback;
  if(typeof raw!=='number'||!Number.isFinite(raw)||raw<0||raw>max)throw Error('LED default value out of range');
  result[key]=Math.trunc(raw);
 }
 return result;
}
export function arrayOptions(values={},limit=256){
 const integer=(data,key,fallback)=>{const raw=data[key]??fallback,n=Number(raw);if(typeof raw==='boolean'||!Number.isInteger(n)||n<1||n>limit)throw Error('LED dimensions must be positive whole numbers within the chip limit');return n;};
 const unit=data=>{const layout=data.LED_LAYOUT||'strip';if(!['strip','ring','panel'].includes(layout))throw Error('Unknown LED array layout');const rows=integer(data,'LED_ROWS',4),columns=integer(data,'LED_COLUMNS',8),count=layout==='panel'?rows*columns:integer(data,'LED_COUNT',8);return {layout,rows,columns,count,serpentine:data.LED_SERPENTINE!==false,defaults:arrayDefaults(data)};};
 const arrayCount=integer(values,'LED_ARRAYS',1),items=values.LED_ARRAY_ITEMS??[];
 if(!Array.isArray(items)||items.some(item=>!item||Array.isArray(item)||typeof item!=='object'))throw Error('Invalid LED array chain definitions');
 if(arrayCount>8)throw Error('Maximum 8 chained arrays per data output');
 const first=unit(values),segments=[first];for(let i=1;i<arrayCount;i++)segments.push(unit({...values,...items[i-1]}));
 if(values.LED_SYNC===true)for(const segment of segments)segment.defaults={...first.defaults};
 const count=segments.reduce((sum,item)=>sum+item.count,0);if(count>limit)throw Error(`Maximum ${limit} LEDs per data output; chain has ${count}`);
 return {...first,count,perArrayCount:first.count,arrayCount,sync:values.LED_SYNC===true,segments};
}
export function arraySvg(options){
 if(options.segments?.length>1){
  const cells=options.segments.map((segment,i)=>`<g data-chain-array="${i}" transform="translate(${i*300},0)"><rect x="2" y="2" width="258" height="286" rx="12" fill="#203548" stroke="#7092a8"/><text x="130" y="19" text-anchor="middle" fill="white" font-size="13">Array ${i+1} · ${segment.count} LEDs</text>${arraySvg({...segment,segments:undefined}).replace('<svg ','<svg x="15" y="28" width="230" height="224" ')}<text x="12" y="279" fill="white" font-size="13">DIN</text><text x="207" y="279" fill="white" font-size="13">DOUT</text></g>`);
  const wires=options.segments.slice(1).map((_,i)=>`<path data-chain-link="${i}" d="M${i*300+250} 270 H${(i+1)*300+10}" stroke="#0cba90" stroke-width="3" fill="none"/><path d="M${(i+1)*300+3} 266 l7 4 -7 4" stroke="#0cba90" fill="none"/>`).join('');
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${options.segments.length*300-40} 290" role="img" aria-label="${options.arrayCount} chained arrays, ${options.count} LEDs">${cells.join('')}${wires}</svg>`;
 }

 const {layout,count,rows,columns,serpentine}=options;let width,height,radius,points=[];
 if(layout==='ring'){width=height=240;radius=Math.max(1.2,Math.min(9,220/count));for(let i=0;i<count;i++)points.push([120+88*Math.cos(-Math.PI/2+i*2*Math.PI/count),120+88*Math.sin(-Math.PI/2+i*2*Math.PI/count)]);}
 else if(layout==='panel'){width=columns*24+40;height=rows*24+40;radius=7;for(let i=0;i<count;i++){let r=Math.floor(i/columns),c=i%columns;if(serpentine&&r%2)c=columns-1-c;points.push([32+24*c,32+24*r]);}}
 else{width=Math.max(150,count*22+44);height=78;radius=7;for(let i=0;i<count;i++)points.push([28+i*22,34]);}
 return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${width} ${height}" role="img" aria-label="${layout}, ${count} LEDs"><rect x="2" y="2" width="${width-4}" height="${height-4}" rx="12" fill="#203548" stroke="#7092a8"/>${points.map(([x,y],i)=>`<circle cx="${x.toFixed(2)}" cy="${y.toFixed(2)}" r="${radius}" fill="#000000"><title>LED ${i+1}</title></circle>`).join('')}<text x="9" y="${height-7}" fill="white" font-size="12">DIN</text><text x="${width-40}" y="${height-7}" fill="white" font-size="12">DOUT</text></svg>`;
}
export function arrayControls(values,limit,onChange,{showPreview=true,compact=false,effects=Object.keys(effectLabels).slice(0,10)}={}){
 const box=document.createElement('fieldset');box.classList.toggle('led-array-compact',compact);const legend=document.createElement('legend');legend.textContent='LED array';box.append(legend);
 const preview=document.createElement('div');preview.style.cssText='max-width:420px;max-height:260px;overflow:auto';
 function render(){box.replaceChildren(legend);const o=arrayOptions(values,limit),defaults=arrayDefaults(values);
  let target=box;
  const field=(name,label,kind,choices,segment=-1)=>{const data=segment<0||(o.sync&&name.startsWith('LED_DEFAULT_'))?values:{...values,...values.LED_ARRAY_ITEMS?.[segment]};const unit=segment<0?o:o.segments[segment+1];const entryDefaults=arrayDefaults(data);const row=document.createElement('label');row.style.cssText='display:inline-flex;gap:8px;margin:8px;align-items:center';row.append(document.createTextNode(label));const input=document.createElement(kind==='select'?'select':'input');input.name=name;input.setAttribute('aria-label',label);
   if(kind==='select')for(const [v,text]of choices)input.append(new Option(text,v));else input.type=kind;
   if(kind==='number'){input.min=name.startsWith('LED_DEFAULT_')?0:1;input.max=name==='LED_ARRAYS'?8:name.startsWith('LED_DEFAULT_')?(['LED_DEFAULT_BRIGHTNESS','LED_DEFAULT_EFFECTSPEED'].includes(name)?100:255):limit;input.step=1;}
   if(kind==='checkbox')input.checked=name==='LED_SYNC'?o.sync:unit.serpentine;else input.value=data[name]??({LED_ARRAYS:1,LED_LAYOUT:'strip',LED_COUNT:8,LED_ROWS:4,LED_COLUMNS:8,LED_DEFAULT_EFFECT:entryDefaults.effect,LED_DEFAULT_BRIGHTNESS:entryDefaults.brightness,LED_DEFAULT_RED:entryDefaults.red,LED_DEFAULT_GREEN:entryDefaults.green,LED_DEFAULT_BLUE:entryDefaults.blue,LED_DEFAULT_EFFECTSPEED:entryDefaults.effectSpeed}[name]);
   if(segment>=0&&o.sync&&name.startsWith('LED_DEFAULT_'))input.disabled=true;
   input.onchange=()=>{const value=kind==='checkbox'?input.checked:kind==='number'?Number(input.value):input.value;const next={...values};if(segment<0){next[name]=value;if(name==='LED_ARRAYS'){next.LED_ARRAY_ITEMS=[...(values.LED_ARRAY_ITEMS||[])];while(next.LED_ARRAY_ITEMS.length<value-1)next.LED_ARRAY_ITEMS.push(Object.fromEntries(Object.entries(values).filter(([key])=>key.startsWith('LED_')&&!['LED_ARRAYS','LED_ARRAY_ITEMS','LED_SYNC'].includes(key))));}}else{next.LED_ARRAY_ITEMS=[...(values.LED_ARRAY_ITEMS||[])];next.LED_ARRAY_ITEMS[segment]={...(values.LED_ARRAY_ITEMS?.[segment]||{}),[name]:value};}try{arrayOptions(next,limit);arrayDefaults(next);onChange(next);Object.assign(values,next);render();}catch(e){input.setCustomValidity(e.message);input.reportValidity();}};
   input.oninput=()=>input.setCustomValidity('');row.append(input);target.append(row);
  };
  field('LED_ARRAYS','Number of arrays','number');
  field('LED_SYNC','Sync all arrays','checkbox');
  for(let i=0;i<o.arrayCount;i++){
   const segment=i-1,unit=o.segments[i];target=document.createElement('div');target.className='led-array-row';target.dataset.arrayIndex=i;box.append(target);
   if(o.arrayCount>1){const title=document.createElement('span');title.className='led-array-number';title.textContent=String(i+1);target.append(title);}
   field('LED_LAYOUT','Array','select',[['strip','Strip'],['ring','Ring'],['panel','Panel']],segment);
   if(unit.layout==='panel'){field('LED_ROWS','Rows','number',null,segment);field('LED_COLUMNS','Columns','number',null,segment);if(!compact)field('LED_SERPENTINE','Serpentine wiring','checkbox',null,segment);}
   else field('LED_COUNT','PIXEL','number',null,segment);
   field('LED_DEFAULT_EFFECT','Effect','select',effects.filter(v=>effectLabels[v]).map(v=>[v,effectLabels[v]]),segment);
   if(!compact)for(const [key,label]of [['BRIGHTNESS','Brightness (%)'],['RED','Red'],['GREEN','Green'],['BLUE','Blue'],['EFFECTSPEED','Speed (%)']])field('LED_DEFAULT_'+key,label,'number',null,segment);
  }
  preview.innerHTML=arraySvg(o);preview.firstElementChild.style.cssText="display:block;width:100%;height:240px";if(showPreview)box.append(preview);const hint=document.createElement('p');hint.textContent=`${o.arrayCount} arrays, ${o.count} LEDs total. Connect the ESP GPIO to DI. DO feeds the next array, not a second GPIO. ${showPreview?"Save settings to apply supported array changes; rebuild firmware when adding a new peripheral driver.":""}`;if(!compact)box.append(hint);
 }render();const stop=showPreview?animateArray(preview,()=>arrayOptions(values,limit),()=>arrayDefaults(values)):()=>{};const observer=new MutationObserver(()=>{if(!box.isConnected){stop();observer.disconnect();}});queueMicrotask(()=>observer.observe(document.body,{childList:true,subtree:true}));return box;
}
