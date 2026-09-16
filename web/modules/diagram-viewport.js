const views=new WeakMap();
export const clampDiagramZoom=value=>Math.min(3,Math.max(0.4,value));
export function zoomDiagramAt(view,point,zoom){
  const next=clampDiagramZoom(zoom),ratio=next/view.zoom;
  return {zoom:next,x:point.x-(point.x-view.x)*ratio,y:point.y-(point.y-view.y)*ratio};
}
export function canvasClientPoint(point,stage){
  const view=views.get(stage);if(!view)return point;
  const rect=stage.getBoundingClientRect();
  return {clientX:rect.left+(point.clientX-rect.left)/view.zoom,clientY:rect.top+(point.clientY-rect.top)/view.zoom};
}
// Keep layout and wiring in canvas units, independently of the view transform.
export function diagramRect(element){
  if(!element)return null;
  const rect=element.getBoundingClientRect(),stage=element.closest?.('.peripheral-diagram-stage'),view=views.get(stage);
  if(!view)return rect;
  const origin=stage.getBoundingClientRect(),left=origin.left+(rect.left-origin.left)/view.zoom,top=origin.top+(rect.top-origin.top)/view.zoom;
  const width=rect.width/view.zoom,height=rect.height/view.zoom;
  return {left,top,width,height,right:left+width,bottom:top+height,x:left,y:top};
}
export function setupDiagramViewport(stage){
  if(!stage||views.has(stage))return;
  const viewport=document.createElement('div');viewport.className='diagram-viewport';
  stage.before(viewport);viewport.append(stage);
  let view={x:0,y:0,zoom:1},gesture=null;
  const pointers=new Map();views.set(stage,view);
  const apply=()=>{views.set(stage,view);stage.style.transform=`translate(${view.x}px,${view.y}px) scale(${view.zoom})`;};
  const local=point=>{const rect=viewport.getBoundingClientRect();return {x:point.clientX-rect.left,y:point.clientY-rect.top};};
  const pair=()=>{const [a,b]=[...pointers.values()].map(local);return {center:{x:(a.x+b.x)/2,y:(a.y+b.y)/2},distance:Math.hypot(a.x-b.x,a.y-b.y)};};
  const interactive=target=>target.closest?.('.peripheral-diagram-node,.peripheral-diagram-board-shell,.peripheral-diagram-floating-label,.peripheral-diagram-wire-hit,.peripheral-diagram-wire-handle,button,input,select,textarea,a');
  viewport.addEventListener('pointerdown',event=>{
    if(event.button!==0)return;
    pointers.set(event.pointerId,{clientX:event.clientX,clientY:event.clientY,target:event.target});
    if(pointers.size===2){
      for(const [id,point] of pointers){point.target.dispatchEvent(new PointerEvent('pointercancel',{pointerId:id,bubbles:true}));}
      const initial=pair();gesture={kind:'pinch',...initial,view:{...view}};
    }else if(!interactive(event.target))gesture={kind:'pan',start:local(event),view:{...view}};
    else return;
    event.preventDefault();event.stopPropagation();viewport.setPointerCapture(event.pointerId);viewport.classList.add('is-panning');
  },true);
  viewport.addEventListener('pointermove',event=>{
    if(!pointers.has(event.pointerId))return;
    const current=pointers.get(event.pointerId);pointers.set(event.pointerId,{...current,clientX:event.clientX,clientY:event.clientY});
    if(!gesture)return;
    event.preventDefault();event.stopPropagation();
    if(gesture.kind==='pinch'&&pointers.size>=2){const next=pair();view=zoomDiagramAt(gesture.view,gesture.center,gesture.view.zoom*next.distance/Math.max(gesture.distance,1));view.x+=next.center.x-gesture.center.x;view.y+=next.center.y-gesture.center.y;}
    else if(gesture.kind==='pan'){const point=local(event);view={...gesture.view,x:gesture.view.x+point.x-gesture.start.x,y:gesture.view.y+point.y-gesture.start.y};}
    apply();
  },true);
  const finish=event=>{
    // Synthetic cancellation ends component/wire drags when a pinch begins.
    if(!event.isTrusted&&event.type==='pointercancel')return;
    pointers.delete(event.pointerId);
    if(!gesture)return;
    event.stopPropagation();gesture=null;viewport.classList.remove('is-panning');
  };
  viewport.addEventListener('pointerup',finish,true);viewport.addEventListener('pointercancel',finish,true);
  viewport.addEventListener('wheel',event=>{event.preventDefault();view=zoomDiagramAt(view,local(event),view.zoom*Math.exp(-event.deltaY*(event.deltaMode===1?0.04:0.0015)));apply();},{passive:false});
  apply();
}
