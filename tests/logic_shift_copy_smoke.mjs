import assert from 'node:assert/strict';
import {duplicate} from '../web/modules/logic-graph-model.js';

const graph={nodes:[
 {id:'a',name:'Source',position:{x:0,y:0}},
 {id:'b',name:'Sink',position:{x:300,y:25}},
 {id:'c',name:'Other',position:{x:700,y:0}}
],connections:[
 {id:'internal',source:{node:'a',port:'out'},target:{node:'b',port:'in'}},
 {id:'external',source:{node:'b',port:'out'},target:{node:'c',port:'in'}}
],groups:[{id:'group',name:'Automation',nodes:['a','b'],rect:{x:-20,y:-20,width:650,height:240}}]};

const copies=duplicate(graph,new Set(['a','b']),{x:145,y:75});
assert.equal(copies.size,2);
assert.equal(graph.nodes.length,5);
assert.deepEqual(graph.nodes.slice(0,3).map(n=>n.position),[{x:0,y:0},{x:300,y:25},{x:700,y:0}]);
assert.deepEqual(graph.nodes.slice(3).map(n=>n.position),[{x:145,y:75},{x:445,y:100}]);
assert.equal(graph.connections.length,3);
assert.ok(copies.has(graph.connections[2].source.node)&&copies.has(graph.connections[2].target.node));
assert.equal(graph.groups.length,2);
assert.deepEqual(graph.groups[1].rect,{x:125,y:55,width:650,height:240});
assert.deepEqual(new Set(graph.groups[1].nodes),copies);
console.log('Shift copy duplicates selected nodes, internal wires and group at the drag offset');
