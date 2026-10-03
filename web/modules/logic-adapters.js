import {clone,compatible,connect,createNode} from './logic-graph-model.js';
export function adapterType(a,b){
 if(a==='execution'&&['number','integer','boolean','string','measurement','scalar'].includes(b))return 'bridge.'+({measurement:'number',scalar:'number'}[b]||b);
 if(a==='boolean'&&['number','integer'].includes(b))return 'convert.boolean_'+b;
 if(['number','integer','analog','boolean','scalar'].includes(a)&&b==='string')return 'value.text';
 if(['number','analog'].includes(a)&&b==='integer')return 'convert.number_integer';
 if(a==='string'&&b==='number')return 'convert.text_number';
 if(a==='measurement'&&b==='number')return 'convert.measurement_number';
 if(['number','integer','analog'].includes(a)&&b==='boolean')return 'condition.compare';
 if(a==='boolean'&&b==='execution')return 'event.rising';
 if(['number','integer','analog'].includes(a)&&b==='execution')return 'number.event';
 return null;
}
export function connectAdapted(graph,specs,a,ap,b,bp){
 const source=graph.nodes.find(n=>n.id===a),target=graph.nodes.find(n=>n.id===b),sp=source?.ports.find(p=>p.id===ap),tp=target?.ports.find(p=>p.id===bp);
 if(!sp||!tp||compatible(sp.type,tp.type)){connect(graph,a,ap,b,bp);return null;}
 const kind=adapterType(sp.type,tp.type),spec=specs.find(s=>s.type===(kind==='number.event'?'condition.compare':kind));
 if(!spec||a===b||sp.enabled===false||tp.enabled===false||sp.direction!=='output'||tp.direction!=='input')throw Error('No automatic conversion for these connectors. Use a compatible value or an explicit condition/event node.');
 if(graph.connections.some(e=>e.target.node===b&&e.target.port===bp))throw Error('This input is already connected. Break its link first.');
 const before=clone(graph);
 try{
  const node=createNode(spec,{x:(source.position.x+target.position.x)/2,y:(source.position.y+target.position.y)/2+80});graph.nodes.push(node);
  if(kind.startsWith('bridge.')){
   if(Object.hasOwn(target.parameters,bp))node.parameters.value=clone(target.parameters[bp]);
   connect(graph,a,ap,node.id,'in');connect(graph,node.id,'value',b,bp);
   const actions=graph.connections.filter(e=>e.source.node===a&&e.source.port===ap&&e.target.node===b&&target.ports.find(p=>p.id===e.target.port)?.type==='execution');
   for(const e of actions)e.source={node:node.id,port:'out'};
   if(!actions.length){let action=target.type==='hardware.led'?'write':target.type==='hardware.gpio'?(bp==='duty'?'pwm':'write'):null;const inputs=target.ports.filter(p=>p.direction==='input'&&p.type==='execution'&&p.enabled!==false);if(!action&&inputs.length===1)action=inputs[0].id;const p=target.ports.find(p=>p.id===action);if(p&&p.enabled!==false)connect(graph,node.id,'out',b,action);}
  }else{
   const compare=['condition.compare','number.event'].includes(kind),ip=kind==='value.text'?'append':compare?'a':kind==='event.rising'?'value':'input',op=compare?'result':kind==='event.rising'?'out':'value';
   if(compare)Object.assign(node.parameters,{operator:'!=',b:0});
   connect(graph,a,ap,node.id,ip);
   if(kind==='number.event'){
    const rising=specs.find(s=>s.type==='event.rising');if(!rising)throw Error('Rising Edge is unavailable for this firmware.');
    const edge=createNode(rising,{x:node.position.x+240,y:node.position.y});graph.nodes.push(edge);
    connect(graph,node.id,'result',edge.id,'value');connect(graph,edge.id,'out',b,bp);
    for(const g of graph.groups||[])if(g.nodes.includes(a)&&g.nodes.includes(b))g.nodes.push(edge.id);
   }else connect(graph,node.id,op,b,bp);
  }
  for(const g of graph.groups||[])if(g.nodes.includes(a)&&g.nodes.includes(b))g.nodes.push(node.id);
  return node;
 }catch(error){Object.assign(graph,before);throw error;}
}
