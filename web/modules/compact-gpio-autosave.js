// The compact interface shares the same deferred settings API as other boards.
export function bindCompactGpioAutosave({form,ready,save,onError,setTimer=setTimeout,clearTimer=clearTimeout}){
 let timer=null,pending=null,active=null;
 function snapshot(){
  const f=form.elements,pin=Number(f.pin.value),green=Number(f.green.value),blue=Number(f.blue.value),kind=f.kind.value;
  if(kind==='rgb'&&pin>=0&&pin!==255&&(new Set([pin,green,blue]).size!==3||[green,blue].some(p=>p<0||p===255))){
   throw Error('Choose three different GPIOs for the RGB LED.');
  }
  return {device:{statusLedPin:pin,statusLedType:kind,statusLedActiveLow:f.activeLow.checked,statusLedGreenPin:green,statusLedBluePin:blue}};
 }
 function flush(){
  if(timer!==null){clearTimer(timer);timer=null;}
  if(active)return active;
  active=(async()=>{while(pending){const data=pending;pending=null;try{await save(data);}catch(error){onError(error.message);}}})().finally(()=>{active=null;});
  return active;
 }
 function queue(){
  if(!ready())return false;
  if(timer!==null){clearTimer(timer);timer=null;}
  try{pending=snapshot();}catch(error){pending=null;onError(error.message);return false;}
  timer=setTimer(flush,150);return true;
 }
 form.addEventListener('change',queue);
 form.onsubmit=event=>{event.preventDefault();if(queue())return flush();};
 return {flush};
}
