export const COLORS={execution:'#edf2fb',boolean:'#f33f4a',number:'#24bc73',integer:'#13bbdb',string:'#df59ba',analog:'#ef9900',peripheral:'#438deb',path:'#438deb',audio:'#20bac4',scalar:'#13bbdb'};
export const clone=x=>JSON.parse(JSON.stringify(x));
export const id=()=>globalThis.crypto?.randomUUID?.()||`${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
export const compatible=(a,b)=>b==='scalar'&&['number','integer','analog','boolean','string'].includes(a)||a===b||b==='number'&&['analog','integer'].includes(a)||a==='path'&&b==='audio';
export const specKey=s=>`${s.type}:${s.peripheral?.id||''}`;
export function createNode(spec,position){return {id:id(),type:spec.type,name:spec.title,ports:clone(spec.ports),parameters:clone(spec.parameters),position:clone(position),...(spec.binding?{binding:clone(spec.binding)}:{}),...(spec.peripheral?{peripheral:clone(spec.peripheral)}:{})};}
export function connect(graph,a,ap,b,bp){
 const source=graph.nodes.find(n=>n.id===a),target=graph.nodes.find(n=>n.id===b),sp=source?.ports.find(p=>p.id===ap&&p.direction==='output'),tp=target?.ports.find(p=>p.id===bp&&p.direction==='input');
 if(a===b||!sp||!tp||!compatible(sp.type,tp.type))throw Error('Choose compatible output and input connectors.');
 if(graph.connections.some(e=>e.target.node===b&&e.target.port===bp))throw Error('This input is already connected. Break its link first.');
 graph.connections.push({id:id(),source:{node:a,port:ap},target:{node:b,port:bp}});
}
export function removeNodes(graph,ids){graph.nodes=graph.nodes.filter(n=>!ids.has(n.id));graph.connections=graph.connections.filter(e=>!ids.has(e.source.node)&&!ids.has(e.target.node));}
export function duplicate(graph,ids,offset=40){const map=new Map(),nodes=graph.nodes.filter(n=>ids.has(n.id)).map(n=>{const copy=clone(n);copy.id=id();map.set(n.id,copy.id);copy.position.x+=offset;copy.position.y+=offset;return copy;});const links=graph.connections.filter(e=>map.has(e.source.node)&&map.has(e.target.node)).map(e=>({...clone(e),id:id(),source:{...e.source,node:map.get(e.source.node)},target:{...e.target,node:map.get(e.target.node)}}));const copies=(graph.groups||[]).filter(g=>g.nodes.length&&g.nodes.every(n=>map.has(n))).map(g=>{const c=clone(g);c.id=id();c.name+=' copy';c.nodes=g.nodes.map(n=>map.get(n));if(c.rect){c.rect.x+=offset;c.rect.y+=offset;}return c;});graph.groups||=[];graph.groups.push(...copies);graph.nodes.push(...nodes);graph.connections.push(...links);return new Set(nodes.map(n=>n.id));}
export function problems(graph,specs){
 const errors=new Map(),hints=new Map(),add=(map,id,message)=>map.set(id,[...(map.get(id)||[]),message]);const lookup=new Map(specs.map(s=>[specKey(s),s]));const adjacency=new Map(graph.nodes.map(n=>[n.id,[]]));
 for(const n of graph.nodes){const incoming=graph.connections.filter(e=>e.target.node===n.id),outgoing=graph.connections.filter(e=>e.source.node===n.id);if(!lookup.has(specKey(n)))add(errors,n.id,'Peripheral/node is unavailable on this device.');for(const p of n.ports)if(p.direction==='input'&&p.required&&!incoming.some(e=>e.target.port===p.id))add(errors,n.id,`Connect required input ${p.label}.`);
 const exec=n.ports.filter(p=>p.direction==='output'&&p.type==='execution'),linked=outgoing.some(e=>exec.some(p=>p.id===e.source.port));
 if(n.type==='timing.repeat'&&!linked){const after=incoming.some(e=>graph.nodes.find(x=>x.id===e.source.node)?.type==='peripheral.play');add(hints,n.id,after?'Repeat cannot replay upstream Play. Put Repeat before Play and connect Repeat.Out to Play.In.':'Connect Repeat.Out to the action to repeat. Count is the total number of pulses.');}
 else if(exec.length&&!linked&&!n.ports.some(p=>p.direction==='input'&&p.type==='execution'&&/^(action|peripheral|mainboard)\./.test(n.type)))add(hints,n.id,'No downstream action is connected yet.');
 if(n.type==='peripheral.play'&&linked)add(hints,n.id,'Play.Out means accepted, not audio finished. A directly connected Stop can cut playback short.');
 if(n.type==='timing.timer'&&!incoming.some(e=>['in','enabled'].includes(e.target.port)))add(errors,n.id,'Timer needs Start or Enabled.');
 if(n.type.startsWith('condition.')&&outgoing.some(e=>graph.nodes.find(x=>x.id===e.target.node)?.type==='event.change'))add(hints,n.id,'On Change fires on true and false. Use Rising Edge to act only on true.');
 if(!outgoing.length&&n.ports.some(p=>p.direction==='output'&&p.type!=='execution'))add(hints,n.id,'Value/source is unused; connect it when ready.');
 }
 const occupied=new Set();for(const e of graph.connections){const a=graph.nodes.find(n=>n.id===e.source.node),b=graph.nodes.find(n=>n.id===e.target.node),ap=a?.ports.find(p=>p.id===e.source.port&&p.direction==='output'),bp=b?.ports.find(p=>p.id===e.target.port&&p.direction==='input');const key=JSON.stringify(e.target);if(!ap||!bp||!compatible(ap.type,bp.type)||occupied.has(key))add(errors,e.target.node,'Invalid connection type or duplicate input.');occupied.add(key);adjacency.get(e.source.node)?.push(e.target.node);}
 const visited=new Set(),stack=[];function visit(n){if(stack.includes(n)){for(const x of stack.slice(stack.indexOf(n)))add(errors,x,'Circular flow: use Repeat instead.');return;}if(visited.has(n))return;stack.push(n);for(const next of adjacency.get(n)||[])visit(next);stack.pop();visited.add(n);}for(const n of graph.nodes)visit(n.id);
 return {errors,hints};
}

export function upgradeNodes(graph,specs){for(const node of graph.nodes){const definition=specs.find(s=>specKey(s)===specKey(node));if(!definition)continue;const old=node.ports||[],next=definition.ports;const safe=old.every(p=>next.some(q=>['id','label','type','direction'].every(key=>p[key]===q[key])&&(!q.required||p.required)))&&next.filter(q=>!old.some(p=>p.id===q.id)).every(q=>!q.required);if(safe){node.ports=clone(next);node.parameters={...clone(definition.parameters),...node.parameters};}}}
