/** Search enhancement preserves select values and its existing change handler. */
export function matchesCountry(label, value, query) {
  const fold = text => String(text).normalize('NFKD').toLocaleLowerCase().replace(/\p{M}/gu, '');
  return fold(`${label} ${value}`).includes(fold(query).trim());
}
export function installCountrySearch(select) {
  if (!select || select.dataset.countrySearch) return;
  select.dataset.countrySearch = 'true';
  select.setAttribute('aria-haspopup', 'dialog');
  const popup = document.createElement('dialog');
  popup.setAttribute('aria-label', 'Search countries');
  popup.innerHTML = '<input type="search" aria-label="Search countries" placeholder="Search countries" autocomplete="off"><div role="listbox" aria-label="Countries"></div>';
  Object.assign(popup.style, {padding:'10px', border:'1px solid var(--line, #8191a6)', borderRadius:'12px', background:'var(--panel, #222c39)', color:'var(--ink, #edf2fb)', maxWidth:'calc(100vw - 24px)', boxSizing:'border-box'});
  const input = popup.querySelector('input'), results = popup.querySelector('div');
  Object.assign(input.style, {width:'100%', boxSizing:'border-box', marginBottom:'8px', fontSize:'16px'});
  Object.assign(results.style, {overflowY:'auto', maxHeight:'45vh', overscrollBehavior:'contain'});
  document.body.append(popup);
  function close() { popup.close(); select.setAttribute('aria-expanded', 'false'); select.focus({preventScroll:true}); }
  function render() {
    results.replaceChildren();
    for (const option of select.options) {
      if (option.disabled) continue;
      let label=option.textContent;
      try { if(option.dataset.countryCode) label=`${new Intl.DisplayNames([document.documentElement.lang||'en'],{type:'region'}).of(option.dataset.countryCode)} (${option.dataset.stationCount})`; } catch {}
      if(!matchesCountry(label,option.value,input.value))continue;
      const button = document.createElement('button'); button.type='button'; button.className='secondary'; button.role='option';
      button.textContent=label; button.setAttribute('aria-selected', String(option.value===select.value));
      Object.assign(button.style,{display:'block',width:'100%',textAlign:'start',minHeight:'44px',margin:'2px 0'});
      button.addEventListener('click',()=>{select.value=option.value;select.dispatchEvent(new Event('change',{bubbles:true}));close();});
      results.append(button);
    }
    if (!results.children.length) { const empty=document.createElement('p');empty.textContent='No countries found';results.append(empty); }
  }
  function open(event) {
    if (select.disabled) return;
    event.preventDefault(); event.stopPropagation(); if(popup.open)return;
    input.value='';render();popup.style.width=`${Math.max(240, Math.min(480,select.getBoundingClientRect().width))}px`;
    popup.showModal();select.setAttribute('aria-expanded','true');input.focus({preventScroll:true});
  }
  // Finish the activation tap before mounting a modal. Opening on pointerdown
  // retargets pointerup/click to the backdrop and dismisses it on touch devices.
  select.addEventListener('pointerdown',event=>{if(event.isPrimary!==false&&event.button===0)event.preventDefault();});
  select.addEventListener('mousedown',event=>event.preventDefault());
  select.addEventListener('click',open);
  select.addEventListener('keydown',event=>{if(['Enter',' ','ArrowDown','ArrowUp'].includes(event.key))open(event);});
  input.addEventListener('input',render);
  input.addEventListener('keydown',event=>{if(event.key==='ArrowDown'){results.querySelector('button')?.focus();event.preventDefault();}});
  results.addEventListener('keydown',event=>{const buttons=[...results.querySelectorAll('button')],index=buttons.indexOf(document.activeElement);if(event.key==='ArrowDown'||event.key==='ArrowUp'){buttons[(index+(event.key==='ArrowDown'?1:-1)+buttons.length)%buttons.length]?.focus();event.preventDefault();}});
  popup.addEventListener('cancel',event=>{event.preventDefault();close();});
  popup.addEventListener('click',event=>{if(event.target===popup){const r=popup.getBoundingClientRect();if(event.clientX<r.left||event.clientX>r.right||event.clientY<r.top||event.clientY>r.bottom)close();}});
}
