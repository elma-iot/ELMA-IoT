import test from 'node:test';
import assert from 'node:assert/strict';
import {createDisplayTab} from '../web/modules/display-tab.js';

function setup(raw=''){
  const controls=new Map(['roundSystemControls','roundSystemDashboard','roundSystemTimeout','displayWiringHelp'].map(id=>[id,{hidden:false,checked:true,value:'60',events:{},addEventListener(name,callback){this.events[name]=callback;}}]));
  globalThis.document={getElementById:id=>controls.get(id),activeElement:null};
  const state={settings:{oled:{circularMenu:raw,brightness:83},wifi:{ssid:'private-network',password:'kept-private'},ui:{gpioBoardSelection:'viewe-uedx48480021-md80et'}}};
  let saves=0;
  const tab=createDisplayTab({state,elements:{displayType:{value:'panel',addEventListener(){}}},namedField:()=>null,queueSettingsSave:()=>++saves});
  tab.bindEvents();
  return {tab,state,controls,saves:()=>saves};
}
test('dashboard options preserve custom menu and unrelated settings',()=>{
  const menu={schemaVersion:1,reverse:true,items:[{id:42,title:'Custom',kind:'text'}]};
  const {state,controls,saves}=setup(JSON.stringify(menu));
  controls.get('roundSystemDashboard').checked=false;controls.get('roundSystemTimeout').value='120';
  controls.get('roundSystemDashboard').events.change();
  assert.deepEqual(JSON.parse(state.settings.oled.circularMenu),{...menu,systemDashboard:false,systemTimeoutSeconds:120});
  assert.equal(state.settings.oled.brightness,83);assert.equal(state.settings.wifi.password,'kept-private');assert.equal(saves(),1);
});
test('fresh dashboard options create a valid bounded custom-menu fallback',()=>{
  const {state,controls}=setup();controls.get('roundSystemTimeout').value='99999';controls.get('roundSystemTimeout').events.change();
  const menu=JSON.parse(state.settings.oled.circularMenu);assert.equal(menu.schemaVersion,1);assert.equal(menu.systemTimeoutSeconds,3600);assert.equal(menu.items[0].id,1);
});
test('round controls stay hidden for other board profiles',()=>{
  const {state,controls,tab}=setup();tab.updateDisplayModeUi();assert.equal(controls.get('roundSystemControls').hidden,false);
  state.settings.ui.gpioBoardSelection='esp32-s3-devkit-c1';tab.updateDisplayModeUi();assert.equal(controls.get('roundSystemControls').hidden,true);
});
