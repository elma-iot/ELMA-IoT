export const BUILTIN_LED_EFFECTS=['solid','blink','breathe','colorloop'];
const labels={solid:'Solid',blink:'Blink',breathe:'Breathe',colorloop:'Color loop'};
export function ledControlField(node,key,value,editor){
 const effect=key==='effect'&&['hardware.led','value.led_effect'].includes(node.type);
 const level=node.type==='value.led_brightness'&&key==='value'||node.type==='value.led_effect'&&key==='speed';
 if(!effect&&!level)return null;
 const field=document.createElement(effect?'select':'input');
 if(effect){for(const name of BUILTIN_LED_EFFECTS)field.add(new Option(labels[name],name));field.value=value;}
 else {field.type='number';field.min=0;field.max=100;field.step=1;field.value=value;}
 field.setAttribute('aria-label',effect?'LED effect':key==='speed'?'Effect speed %':'Brightness %');
 field.onchange=()=>{editor.begin();node.parameters[key]=effect?field.value:Number(field.value);editor.changed(false);};return field;
}
