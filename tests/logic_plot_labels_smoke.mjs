import assert from 'node:assert/strict';
import {LOGIC_CATALOG} from '../web/modules/logic-catalog.js';
import {suggestedPlotLabels,markPlotLabelManual,updateAutoPlotLabels} from '../web/modules/logic-plot-labels.js';

const make=(type,id)=>({id,type,name:LOGIC_CATALOG[type].title,ports:structuredClone(LOGIC_CATALOG[type].ports),parameters:structuredClone(LOGIC_CATALOG[type].parameters)});
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

console.log('Device plot labels: interval inference, manual override and custom labels passed');
