import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {BOARD_DEFAULTS} from '../web/modules/board-defaults.js';
import {chipPins,pinChoices} from '../web/modules/board-pin-policy.js';

test('round power indicator remains disabled; external GPIO4 is selectable',()=>{
  const source=fs.readFileSync(new URL('../web/app.js',import.meta.url),'utf8');
  const names=['fillBoardPinSelect','boardDefaultStatusLedPin','populateStatusLedPinOptions'];
  const functions=names.map(name=>source.match(new RegExp('function '+name+'\\([^]*?\\n\\}'))[0]).join('\n');
  for(const board of ['viewe-uedx48480021-md80et','viewe-uedx48480021-md80et-st7701s']){
    assert.equal(BOARD_DEFAULTS[board].statusLedPin,-1);
    assert.deepEqual(chipPins('esp32s3',true,board),[4]);
    for(const pin of [-1,255,4]){
      const field=()=>({value:'',options:[],set innerHTML(v){this.options=[];},append(option){this.options.push(option);}});
      const elements={statusLedPin:field(),statusLedGreenPin:field(),statusLedBluePin:field()};
      const context={BOARD_DEFAULTS,elements,state:{settings:{}},activeGpioBoardProfile:()=>board,
        WS_STATUS_LED_BOARD_PROFILES:new Set(),validBoardPins:()=>[4],pinChoices,
        currentI2sPins:()=>[],currentSdPins:()=>[],activeChipFamily:()=> 'esp32s3',statusLedDefaultRgbPins:()=>[-1,-1],
        syncStatusLedTypeFields(){},document:{getElementById:()=>null},
        Option:function(label,value){this.label=label;this.value=value;}};
      vm.createContext(context);vm.runInContext(functions,context);
      context.populateStatusLedPinOptions({device:{statusLedPin:pin}});
      assert.equal(elements.statusLedPin.value,String(pin===255?-1:pin));
      assert.deepEqual(elements.statusLedPin.options.map(x=>x.value),['-1','4']);
      assert.equal(elements.statusLedPin.options.find(x=>x.value==='-1').disabled,undefined);
    }
  }
});
