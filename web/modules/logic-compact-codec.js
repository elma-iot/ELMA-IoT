import {gzipSync,gunzipSync,strToU8,strFromU8} from 'fflate';
export function packCompactGraph(input){
 const g=input.compactFormat===2?expandCompactGraph(input):structuredClone(input),layout={view:g.view||{},nodes:{},connections:{},groups:{}};
 delete g.view;delete g.devices;g.compactFormat=2;
 const fields={nodes:['name','position','wizardPosition'],connections:['routingPoints'],groups:['name','color','label','rect']};
 for(const key of Object.keys(fields))for(const item of g[key]||[]){const meta={};for(const field of fields[key])if(Object.hasOwn(item,field)){meta[field]=item[field];delete item[field];}layout[key][item.id]=meta;}
 for(const n of g.nodes){delete n.ports;delete n.binding;delete n.description;if(n.peripheral==null)delete n.peripheral;}
 const raw=strToU8(JSON.stringify(layout));if(raw.length>32768)throw Error('Compact editor layout exceeds 32 KiB');
 g.layoutGzip=btoa(String.fromCharCode(...gzipSync(raw)));return g;
}
export function expandCompactGraph(input){
 const g=structuredClone(input);if(g.compactFormat!==2)return g;
 const bytes=Uint8Array.from(atob(g.layoutGzip),c=>c.charCodeAt(0));
 if(bytes.length<18||bytes.length>16384||new DataView(bytes.buffer).getUint32(bytes.length-4,true)>32768)throw Error('Invalid compact editor layout');
 const layout=JSON.parse(strFromU8(gunzipSync(bytes,{out:new Uint8Array(32768)})).replace(/\u0000+$/,''));
 const fields={nodes:['name','position','wizardPosition'],connections:['routingPoints'],groups:['name','color','label','rect']};
 g.view=layout.view||{};
 for(const key of Object.keys(fields))for(const item of g[key]||[])for(const field of fields[key])if(Object.hasOwn(layout[key]?.[item.id]||{},field))item[field]=layout[key][item.id][field];
 delete g.layoutGzip;g.compactFormat=1;return g;
}
