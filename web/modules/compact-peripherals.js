// Compact boards use the same image-owned driver list as the full web UI.
export function renderCompactPeripherals({host, board, settings, profiles, bindings, save, reload, onError}) {
  host.querySelector('[data-runtime-peripherals]')?.remove();
  const capabilities = board.peripheralCapabilities;
  if (capabilities?.schema !== 1) return;
  const panel = document.createElement('section'); panel.dataset.runtimePeripherals = '';
  const heading = document.createElement('h3'); heading.textContent = 'Peripherals'; panel.append(heading);
  const names = {input:'inputs', control:'controls'};
  const label = profile => profile === 'ws2812-neopixel-led-strip' ? 'WS2812 / NeoPixel LED array' : profile.replaceAll('-', ' ');
  const commit = async () => {
    try { await save({ui:{peripheralProfiles:profiles, peripheralHelperBindings:bindings}}); await reload(); }
    catch (error) { onError(error.message); }
  };
  const occupied = slot => new Set([Number(settings.device?.statusLedPin), Number(settings.device?.statusLedGreenPin), Number(settings.device?.statusLedBluePin),
    ...Object.entries(bindings).filter(([key]) => key !== slot).flatMap(([,value]) => Object.entries(value).filter(([key]) => !key.startsWith('LED_')).map(([,pin]) => Number(pin)))]);
  for (const [group,field] of Object.entries(names)) {
    const allowed = capabilities.profiles?.[group] || ['none'];
    const maximum = Number(capabilities.maxInstances?.[group] || 0);
    if (allowed.length < 2 || !maximum) continue;
    const title = document.createElement('h4'); title.textContent = group === 'control' ? 'Controls' : 'Inputs'; panel.append(title);
    const slots = Array.isArray(profiles[field]) && profiles[field].length ? profiles[field] : ['none']; profiles[field] = slots;
    for (const [index,profile] of slots.entries()) {
      const row = document.createElement('div'); row.className = 'compact-form';
      const name = document.createElement('label'); name.textContent = `${title.textContent} ${index+1}`;
      const select = document.createElement('select'); select.setAttribute('aria-label', name.textContent);
      const cost=value=>Number(capabilities.resourceCosts?.[group]?.[value]||0);
      for (const value of allowed) if (value === 'none' || value === profile || (index < Number(capabilities.profileSlots?.[group]?.[value] ?? maximum) && cost(value) <= Number(capabilities.resourceAvailable?.pwmChannels ?? Infinity)+cost(profile))) select.append(new Option(value === 'none' ? 'None' : label(value), value));
      if (!allowed.includes(profile)) { const legacy = new Option(`${label(profile)} (unavailable)`,profile); legacy.disabled = true; select.append(legacy); }
      select.value = profile; row.append(name,select);
      const slot = `${group}:${index}`;
      const pinsFor = value => capabilities.pinSignals?.[`${group}:${value}`] || [];
      const choices = (value,signal) => Object.entries(board.capabilities || {}).filter(([,caps]) => {
        if (/potentiometer|joystick/.test(value) && signal !== 'SW') return caps.analog;
        return group === 'control' ? caps.output : caps.input;
      }).map(([pin]) => Number(pin));
      select.onchange = async () => {
        const value = select.value; slots[index] = value; delete bindings[slot];
        if (value !== 'none') {
          const used = occupied(slot); const pins = {};
          for (const signal of pinsFor(value)) { const pin = choices(value,signal).find(pin => !used.has(pin)); if (pin === undefined) { onError('Not enough free compatible GPIOs for this peripheral.'); return; } pins[signal] = String(pin); used.add(pin); }
          bindings[slot] = pins;
        }
        await commit();
      };
      for (const signal of pinsFor(profile)) {
        const text = document.createElement('label'); text.textContent = signal;
        const pin = document.createElement('select'); pin.setAttribute('aria-label',`${name.textContent} ${signal}`);
        const used = occupied(slot);
        for (const value of choices(profile,signal)) { const option = new Option(board.capabilities?.[value]?.label || `GPIO${value}`,String(value)); option.disabled = used.has(value); pin.append(option); }
        pin.value = String(bindings[slot]?.[signal] ?? '');
        pin.onchange = async () => { bindings[slot] ||= {}; bindings[slot][signal] = pin.value; await commit(); };
        row.append(text,pin);
      }
      panel.append(row);
    }
    const add = document.createElement('button'); add.type='button'; add.textContent=`Add ${group}`; add.disabled=slots.length>=maximum;
    add.onclick=()=>{slots.push('none');renderCompactPeripherals({host,board,settings,profiles,bindings,save,reload,onError});};panel.append(add);
  }
  host.append(panel);
}
