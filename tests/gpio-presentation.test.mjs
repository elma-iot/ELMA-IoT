import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const source=fs.readFileSync(new URL('../web/modules/configuration-gpio-tab.js',import.meta.url),'utf8');
const fn=source.match(/  function updateGpioBoardImage\(\) \{[^]*?\n  \}/)[0];
for(const board of ['viewe-uedx48480021-md80et','viewe-uedx48480021-md80et-st7701s','esp32-s3-super-mini','unknown']){
 for(const presentation of [{},{[board]:{rotation:'rotate(-90deg)',rank:'Existing',recommendation:'Existing',tone:'good'}}]){
 const img={style:{}};const diagram={style:{}};let rendered=0;
 const context={elements:{gpioBoardSelector:{value:board},gpioBoardImage:img,peripheralDiagramBoardImage:diagram,gpioBoardRecommendations:{}},gpioBoardAssets:{[board]:{src:'board.svg',alt:'Board'},'esp32-s3-super-mini':{src:'fallback.svg',alt:'Fallback'}},gpioBoardPresentation:presentation,escapeHtml:x=>String(x),gpioBoardLegendMarkup:()=>'',renderPeripheralDiagram:()=>++rendered,renderGpioOverview:()=>++rendered};
 vm.runInNewContext(fn+';updateGpioBoardImage()',context);
 assert.equal(img.style.transform,presentation[board]?.rotation||'none');assert.equal(diagram.style.transform,img.style.transform);assert.equal(rendered,2);
 }
}
console.log('8 board presentation/fallback cases passed');
