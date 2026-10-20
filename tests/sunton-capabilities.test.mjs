import test from 'node:test';
import assert from 'node:assert/strict';
import {suntonProfileFits,suntonLogicSupported,fixedOnboardProfile} from '../web/modules/sunton-capabilities.js';
import {chipPins} from '../web/modules/board-pin-policy.js';
import {ONBOARD_BOARDS} from '../web/modules/onboard-boards.js';

test('stale external amplifier selections cannot replace the fitted DAC speaker in the diagram',()=>{
 const board=ONBOARD_BOARDS['esp32-3248s035c'];
 assert.equal(fixedOnboardProfile(board,'audio','max98357a-i2s-amp'),'sunton-speaker');
 assert.equal(fixedOnboardProfile(board,'display','none'),'viewe-onboard-lcd');
 assert.equal(fixedOnboardProfile({},'audio','max98357a-i2s-amp'),'max98357a-i2s-amp');
});
test('3248 connectors contain only exposed pins and GPIO35 cannot drive outputs',()=>{
 assert.deepEqual(chipPins('esp32',false,'esp32-3248s035c'),[21,22,35]);
 assert.deepEqual(chipPins('esp32',true,'esp32-3248s035c'),[21,22]);
});
test('onboard hardware remains while impossible external wiring is removed',()=>{
 assert.ok(suntonProfileFits('audio','sunton-speaker',[]));
 assert.ok(suntonProfileFits('storage','microsd-spi',['CS','SCK','MOSI','MISO']));
 assert.ok(!suntonProfileFits('control','drv8833-dual-motor-driver',['IN1','IN2','IN3','IN4']));
 assert.ok(!suntonProfileFits('audio','max98357a-i2s-amp',['WS','BCLK','DIN']));
 assert.ok(!suntonProfileFits('input','joystick-analog',['VRX','VRY'],{VRX:'adc',VRY:'adc'}));
 assert.ok(!suntonProfileFits('input','rotary-encoder',['A','B','SW'],{A:'pull-input',B:'pull-input',SW:'pull-input'}));
 assert.ok(suntonProfileFits('sensor','bno055',['SDA','SCL','INT'],{SDA:'output',SCL:'output',INT:'input'}));
});
test('limited logic retains playback and simple controls and drops unavailable engines',()=>{
 for(const type of ['audio.file','audio.volume','audio.equalizer','hardware.gpio','timing.timer'])assert.ok(suntonLogicSupported(type));
 for(const type of ['audio.piano','audio.tts','audio.pitch','audio.speed','hardware.hmi.create','mainboard.memory.psram.freeBytes','mainboard.memory.spiffs.freeBytes'])assert.ok(!suntonLogicSupported(type));
});
