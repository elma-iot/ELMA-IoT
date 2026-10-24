import test from 'node:test';
import assert from 'node:assert/strict';
import {menuPosition,filterMenuActions} from '../web/modules/logic-context-menu.js';
test('expanded menu stays inside desktop and small viewports',()=>{
 for(const viewport of [{width:1720,height:900},{width:360,height:280}])for(const size of [{width:338,height:220},{width:338,height:900}]){
  const p=menuPosition({x:viewport.width-12,y:viewport.height-12},size,viewport);
  assert.ok(p.left>=8&&p.top>=8);assert.ok(p.top+Math.min(size.height,p.maxHeight)<=viewport.height-8);
 }
});
test('search opens Canvas actions and reveals Group rather than only its heading',()=>{
 const group={textContent:'Group'},save={textContent:'Save & apply'},section={tagName:'DETAILS',dataset:{},open:false,querySelectorAll:()=>[group,save]};
 const menu={children:[section]};filterMenuActions(menu,'group');assert.equal(section.open,true);assert.equal(section.hidden,false);assert.equal(group.hidden,false);assert.equal(save.hidden,true);
 filterMenuActions(menu,'unmatched');assert.equal(section.hidden,true);
 filterMenuActions(menu,'');assert.equal(section.open,false);assert.equal(section.hidden,false);assert.equal(save.hidden,false);
});
