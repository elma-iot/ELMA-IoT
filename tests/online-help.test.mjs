import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

globalThis.window={ElmaI18n:{language:'ru'}};
globalThis.document={documentElement:{lang:'en'}};
const {helpUrl,helpIdForNode}=await import('../web/modules/online-help.js');

test('device Help routes use the active locale and stable topic IDs',()=>{
 assert.equal(helpUrl('logics.gate'),'https://elma-iot.github.io/elma-iot-docs/ru/logics/gate/');
 assert.equal(helpIdForNode({type:'timing.repeat'}),'logics.repeat');
 assert.equal(helpIdForNode({type:'peripheral.play',peripheral:{group:'control',profile:'buzzer'}}),'peripheral.control-buzzer');
});

test('device section Help uses contextual right-click and long-press targets',()=>{
 const source=readFileSync(new URL('../web/modules/online-help.js',import.meta.url),'utf8');
 assert.match(source,/bindContextHelp/);assert.match(source,/contextmenu/);assert.match(source,/long-press/);
 assert.doesNotMatch(source,/button\.textContent='i'/);
});
