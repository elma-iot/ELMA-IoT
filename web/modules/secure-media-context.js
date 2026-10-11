export async function requireSecureMedia(request) {
  if (window.isSecureContext && navigator.mediaDevices?.getUserMedia) return true;
  const dialog=document.createElement('dialog');dialog.className='camera-controls-dialog';
  const title=document.createElement('h3');title.textContent='Open a trusted HTTPS connection';
  const text=document.createElement('p');text.textContent='Browser microphone access requires a trusted secure connection. HTTP at a .local name does not qualify. Install this device’s local CA on your device, then open HTTPS.';
  const open=document.createElement('button');open.type='button';open.textContent='Open HTTPS';
  const cancel=document.createElement('button');cancel.type='button';cancel.textContent='Cancel';cancel.className='secondary';
  let destination;
  try {const status=await request('/api/web-access');if(status.httpsListening){destination=new URL(location.href);destination.protocol='https:';destination.port=String(status.httpsPort||443);}else{text.textContent+=' Enable HTTPS in Configuration first.';open.disabled=true;}}
  catch {open.disabled=true;text.textContent+=' Connection diagnostics are unavailable.';}
  dialog.append(title,text,open,cancel);document.body.append(dialog);
  open.onclick=()=>{if(destination)location.assign(destination.href);};cancel.onclick=()=>dialog.close();dialog.onclose=()=>dialog.remove();dialog.showModal();return false;
}
