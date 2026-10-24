import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const roots=[new URL('../web/',import.meta.url),new URL('../../ELMA-IoT-Android-main/shared-web/',import.meta.url)].filter(url=>fs.existsSync(url));
const boards=Object.keys(JSON.parse(fs.readFileSync(new URL('../scripts/runtime-board-catalog.json',import.meta.url),'utf8')));
const names=['device.statusLedPin','device.statusLedGreenPin','device.statusLedBluePin','oled.sdaPin','oled.sclPin','oled.resetPin','oled.wapeTriggerPin','battery.adcPin','sd.csPin','sd.sckPin','sd.mosiPin','sd.misoPin','audio.wsPin','audio.bclkPin','audio.doutPin'];
function field(name,value=9){return {name,value:String(value),type:'select-one',tagName:'SELECT',listeners:{},hasAttribute:()=>false,addEventListener(event,fn){(this.listeners[event]??=[]).push(fn);},emit(){for(const fn of this.listeners.change||[])fn({target:this});}};}
for(const root of roots){
 const {createConfigurationSettingsPersistenceModule}=await import(new URL('modules/configuration-settings-persistence.js',root));
 const {createAudioTab}=await import(new URL('modules/audio-tab.js',root));
 const source=fs.readFileSync(new URL('app.js',root),'utf8').replaceAll('\r\n','\n');
 const start=source.indexOf('for (const field of elements.settingsForm.elements)');
 const bindings=source.slice(start,source.indexOf('\nfunction handleError(',start));
 test(`${root.pathname}: GPIO edits autosave complete snapshots for every board`,async()=>{
  for(const board of boards){
   const fields=names.map(name=>field(name));
   const timers=new Map(),posted=[],drafts=[];let next=0;
   const local=root.pathname.includes('shared-web');
   globalThis.document={body:{classList:{contains:name=>local&&['local-builder-mode','android-designer'].includes(name)}}};
   globalThis.window={setTimeout(fn){timers.set(++next,fn);return next;},clearTimeout(id){timers.delete(id);},elmaPersistAndroidDraft:value=>drafts.push(value)};
   const state={settings:{ui:{gpioBoardSelection:board}}};
   const snapshot=()=>{const result={ui:{gpioBoardSelection:board}};for(const f of fields){const [section,key]=f.name.split('.');(result[section]??={})[key]=Number(f.value);}return result;};
   const elements={settingsForm:{elements:fields},audioWsPin:fields[12],audioBclkPin:fields[13],audioDoutPin:fields[14]};
   const module=createConfigurationSettingsPersistenceModule({state,elements,normalizeDecimalField(){},currentSettingsSnapshot:snapshot,settingsAutosaveDelayMs:150,normalizeUiSettings:x=>x,applyPeripheralProfileSelections(){},validateSettingsPayload(){},handleError:error=>{throw error;},request:async(path,options)=>{assert.equal(path,'/api/settings');posted.push(JSON.parse(options.body));}});
   const noop=()=>{};
   const context={state,elements,document,queueSettingsSave:module.queueSettingsSave,saveSettings:module.saveSettings,populateSdPinOptions:noop,populateOledPinOptions:noop,populateBatteryAdcPinOptions:noop,populateWapeTriggerPinOptions:noop,updateBatteryUi:noop,renderOledPreview:noop};
   vm.runInNewContext(bindings,context);
   const audio=createAudioTab({...context,hasDistinctI2sPins:()=>new Set(fields.slice(12).map(f=>f.value)).size===3,waitForSettingsIdle:async()=>{},handleError:error=>{throw error;}});audio.bindEvents();
   fields[12].value='1';fields[13].value='2';fields[14].value='3';
   for(const f of fields){
    const before=posted.length;f.value=f.name==='audio.wsPin'?'4':f.name==='audio.bclkPin'?'5':f.name==='audio.doutPin'?'6':'0';f.emit();
    assert.equal(timers.size,1,`${board}: ${f.name} must queue one autosave`);
    const [id,run]=timers.entries().next().value;timers.delete(id);run();await new Promise(resolve=>setImmediate(resolve));
    assert.equal(posted.length,before+1,`${board}: ${f.name} POST`);
    const [section,key]=f.name.split('.');assert.equal(posted.at(-1)[section][key],Number(f.value));assert.equal(state.settings[section][key],Number(f.value));
    if(local)assert.ok(drafts.length,'Android draft persists before its timer');
   }
   const before=posted.length;fields[12].value=fields[13].value;fields[12].emit();assert.equal(timers.size,0,'Incomplete I2S swaps must wait for distinct pins');assert.equal(posted.length,before);
  }
 });
 test(`${root.pathname}: every peripheral helper GPIO queues its updated binding`,()=>{
  const peripheralSource=fs.readFileSync(new URL('modules/configuration-peripherals-tab.js',root),'utf8').replaceAll('\r\n','\n');
  const begin=peripheralSource.indexOf('    gpioTabElement?.addEventListener("change"');
  const handlerSource=peripheralSource.slice(begin,peripheralSource.indexOf('\n    elements.peripheralAudioInList?',begin));
  class Select{matches(selector){return selector==='[data-peripheral-helper-signal]';}}
  for(const board of boards)for(const group of ['audio','audioIn','display','sensor','input','power','control','expansion','storage','communication']){
   let change,queued=0,refreshed=0;const stored={};
   const target=new Select();target.value='0';target.dataset={peripheralHelperGroup:group,peripheralHelperIndex:'0',peripheralHelperSignal:'SIG'};
   const context={HTMLSelectElement:Select,gpioTabElement:{addEventListener:(name,fn)=>change=fn},state:{settings:{ui:{gpioBoardSelection:board}}},batteryDividerSensorProfile:'divider',setPeripheralHelperBindingValue:(g,i,key,value)=>{(stored[g+':'+i]??={})[key]=value;},syncGpioMappingControls:()=>refreshed++,renderPeripheralDiagram:()=>refreshed++,queueSettingsSave:()=>{queued++;assert.equal(stored[group+':0'].SIG,'0');}};
   vm.runInNewContext(handlerSource,context);change({target});assert.equal(queued,1,`${board}: ${group}`);assert.equal(refreshed,2);
  }
 });
}
