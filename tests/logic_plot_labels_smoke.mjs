import assert from 'node:assert/strict';
import {LOGIC_CATALOG} from '../web/modules/logic-catalog.js';
import {suggestedPlotLabels,markPlotLabelManual,updateAutoPlotLabels} from '../web/modules/logic-plot-labels.js';
import {connect,duplicate} from '../web/modules/logic-graph-model.js';

const make=(type,id)=>({id,type,name:LOGIC_CATALOG[type].title,ports:structuredClone(LOGIC_CATALOG[type].ports),parameters:structuredClone(LOGIC_CATALOG[type].parameters),position:{x:0,y:0}});
const source=make('mainboard.wifi.signal','wifi');
const interval=make('recording.interval','interval');
const plot=make('mainboard.plot','plot');
const graph={nodes:[source,interval,plot],connections:[
 {source:{node:'wifi',port:'value'},target:{node:'interval',port:'value'}},
 {source:{node:'interval',port:'out'},target:{node:'plot',port:'value'}}]};

assert.deepEqual(suggestedPlotLabels(graph,'plot'),{plot:'Signal strength',series:'Signal strength',unit:'dBm'});
assert.equal(updateAutoPlotLabels(graph),true);
assert.deepEqual(plot.parameters,{plot:'Signal strength',series:'Signal strength',unit:'dBm'});
assert.deepEqual(plot.autoLabels,{plot:true,series:true,unit:true});
assert.equal(updateAutoPlotLabels(graph),false);

markPlotLabelManual(plot,'series');plot.parameters.series='Office Wi-Fi';
source.name='Wi-Fi · RSSI (dBm)';
source.ports.find(port=>port.id==='value').label='RSSI (dBm)';
assert.equal(updateAutoPlotLabels(graph),true);
assert.deepEqual(plot.parameters,{plot:'RSSI',series:'Office Wi-Fi',unit:'dBm'});

const custom=make('mainboard.plot','custom');
custom.parameters={plot:'My chart',series:'My sensor',unit:'custom'};
graph.nodes.push(custom);
graph.connections.push({source:{node:'wifi',port:'value'},target:{node:'custom',port:'value'}});
updateAutoPlotLabels(graph);
assert.deepEqual(custom.parameters,{plot:'My chart',series:'My sensor',unit:'custom'});
assert.deepEqual(custom.autoLabels,{plot:false,series:false,unit:false});

const copiedGraph={nodes:[make('mainboard.hardware.temperature','temperature'),make('recording.interval','oldInterval'),make('mainboard.plot','oldPlot')],connections:[],groups:[]};
connect(copiedGraph,'temperature','value','oldInterval','value');
connect(copiedGraph,'oldInterval','out','oldPlot','value');
copiedGraph.nodes[2].parameters={plot:'CPU Temp.',series:'Chip temperature',unit:'°C'};
copiedGraph.nodes[2].autoLabels={plot:false,series:true,unit:false};
const copies=duplicate(copiedGraph,new Set(['oldInterval','oldPlot']));
const copiedInterval=copiedGraph.nodes.find(n=>copies.has(n.id)&&n.type==='recording.interval');
const copiedPlot=copiedGraph.nodes.find(n=>copies.has(n.id)&&n.type==='mainboard.plot');
assert.deepEqual(copiedPlot.parameters,{plot:'Plot 1',series:'Value',unit:''});
assert.deepEqual(copiedPlot.autoLabels,{plot:true,series:true,unit:true});
assert.deepEqual(copiedInterval.parameters,copiedGraph.nodes[1].parameters);
copiedGraph.nodes.push(make('mainboard.wifi.signal','newWifi'));
connect(copiedGraph,'newWifi','value',copiedInterval.id,'value');
updateAutoPlotLabels(copiedGraph);
assert.deepEqual(copiedPlot.parameters,{plot:'Signal strength',series:'Signal strength',unit:'dBm'});

const complete=duplicate(copiedGraph,new Set(['newWifi',copiedInterval.id,copiedPlot.id]));
const completePlot=copiedGraph.nodes.find(n=>complete.has(n.id)&&n.type==='mainboard.plot');
assert.deepEqual(completePlot.parameters,copiedPlot.parameters);

console.log('Device plot labels: interval inference, detached copy reset, reconnection and manual overrides passed');
