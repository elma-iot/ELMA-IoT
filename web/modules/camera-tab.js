export function createCameraTab({request}) {
  const panel=document.getElementById('tab-camera'),tab=document.querySelector('[data-tab="camera"]');
  if(!panel||!tab)return {setActive(){},update(){}};
  const feed=panel.querySelector('img'),message=panel.querySelector('[data-camera-status]'),dialog=panel.querySelector('dialog'),form=panel.querySelector('[data-camera-controls]');
  let active=false,available=false,timer=0,controller=null,url='',busy=false;
  const fields=[['framesize','Resolution',[[0,'160 × 120'],[1,'320 × 240'],[2,'640 × 480']]],['quality','JPEG quality (lower is better)',[10,40]],['brightness','Brightness',[-2,2]],['contrast','Contrast',[-2,2]],['saturation','Saturation',[-2,2]],['hmirror','Mirror'],['vflip','Flip'],['awb','Auto white balance'],['aec','Auto exposure'],['agc','Auto gain']];
  for(const [key,title,range] of fields){
    const label=document.createElement('label');label.textContent=title;const input=document.createElement(key==='framesize'?'select':'input');input.name=key;
    if(key==='framesize')for(const [value,text] of range){const option=document.createElement('option');option.value=value;option.textContent=text;input.append(option);}
    else {input.type=range?'range':'checkbox';if(range){input.min=range[0];input.max=range[1];input.step=1;}}
    label.append(input);form.append(label);
    input.addEventListener('change',async()=>{input.disabled=true;try{await request('/api/camera/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({name:key,value:input.type==='checkbox'?Number(input.checked):input.value}).toString()});message.textContent='Camera settings applied';}catch(e){message.textContent=e.message;}finally{input.disabled=false;}});
  }
  panel.querySelector('[data-camera-settings]').addEventListener('click',async()=>{dialog.showModal();try{const status=await request('/api/camera/status');for(const [key] of fields){const input=form.elements.namedItem(key);if(input.type==='checkbox')input.checked=!!status[key];else input.value=status[key];}form.elements.namedItem('framesize').options[2].disabled=!status.psram;message.textContent=status.ready?'Live camera':'Camera not ready'+(status.errorCode?` (${status.errorCode})`:'');}catch(e){message.textContent=e.message;}});
  dialog.querySelector('[data-close]').addEventListener('click',()=>dialog.close());
  async function frame(){
    if(!active||!available||document.hidden||busy)return;
    busy=true;controller=new AbortController();
    try{const response=await fetch(`/api/camera/frame?t=${Date.now()}`,{cache:'no-store',signal:controller.signal});if(!response.ok)throw Error(response.status===503?'Camera warming up or unavailable':`Camera: HTTP ${response.status}`);const blob=await response.blob();if(active&&available){const next=URL.createObjectURL(blob);feed.src=next;if(url)URL.revokeObjectURL(url);url=next;message.textContent='Live camera';}}
    catch(e){if(e.name!=='AbortError')message.textContent=e.message;}
    finally{busy=false;controller=null;if(active&&available&&!document.hidden)timer=setTimeout(frame,200);}
  }
  function stop(){clearTimeout(timer);controller?.abort();feed.removeAttribute('src');if(url)URL.revokeObjectURL(url);url='';if(dialog.open)dialog.close();}
  document.addEventListener('visibilitychange',()=>{if(document.hidden)stop();else if(active)frame();});
  return {setActive(value){active=value;if(value)frame();else stop();},update(status){available=!!status?.firmware?.camera&&!status?.system?.webUiLocked;tab.hidden=!available;if(!available)stop();else if(active&&!busy){clearTimeout(timer);frame();}}};
}
