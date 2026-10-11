import * as THREE from 'three';
import {GLTFLoader} from 'three/addons/loaders/GLTFLoader.js';
export function createBno055Tab({request}) {
 const tab=document.querySelector('[data-tab="bno055"]'),panel=document.getElementById('tab-bno055');
 const old=panel.querySelector('canvas'),notice=panel.querySelector('[data-notice]'),enabled=panel.querySelector('[data-enabled]'),mode=panel.querySelector('[data-mode]'),reset=panel.querySelector('button');
 const dashboard=document.createElement('div');dashboard.className='bno-dashboard';old.replaceWith(dashboard);
 const view=document.createElement('div');view.className='bno-view sensor-card';view.innerHTML='<h3>Orientation (3D View)</h3><button type="button" class="model-settings" aria-label="Model pivot settings" title="Model pivot settings">⚙</button><button type="button" class="model-upload" aria-label="Upload GLB model" title="Click model to upload GLB">＋</button>';dashboard.append(view);
 const viewport=document.createElement('div');viewport.className='bno-viewport';view.append(viewport);
 const metrics={};
 for(const [key,title,labels,unit] of [['euler','Euler Angles',['Heading (Yaw)','Roll','Pitch'],'°'],['quaternion','Quaternion',['W','X','Y','Z'],''],['linear','Linear Acceleration',['X','Y','Z'],'m/s²'],['gravity','Gravity Vector',['X','Y','Z'],'m/s²'],['accel','Accelerometer (Raw)',['X','Y','Z'],'m/s²'],['gyro','Gyroscope (Raw)',['X','Y','Z'],'°/s'],['mag','Magnetometer (Raw)',['X','Y','Z'],'µT']]){
  const card=document.createElement('div');card.className='sensor-card';const h=document.createElement('h3');h.textContent=title;card.append(h);metrics[key]=labels.map((label,i)=>{const row=document.createElement('div');row.className='sensor-value';const name=document.createElement('span');name.textContent=label;const v=document.createElement('strong');v.style.color=['#69cfff','#6bdd67','#ffc04d','#9abfff'][i];v.textContent='—';row.append(name,v);card.append(row);return {v,unit};});dashboard.append(card);
 }
 const file=document.createElement('input');file.type='file';file.accept='.glb';file.hidden=true;view.append(file);
 const dialog=document.createElement('dialog');dialog.className='camera-controls-dialog';dialog.innerHTML='<h3>Model pivots</h3><p>Rotate the model relative to the sensor axes.</p>'+['X','Y','Z'].map((axis,i)=>`<label>${axis} degrees<input data-axis="${i}" type="number" min="-360" max="360" value="0"></label>`).join('')+'<button data-save>Save</button><button data-close>Close</button>';panel.append(dialog);
 let active=false,available=false,busy=false,saving=false,timer,data=null,loadedPath='',alignment=[0,0,0],renderer,scene,camera,root,model,animation;
 try{
  renderer=new THREE.WebGLRenderer({antialias:true,alpha:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));viewport.append(renderer.domElement);renderer.domElement.setAttribute('aria-label','Click to upload orientation model');renderer.domElement.onclick=()=>file.click();
  scene=new THREE.Scene();camera=new THREE.PerspectiveCamera(40,1,.01,100);camera.position.set(3,2,4);camera.lookAt(0,0,0);root=new THREE.Group();scene.add(root);scene.add(new THREE.HemisphereLight(0xffffff,0x243040,3));const light=new THREE.DirectionalLight(0xffffff,3);light.position.set(3,4,5);scene.add(light);scene.add(new THREE.AxesHelper(1.6));
  model=new THREE.Mesh(new THREE.BoxGeometry(1.3,1.3,1.3),new THREE.MeshStandardMaterial({color:0x99b8e8,metalness:.15,roughness:.4}));root.add(model);
  new ResizeObserver(resize).observe(viewport);
 }catch(e){renderer=null;viewport.textContent='3D rendering unavailable: '+e.message;}
 function resize(){
  if(!renderer)return false;
  const width=viewport.clientWidth,height=viewport.clientHeight;
  if(width<1||height<1)return false;
  // CSS owns layout; updating the drawing buffer must never resize the card.
  const ratio=renderer.getPixelRatio();
  if(renderer.domElement.width!==Math.floor(width*ratio)||renderer.domElement.height!==Math.floor(height*ratio))renderer.setSize(width,height,false);
  camera.aspect=width/height;camera.updateProjectionMatrix();return true;
 }
 function disposeModel(){model?.traverse(o=>{o.geometry?.dispose();const materials=Array.isArray(o.material)?o.material:[o.material];materials.forEach(m=>m?.dispose());});if(model)root.remove(model);}
 async function load(path){if(!renderer||!path||path===loadedPath)return;loadedPath=path;
  try{const gltf=await new GLTFLoader().loadAsync('/api/storage/file?target=sd&path='+encodeURIComponent(path));disposeModel();model=gltf.scene;const box=new THREE.Box3().setFromObject(model),center=box.getCenter(new THREE.Vector3()),size=box.getSize(new THREE.Vector3());model.position.sub(center);const group=new THREE.Group();group.add(model);group.scale.setScalar(2/Math.max(size.x,size.y,size.z,.001));model=group;root.add(model);}catch(e){loadedPath='';notice.textContent='Model: '+e.message;}
 }
 function render(){if(!active||document.hidden)return;if(renderer&&root&&resize()){const q=data?.quaternion;if(data?.ready&&q?.length===4)root.quaternion.set(q[1],q[2],q[3],q[0]).normalize();else root.quaternion.identity();if(model)model.rotation.set(...alignment.map(v=>v*Math.PI/180));renderer.render(scene,camera);}animation=requestAnimationFrame(render);}
 view.querySelector('.model-upload').onclick=()=>file.click();view.querySelector('.model-settings').onclick=()=>{dialog.querySelectorAll('[data-axis]').forEach((input,i)=>input.value=alignment[i]);dialog.showModal();};dialog.querySelector('[data-close]').onclick=()=>dialog.close();dialog.querySelector('[data-save]').onclick=async()=>{const values=[...dialog.querySelectorAll('[data-axis]')].map(i=>Number(i.value));try{await save({alignment:values});alignment=values;dialog.close();}catch(e){notice.textContent=e.message;}};
 file.onchange=async()=>{const selected=file.files[0];if(!selected)return;try{if(!selected.name.toLowerCase().endsWith('.glb')||selected.size>16*1024*1024)throw Error('Choose a GLB model up to 16 MB');const status=await request('/api/storage?target=sd&dir=/');if(!status.storage?.mounted)throw Error('Mount an SD card first');if(!status.entries?.some(e=>e.name==='3d')){try{await request('/api/storage/mkdir?target=sd&dir=/',{method:'POST',body:JSON.stringify({name:'3d'})});}catch(e){if(!/already exists/i.test(e.message))throw e;}}const form=new FormData();form.append('file',selected,selected.name.replace(/[^a-zA-Z0-9_.-]/g,'_').replace(/\.GLB$/i,'.glb'));let result;for(let attempt=0;attempt<12;attempt++){const response=await fetch('/api/storage/upload?target=sd&dir=/3d',{method:'POST',body:form});result=await response.json();if(response.ok)break;if(result.error?.includes('Storage is busy')&&attempt<11){await new Promise(resolve=>setTimeout(resolve,250));continue;}throw Error(result.error||'Model upload failed');}const path=result.path||'/3d/'+selected.name;await save({model:path});await load(path);}catch(e){notice.textContent=e.message;}finally{file.value='';}};
 async function poll(){clearTimeout(timer);if(!active||!available||document.hidden||busy||saving)return;busy=true;
  try{data=await request('/api/bno055');enabled.checked=data.enabled;mode.value=data.mode;alignment=data.alignment||[0,0,0];load(data.model);const values={...data,euler:[data.heading,data.roll,data.pitch]};for(const [key,rows] of Object.entries(metrics))rows.forEach(({v,unit},i)=>v.textContent=data.ready&&data.ageMs<2000?`${Number(values[key]?.[i]||0).toFixed(key==='quaternion'?4:2)} ${unit}`:'—');notice.textContent=data.error||(!data.enabled?'Sampling paused':data.ready?`Sensor Online · Calibration: system ${data.calibration.system}, gyro ${data.calibration.gyro}, accel ${data.calibration.accel}, mag ${data.calibration.mag}`:'Waiting for sensor');}
  catch(e){notice.textContent=e.message;}finally{busy=false;if(active&&available)timer=setTimeout(poll,150);}}
 async function save(extra={}){if(saving)return;saving=true;try{await request('/api/bno055',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:enabled.checked,mode:mode.value,...extra})});}finally{saving=false;poll();}}
 enabled.onchange=()=>save().catch(e=>notice.textContent=e.message);mode.onchange=enabled.onchange;reset.onclick=()=>save({reinitialize:true}).catch(e=>notice.textContent=e.message);
 document.addEventListener('visibilitychange',()=>{cancelAnimationFrame(animation);if(!document.hidden){poll();render();}});
 return {setActive(value){active=value;clearTimeout(timer);cancelAnimationFrame(animation);if(value){poll();render();}else if(dialog.open)dialog.close();},update(settings,locked){let profiles=settings?.ui?.peripheralProfiles||{};if(typeof profiles==='string'){try{profiles=JSON.parse(profiles);}catch{profiles={};}}available=profiles.sensors?.includes('bno055')&&!locked;tab.hidden=panel.hidden=!available;if(!available){clearTimeout(timer);cancelAnimationFrame(animation);}else if(active&&!busy)poll();}};
}


