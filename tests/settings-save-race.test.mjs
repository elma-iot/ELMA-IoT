import test from 'node:test';
import assert from 'node:assert/strict';
import {createConfigurationSettingsPersistenceModule} from '../web/modules/configuration-settings-persistence.js';

test('an in-flight save cannot overwrite a newer effect and the next save sends it',async()=>{
 let local=false,release,snapshot={ui:{},effect:'blink'};const posted=[];
 globalThis.document={body:{classList:{contains:name=>name==='local-builder-mode'&&local}}};
 globalThis.window={clearTimeout(){},setTimeout(){return 1;}};
 const state={settings:{},settingsEditRevision:1,settingsDirty:true};
 const module=createConfigurationSettingsPersistenceModule({state,elements:{},
  applyPeripheralProfileSelections(){},normalizeUiSettings:x=>x,validateSettingsPayload(){},
  normalizeDecimalField(){},currentSettingsSnapshot:()=>structuredClone(snapshot),
  request:async(path,options)=>{posted.push(JSON.parse(options.body));if(posted.length===1)await new Promise(resolve=>release=resolve);},
 });
 const first=module.saveSettings({silent:true});
 snapshot.effect='scan';module.queueSettingsSave();local=true;
 // A late response/DOM rebuild must not change the queued edit itself.
 snapshot.effect='blink';
 const second=module.saveSettings({silent:true});release();await Promise.all([first,second]);
 assert.deepEqual(posted.map(x=>x.effect),['blink','scan']);assert.equal(state.settingsDirty,false);
 assert.equal(state.settings.effect,'scan');
});

test('verification read started before another edit does not refill old settings',async()=>{
 let release;const state={settingsEditRevision:1,settingsDirty:false,settings:{effect:'scan'}};
 const module=createConfigurationSettingsPersistenceModule({state,request:()=>new Promise(resolve=>release=resolve)});
 const check=module.refreshSettingsAfterSave({effect:'blink'});state.settingsEditRevision++;release({effect:'blink'});
 assert.equal(await check,false);assert.equal(state.settings.effect,'scan');
});
