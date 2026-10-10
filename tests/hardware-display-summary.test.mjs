import assert from 'node:assert/strict';
import {createHardwareTab} from '../web/modules/hardware-tab.js';
function label(displayType, board = '') {
 const target={textContent:''};
 const ui=createHardwareTab({state:{settings:{oled:{displayType,enabled:true,sdaPin:21,sclPin:22},ui:{gpioBoardSelection:board},audio:{enabled:false},sd:{enabled:false}}},elements:{deviceHardwareDisplay:target},formatBytes:String,pinSummary:n=>`GPIO${n}`,updateResourceCard:()=>{}});
 ui.renderHardwareSummary({});return target.textContent;
}
assert.equal(label('panel','viewe-uedx48480021-md80et'),'480 x 480 round LCD / rotary encoder');
assert.equal(label('panel'),'Onboard LCD');
assert.equal(label('oled'),'OLED • SDA GPIO21 • SCL GPIO22');
assert.match(label('wape'),/^Wape/);
console.log('Round, generic panel, OLED and Wape summaries passed');
