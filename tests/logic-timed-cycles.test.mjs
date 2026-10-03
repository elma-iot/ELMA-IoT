import assert from 'node:assert/strict';
import {LOGIC_CATALOG} from '../web/modules/logic-catalog.js';
import {connect,immediateCycles,problems} from '../web/modules/logic-graph-model.js';
const node=(type,id)=>({...structuredClone(LOGIC_CATALOG[type]),id});
for(const kind of ['timing.delay','timing.timer','timing.debounce','timing.countdown']){
 const g={nodes:[node('event.start','start'),node(kind,'wait'),node('flow.sequence','step')],connections:[]};
 connect(g,'start','out','wait','in');connect(g,'wait','out','step','in');connect(g,'step','first','wait','in');
 assert.equal(immediateCycles(g).size,0);assert.equal(problems(g,Object.values(LOGIC_CATALOG)).errors.size,0);
 assert.throws(()=>connect(g,'step','first','wait','in'));
}
for(const kind of ['flow.sequence','timing.repeat','timing.cooldown']){
 const g={nodes:[node(kind,'a'),node('flow.sequence','b')],connections:[]};
 connect(g,'a',kind==='flow.sequence'?'first':'out','b','in');assert.throws(()=>connect(g,'b','first','a','in'),/Immediate circular/);assert.equal(g.connections.length,1);
}
const g={nodes:[node('flow.sequence','a'),node('flow.sequence','b'),node('timing.delay','wait')],connections:[]};
connect(g,'a','first','wait','in');connect(g,'wait','out','b','in');connect(g,'a','second','b','in');assert.throws(()=>connect(g,'b','first','a','in'),/Immediate circular/);
const data={nodes:[node('boolean.not','a'),node('boolean.not','b')],connections:[]};connect(data,'a','result','b','value');assert.throws(()=>connect(data,'b','result','a','value'),/Immediate circular/);
console.log('PASS: web timed loops, trigger fan-in, immediate/data cycle rejection and delayed-bypass protection');
