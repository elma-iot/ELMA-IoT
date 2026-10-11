export async function addDisarmPin(request, body) {
 const security=await request('/api/security');
 if(!security.enabled)return true;
 return new Promise(resolve=>{
  const dialog=document.createElement('dialog');
  dialog.className='capture-pin-dialog';
  dialog.innerHTML='<form method="dialog"><h3>Disarm motion capture</h3><p>Enter the device unlock PIN.</p><input type="password" inputmode="numeric" pattern="[0-9]{4}" maxlength="4" autocomplete="off" required aria-label="Unlock PIN"><div><button type="button" data-cancel>Cancel</button><button type="submit">Disarm</button></div></form>';
  const input=dialog.querySelector('input');let submitted=false;
  dialog.querySelector('form').onsubmit=event=>{event.preventDefault();if(!/^\d{4}$/.test(input.value))return;body.set('pin',input.value);submitted=true;input.value='';dialog.close();};
  dialog.querySelector('[data-cancel]').onclick=()=>dialog.close();
  dialog.onclose=()=>{input.value='';dialog.remove();resolve(submitted);};
  document.body.append(dialog);dialog.showModal();input.focus();
 });
}
