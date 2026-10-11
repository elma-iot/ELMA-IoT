import {attachMedia} from './spk-media.js';
export function createCameraTab({request}) {
  const panel=document.getElementById('tab-camera'),tab=document.querySelector('[data-tab="camera"]');
  if(!panel||!tab)return {setActive(){},update(){}};
  const feed=panel.querySelector('img'),message=panel.querySelector('[data-camera-status]'),dialog=panel.querySelector('dialog'),form=panel.querySelector('[data-camera-controls]');
  const media=attachMedia({request,panel,video:true,notice:message});
  const integration=document.createElement('fieldset');integration.innerHTML='<legend>Home Assistant camera</legend><label><input type="checkbox" data-mjpeg-enabled> Enable local MJPEG stream</label><label>Maximum stream FPS <input type="number" min="1" max="10" value="5" data-mjpeg-fps></label><button type="button" data-mjpeg-save>Save stream settings</button><p data-mjpeg-diagnostics></p><p>In Home Assistant, add the MJPEG IP Camera integration with these URLs. This stream contains video only. Trust the device certificate on the Home Assistant host when using HTTPS. Device access protection also applies to the stream. RTSP and ONVIF are not enabled; pan/tilt hardware is not configured.</p>';
  const urls=document.createElement('p');for(const [title,path] of [['Stream','/api/camera/stream'],['Snapshot','/api/camera/frame']]){const label=document.createElement('label');label.textContent=title;const input=document.createElement('input');input.readOnly=true;input.value=new URL(path,location.origin).href;input.onclick=()=>input.select();label.append(input);urls.append(label);}integration.append(urls);form.after(integration);
  const enabled=integration.querySelector('[data-mjpeg-enabled]'),fps=integration.querySelector('[data-mjpeg-fps]'),diagnostics=integration.querySelector('[data-mjpeg-diagnostics]');
  function integrationStatus(status){enabled.checked=!!status.mjpegEnabled;fps.value=status.mjpegFps||5;diagnostics.textContent=`${status.mjpegEnabled?'Enabled':'Disabled'} · ${status.streamClients||0}/1 external clients · PTZ unavailable`;}
  integration.querySelector('[data-mjpeg-save]').onclick=async()=>{try{await request('/api/camera/integration',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({enabled:Number(enabled.checked),fps:fps.value}).toString()});integrationStatus(await request('/api/camera/status'));message.textContent='Camera integration settings saved';}catch(error){message.textContent=error.message;}};
  let active=false,available=false,timer=0,controller=null,url='',busy=false;
  const fields=[['framesize','Resolution',[[0,'160 × 120'],[1,'320 × 240'],[2,'640 × 480'],[3,'800 × 600'],[4,'1024 × 768'],[5,'1280 × 720'],[6,'1280 × 1024'],[7,'1600 × 1200']]],['quality','JPEG quality (lower is better)',[4,63]],['brightness','Brightness',[-2,2]],['contrast','Contrast',[-2,2]],['saturation','Saturation',[-2,2]],['hmirror','Mirror'],['vflip','Flip'],['awb','Auto white balance'],['aec','Auto exposure'],['agc','Auto gain'],['awb_gain','Awb Gain'],['wb_mode','Wb Mode',[0,4]],['aec2','Aec2'],['ae_level','Ae Level',[-2,2]],['aec_value','Aec Value',[0,1200]],['agc_gain','Agc Gain',[0,30]],['gainceiling','Gainceiling',[0,6]],['bpc','Bpc'],['wpc','Wpc'],['raw_gma','Raw Gma'],['lenc','Lenc'],['dcw','Dcw'],['colorbar','Colorbar'],['special_effect','Special Effect',[0,6]],['sharpness','Sharpness',[-2,2]]];
  for(const [key,title,range] of fields){
    const label=document.createElement('label');label.textContent=title;const input=document.createElement(key==='framesize'?'select':'input');input.name=key;
    if(key==='framesize')for(const [value,text] of range){const option=document.createElement('option');option.value=value;option.textContent=text;input.append(option);}
    else {input.type=range?'range':'checkbox';if(range){input.min=range[0];input.max=range[1];input.step=1;}}
    label.append(input);form.append(label);
    input.addEventListener('change',async()=>{input.disabled=true;try{await request('/api/camera/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({name:key,value:input.type==='checkbox'?Number(input.checked):input.value}).toString()});message.textContent='Camera settings applied';}catch(e){message.textContent=e.message;}finally{input.disabled=false;}});
  }
  panel.querySelector('[data-camera-settings]').addEventListener('click',async()=>{dialog.showModal();try{const status=await request('/api/camera/status');integrationStatus(status);for(const [key] of fields){const input=form.elements.namedItem(key);if(input.type==='checkbox')input.checked=!!status[key];else input.value=status[key];}for(const option of form.elements.namedItem('framesize').options)option.disabled=Number(option.value)>1&&!status.psram;message.textContent=status.ready?'Live camera':'Camera not ready'+(status.errorCode?` (${status.errorCode})`:'');}catch(e){message.textContent=e.message;}});
  panel.querySelector('[data-camera-fit]').onclick=()=>panel.querySelector('.camera-viewport').classList.toggle('camera-fill');
  dialog.querySelector('[data-close]').addEventListener('click',()=>dialog.close());
  async function frame(){
    if(!active||!available||document.hidden||busy)return;
    busy=true;controller=new AbortController();
    try{const response=await fetch(`/api/camera/frame?t=${Date.now()}`,{cache:'no-store',signal:controller.signal});if(!response.ok)throw Error(response.status===503?'Camera warming up or unavailable':`Camera: HTTP ${response.status}`);const blob=await response.blob();if(active&&available){const next=URL.createObjectURL(blob);feed.src=next;if(url)URL.revokeObjectURL(url);url=next;message.textContent='Live camera';}}
    catch(e){if(e.name!=='AbortError')message.textContent=e.message;}
    finally{busy=false;controller=null;if(active&&available&&!document.hidden)timer=setTimeout(frame,30);}
  }
  function stop(){clearTimeout(timer);controller?.abort();feed.removeAttribute('src');if(url)URL.revokeObjectURL(url);url='';if(dialog.open)dialog.close();}
  document.addEventListener('visibilitychange',()=>{if(document.hidden)stop();else if(active)frame();});
  return {setActive(value){media.setActive(value);active=value;if(value)frame();else stop();},update(status){media.update(status);available=!!status?.firmware?.camera&&!status?.system?.webUiLocked;tab.hidden=!available;if(!available)stop();else if(active&&!busy){clearTimeout(timer);frame();}}};
}
