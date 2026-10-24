import test from 'node:test';
import assert from 'node:assert/strict';
import {compactLogicRequest as web} from '../web/modules/logic-graph-model.js';
import {compactLogicRequest as android} from '../../ELMA-IoT-Android-main/shared-web/modules/logic-graph-model.js';

for(const [name,compact] of [['web',web],['Android shared editor',android]]) {
 test(`${name}: graph saves retain editable data without allocating editor contracts on ESP32`,()=>{
  const payload={mode:'playing',graph:{schemaVersion:1,devices:[{description:'d'.repeat(20000)}],
   nodes:[{id:'led',type:'hardware.led',name:'My LED',parameters:{red:150},position:{x:10,y:20},
    peripheral:{id:'onboard'},ports:[{description:'p'.repeat(20000)}],binding:{description:'b'.repeat(20000)}}],
   connections:[],groups:[{id:'group',nodes:['led'],mode:'paused'}],view:{labels:[{text:'Keep me'}]}}};
  const original=structuredClone(payload),result=compact(payload);
  assert.deepEqual(payload,original);
  assert.equal(result.mode,'playing');
  assert.equal(result.graph.devices,undefined);
  assert.equal(result.graph.nodes[0].ports,undefined);
  assert.equal(result.graph.nodes[0].binding,undefined);
  assert.deepEqual(result.graph.nodes[0].parameters,{red:150});
  assert.deepEqual(result.graph.nodes[0].peripheral,{id:'onboard'});
  assert.deepEqual(result.graph.groups,payload.graph.groups);
  assert.deepEqual(result.graph.view,payload.graph.view);
  assert.ok(JSON.stringify(result).length<JSON.stringify(payload).length/50);
  const control={mode:'stopped'};assert.equal(compact(control),control);assert.equal(compact(null),null);
 });
}
