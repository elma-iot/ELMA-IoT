// Shift on desktop, or a stationary long press on touch, moves one wire endpoint.
export function wireGesture(element,move,start=null){
 let timer=null,down=null,started=false;
 const clear=()=>{clearTimeout(timer);timer=null;down=null;};
 element.style.touchAction='none';
 element.onpointerdown=e=>{
  if(e.button!==0)return;
  if(e.shiftKey){e.preventDefault();e.stopPropagation();move(e);return;}
  if(e.pointerType!=='touch'){start?.(e);return;}
  e.preventDefault();e.stopPropagation();down=e;started=false;element.setPointerCapture(e.pointerId);
  timer=setTimeout(()=>{timer=null;started=true;move(down);},450);
 };
 element.addEventListener('pointermove',e=>{if(timer&&Math.hypot(e.clientX-down.clientX,e.clientY-down.clientY)>10){const event=down;clear();started=true;start?.(event);}});
 element.addEventListener('pointerup',()=>clear());
 element.addEventListener('pointercancel',()=>clear());
 element.addEventListener('lostpointercapture',()=>clear());
}
