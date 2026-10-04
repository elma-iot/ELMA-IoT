import boardPinContacts from '../board-pin-contacts.json' with { type: 'json' };

export function exactBoardAnchors({profile,boardRect,primary,extra,offset,signalKey,boardRailForPeripheral,turns=0}){
  const contacts=boardPinContacts[profile];
  if(!contacts)return null;
  const entries=[...(primary.left||[]),...(primary.right||[]),...(extra.left||[]),...(extra.right||[])];
  const labels=new Map(entries.map(entry=>[entry.pin!=null?String(entry.pin):signalKey(entry.label),String(entry.label||(entry.pin!=null?`GPIO${entry.pin}`:""))]));
  const anchors=new Map();
  for(const [rawKey,points] of Object.entries(contacts)){
    const gpio=/^\d+$/.test(rawKey);
    const key=gpio?`gpio:${rawKey}`:`rail:${boardRailForPeripheral("",rawKey)||signalKey(rawKey)}`;
    for(const point of points){
      let [xf,yf]=point;
      for(let turn=0;turn<((turns%4)+4)%4;turn++)[xf,yf]=[1-yf,xf];
      const distances={left:xf,right:1-xf,top:yf,bottom:1-yf};
      const side=Object.keys(distances).reduce((best,item)=>distances[item]<distances[best]?item:best,'left');
      let x=boardRect.left+xf*boardRect.width,y=boardRect.top+yf*boardRect.height;
      if(side==='left')x-=offset;if(side==='right')x+=offset;if(side==='top')y-=offset;if(side==='bottom')y+=offset;
      if(!anchors.has(key))anchors.set(key,[]);
      anchors.get(key).push({x,y,side,lane:0,boardLabel:labels.get(rawKey)||rawKey,pin:gpio?Number(rawKey):null,key});
    }
  }
  return anchors.size?anchors:null;
}

// Keep automatic labels on the same contact as the wire, including rotated boards.
export function contactLabelLayout(anchor,rect,size,gap=3){
  let x=anchor.x,y=anchor.y;
  if(anchor.side==='left')x-=size.width/2+gap;
  if(anchor.side==='right')x+=size.width/2+gap;
  if(anchor.side==='top')y-=size.height/2+gap;
  if(anchor.side==='bottom')y+=size.height/2+gap;
  return {xFactor:(x-rect.left)/Math.max(rect.width,1)-.5,yFactor:(y-rect.top)/Math.max(rect.height,1)-.5,rotation:0};
}
