import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createConfigurationSettingsPersistenceModule} from '../web/modules/configuration-settings-persistence.js';

globalThis.document={activeElement:null};
test('touchscreen navigation follows the web tab order',()=>{
  const web=readFileSync(new URL('../web/index.html',import.meta.url),'utf8');
  const panel=readFileSync(new URL('../src/panel_dashboard.cpp',import.meta.url),'utf8');
  const webKeys=[...web.matchAll(/<button[^>]*role="tab"[^>]*data-tab="([^"]+)"/g)].map(m=>m[1]);
  const table=panel.split('const Tab tabList[]={')[1].split('};')[0];
  const panelKeys=[...table.matchAll(/\{"([^"]+)",/g)].map(m=>m[1]);
  assert.deepEqual(panelKeys,webKeys);
});
test('touchscreen refresh does not read or overwrite a dirty browser form',async()=>{
  let calls=0;
  const state={settings:{device:{friendlyName:'local'}},settingsDirty:true};
  const module=createConfigurationSettingsPersistenceModule({state,request:async()=>{calls++;},isGpioUiInteracting:()=>false});
  await module.refreshExternalSettings();
  assert.equal(calls,0);
  assert.equal(state.settings.device.friendlyName,'local');
});
test('a browser edit made during the touchscreen refresh wins over the response',async()=>{
  let resolve;
  const state={settings:{device:{friendlyName:'before'}},settingsEditRevision:0};
  const module=createConfigurationSettingsPersistenceModule({state,request:()=>new Promise(r=>{resolve=r;}),isGpioUiInteracting:()=>false});
  const pending=module.refreshExternalSettings();
  state.settingsEditRevision++;
  state.settings.device.friendlyName='editing';
  resolve({device:{friendlyName:'remote'}});
  await pending;
  assert.equal(state.settings.device.friendlyName,'editing');
});
