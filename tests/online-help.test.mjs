import test from 'node:test';
import assert from 'node:assert/strict';

globalThis.window={ElmaI18n:{language:'ru'}};
globalThis.document={documentElement:{lang:'en'}};
const {helpUrl,helpIdForNode}=await import('../web/modules/online-help.js');

test('device Help routes use the active locale and stable topic IDs',()=>{
 assert.equal(helpUrl('logics.gate'),'https://elma-iot.github.io/elma-iot-docs/ru/logics/gate/');
 assert.equal(helpIdForNode({type:'timing.repeat'}),'logics.repeat');
 assert.equal(helpIdForNode({type:'peripheral.play',peripheral:{group:'control',profile:'buzzer'}}),'peripheral.control-buzzer');
});
