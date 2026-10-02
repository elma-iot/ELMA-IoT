// Editable notes shared by the device and local web Logics canvases.
import {id} from './logic-graph-model.js';

function bindEditing(element,editor,read,write,onDelete){
 let editing=false;
 const finish=()=>{
  if(!editing)return;
  editing=false;
  const value=element.textContent.trim().slice(0,128)||'Label';
  element.contentEditable='false';
  element.textContent=value;
  write(value);
  editor.changed(false);
 };
 element.beginLabelEdit=()=>{
  if(editing)return;
  editor.begin();editing=true;element.contentEditable='true';element.focus();
  const selection=window.getSelection(),range=document.createRange();range.selectNodeContents(element);selection.removeAllRanges();selection.addRange(range);
 };
 element.ondblclick=e=>{e.preventDefault();e.stopPropagation();element.beginLabelEdit();};
 element.onkeydown=e=>{e.stopPropagation();if(!editing&&['Delete','Backspace'].includes(e.key)){e.preventDefault();onDelete();return;}if(e.key==='Enter'){e.preventDefault();element.blur();}else if(e.key==='Escape'){e.preventDefault();element.textContent=read();element.blur();}};
 element.onblur=finish;
 return ()=>editing;
}

export function renderCanvasLabels(editor,stage,origin,scene,openMenu,refreshExtent,groups,groupBounds){
 const elements=new Map();
 for(const label of editor.graph.view?.labels||[]){
  const group=editor.graph.groups?.find(item=>item.id===label.groupId),parent=group&&groups.get(group.id);
  const element=document.createElement('div');element.className='logic-text-label';element.classList.toggle('logic-attached-label',!!parent);element.textContent=label.text;element.style.left=label.x+(parent?0:origin.x)+'px';element.style.top=label.y+(parent?0:origin.y)+'px';element.tabIndex=0;
  const editing=bindEditing(element,editor,()=>label.text,value=>{label.text=value;},()=>{editor.begin();editor.graph.view.labels=editor.graph.view.labels.filter(item=>item.id!==label.id);editor.changed();});
  let drag=null;
  element.onpointerdown=e=>{e.stopPropagation();if(e.button!==0||editing())return;e.preventDefault();editor.begin();drag={at:scene(e),x:label.x,y:label.y};element.setPointerCapture(e.pointerId);};
  element.onpointermove=e=>{if(!drag)return;const at=scene(e);label.x=drag.x+at.x-drag.at.x;label.y=drag.y+at.y-drag.at.y;element.style.left=label.x+(parent?0:origin.x)+'px';element.style.top=label.y+(parent?0:origin.y)+'px';refreshExtent();};
  element.onpointerup=e=>{if(!drag)return;drag=null;element.releasePointerCapture(e.pointerId);if(parent){const bounds=groupBounds(group),x=bounds.x+label.x+element.offsetWidth/2,y=bounds.y+label.y+element.offsetHeight/2;if(x<bounds.x||x>bounds.x+bounds.width||y<bounds.y||y>bounds.y+bounds.height){label.x+=bounds.x;label.y+=bounds.y;delete label.groupId;editor.changed();return;}}editor.changed(false);};
  element.onpointercancel=()=>{if(drag){drag=null;editor.changed(false);}};
  element.oncontextmenu=e=>{e.preventDefault();e.stopPropagation();openMenu(e,{kind:'canvas',record:label,element});};
  (parent||stage).append(element);elements.set(label.id,element);
 }
 return elements;
}

export function upgradeGroupLabels(graph){
 graph.view??={};graph.view.labels??=[];
 for(const group of graph.groups||[])if(group.label){graph.view.labels.push({id:id(),text:group.label,x:14,y:32,groupId:group.id});delete group.label;}
}

export function attachGroupLabel(editor,group,box,openMenu){
 if(!group.label)return null;
 const element=document.createElement('span');element.className='logic-group-label';element.textContent=group.label;element.tabIndex=0;box.append(element);
 bindEditing(element,editor,()=>group.label,value=>{group.label=value;},()=>{editor.begin();delete group.label;editor.changed();});
 element.onpointerdown=e=>e.stopPropagation();
 element.oncontextmenu=e=>{e.preventDefault();e.stopPropagation();openMenu(e,{kind:'group',group,element});};
 return element;
}
