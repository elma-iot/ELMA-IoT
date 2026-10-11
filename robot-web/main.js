import {createRobotTab} from '../web/modules/robot-tab.js';
import {attachMedia} from '../web/modules/spk-media.js';
async function request(path,options={}){const response=await fetch(path,{...options,cache:'no-store'});const data=await response.json();if(!response.ok)throw Error(data.error||`Device HTTP ${response.status}`);return data;}
const controls=createRobotTab({request,board:()=> 'miniexco-v2'});controls.setActive(true);
const panel=document.getElementById('tab-robot');panel.querySelector('.robot-camera').classList.add('camera-viewport');
const media=attachMedia({request,panel,video:true,notice:panel.querySelector('[data-robot-status]')});
request('/api/status').then(status=>{media.update(status);media.setActive(true);}).catch(()=>{});
