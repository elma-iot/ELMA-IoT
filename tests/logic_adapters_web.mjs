import assert from 'node:assert/strict';
import {LOGIC_CATALOG} from '../web/modules/logic-catalog.js';
import {compatible,createNode} from '../web/modules/logic-graph-model.js';
import {adapterType,connectAdapted} from '../web/modules/logic-adapters.js';
const specs=Object.values(LOGIC_CATALOG),types=new Set(specs.flatMap(s=>s.ports.map(p=>p.type)));
let cases=0;
for(const a of types)for(const b of types){
 if(compatible(a,b)||!adapterType(a,b))continue;
 const graph={nodes:[{id:'a',position:{x:0,y:0},parameters:{},ports:[{id:'out',type:a,direction:'output'}]},{id:'b',position:{x:500,y:0},parameters:{},ports:[{id:'in',type:b,direction:'input'}]}],connections:[],groups:[{nodes:['a','b']}]};
 connectAdapted(graph,specs,'a','out','b','in');
 for(const e of graph.connections){const port=x=>graph.nodes.find(n=>n.id===x.node).ports.find(p=>p.id===x.port);assert(compatible(port(e.source).type,port(e.target).type),`${a} -> ${b}`);}
 assert.equal(graph.groups[0].nodes.length,graph.nodes.length);cases++;
}
const start=createNode(LOGIC_CATALOG['event.start'],{x:0,y:0}),led=createNode(LOGIC_CATALOG['hardware.led'],{x:500,y:0});
const g={nodes:[start,led],connections:[]};connectAdapted(g,specs,start.id,'out',led.id,'brightness');
assert(g.connections.some(e=>e.target.port==='write'));assert.equal(g.nodes[2].parameters.value,20);
const before=JSON.stringify(g);assert.throws(()=>connectAdapted(g,specs,start.id,'out',led.id,'brightness'));assert.equal(JSON.stringify(g),before);
console.log(`${cases} conversion pairs passed; LED apply wiring, groups and rollback passed`);
