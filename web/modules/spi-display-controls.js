import controllers from './spi-display-catalog.json' with {type:'json'};
export {controllers as SPI_DISPLAY_CONTROLLERS};
export function spiController(values={}){return controllers.find(c=>c.id===values.DISPLAY_CONTROLLER)||controllers.find(c=>c.id==='st7789-240');}
export function isOledController(controller){return /^(ssd|sh1106)/.test(controller.driver);}
export function createSpiDisplayControls({values={},onChange=()=>{}}){
  const box=document.createElement('fieldset');box.className='spi-display-controls';
  const legend=document.createElement('legend');legend.textContent='SPI display';box.append(legend);
  const controller=spiController(values);
  const add=(title,field)=>{const label=document.createElement('label');label.append(document.createTextNode(title+' '),field);box.append(label);return field;};
  const select=(title,key,options,fallback)=>{
    const field=document.createElement('select');field.dataset.spiDisplayField=key;
    for(const [value,label] of options)field.add(new Option(label,String(value)));
    field.value=String(values[key]??fallback);field.addEventListener('change',()=>onChange({[key]:key==='DISPLAY_ROTATION'?Number(field.value):field.value}));return add(title,field);
  };
  const number=(title,key,min,max,fallback)=>{
    const field=document.createElement('input');field.type='number';field.min=String(min);field.max=String(max);field.value=String(values[key]??fallback);field.dataset.spiDisplayField=key;
    field.addEventListener('change',()=>{const value=Number(field.value);if(Number.isInteger(value)&&value>=min&&value<=max)onChange({[key]:value});});return add(title,field);
  };
  const check=(title,key,fallback)=>{
    const field=document.createElement('input');field.type='checkbox';field.checked=values[key]??fallback;field.dataset.spiDisplayField=key;
    field.addEventListener('change',()=>onChange({[key]:field.checked}));const label=add(title,field);label.parentElement.classList.add('check');return field;
  };
  const driver=select('Controller','DISPLAY_CONTROLLER',controllers.map(c=>[c.id,c.label]),controller.id);
  const mode=select('Interface','DISPLAY_INTERFACE',[['status','Status / Logics text'],['clock','Clock and status']],controller.round?'clock':'status');
  select('Rotation','DISPLAY_ROTATION',[[0,'0°'],[90,'90°'],[180,'180°'],[270,'270°']],0);
  check('Enable display','DISPLAY_ENABLED',true);
  number('Brightness %','DISPLAY_BRIGHTNESS',0,100,100);
  check('Enable display timeout','DISPLAY_TIMEOUT_ENABLED',true);
  const timeout=number('Screen off after seconds · 0 = always on','DISPLAY_TIMEOUT_SECONDS',0,3600,isOledController(controller)?60:0);
  const hint=document.createElement('small');hint.textContent='OLEDs wake on displayed-content changes or alerts. Clock ticks alone do not wake a timed-out OLED. MISO is not required. BL and RST are optional; an unconnected BL pin needs the module’s normal power wiring.';box.append(hint);
  driver.addEventListener('change',()=>{
    const c=controllers.find(c=>c.id===driver.value);if(!c)return;
    const patch={DISPLAY_CONTROLLER:c.id,DISPLAY_INTERFACE:c.round?'clock':'status'};mode.value=patch.DISPLAY_INTERFACE;
    if(values.DISPLAY_TIMEOUT_SECONDS===undefined){patch.DISPLAY_TIMEOUT_SECONDS=isOledController(c)?60:0;timeout.value=String(patch.DISPLAY_TIMEOUT_SECONDS);}
    onChange(patch);
  });
  return box;
}
