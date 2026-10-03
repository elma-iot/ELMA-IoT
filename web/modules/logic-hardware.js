import {sleepField,updateSleep} from './logic-sleep.js';
export const GPIO_MODES=['input','input_pullup','input_pulldown','output','pwm','analog'];
export function applyLiveLedBinding(graph,live){
 if(!live)return false;
 const definitions=(graph.devices||[]).filter(d=>d.type==='hardware.led');
 const nodes=(graph.nodes||[]).filter(n=>n.type==='hardware.led');
 if([...definitions,...nodes].every(n=>JSON.stringify(n.binding)===JSON.stringify(live)))return false;
 for(const node of [...definitions,...nodes])node.binding={...live};
 updateHardware(graph);return true;
}
const binding=(node,graph)=>graph.devices?.find(d=>d.type===node.type)?.binding||node.binding||{};
export function gpioPins(node,graph){const used=new Set(graph.nodes.filter(n=>['hardware.gpio','hardware.wake_gpio'].includes(n.type)&&n.id!==node.id).map(n=>String(n.parameters.pin)));return Object.entries(binding(node,graph).allowedPins||{}).filter(([pin])=>!used.has(pin));}
export function gpioMode(mode,caps={},pwm=true){return !!(mode==='pwm'?pwm&&caps.output:mode==='output'?caps.output:mode==='analog'?caps.analog:mode==='input_pullup'?(caps.pullup??caps.pull):mode==='input_pulldown'?(caps.pulldown??caps.pull):mode==='input'&&caps.input);}
export function updateHardware(graph){for(const n of graph.nodes){if(updateSleep(n,graph))continue;if(!n.type.startsWith('hardware.'))continue;const b=binding(n,graph);n.binding=b;let enabled=new Set();if(n.type==='hardware.gpio'){const caps=gpioPins(n,graph).find(([pin])=>Number(pin)===n.parameters.pin)?.[1];const mode=n.parameters.mode;if(gpioMode(mode,caps,b.pwmAllowed!==false)){enabled=new Set(caps.input||caps.output?['digital']:[]);if(['output','pwm'].includes(mode))enabled.add('out');if(mode==='output')for(const p of ['toggle','write','state'])enabled.add(p);if(mode==='pwm')for(const p of ['pwm','duty'])enabled.add(p);if(mode==='analog')for(const p of ['analog','millivolts'])enabled.add(p);}}else if(b.pin>=0){enabled=new Set(n.ports.map(p=>p.id));if(b.ledType==='regular'){for(const p of ['red','green','blue'])enabled.delete(p);if(!b.brightnessSupported)enabled.delete('brightness');}}for(const p of n.ports)p.enabled=enabled.has(p.id);}}
export function hardwareField(node,key,value,editor){const sleep=sleepField(node,key,value,editor);if(sleep)return sleep;if(!node.type.startsWith('hardware.'))return null;const graph=editor.graph,b=binding(node,graph);let field;
 if(key==='state'){field=document.createElement('select');field.add(new Option('High','true'));field.add(new Option('Low','false'));field.value=String(value);}
 else if(key==='pin'||key==='mode'){field=document.createElement('select');if(key==='pin'){field.add(new Option('Select available GPIO','-1'));for(const [pin,caps] of gpioPins(node,graph))field.add(new Option(caps.label||`GPIO${pin} · ${caps.output?'I/O · PWM':'Input only'}${caps.analog?' · ADC':''}`,pin));if(value>=0&&![...field.options].some(o=>Number(o.value)===value)){const option=new Option(`GPIO${value} · unavailable`,value);option.disabled=true;field.add(option);}}
 else {const caps=b.allowedPins?.[node.parameters.pin];for(const mode of GPIO_MODES){const option=new Option(mode.replaceAll('_',' '),mode);option.disabled=!gpioMode(mode,caps,b.pwmAllowed!==false);field.add(option);}}field.value=String(value);}
 else {field=document.createElement('input');field.type=typeof value==='boolean'?'checkbox':'number';if(field.type==='checkbox')field.checked=value;else {field.value=value;field.min=key==='frequency'?100:0;field.max=key==='frequency'?(b.maxPwmFrequency||20000):['red','green','blue'].includes(key)?255:100;field.step=1;}}
 const port=node.ports.find(p=>p.id===key);if(port?.enabled===false||key==='frequency'&&node.parameters.mode!=='pwm')field.disabled=true;
 field.setAttribute('aria-label',key);field.onchange=()=>{editor.begin();node.parameters[key]=field.type==='checkbox'?field.checked:key==='state'?field.value==='true':key==='mode'?field.value:Number(field.value);updateHardware(graph);editor.changed();};return field;
}
