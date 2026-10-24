
import {createLogicsTab} from '../web/modules/logic-editor.js';
import {LOGIC_CATALOG} from '../web/modules/logic-catalog.js';
import {createNode,connect} from '../web/modules/logic-graph-model.js';
const led={...structuredClone(LOGIC_CATALOG['hardware.led']),binding:{kind:'led',pin:4,greenPin:16,bluePin:17,ledType:'rgb'}};
const graph={schemaVersion:1,nodes:[],connections:[],groups:[],devices:[led],view:{grid:true,snap:false}};
const brightness=createNode(LOGIC_CATALOG['value.led_brightness'],{x:20,y:20}),effect=createNode(LOGIC_CATALOG['value.led_effect'],{x:20,y:240}),output=createNode(led,{x:500,y:20});
graph.nodes.push(brightness,effect,output);connect(graph,brightness.id,'value',output.id,'brightness');connect(graph,effect.id,'effect',output.id,'effect');connect(graph,effect.id,'speed',output.id,'effectSpeed');
const tab=createLogicsTab({local:true,request:async()=>({graph:structuredClone(graph),mode:'stopped',live:{},groups:[]})});await tab.load();
document.getElementById('menu-test').onclick=e=>{tab.editor.selected=new Set(tab.editor.graph.nodes.map(n=>n.id));tab.editor.menu(e,null,null,null,{x:0,y:0});};
