export const BUILTIN_LED_EFFECTS=['solid','blink','breathe','colorloop'];
const labels={solid:'Solid',blink:'Blink',breathe:'Breathe',colorloop:'Color loop'};
export function ledControlField(node,key,value,editor){
 const effect=key==='effect'&&['hardware.led','value.led_effect','value.led_color'].includes(node.type);
 const channel=['value.led_color','value.rgb_mixer'].includes(node.type)&&/^(red|green|blue|baseRed|baseGreen|baseBlue|red[AB]|green[AB]|blue[AB])$/.test(key);
 const level=node.type==='value.led_brightness'&&key==='value'||['value.led_effect','value.led_color'].includes(node.type)&&['speed','brightness'].includes(key)||channel;
 if(!effect&&!level)return null;
 const field=document.createElement(effect?'select':'input');
 if(effect){for(const name of BUILTIN_LED_EFFECTS)field.add(new Option(labels[name],name));field.value=value;}
 else {field.type='number';field.min=0;field.max=channel?255:100;field.step=1;field.value=value;}
 field.setAttribute('aria-label',effect?'LED effect':channel?key+' 0–255':key==='speed'?'Effect speed %':'Brightness %');
 field.onchange=()=>{editor.begin();node.parameters[key]=effect?field.value:Number(field.value);editor.changed(false);};return field;
}
