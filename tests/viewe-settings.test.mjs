import test from 'node:test';
import assert from 'node:assert/strict';
import {createConfigurationSettingsSnapshotModule} from '../web/modules/configuration-settings-snapshot.js';

for (const [width,height] of [[240,320],[320,480]]) test(`onboard ${width}x${height} panel saves retain dimensions, touch and interface without OLED migration`,()=>{
  const module=createConfigurationSettingsSnapshotModule({state:{},
    normalizedPeripheralAudioProfiles:()=>['none'],
    normalizedPeripheralDisplayProfiles:()=>['viewe-onboard-lcd'],
    normalizedPeripheralStorageProfiles:()=>['none'],
  });
  const snapshot={oled:{enabled:true,displayType:'panel',width,height,
    interfaceMode:'lvgl',touchEnabled:true,brightness:73,rotation:90}};
  const expected=structuredClone(snapshot.oled);
  module.applyPeripheralProfileSelections(snapshot);
  assert.deepEqual(snapshot.oled,expected);
});
