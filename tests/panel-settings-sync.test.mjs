import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createConfigurationSettingsPersistenceModule} from '../web/modules/configuration-settings-persistence.js';
import {ONBOARD_BOARDS} from '../web/modules/onboard-boards.js';

globalThis.document={activeElement:null};
for(const value of ['180','0'])test(`screen timeout ${value} is sent as an integer, including zero to disable`,()=>{
  const module=createConfigurationSettingsPersistenceModule({
    state:{settings:{}},elements:{settingsForm:{elements:[{name:'oled.screenTimeoutSeconds',type:'number',value}]}},
    defaultEsp32S3AudioPins:{dout:1,ws:2,bclk:3},defaultSdGpioPins:{cs:4,sck:5,mosi:6,miso:7},effectSelectConfig:[],
    normalizeDecimalField:()=>{},parseDecimalFieldValue:()=>0,currentBatteryCalibrationMultiplier:()=>1,
    choosePreferredOledPins:()=>({sda:21,scl:22}),
  });
  assert.equal(module.collectForm().oled.screenTimeoutSeconds,Number(value));
});
test('bounded initial settings retries recover a busy board without retrying writes',async()=>{
  let reads=0;const waits=[];
  const module=createConfigurationSettingsPersistenceModule({
    state:{settingsDirty:true},settingsReadRetries:()=>3,
    request:async path=>{assert.equal(path,'/api/settings');if(++reads<3)throw Object.assign(new Error('Device offline'),{isDeviceOffline:true});return {};},
    delay:async ms=>waits.push(ms),normalizeUiSettings:value=>value||{},
  });
  await module.loadSettings();
  assert.equal(reads,3);assert.deepEqual(waits,[400,800]);
});
test('touchscreen navigation follows the web tab order for supported LCD hardware',()=>{
  const web=readFileSync(new URL('../web/index.html',import.meta.url),'utf8');
  const panel=readFileSync(new URL('../src/panel_dashboard.cpp',import.meta.url),'utf8');
  for(const board of Object.values(ONBOARD_BOARDS).filter(board=>board.displayProfile)) {
    assert.ok(!board.cameraPins && !board.microphone, 'LCD camera/microphone hardware needs corresponding panel pages');
  }
  // MiniExco uses a separate board profile without an onboard LCD.
  const webKeys=[...web.matchAll(/<button[^>]*role="tab"[^>]*data-tab="([^"]+)"/g)].map(m=>m[1]).filter(key=>!['camera','microphones','robot'].includes(key));
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
