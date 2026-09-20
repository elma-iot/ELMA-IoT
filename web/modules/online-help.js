const BASE='https://elma-iot.github.io/elma-iot-docs';
const SUPPORTED=new Set(['en','es','zh','hi','ar','pt','bn','ru','ja','de','fr','ko','tr','it','id','pl','uk','vi','th','fa']);
export function helpLocale(){const raw=String(window.ElmaI18n?.language||window.ElmaFirmwareI18n?.language||document.documentElement.lang||navigator.language||'en').toLowerCase().replace('_','-').split('-')[0];return SUPPORTED.has(raw)?raw:'en';}
export function helpIdForNode(node){if(node?.helpId)return node.helpId;const type=String(node?.type||'');const map={'flow.gate':'logics.gate','flow.sequence':'logics.sequence','flow.branch':'logics.branch','timing.delay':'logics.delay','timing.timer':'logics.timer','timing.repeat':'logics.repeat','timing.debounce':'logics.debounce','timing.cooldown':'logics.cooldown','condition.compare':'logics.compare','condition.if':'logics.if','mainboard.mqtt.publish':'mqtt.publish'};if(node?.peripheral?.profile){let value=String(node.peripheral.profile).split(':').at(-1).replaceAll('_','-');if(['buzzer','custom','es8388-audio-codec','wm8960-audio-codec'].includes(value))value=String(node.peripheral.group||'hardware').replaceAll('_','-')+'-'+value;return 'peripheral.'+value;}return map[type]||('logics.'+type.replaceAll('.','-').replaceAll('_','-'));}
export function helpUrl(helpId='home',locale=helpLocale()){const topic=String(helpId||'home').replace(/^\.+|\.+$/g,'');return BASE+'/'+locale+'/'+(topic==='home'?'':topic.split('.').map(encodeURIComponent).join('/')+'/');}
export function openOnlineHelp(helpId='home'){if(navigator.onLine===false){alert('Online documentation requires an Internet connection.');return false;}const url=helpUrl(helpId),opened=window.open(url,'_blank','noopener,noreferrer');if(!opened&&!window.ElmaAndroidConfig){alert('Unable to open online documentation. Check the Internet connection and allow pop-ups for this device.');return false;}return true;}
const TAB_HELP={gpio:'setup.gpio',logics:'logics.overview',motor:'setup.peripherals',playback:'audio.overview',effects:'audio.overview',wifi:'wifi',mqtt:'mqtt.overview',battery:'peripheral.voltage-divider',device:'web-interface',oled:'setup.peripherals',hardware:'web-interface','storage-internal':'setup.peripherals','storage-external':'setup.peripherals',migration:'setup.overview',firmware:'flash.ota',logs:'serial-monitor',info:'about'};
function bindContextHelp(element,helpId){
 if(!element||element.dataset.contextHelpId)return;
 const label=window.ElmaI18n?.t?.('Help / Documentation')||'Help / Documentation';
 element.dataset.contextHelpId=helpId;element.title=(element.title?element.title+'\n':'')+label+': right-click or long-press';
 element.addEventListener('contextmenu',event=>{event.preventDefault();event.stopPropagation();openOnlineHelp(helpId);});
 let timer=0,start=null;
 element.addEventListener('pointerdown',event=>{if(event.pointerType!=='touch')return;start={x:event.clientX,y:event.clientY};clearTimeout(timer);timer=setTimeout(()=>{timer=0;start=null;openOnlineHelp(helpId);},600);},{capture:true});
 element.addEventListener('pointermove',event=>{if(start&&Math.hypot(event.clientX-start.x,event.clientY-start.y)>8){clearTimeout(timer);timer=0;start=null;}},{capture:true});
 for(const type of ['pointerup','pointercancel'])element.addEventListener(type,()=>{clearTimeout(timer);timer=0;start=null;},{capture:true});
}
export function installOnlineHelpLinks(){
 if(document.getElementById('elma-online-help-style'))return;
 const style=document.createElement('style');style.id='elma-online-help-style';style.textContent='[data-context-help-id]{cursor:help}.logic-context-menu .online-help-menu{border-color:#ef8b00}';document.head.append(style);
 // Remove controls created by earlier builds without disturbing their headings.
 for(const button of document.querySelectorAll('.online-help-button'))button.remove();
 for(const row of document.querySelectorAll('.online-help-heading')){const heading=row.querySelector(':scope > h1,:scope > h2,:scope > h3');if(heading)row.replaceWith(heading);}
 for(const [tab,id] of Object.entries(TAB_HELP)){
  const panel=document.getElementById('tab-'+tab);if(!panel)continue;
  let heading=panel.querySelector(':scope > h1,:scope > h2,:scope > h3,:scope > .device-log-heading > h2');
  if(!heading){heading=document.createElement('h2');heading.textContent=panel.getAttribute('aria-label')||document.querySelector(`[aria-controls="tab-${tab}"]`)?.getAttribute('aria-label')||tab;panel.prepend(heading);}
  bindContextHelp(heading,id);
  bindContextHelp(document.querySelector(`[aria-controls="tab-${tab}"],[data-tab="${tab}"]`),id);
 }
}
