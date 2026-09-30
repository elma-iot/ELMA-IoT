// The ESP is authoritative. No PIN, unlock flag or retry counter is persisted
// in browser storage. Polls observe state; only real interaction sends activity.
export function createSecurityInterface({root=document,fetcher=window.fetch.bind(window),reload=()=>location.reload()}={}){
 const lower=root.querySelector('.grid'),hero=root.querySelector('.hero'),panel=root.getElementById('tab-security');
 if(!lower||!panel)return null;
 const mask=root.createElement('div');mask.className='security-mask';mask.setAttribute('role','dialog');mask.setAttribute('aria-modal','true');mask.setAttribute('aria-label','Unlock interface');
 mask.innerHTML='<form class="security-dialog"><h2 data-title>Interface locked</h2><p data-tip>Enter your four-digit PIN.</p><input data-pin type="password" inputmode="numeric" pattern="[0-9]{4}" maxlength="4" autocomplete="off" aria-label="Four-digit PIN"><div class="security-keypad"></div><p data-error role="status" aria-live="polite"></p><button type="submit" data-submit>Continue</button><button type="button" class="secondary" data-cancel hidden>Cancel</button></form>';
 lower.append(mask);
 const form=mask.querySelector('form'),pin=mask.querySelector('[data-pin]'),tip=mask.querySelector('[data-tip]'),title=mask.querySelector('[data-title]'),error=mask.querySelector('[data-error]'),submit=mask.querySelector('[data-submit]'),cancel=mask.querySelector('[data-cancel]');
 const timeout=panel.querySelector('[data-security-timeout]'),summary=panel.querySelector('[data-security-state]');
 let state=null,flow='',stage='',first='',ticket='',busy=false,stopped=false,retryUntil=0,lastActivity=0,ready=false,refreshTimer,timer;
 const disabledBefore=new Map();
 const endpoint=async(body)=>{const response=await fetcher('/api/security',{method:body?'POST':'GET',cache:'no-store',headers:body?{'Content-Type':'application/json','X-ELMA-Security':'1'}:{},...(body?{body:JSON.stringify(body)}:{})});let data;if(response.status===404){const e=Error('Security is available on the ESP web interface.');e.unavailable=true;throw e;}try{data=await response.json();}catch{throw Error('Security service unavailable');}if(data.locked!==undefined)apply(data);if(!response.ok)throw Error(data.error||'Security request failed');return data;};
 function protect(blocked){
  lower.classList.toggle('security-blocked',blocked);for(const child of lower.children)if(child!==mask){child.inert=blocked;child.setAttribute('aria-hidden',String(blocked));}
  for(const control of hero?.querySelectorAll('button,input,select,a')||[]){if(blocked){if(!disabledBefore.has(control))disabledBefore.set(control,{disabled:control.disabled,tabIndex:control.tabIndex});control.disabled=true;control.tabIndex=-1;control.setAttribute('aria-disabled','true');}else if(disabledBefore.has(control)){const old=disabledBefore.get(control);control.disabled=old.disabled;control.tabIndex=old.tabIndex;control.removeAttribute('aria-disabled');disabledBefore.delete(control);}}
  const menu=root.getElementById('headerActionsMenu');if(blocked&&menu){menu.dataset.open='false';menu.setAttribute('aria-hidden','true');}const gear=root.getElementById('headerActionsButton');if(blocked&&gear)gear.setAttribute('aria-expanded','false');
 }
 function apply(data){
  const wasLocked=state?.locked;state=data;ready=true;
  const tab=root.querySelector('[data-tab="security"]');
  if(tab){const label=data.enabled?'Security — PIN set':'Security — PIN not set';tab.title=label;tab.dataset.tooltip=label;tab.setAttribute('aria-label',label);tab.dataset.pinEnabled=String(!!data.enabled);tab.querySelector('[data-security-shackle]')?.setAttribute('d',data.enabled?'M8 10V6a4 4 0 0 1 8 0v4':'M8 10V6a4 4 0 0 1 8 0');}
  retryUntil=Date.now()+Number(data.retryAfterSeconds||0)*1000;
  if(root.activeElement!==timeout)timeout.value=data.timeoutSeconds||300;
  summary.textContent=data.available===false?'Security storage unavailable':data.enabled?(data.locked?'Locked for all browsers':'Unlocked for all browsers'):'PIN lock disabled';
  for(const button of panel.querySelectorAll('[data-security-action]')){const action=button.dataset.securityAction;button.hidden=action==='set'?data.enabled:!data.enabled;}
  if(wasLocked&&!data.locked&&!flow){protect(false);mask.hidden=true;reload();return;}
  render();
 }
 function render(){
  const blocked=!ready||!!state?.locked||!!flow;protect(blocked);mask.hidden=!blocked;
  if(!blocked)return;
  title.textContent=flow==='change'?'Change PIN':flow==='disable'?'Disable PIN lock':flow==='set'?'Set PIN':'Interface locked';
  tip.textContent=!ready?'Checking device lock…':state?.available===false?'Security storage unavailable. Retry after checking the device.':stage==='old'?'Enter the old PIN.':stage==='new'?'Enter a new four-digit PIN.':stage==='confirm'?'Enter the new PIN again.':state?.enabled===false?'Interface locked by a device command. Unlock through the same device control.':'Enter your four-digit PIN to unlock for all browsers.';
  const wait=Math.max(0,Math.ceil((retryUntil-Date.now())/1000));
  if(wait)error.textContent=`Too many attempts. Try again in ${Math.floor(wait/60)}:${String(wait%60).padStart(2,'0')}.`;
  pin.disabled=busy||!ready||wait>0||state?.available===false||(!state?.enabled&&!flow);submit.disabled=pin.disabled;
  for(const b of mask.querySelectorAll('.security-keypad button'))b.disabled=pin.disabled;
  cancel.hidden=!flow;cancel.disabled=busy;
 }
 function resetFlow(){flow='';stage='';first='';ticket='';pin.value='';error.textContent='';render();}
 async function start(action){
  if(busy||state?.locked)return;flow=action;stage=action==='set'?'new':'old';pin.value='';error.textContent='';first='';ticket='';busy=true;render();
  try{if(action!=='set')await endpoint({action:'lock'});}catch(e){error.textContent=e.message;resetFlow();}finally{busy=false;render();pin.focus();}
 }
 form.addEventListener('submit',async event=>{event.preventDefault();if(busy||pin.disabled)return;const value=pin.value;pin.value='';error.textContent='';if(!/^\d{4}$/.test(value)){error.textContent='Enter exactly four digits.';return;}
  busy=true;render();
  try{
   if(stage==='new'){first=value;stage='confirm';}
   else if(stage==='confirm'){
    if(value!==first){first='';stage='new';error.textContent='PINs did not match. Enter a new PIN again.';}
    else {await endpoint({action:flow,newPin:first,confirmPin:value,ticket,timeoutSeconds:Number(timeout.value)});resetFlow();reload();}
   }else if(flow==='change'){const result=await endpoint({action:'verify',pin:value});ticket=result.ticket;stage='new';}
   else if(flow==='disable'){await endpoint({action:'disable',pin:value});resetFlow();reload();}
   else {await endpoint({action:'unlock',pin:value});resetFlow();}
  }catch(e){error.textContent=e.message;if(e.message.includes('Verify the old PIN')){stage='old';first='';ticket='';}}
  finally{busy=false;render();pin.focus();}
 });
 pin.addEventListener('input',()=>{pin.value=pin.value.replace(/\D/g,'').slice(0,4);});
 for(const key of ['1','2','3','4','5','6','7','8','9','Clear','0','Back']){const button=root.createElement('button');button.type='button';button.textContent=key;button.setAttribute('aria-label',key==='Back'?'Delete last digit':key);button.onclick=()=>{pin.value=key==='Clear'?'':key==='Back'?pin.value.slice(0,-1):(pin.value+key).slice(0,4);pin.focus();};mask.querySelector('.security-keypad').append(button);}
 cancel.onclick=resetFlow;
 for(const button of panel.querySelectorAll('[data-security-action]'))button.onclick=async()=>{if(button.dataset.securityAction==='lock'){try{await endpoint({action:'lock'});pin.focus();}catch(e){summary.textContent=e.message;}}else start(button.dataset.securityAction);};
 panel.querySelector('[data-security-save-timeout]').onclick=async()=>{try{await endpoint({action:'timeout',timeoutSeconds:Number(timeout.value)});}catch(e){summary.textContent=e.message;}};
 const activity=event=>{if(!event.isTrusted||!state?.enabled||state.locked||flow||Date.now()-lastActivity<5000)return;lastActivity=Date.now();endpoint({action:'activity'}).catch(()=>{});};
 for(const name of ['pointerdown','pointermove','keydown','wheel','touchstart'])root.addEventListener(name,activity,{passive:true,capture:true});
 // Reject gear/header interaction in capture phase even if another renderer
 // updates its disabled attributes while status is being refreshed.
 hero?.addEventListener('click',event=>{if(!ready||state?.locked||flow){if(event.target.closest('button,input,select,a')){event.preventDefault();event.stopImmediatePropagation();}}},true);
 mask.addEventListener('keydown',event=>{if(event.key!=='Tab')return;const focusable=[...mask.querySelectorAll('input,button')].filter(x=>!x.disabled&&!x.hidden);if(!focusable.length)return;const first=focusable[0],last=focusable.at(-1);if(event.shiftKey&&root.activeElement===first){last.focus();event.preventDefault();}else if(!event.shiftKey&&root.activeElement===last){first.focus();event.preventDefault();}});
 async function poll(){if(stopped)return;try{await endpoint();}catch(e){if(e.unavailable){ready=true;state={enabled:false,locked:false};protect(false);mask.hidden=true;summary.textContent=e.message;for(const control of panel.querySelectorAll('button,input'))control.disabled=true;stopped=true;return;}ready=false;error.textContent=e.message;render();}finally{if(!stopped)refreshTimer=setTimeout(poll,2000);}}
 // Header stays readable even when application startup cannot fetch settings.
 async function header(){if(stopped)return;if(state?.locked)try{const response=await fetcher('/api/status',{cache:'no-store'});const data=await response.json();const title=root.getElementById('deviceTitle');if(title&&data.device)title.textContent=data.device.friendlyName||data.device.deviceName||'ELMA IoT';}catch{}setTimeout(header,4000);}
 render();poll();header();timer=setInterval(render,1000);
 return {refresh:poll,destroy(){stopped=true;clearTimeout(refreshTimer);clearInterval(timer);for(const name of ['pointerdown','pointermove','keydown','wheel','touchstart'])root.removeEventListener(name,activity,true);mask.remove();protect(false);},get state(){return state;}};
}
if(typeof document!=='undefined')window.elmaSecurity=createSecurityInterface();
