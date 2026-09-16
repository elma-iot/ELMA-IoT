import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const src = await readFile(new URL('../web/modules/board-pin-policy.js', import.meta.url),'utf8');
const {boardChipFamily, chipPins, pinChoices, applyBoardFilter} = await import(`data:text/javascript;base64,${Buffer.from(src).toString('base64')}`);
test('SPK N16R8 maps to S3 without relying on an S3 substring',()=>assert.equal(boardChipFamily('esp32-spk-n16r8'),'esp32s3'));
test('classic ESP32 permits legacy DAC pins and excludes flash, nonexistent and input-only outputs',()=>{
 const pins=chipPins('esp32',true,'wemos-lolin32-mini');
 for(const pin of [22,23,19,25,26,27]) assert.ok(pins.includes(pin));
 for(const pin of [6,7,8,9,10,11,20,24,28,34,35,36,39,48]) assert.ok(!pins.includes(pin));
 assert.ok(chipPins('esp32').includes(36));
});
test('S3 allows GPIO8/48 and flexible I2S, but not flash or input-only outputs',()=>{
 const pins=chipPins('esp32s3',true,'esp32-s3-super-mini');
 for(const pin of [8,13,14,15,48]) assert.ok(pins.includes(pin));
 for(const pin of [22,25,26,27,32,46]) assert.ok(!pins.includes(pin));
});
test('WROVER and octal S3 PSRAM pins are reserved',()=>{
 assert.ok(!chipPins('esp32',true,'esp32-wrover').includes(16));
 assert.ok(!chipPins('esp32s3',true,'esp32-s3-psram').includes(35));
});
test('invalid saved assignments stay visible without becoming selectable',()=>{
 const options=pinChoices(chipPins('esp32',true),'8');
 assert.equal(options[0].value,'8'); assert.equal(options[0].disabled,true); assert.match(options[0].label,/reassign/);
 const blocked=pinChoices([8,9,10],'8',new Set([8])); assert.match(blocked[0].label,/another function/);
});
test('manual selection and Auto clear stale chip filters; unsupported targets remain explained',()=>{
 const board={options:['esp32-wrover','esp32-spk-n16r8','esp32-c3','esp32-s2-psram','esp32-c6'].map(value=>({value})), selectedOptions:[], dispatchEvent(){}};
 applyBoardFilter(board,'esp32s3',true);
 assert.equal(board.options[0].disabled,true); assert.equal(board.options[1].disabled,false);
 applyBoardFilter(board,'esp32s3',false); assert.equal(board.options[0].disabled,false); assert.equal(board.options[2].disabled,false);
 applyBoardFilter(board,'esp32c3',true); applyBoardFilter(board,'auto',true);
 assert.equal(board.options[0].disabled,false); assert.equal(board.options[1].disabled,false);
 assert.equal(board.options[3].disabled,true); assert.match(board.options[3].title,/No firmware/);
});
