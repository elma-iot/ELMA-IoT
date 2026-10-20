import test from 'node:test';
import assert from 'node:assert/strict';
import {createConfigurationSettingsSnapshotModule} from '../web/modules/configuration-settings-snapshot.js';
globalThis.window={};
function snapshotFor(board,enabled){
 const groups={Audio:['sunton-speaker'],AudioIn:['none'],Display:['viewe-onboard-lcd'],Sensor:['ldr'],Input:['none'],Control:['none'],Expansion:['none'],Storage:['microsd-spi'],Communication:['none'],Power:['none']};
 const context={state:{settings:{ui:{gpioBoardSelection:board},oled:{enabled:true,width:320,height:480},device:{}}},elements:{gpioBoardSelector:{value:board}},isPlainObject:value=>value&&Object.getPrototypeOf(value)===Object.prototype,cloneSettingsObject:structuredClone,normalizeUiSettings:value=>({...value,peripheralHelperBindings:value?.peripheralHelperBindings||{}}),collectForm:()=>({oled:{enabled},device:{statusLedType:'rgb',statusLedPin:21,statusLedGreenPin:21,statusLedBluePin:22}})};
 for(const [name,profiles]of Object.entries(groups))context['normalizedPeripheral'+name+'Profiles']=()=>profiles;
 return createConfigurationSettingsSnapshotModule(context).currentSettingsSnapshot();
}
test('3248 display can be switched off without removing its fitted LCD profile or validating hidden RGB wiring',()=>{
 const off=snapshotFor('esp32-3248s035c',false);
 assert.equal(off.oled.enabled,false);assert.equal(off.oled.displayType,'panel');
 assert.equal(off.ui.peripheralProfiles.displayProfile,'viewe-onboard-lcd');
 assert.equal(off.device.statusLedPin,4);assert.equal(off.device.statusLedGreenPin,16);assert.equal(off.device.statusLedBluePin,17);assert.equal(off.device.statusLedType,'rgb');
 assert.equal(off.audio.doutPin,26);assert.equal(off.audio.enabled,true);
 assert.equal(snapshotFor('esp32-3248s035c',true).oled.enabled,true);
});
test('other board profiles retain their RGB settings',()=>{
 const settings=snapshotFor('esp32-wroom',false);assert.equal(settings.device.statusLedType,'rgb');assert.equal(settings.device.statusLedPin,21);
});
