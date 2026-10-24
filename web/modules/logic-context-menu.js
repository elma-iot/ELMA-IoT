export function menuPosition(anchor,size,viewport){
 const margin=8,width=Math.min(size.width,Math.max(0,viewport.width-margin*2)),height=Math.min(size.height,Math.max(0,viewport.height-margin*2));
 return {left:Math.max(margin,Math.min(anchor.x,viewport.width-width-margin)),top:Math.max(margin,Math.min(anchor.y,viewport.height-height-margin)),maxHeight:Math.max(0,viewport.height-margin*2)};
}
export function filterMenuActions(menu,query){
 for(const section of [...menu.children].filter(e=>e.tagName==='DETAILS')){
  section.dataset.initialOpen??=String(section.open);
  const actions=[...section.querySelectorAll('button')];
  for(const button of actions)button.hidden=!!query&&!button.textContent.toLowerCase().includes(query);
  section.hidden=!!query&&!actions.some(button=>!button.hidden);
  section.open=query?!section.hidden:section.dataset.initialOpen==='true';
 }
}
export function keepMenuInViewport(menu,event){
 const anchor={x:event.clientX,y:event.clientY};let pending=false;
 const fit=()=>{pending=false;if(!menu.isConnected)return;const v=window.visualViewport,viewport={width:v?.width||window.innerWidth,height:v?.height||window.innerHeight},p=menuPosition(anchor,menu.getBoundingClientRect(),viewport);menu.style.boxSizing='border-box';menu.style.maxWidth=Math.max(0,viewport.width-16)+'px';menu.style.maxHeight=p.maxHeight+'px';menu.style.left=(p.left+(v?.offsetLeft||0))+'px';menu.style.top=(p.top+(v?.offsetTop||0))+'px';};
 const schedule=()=>{if(!pending){pending=true;requestAnimationFrame(fit);}};
 const resize=new ResizeObserver(schedule);resize.observe(menu);
 menu.addEventListener('toggle',schedule,true);window.addEventListener('resize',schedule);window.visualViewport?.addEventListener('resize',schedule);
 const removed=new MutationObserver(()=>{if(menu.isConnected)return;resize.disconnect();removed.disconnect();window.removeEventListener('resize',schedule);window.visualViewport?.removeEventListener('resize',schedule);});removed.observe(document.body,{childList:true});schedule();
}
