import boardPinContacts from '../board-pin-contacts.json';

export function exactBoardAnchors({profile,boardRect,primary,extra,offset,signalKey,boardRailForPeripheral}){
  const contacts=boardPinContacts[profile];
  if(!contacts)return null;
  const entries=[...(primary.left||[]),...(primary.right||[]),...(extra.left||[]),...(extra.right||[])];
  const labels=new Map(entries.map(entry=>[entry.pin!=null?String(entry.pin):signalKey(entry.label),String(entry.label||(entry.pin!=null?`GPIO${entry.pin}`:""))]));
  const anchors=new Map();
  for(const [rawKey,points] of Object.entries(contacts)){
    const gpio=/^\d+$/.test(rawKey);
    const key=gpio?`gpio:${rawKey}`:`rail:${boardRailForPeripheral("",rawKey)||signalKey(rawKey)}`;
    for(const [xf,yf] of points){
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
