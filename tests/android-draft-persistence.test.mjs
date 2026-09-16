import assert from 'node:assert/strict';
import {createConfigurationSettingsPersistenceModule} from '../web/modules/configuration-settings-persistence.js';
let persisted=null,timer=null;
globalThis.document={body:{classList:{contains:()=>true}}};
globalThis.window={elmaPersistAndroidDraft:value=>persisted=structuredClone(value),setTimeout:fn=>{timer=fn;return 1;},clearTimeout:()=>{}};
const snapshot={audio:{wsPin:0,bclkPin:26,doutPin:22},ui:{peripheralProfiles:{audioProfiles:['pcm5102-i2s-dac']},peripheralHelperBindings:{'controls:0':{pin:27}}}};
const state={settingsLoading:false};
const module=createConfigurationSettingsPersistenceModule({state,currentSettingsSnapshot:()=>snapshot,handleError:e=>{throw e;},settingsAutosaveDelayMs:1000});
module.queueSettingsSave();
assert.ok(timer);assert.deepEqual(persisted,snapshot); // Timer deliberately never runs: Android suspended.
state.settingsLoading=true;persisted=null;module.queueSettingsSave();assert.equal(persisted,null);
console.log('Android peripheral snapshot committed before suspended autosave timer; initialization cannot overwrite it');
