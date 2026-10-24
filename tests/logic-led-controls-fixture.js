import {createLogicsTab} from '../web/modules/logic-editor.js';
import {LOGIC_CATALOG} from '../web/modules/logic-catalog.js';
import {createNode,connect} from '../web/modules/logic-graph-model.js';
const a=createNode(LOGIC_CATALOG['value.number'],{x:30,y:70}),b=createNode(LOGIC_CATALOG['condition.compare'],{x:430,y:70});a.id='node-a';a.name='Node A';b.id='node-b';b.name='Node B';
const graph={schemaVersion:1,nodes:[a,b],connections:[],groups:[{id:'demo',name:'Automation',mode:'stopped',color:'#24bc73',nodes:[a.id,b.id],rect:{x:0,y:0,width:800,height:320}}],devices:[],view:{grid:true,snap:false}};connect(graph,a.id,'value',b.id,'a');
const tab=createLogicsTab({local:true,request:async()=>({graph:structuredClone(graph),mode:'stopped',live:{},groups:[]})});await tab.load();
document.getElementById('menu-test').onclick=e=>{tab.editor.selected=new Set(tab.editor.graph.nodes.map(n=>n.id));tab.editor.menu(e,null,null,null,{x:0,y:0});};
document.getElementById('controls-test').onclick=()=>{const nodes=['value.led_brightness','value.led_effect','value.led_color','value.rgb_mixer'].map((type,i)=>{const n=createNode(LOGIC_CATALOG[type],{x:i*340,y:0});n.name=LOGIC_CATALOG[type].title;return n;});tab.replaceProject({...structuredClone(graph),nodes,connections:[],groups:[]});};
