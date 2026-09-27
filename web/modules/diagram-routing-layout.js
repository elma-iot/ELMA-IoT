// Rooted trees keep every branch connected to the board without joining rails.
export function powerRailTree(root, endpoints) {
  const connected=[root],pending=[...endpoints],parents=new Map();
  while(pending.length){
    let best;
    for(const point of pending)for(const parent of connected){
      const distance=Math.abs(point.x-parent.x)+Math.abs(point.y-parent.y);
      if(!best||distance<best.distance)best={point,parent,distance};
    }
    parents.set(best.point.id,best.parent);
    connected.push(best.point);pending.splice(pending.indexOf(best.point),1);
  }
  return parents;
}

const intersects=(a,b)=>a.left<b.left+b.width&&a.left+a.width>b.left&&a.top<b.top+b.height&&a.top+a.height>b.top;
export function automaticPeripheralPositions({width,height,board,nodes,fixed=[]}) {
  const occupied=[board,...fixed],result=new Map();
  for(const node of nodes){
    // Reserve room for the connector column on the side facing the board.
    const labelWidth=Math.max(65,Number(node.labelWidth)||65);
    const verticalPadding=Math.max(8,((Number(node.labelHeight)||node.height)-node.height)/2);
    let best;
    for(let top=12;top+node.height+12<=height;top+=8){
      for(let left=12;left+node.width+12<=width;left+=12){
        const rightFacing=left+node.width/2<board.left+board.width/2;
        const footprint={left:left-(rightFacing?0:labelWidth),top:top-verticalPadding,width:node.width+labelWidth,height:node.height+verticalPadding*2};
        if(footprint.left<8||footprint.left+footprint.width>width-8)continue;
        if(footprint.top<8||footprint.top+footprint.height>height-8)continue;
        const overlap=occupied.reduce((sum,rect)=>sum+(intersects(footprint,rect)?1:0),0);
        const target=node.target||{x:board.left+board.width/2,y:board.top+board.height/2};
        const cost=overlap*100000+Math.abs(left+node.width/2-target.x)+Math.abs(top+node.height/2-target.y);
        if(!best||cost<best.cost)best={left,top,footprint,cost};
      }
    }
    if(best){result.set(node.id,{x:best.left,y:best.top});occupied.push(best.footprint);}
  }
  return result;
}

export function directCorridorPoints(start,end,lane=0) {
  const horizontal=Math.abs(end.x-start.x)>=Math.abs(end.y-start.y);
  // Keep the corridor between the endpoints instead of reversing beyond them.
  const offset=Math.min(Math.max(0,lane)*6,Math.abs(horizontal?end.x-start.x:end.y-start.y)/3);
  if(horizontal){const x=(start.x+end.x)/2+Math.sign(end.x-start.x)*offset;return [start,{x,y:start.y},{x,y:end.y},end];}
  const y=(start.y+end.y)/2+Math.sign(end.y-start.y)*offset;return [start,{x:start.x,y},{x:end.x,y},end];
}

export function obstacleAwareRoute(start,end,owners,lane=0,bounds={}) {
  const gap=16;
  const occupied=bounds.usedRoutes||[],spacing=8;
  const laneNumber=Math.max(0,lane);
  const direction=end.x>=start.x?1:-1;
  const maximumShift=Math.max(0,Math.abs(end.x-start.x)/2-2);
  const laneShift=-direction*Math.min(laneNumber*10,maximumShift);
  const xs=[(start.x+end.x)/2+laneShift,start.x,end.x],ys=[(start.y+end.y)/2,start.y,end.y];
  const routeOwners=[...owners,...(bounds.obstacles||[])].filter((rect,index,list)=>rect&&list.indexOf(rect)===index);
  for(const r of routeOwners){xs.push(r.left-gap,r.left+r.width+gap);ys.push(r.top-gap,r.top+r.height+gap);}
  // Offer corridors beside existing runs; lane numbers alone do not prevent
  // several routes choosing the same midpoint or component edge.
  for(const route of occupied)for(let i=1;i<route.length;i++){
    const a=route[i-1],b=route[i];
    if(a.x===b.x)xs.push(a.x-spacing,a.x+spacing);
    if(a.y===b.y)ys.push(a.y-spacing,a.y+spacing);
  }
  const preferredX=((start.x+end.x)/2)+laneShift;
  const preferred=[start,{x:preferredX,y:start.y},{x:preferredX,y:end.y},end];
  const segmentBlocked=(a,b)=>{
    for(const r of routeOwners){
      const horizontal=a.y===b.y&&a.y>r.top&&a.y<r.top+r.height&&Math.max(a.x,b.x)>r.left&&Math.min(a.x,b.x)<r.left+r.width;
      const vertical=a.x===b.x&&a.x>r.left&&a.x<r.left+r.width&&Math.max(a.y,b.y)>r.top&&Math.min(a.y,b.y)<r.top+r.height;
      if(horizontal||vertical)return true;
    }
    for(const route of occupied)for(let i=1;i<route.length;i++){
      const c=route[i-1],d=route[i];
      const parallelHorizontal=a.y===b.y&&c.y===d.y&&Math.abs(a.y-c.y)<spacing&&Math.min(Math.max(a.x,b.x),Math.max(c.x,d.x))-Math.max(Math.min(a.x,b.x),Math.min(c.x,d.x))>0;
      const parallelVertical=a.x===b.x&&c.x===d.x&&Math.abs(a.x-c.x)<spacing&&Math.min(Math.max(a.y,b.y),Math.max(c.y,d.y))-Math.max(Math.min(a.y,b.y),Math.min(c.y,d.y))>0;
      const crossing=(a.y===b.y&&c.x===d.x&&Math.min(a.x,b.x)<c.x&&c.x<Math.max(a.x,b.x)&&Math.min(c.y,d.y)<a.y&&a.y<Math.max(c.y,d.y))
        ||(a.x===b.x&&c.y===d.y&&Math.min(a.y,b.y)<c.y&&c.y<Math.max(a.y,b.y)&&Math.min(c.x,d.x)<a.x&&a.x<Math.max(c.x,d.x));
      if(parallelHorizontal||parallelVertical||crossing)return true;
    }
    return false;
  };
  if(preferred.every((point,index)=>index===0||!segmentBlocked(preferred[index-1],point)))return preferred;
  const candidates=[];
  for(const x of new Set(xs))candidates.push([start,{x,y:start.y},{x,y:end.y},end]);
  for(const y of new Set(ys))candidates.push([start,{x:start.x,y},{x:end.x,y},end]);
  let best;
  for(const points of candidates){
    let cost=points.length*20;
    // Prefer monotonic orthogonal paths: a fallback may leave the source-target
    // corridor only when an obstacle makes the tidy in-corridor route impossible.
    for(const point of points)if(point.x<Math.min(start.x,end.x)||point.x>Math.max(start.x,end.x)||point.y<Math.min(start.y,end.y)||point.y>Math.max(start.y,end.y))cost+=500000;
    for(const p of points)if(p.x<6||p.y<6||(bounds.width&&p.x>bounds.width-6)||(bounds.height&&p.y>bounds.height-6))cost+=1000000;
    for(let i=1;i<points.length;i++){
      const a=points[i-1],b=points[i];cost+=Math.abs(a.x-b.x)+Math.abs(a.y-b.y);
      for(const route of occupied)for(let j=1;j<route.length;j++){
        const c=route[j-1],d=route[j];
        const horizontal=a.y===b.y&&c.y===d.y;
        const vertical=a.x===b.x&&c.x===d.x;
        const crossing=(a.y===b.y&&c.x===d.x&&Math.min(a.x,b.x)<c.x&&c.x<Math.max(a.x,b.x)&&Math.min(c.y,d.y)<a.y&&a.y<Math.max(c.y,d.y))
          ||(a.x===b.x&&c.y===d.y&&Math.min(a.y,b.y)<c.y&&c.y<Math.max(a.y,b.y)&&Math.min(c.x,d.x)<a.x&&a.x<Math.max(c.x,d.x));
        if(crossing)cost+=150000;
        if(!horizontal&&!vertical)continue;
        const distance=Math.abs(horizontal?a.y-c.y:a.x-c.x);
        const overlap=horizontal
          ?Math.min(Math.max(a.x,b.x),Math.max(c.x,d.x))-Math.max(Math.min(a.x,b.x),Math.min(c.x,d.x))
          :Math.min(Math.max(a.y,b.y),Math.max(c.y,d.y))-Math.max(Math.min(a.y,b.y),Math.min(c.y,d.y));
        // Shared junctions are necessary, but long parallel runs need separation.
        if(distance<spacing&&overlap>8)cost+=(spacing-distance)*overlap*100;
      }
      for(const r of routeOwners){
        const horizontal=a.y===b.y&&a.y>r.top&&a.y<r.top+r.height&&Math.max(a.x,b.x)>r.left&&Math.min(a.x,b.x)<r.left+r.width;
        const vertical=a.x===b.x&&a.x>r.left&&a.x<r.left+r.width&&Math.max(a.y,b.y)>r.top&&Math.min(a.y,b.y)<r.top+r.height;
        if(horizontal||vertical)cost+=1000000000;
      }
    }
    if(!best||cost<best.cost)best={points,cost};
  }
  return best.points;
}
