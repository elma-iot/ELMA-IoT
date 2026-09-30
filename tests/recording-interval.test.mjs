import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const load=async p=>import('data:text/javascript;base64,'+readFileSync(new URL(p,import.meta.url)).toString('base64'));
const {createNode,connect,problems,upgradeNodes}=await load('../web/modules/logic-graph-model.js');
const {LOGIC_CATALOG:c}=await load('../web/modules/logic-catalog.js');
test('recording data links, restrictions, interval validation and migration',()=>{
 const graph={nodes:[],connections:[]},add=type=>{const n=createNode(c[type],{x:0,y:0});graph.nodes.push(n);return n;};
 const source=add('value.boolean'),interval=add('recording.interval'),save=add('mainboard.save_data');
 connect(graph,source.id,'value',interval.id,'value');connect(graph,interval.id,'out',save.id,'value');
 assert.equal(problems(graph,Object.values(c)).errors.size,0);
 const plot=add('mainboard.plot');assert.throws(()=>connect(graph,interval.id,'out',plot.id,'value'),/only to Save Data/);
 interval.parameters.duration=0;assert.ok(problems(graph,Object.values(c)).errors.has(interval.id));interval.parameters.duration=365;interval.parameters.timeUnit='days';assert.ok(!problems(graph,Object.values(c)).errors.has(interval.id));
 save.ports.find(p=>p.id==='value').type='number';save.ports.find(p=>p.id==='in').required=true;upgradeNodes(graph,Object.values(c));assert.deepEqual(save.ports,c['mainboard.save_data'].ports);
 graph.connections.push({source:{node:interval.id,port:'out'},target:{node:plot.id,port:'value'}});assert.ok(problems(graph,Object.values(c)).errors.has(plot.id));
});
