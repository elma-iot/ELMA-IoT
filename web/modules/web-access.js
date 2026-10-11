export function installWebAccess({request}) {
  const output=document.querySelector('[data-web-access-diagnostics]');
  if(!output)return;
  const context=()=>`Browser secure context: ${window.isSecureContext?'yes':'no'}; microphone API: ${navigator.mediaDevices?.getUserMedia?'available':'unavailable'}. HTTP .local is not localhost.`;
  output.textContent=context();
  document.querySelector('[data-web-access-check]')?.addEventListener('click',async()=>{
    try {
      const data=await request('/api/web-access');
      output.textContent=`${context()}\n${JSON.stringify(data,null,2)}`;
      const link=document.querySelector('[data-web-access-open]');
      if(link && data.httpsUrl){link.href=data.httpsUrl;link.hidden=false;}
    } catch(error){output.textContent=`${context()}\n${error.message}\nConnect to the board to inspect its HTTPS listener.`;}
  });
  document.querySelector('[data-web-access-provision]')?.addEventListener('click',async()=>{
    const button=document.querySelector('[data-web-access-provision]');
    const cert=document.querySelector('[data-web-access-certificate]')?.files?.[0], key=document.querySelector('[data-web-access-key]')?.files?.[0];
    if(!cert || !key){output.textContent='Select the device certificate and matching device private key. Never upload your CA private key.';return;}
    if(cert.size>8192 || key.size>4096){output.textContent='Certificate maximum: 8 KiB; device key maximum: 4 KiB.';return;}
    if(!window.isSecureContext && !window.confirm('This connection is HTTP. Certificate provisioning sends the device private key over this LAN. Continue only on a trusted private network; USB provisioning is preferable.'))return;
    button.disabled=true;
    try {const data=await request('/api/web-access/certificate',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({certificate:await cert.text(),privateKey:await key.text()})});output.textContent=data.message || 'Certificate saved.';}
    catch(error){output.textContent=error.message;}
    finally {button.disabled=false;document.querySelector('[data-web-access-key]').value='';}
  });
}
