import test from 'node:test';
import assert from 'node:assert/strict';
import {createPlaybackStatusModule} from '../web/modules/playback-status.js';

function player() {
  const state={radioStations:[{name:'One',url:'https://one'},{name:'Two',url:'https://two'}],status:{firmware:{audioEnabled:true},playback:{url:'https://two'}}};
  const elements={radioStationSelect:{value:'0',disabled:false},playUrl:{value:'https://one'},playLabel:{value:'One'}};
  let plays=0;
  const module=createPlaybackStatusModule({state,elements,isPlaybackActive:()=>true,isFileManagerPlayback:()=>false,applySelectedRadioStation:async()=>{plays++;}});
  return {state,elements,module,plays:()=>plays};
}
test('LCD/remote station changes select the matching web station without starting playback again',()=>{
  const p=player();p.module.updatePlaybackHeroControls();
  assert.equal(p.elements.radioStationSelect.value,'1');assert.equal(p.plays(),0);
  assert.equal(p.elements.playUrl.value,'https://two');assert.equal(p.elements.playLabel.value,'Two');
});
test('polling preserves a pending manual station choice until the live source changes',()=>{
  const p=player();p.module.updatePlaybackHeroControls();p.elements.radioStationSelect.value='0';
  p.module.updatePlaybackHeroControls();assert.equal(p.elements.radioStationSelect.value,'0');
  p.state.status.playback.url='https://one';p.module.updatePlaybackHeroControls();
  p.state.status.playback.url='https://two';p.module.updatePlaybackHeroControls();
  assert.equal(p.elements.radioStationSelect.value,'1');assert.equal(p.plays(),0);
});
test('SD playback does not replace the radio selection',()=>{
  const p=player();p.state.status.playback.url='sd:/Music/song.mp3';p.module.updatePlaybackHeroControls();
  assert.equal(p.elements.radioStationSelect.value,'0');assert.equal(p.plays(),0);
});
test('a source field being edited is preserved and catches up after focus leaves',()=>{
  const p=player();globalThis.document={activeElement:p.elements.playUrl};
  p.module.updatePlaybackHeroControls();assert.equal(p.elements.playUrl.value,'https://one');
  globalThis.document.activeElement=null;p.module.updatePlaybackHeroControls();
  assert.equal(p.elements.playUrl.value,'https://two');
});
