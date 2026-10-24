import test from 'node:test';
import assert from 'node:assert/strict';
import {LOGIC_CATALOG} from '../web/modules/logic-catalog.js';
import {createNode,problems} from '../web/modules/logic-graph-model.js';

test('LED speed accepts 1000 while brightness and colors keep their bounds',()=>{
 const specs=Object.values(LOGIC_CATALOG);
 for(const type of ['value.led_effect','value.led_color','hardware.led']){
  const node=createNode(LOGIC_CATALOG[type],{x:0,y:0}),key=type==='hardware.led'?'effectSpeed':'speed';
  const graph={nodes:[node],connections:[],groups:[]};
  node.parameters[key]=1000;assert.equal(problems(graph,specs).errors.size,0,type);
  node.parameters[key]=1001;assert.ok(problems(graph,specs).errors.has(node.id),type);
  node.parameters[key]=1000;
  if(type==='value.led_color'){
   node.parameters.brightness=101;assert.ok(problems(graph,specs).errors.has(node.id));
   node.parameters.brightness=100;node.parameters.red=256;assert.ok(problems(graph,specs).errors.has(node.id));
  }
 }
});
