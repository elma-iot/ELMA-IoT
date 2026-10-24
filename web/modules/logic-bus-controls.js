const ranges={baud:[1200,115200],stops:[1,2],unit:[1,247],unitFilter:[0,247],function:[1,4],address:[0,65535],count:[1,32],index:[0,31],identifierFilter:[-1,0x1fffffff],length:[0,8]};
export function busField(node,key,value,editor){
 if(!node.type.startsWith('hardware.can.')&&!node.type.startsWith('hardware.modbus.'))return null;
 const choices=key==='parity'?['N','E','O']:key==='bitrate'?[125000,250000,500000,1000000]:key==='baud'?[1200,2400,4800,9600,19200,38400,57600,115200]:null;
 const field=document.createElement(choices?'select':'input');
 if(choices){for(const v of choices)field.add(new Option(String(v),String(v)));field.value=String(value);}
 else if(typeof value==='boolean'){field.type='checkbox';field.checked=value;}
 else {field.type=typeof value==='string'?'text':'number';field.value=value;if(field.type==='number'){const [lo,hi]=key==='identifier'?[0,node.parameters.extended?0x1fffffff:0x7ff]:ranges[key]||[0,65535];field.min=lo;field.max=hi;field.step=1;}}
 field.setAttribute('aria-label',key);field.onchange=()=>{editor.begin();node.parameters[key]=typeof value==='boolean'?field.checked:typeof value==='number'?Number(field.value):field.value;editor.changed();};return field;
}
export function busProblem(node){
 const p=node.parameters,t=node.type,integer=(key,lo,hi)=>Number.isInteger(p[key])&&p[key]>=lo&&p[key]<=hi;
 if(t==='hardware.can.configure'&&![125000,250000,500000,1000000].includes(p.bitrate))return 'Unsupported CAN bitrate.';
 if(t==='hardware.can.received'&&!integer('identifierFilter',-1,0x1fffffff))return 'Invalid CAN identifier filter.';
 if(t==='hardware.can.send'){
  const data=typeof p.data==='string'?p.data.trim().split(/\s+/).filter(Boolean):null;
  if(!integer('identifier',0,p.extended?0x1fffffff:0x7ff)||!integer('length',0,8)||!data||data.length>8||data.some(v=>!/^\w{2}$/.test(v)||!/^[0-9a-f]{2}$/i.test(v))||p.remote&&data.length)return 'Invalid CAN frame: check identifier, length and hex bytes.';
 }
 if(t==='hardware.modbus.read'&&(![1200,2400,4800,9600,19200,38400,57600,115200].includes(p.baud)||!['N','E','O'].includes(p.parity)||!integer('stops',1,2)||!integer('unit',1,247)||!integer('function',1,4)||!integer('address',0,65535)||!integer('count',1,32)||p.address+p.count>65536))return 'Invalid Modbus read settings.';
 if(t==='hardware.modbus.value'&&!integer('index',0,31)||t==='hardware.modbus.received'&&!integer('unitFilter',0,247))return 'Invalid Modbus value or unit filter.';
 return '';
}
