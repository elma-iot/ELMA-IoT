const tr=s=>window.ElmaI18n?.t?.(s)||window.ElmaFirmwareI18n?.t?.(s)||s;
const binding=(n,g)=>g.devices?.find(d=>d.type===n.type)?.binding||n.binding||{};
export function wakePins(node,graph){
 const used=new Set(graph.nodes.filter(n=>['hardware.gpio','hardware.wake_gpio'].includes(n.type)&&n.id!==node.id).map(n=>Number(n.parameters.pin)));
 return Object.entries(binding(node,graph).allowedPins||{}).filter(([pin])=>!used.has(Number(pin)));
}
export function updateSleep(node,graph){
 if(!['hardware.sleep','hardware.wake_gpio'].includes(node.type))return false;
 node.binding=binding(node,graph);
 for(const port of node.ports)port.enabled=node.type==='hardware.sleep'?!(port.id==='out'&&node.parameters.mode==='deep')&&!(port.id==='seconds'&&!node.parameters.timerEnabled):wakePins(node,graph).some(([pin])=>Number(pin)===node.parameters.pin);
 return true;
}
export function sleepProblem(node,graph){
 const p=node.parameters;
 if(node.type==='hardware.wake_gpio')return wakePins(node,graph).some(([pin])=>Number(pin)===p.pin)&&['high','low'].includes(p.level)?'':'Select an unused wake-capable GPIO and High/Low level';
 if(node.type!=='hardware.sleep')return '';
 if(!binding(node,graph).modes?.includes(p.mode))return 'Sleep mode unavailable on this chip';
 const link=graph.connections.find(e=>e.target.node===node.id&&e.target.port==='wake');
 if(!p.timerEnabled&&!link)return 'Enable a wake timer or connect Wake GPIO';
 if(p.timerEnabled&&!graph.connections.some(e=>e.target.node===node.id&&e.target.port==='seconds')&&(!Number.isFinite(p.seconds)||p.seconds<.001||p.seconds>604800))return 'Wake delay must be 0.001–604800 seconds';
 if(link){const source=graph.nodes.find(n=>n.id===link.source.node);if(source?.type!=='hardware.wake_gpio'||!wakePins(source,graph).find(([pin])=>Number(pin)===source.parameters.pin)?.[1]?.[p.mode])return 'Connected GPIO cannot wake this board in the selected mode';}
 return '';
}
export function sleepField(node,key,value,editor){
 if(!['hardware.sleep','hardware.wake_gpio'].includes(node.type))return null;
 const field=document.createElement(['mode','level','pin'].includes(key)?'select':'input');
 if(field.tagName==='SELECT'){
  let options=key==='mode'?[['light','Light sleep'],['deep','Deep sleep']]:key==='level'?[['low','Low'],['high','High']]:[[-1,'Select available GPIO'],...wakePins(node,editor.graph).filter(([pin,caps])=>!editor.graph.connections.some(e=>e.source.node===node.id&&editor.graph.nodes.find(n=>n.id===e.target.node)?.parameters.mode==='deep')||caps.deep).map(([pin])=>[pin,'GPIO'+pin])];
  for(const [id,label] of options){const option=new Option(tr(label),id);if(key==='mode')option.disabled=!binding(node,editor.graph).modes?.includes(id);field.add(option);}
  if(!options.some(([id])=>String(id)===String(value))){const option=new Option(String(value)+' · '+tr('Unavailable'),value);option.disabled=true;field.add(option);}field.value=String(value);
 }else if(key==='timerEnabled'){field.type='checkbox';field.checked=value;}else{field.type='number';field.min=.001;field.max=604800;field.step=.001;field.value=value;field.disabled=!node.parameters.timerEnabled;}
 field.setAttribute('aria-label',tr(key==='timerEnabled'?'Wake timer':key));field.onchange=()=>{editor.begin();node.parameters[key]=field.type==='checkbox'?field.checked:['pin','seconds'].includes(key)?Number(field.value):field.value;editor.changed();};return field;
}
