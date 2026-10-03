const add=(a,b)=>({x:a.x+b.x,y:a.y+b.y}),sub=(a,b)=>({x:a.x-b.x,y:a.y-b.y}),mul=(a,k)=>({x:a.x*k,y:a.y*k}),len=a=>Math.hypot(a.x,a.y),mix=(a,b)=>mul(add(a,b),.5),xy=a=>`${a.x},${a.y}`;
export function validRouting(edge){const points=edge.routingPoints;if(points===undefined)return true;return Array.isArray(points)&&points.length<=64&&points.every(p=>p&&['x','y'].every(k=>Number.isFinite(p[k]))&&('controlIn' in p)===('controlOut' in p)&&['controlIn','controlOut'].every(side=>!(side in p)||p[side]&&['x','y'].every(k=>Number.isFinite(p[side][k]))));}
export function routeGeometry(start,end,records=[],origin={x:0,y:0}){
 constrainHandles(records,sub(start,origin),sub(end,origin));
 const routing=records.map(p=>add(p,origin)),segments=[],dots=[];let part=`M${xy(start)}`;
 if(!routing.length){const d=Math.max(60,Math.abs(end.x-start.x)*.5);segments.push(`${part} C${xy(add(start,{x:d,y:0}))} ${xy(sub(end,{x:d,y:0}))} ${xy(end)}`);}
 else if(records.some(p=>p.controlIn)){
  const points=[start,...routing,end],rs=[null,...records,null];
  for(let i=0;i<points.length-1;i++){const a=points[i],b=points[i+1],delta=sub(b,a),length=len(delta),short=length?mul(delta,Math.min(60,length/3)/length):{x:0,y:0};let c1=add(a,i===0?{x:Math.min(60,Math.max(20,length/3)),y:0}:short),c2=sub(b,i===points.length-2?{x:Math.min(60,Math.max(20,length/3)),y:0}:short);if(rs[i]?.controlOut)c1=add(a,rs[i].controlOut);if(rs[i+1]?.controlIn)c2=add(b,rs[i+1].controlIn);segments.push(`M${xy(a)} C${xy(c1)} ${xy(c2)} ${xy(b)}`);}dots.push(...routing);
 }else{
  const leadA=Math.min(32,len(sub(routing[0],start))*.25),leadB=Math.min(32,len(sub(end,routing.at(-1)))*.25),points=[start,add(start,{x:leadA,y:0}),...routing,sub(end,{x:leadB,y:0}),end];
  for(let i=1;i<points.length-1;i++){const corner=points[i],incoming=sub(corner,points[i-1]),outgoing=sub(points[i+1],corner),before=len(incoming),after=len(outgoing),radius=Math.min(24,before*.25,after*.25),entry=before?sub(corner,mul(incoming,radius/before)):corner,exit=after?add(corner,mul(outgoing,radius/after)):corner,left=mix(entry,corner),right=mix(corner,exit),middle=mix(left,right);part+=` L${xy(entry)} Q${xy(left)} ${xy(middle)}`;if(i>=2&&i<points.length-2){dots.push(middle);segments.push(part);part=`M${xy(middle)}`;}part+=` Q${xy(right)} ${xy(exit)}`;}segments.push(part+` L${xy(end)}`);
 }
 return {d:segments.join(' '),segments,dots};
}
// Bound reach by the straight chord on each side, never by curved arc length.
export function constrainHandles(records,start,end){
 for(let i=0;i<records.length;i++){const p=records[i];for(const [side,next] of [['controlIn',i?records[i-1]:start],['controlOut',i+1<records.length?records[i+1]:end]]){
  if(!p[side])continue;const chord=sub(next,p),distance=len(chord),v=p[side],length=len(v),reach=Math.max(distance*.1,Math.min(distance*.5,length));
  p[side]=length?mul(v,reach/length):distance?mul(chord,reach/distance):{x:0,y:0};
 }}
}
export function enableHandles(edge,index,start,end,geometry,origin){const points=edge.routingPoints,p=points[index];if(p.controlIn)return;const at=sub(geometry.dots[index],origin);p.x=at.x;p.y=at.y;const before=index?points[index-1]:sub(start,origin),after=index===points.length-1?sub(end,origin):points[index+1];for(const [side,neighbor] of [['controlIn',before],['controlOut',after]]){const delta=sub(neighbor,at),length=len(delta);const reach=(Math.min(60,length/3)+length*.5)*.5;p[side]=length?mul(delta,reach/length):{x:0,y:0};}}
export function translateRoutes(graph,positions,original=[]){for(const edge of graph.connections){const keys=[edge.source.node,edge.target.node];if(!keys.every(k=>positions.has(k)))continue;const shifts=keys.map(k=>sub(graph.nodes.find(n=>n.id===k).position,positions.get(k)));if(shifts[0].x===shifts[1].x&&shifts[0].y===shifts[1].y){const previous=original.find(e=>e.id===edge.id);if(previous?.routingPoints)edge.routingPoints=JSON.parse(JSON.stringify(previous.routingPoints));for(const p of edge.routingPoints||[]){p.x+=shifts[0].x;p.y+=shifts[0].y;}}}}
export function groupRoutes(graph,group,rect){const members=new Set(group.nodes);return graph.connections.flatMap(edge=>(edge.routingPoints||[]).filter(p=>p.x>=rect.x&&p.x<=rect.x+rect.width&&p.y>=rect.y&&p.y<=rect.y+rect.height||members.has(edge.source.node)&&members.has(edge.target.node)).map(point=>({point,before:JSON.parse(JSON.stringify(point))})));}
export function moveGroupRoutes(routes,dx,dy){for(const {point,before} of routes)Object.assign(point,JSON.parse(JSON.stringify(before)),{x:before.x+dx,y:before.y+dy});}
