/** Keep mobile menus and transformed canvas inputs inside the live viewport. */
export function installLogicLayout(tab){
 const viewport=document.querySelector('[data-logic-canvas]');
 const originalMenu=tab.editor.menu;
 tab.editor.menu=function(...args){
  originalMenu.apply(this,args);
  const menu=document.querySelector('.logic-context-menu'),search=menu?.querySelector('#logic-add-node-search'),results=menu?.querySelector('.logic-menu-results');
  if(search){menu.prepend(search);if(results)search.after(results);if(args[1]||args[2]||args[5]||tab.editor.selected.size)search.blur();}
  keepVisible();
 };
 function keepVisible(){
  const visual=window.visualViewport,top=visual?.offsetTop||0,bottom=Math.min(innerHeight,top+(visual?.height||innerHeight));
  const menu=document.querySelector('.logic-context-menu');
  if(menu){menu.style.maxHeight=Math.max(80,bottom-top-24)+'px';const r=menu.getBoundingClientRect();menu.style.top=Math.max(top+12,Math.min(r.top,bottom-r.height-12))+'px';menu.style.left=Math.max(12,Math.min(r.left,innerWidth-r.width-12))+'px';}
  const field=document.activeElement;
  if(!field?.matches('input,textarea,select')||!field.closest('#tab-logics,.logic-context-menu'))return;
  const r=field.getBoundingClientRect(),canvas=viewport.getBoundingClientRect();
  if(field.closest('.logic-node')){
   const visibleBottom=Math.min(canvas.bottom,bottom)-16,visibleTop=Math.max(canvas.top,top)+16;
   if(r.bottom>visibleBottom)viewport.scrollTop+=r.bottom-visibleBottom;
   else if(r.top<visibleTop)viewport.scrollTop-=visibleTop-r.top;
   if(r.left<canvas.left+16)viewport.scrollLeft-=canvas.left+16-r.left;
   else if(r.right>canvas.right-16)viewport.scrollLeft+=r.right-canvas.right+16;
  }
  field.scrollIntoView({block:'nearest',inline:'nearest',behavior:'instant'});
 }
 let pending=0;
 const schedule=()=>{cancelAnimationFrame(pending);pending=requestAnimationFrame(keepVisible);};
 document.addEventListener('focusin',schedule);
 window.addEventListener('resize',schedule);window.addEventListener('elma-window-metrics-changed',schedule);
 window.visualViewport?.addEventListener('resize',schedule);window.visualViewport?.addEventListener('scroll',schedule);
}
