// Device-owned state synchronizes the LCD and every open browser.
export function createSdFormatDialog({ request, document = globalThis.document }) {
  let modal, previous = 0;
  function close() { if (modal) { modal.remove(); modal = null; } }
  function update(status) {
    const state = status?.sdFormat;
    if (!state || status.system?.webUiLocked) { close(); return; }
    const busy = state.state === 1 || state.state === 2;
    const mode = busy ? 'busy' : state.prompt ? 'prompt' : '';
    if (!mode) {
      close();
      if ((previous === 1 || previous === 2) && state.state >= 3) {
        const result = document.createElement('div');
        result.setAttribute('role', 'status');
        result.textContent = state.state === 3 ? 'SD card ready.' : 'Formatting failed. Check the card on a computer.';
        Object.assign(result.style, { position:'fixed', bottom:'20px', left:'20px', zIndex:10001, background:'#1f2937', color:'#fff', padding:'16px' });
        document.body.append(result);setTimeout(() => result.remove(),8000);
      }
      previous = state.state;return;
    }
    previous = state.state;
    if (modal?.dataset.mode === mode) return;
    close();modal = document.createElement('dialog');modal.dataset.mode = mode;
    const title = document.createElement('h2');title.textContent = busy ? 'Formatting SD card' : 'SD card detected';
    const text = document.createElement('p');
    text.textContent = busy ? 'Keep power connected. Please wait…' : 'No supported filesystem was found. Formatting erases all files. Back up the card first. Format now?';
    modal.append(title,text);
    modal.addEventListener('cancel', event => event.preventDefault());
    if (busy) { const progress=document.createElement('progress');progress.setAttribute('aria-label','Formatting SD card');modal.append(progress); }
    else for (const [label,action] of [['Cancel','cancel'],['Erase and format','confirm&erase=yes']]) {
      const button=document.createElement('button');button.textContent=label;
      button.onclick=async()=>{
        const current=modal;for(const b of current.querySelectorAll('button'))b.disabled=true;
        try { await request(`/api/storage/format?action=${action}`,{method:'POST'});close();if(action.startsWith('confirm'))update({sdFormat:{state:1,prompt:false}}); }
        catch(error){text.textContent=error.message;for(const b of current.querySelectorAll('button'))b.disabled=false;}
      };modal.append(button);
    }
    document.body.append(modal);modal.showModal();
  }
  return { update };
}
