import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {restoreLegacyPeripheralProfiles as restore} from '../web/modules/legacy-peripheral-profiles.js';

test('legacy hardware flags restore dropdowns without changing pins or settings', () => {
  const settings = {audio: {enabled:true, doutPin:25}, oled:{enabled:true, displayType:'oled', sdaPin:23}, sd:{enabled:true}, ui:{peripheralProfiles:{}}};
  const before = JSON.stringify(settings);
  const restored = restore(settings);
  assert.deepEqual(restored.audioProfiles, ['max98357a-i2s-amp']);
  assert.deepEqual(restored.displayProfiles, ['i2c-oled']);
  assert.deepEqual(restored.storage, ['microsd-spi']);
  assert.equal(JSON.stringify(settings), before);
});
test('explicit arrays and scalar selections override legacy inference', () => {
  const profiles = {audioProfiles:['pcm5102-i2s-dac','max98357a-i2s-amp'], displayProfile:'none', storage:['none'], controls:['drv8833-dual-motor-driver']};
  const restored = restore({audio:{enabled:true},oled:{enabled:true},sd:{enabled:true},ui:{peripheralProfiles:profiles}});
  assert.deepEqual(restored.audioProfiles, profiles.audioProfiles);
  assert.deepEqual(restored.displayProfiles, ['none']);
  assert.deepEqual(restored.storage, ['none']);
  assert.deepEqual(restored.controls, profiles.controls);
});
test('disabled and Waveshare devices retain the appropriate profiles', () => {
  assert.deepEqual(restore({}).audioProfiles, ['none']);
  assert.deepEqual(restore({oled:{enabled:true,displayType:'wape'}}).displayProfiles,['waveshare-screen']);
});

test('VIEWE panel survives legacy inference without becoming an I2C OLED', () => {
  assert.deepEqual(restore({oled:{enabled:true,displayType:'panel'}}).displayProfiles,['viewe-onboard-lcd']);
});

test('VIEWE does not invent a DAC from legacy audio flag; explicit DAC survives',()=>{
 const settings={audio:{enabled:true},ui:{gpioBoardSelection:'viewe-uedx32480035e-wb-a',peripheralProfiles:{}}};
 assert.deepEqual(restore(settings).audioProfiles,['none']);
 settings.ui.peripheralProfiles.audioProfiles=['max98357a-i2s-amp'];
 assert.deepEqual(restore(settings).audioProfiles,['max98357a-i2s-amp']);
});
