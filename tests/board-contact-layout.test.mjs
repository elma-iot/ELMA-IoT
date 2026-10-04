import test from 'node:test';
import assert from 'node:assert/strict';
import contacts from '../web/board-pin-contacts.json' with {type:'json'};
import {exactBoardAnchors,contactLabelLayout} from '../web/modules/board-contact-layout.js';

test('C3 contacts follow the displayed USB-down silkscreen, without a second rotation',()=>{
 const c3=contacts['esp32-c3'];
 assert.ok(c3['0'][0][0]<.5 && c3['0'][0][1]<.3,'GPIO0 is the top left pad');
 assert.ok(c3['21'][0][0]>.5 && c3['21'][0][1]<.3,'GPIO21 is the top right pad');
 assert.ok(c3['5V'][0][0]<.5 && c3['5V'][0][1]>.7,'5V is the bottom left pad');
});

test('every exported board contact stays centered on its label at all four rotations',()=>{
 const rect={left:103,top:71,width:220,height:360},size={width:54,height:18};
 assert.ok(Object.keys(contacts).length>=20);
 for(const profile of Object.keys(contacts))for(let turns=0;turns<4;turns++){
  const anchors=exactBoardAnchors({profile,boardRect:rect,primary:{},extra:{},offset:0,signalKey:s=>s,boardRailForPeripheral:(_,s)=>s,turns});
  assert.ok(anchors?.size,profile);
  for(const entries of anchors.values())for(const anchor of entries){
   const layout=contactLabelLayout(anchor,rect,size);
   const x=rect.left+(layout.xFactor+.5)*rect.width,y=rect.top+(layout.yFactor+.5)*rect.height;
   if(['left','right'].includes(anchor.side))assert.ok(Math.abs(y-anchor.y)<1e-8,profile);
   else assert.ok(Math.abs(x-anchor.x)<1e-8,profile);
   const edge=anchor.side==='left'?x+size.width/2:anchor.side==='right'?x-size.width/2:anchor.side==='top'?y+size.height/2:y-size.height/2;
   assert.ok(Math.abs(Math.abs(edge-(['left','right'].includes(anchor.side)?anchor.x:anchor.y))-3)<1e-8);
  }
 }
});

test('S2 Mini keeps four distinct header columns and both ground pads',()=>{
 for(const profile of ['esp32-s2-psram','esp32-s2-wemos-mini']){
  const p=contacts[profile],x=k=>p[k][0][0],y=k=>p[k][0][1];
  assert.ok(x('1')<x('EN') && x('EN')<x('39') && x('39')<x('40'));
  assert.ok(Math.abs(y('1')-y('EN'))<.001 && Math.abs(y('39')-y('40'))<.001);
  assert.equal(p.GND.length,2);
  assert.ok(y('3V3')>y('EN') && y('5V')>y('39'));
 }
});

test('ESP-01 antenna-up contacts preserve the two datasheet rows',()=>{
 for(const profile of ['esp8266-esp01','esp8266-esp01s']){
  const p=contacts[profile];
  for(const row of [['GND','2','0','3'],['1','EN','RST','3V3']]){
   for(let i=1;i<row.length;i++)assert.ok(p[row[i-1]][0][0]<p[row[i]][0][0]);
   for(const key of row)assert.ok(Math.abs(p[key][0][1]-p[row[0]][0][1])<.002);
  }
  assert.ok(p.GND[0][1]<p['1'][0][1]);
 }
});
