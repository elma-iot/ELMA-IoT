import {LOGIC_CATALOG} from './logic-catalog.js';

const KEYS=['plot','series','unit'];
const DEFAULTS=LOGIC_CATALOG['mainboard.plot'].parameters;
const limit=(value,size)=>{
 const bytes=new TextEncoder().encode(String(value||'').trim()).slice(0,size);
 return new TextDecoder().decode(bytes).replace(/\uFFFD$/,'').trim();
};

export function suggestedPlotLabels(graph,plotId){
 let nodeId=plotId,portId='value';const seen=new Set();
 while(!seen.has(nodeId)){
  seen.add(nodeId);
  const wire=graph.connections.find(c=>c.target.node===nodeId&&c.target.port===portId);
  if(!wire)return null;
  nodeId=wire.source.node;portId=wire.source.port;
  const node=graph.nodes.find(n=>n.id===nodeId);
  if(!node)return null;
  if(node.type==='recording.interval'){portId='value';continue;}
  const label=String(node.ports?.find(p=>p.id===portId&&p.direction==='output')?.label||'').trim();
  let name=String(node.name||'').split(' · ').at(-1).trim();
  if(!name||name==='Value'||name==='Transfer to Plotter')name=label||'Value';
  const unit=(label.match(/\(([^()]*)\)\s*$/)||name.match(/\(([^()]*)\)\s*$/))?.[1]?.trim()||'';
  const series=name.replace(/\s*\([^()]*\)\s*$/,'').trim();
  let quantity=label.replace(/\s*\([^()]*\)\s*$/,'').trim();
  if(['value','result','output','out',''].includes(quantity.toLowerCase()))quantity=series;
  const lowered=quantity.toLowerCase();
  const plot=lowered.includes('temperature')?'Temperature':lowered.includes('humidity')?'Humidity':lowered.includes('voltage')?'Voltage':lowered.includes('current')?'Current':quantity;
  return {plot:limit(plot,32)||'Plot 1',series:limit(series,32)||'Value',unit:limit(unit,16)};
 }
 return null;
}

export function markPlotLabelManual(node,key){
 if(node.type!=='mainboard.plot'||!KEYS.includes(key))return;
 node.autoLabels??=Object.fromEntries(KEYS.map(name=>[name,node.parameters[name]===DEFAULTS[name]]));
 node.autoLabels[key]=false;
}

export function updateAutoPlotLabels(graph){
 let changed=false;
 for(const node of graph.nodes){
  if(node.type!=='mainboard.plot')continue;
  const labels=suggestedPlotLabels(graph,node.id);
  if(!labels)continue;
  if(!node.autoLabels){node.autoLabels=Object.fromEntries(KEYS.map(key=>[key,node.parameters[key]===DEFAULTS[key]]));changed=true;}
  for(const key of KEYS)if(node.autoLabels[key]&&node.parameters[key]!==labels[key]){node.parameters[key]=labels[key];changed=true;}
 }
 return changed;
}

export function resetDetachedCopiedPlotLabels(graph,copiedIds){
 for(const node of graph.nodes){
  if(!copiedIds.has(node.id)||node.type!=='mainboard.plot'||suggestedPlotLabels(graph,node.id))continue;
  for(const key of KEYS)node.parameters[key]=DEFAULTS[key];
  node.autoLabels=Object.fromEntries(KEYS.map(key=>[key,true]));
 }
}
