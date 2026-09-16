// R1: VIN to ADC junction. R2: ADC junction to GND. Values are ohms/volts.
const e24=[10,11,12,13,15,16,18,20,22,24,27,30,33,36,39,43,47,51,56,62,68,75,82,91];
export const standardResistors=Object.freeze(Array.from({length:7},(_,decade)=>e24.map(value=>value*10**decade)).flat().filter(value=>value<=10000000));
export function voltageDividerSettings(battery={}){
 const resistance=value=>Number.isFinite(Number(value))&&Number(value)>=10&&Number(value)<=10000000?Math.round(Number(value)):220000;
 const vin=Number(battery.dividerMaxVin);
 return {dividerR1Ohms:resistance(battery.dividerR1Ohms),dividerR2Ohms:resistance(battery.dividerR2Ohms),dividerMaxVin:Number.isFinite(vin)&&vin>=0.1&&vin<=100?vin:4.2};
}
export function voltageDividerMaximum(battery={}){
 const values=voltageDividerSettings(battery),ratio=1+values.dividerR1Ohms/values.dividerR2Ohms,output=values.dividerMaxVin/ratio;
 return {...values,ratio,output,unsafe:output>3.3};
}
export function resistorLabel(ohms){return ohms>=1000000?`${ohms/1000000} MΩ`:ohms>=1000?`${ohms/1000} kΩ`:`${ohms} Ω`;}
export function resistorBands(ohms){
 const colors=['#171717','#795548','#e53935','#fb8c00','#fdd835','#43a047','#1e88e5','#8e24aa','#9e9e9e','#fafafa'];
 const exponent=Math.floor(Math.log10(ohms))-2,digits=String(Math.round(ohms/10**exponent)).padStart(3,'0');
 return [...digits].map(digit=>colors[Number(digit)]).concat(exponent===-1?'#c9a227':exponent===-2?'#b0bec5':colors[exponent],colors[1]);
}
export function createVoltageDividerControls({battery={},onChange,formId='settingsForm'}){
 const group=document.createElement('div');group.className='voltage-divider-controls';
 const fields={},values=voltageDividerSettings(battery);
 for(const [key,title] of [['dividerR1Ohms','R1'],['dividerR2Ohms','R2'],['dividerMaxVin','Maximum VIN']]){
  const label=document.createElement('label'),caption=document.createElement('span');caption.textContent=title;label.append(caption);
  const field=document.createElement(key==='dividerMaxVin'?'input':'select');field.name=`battery.${key}`;field.setAttribute('form',formId);
  if(key==='dividerMaxVin'){field.type='number';field.min='0.1';field.max='100';field.step='0.1';field.inputMode='decimal';}
  else{for(const value of [...new Set([...standardResistors,values[key]])].sort((a,b)=>a-b))field.append(new Option(resistorLabel(value),String(value)));}
  field.value=String(values[key]);fields[key]=field;label.append(field);group.append(label);
 }
 const maximum=document.createElement('output'),warning=document.createElement('p');maximum.className='voltage-divider-maximum';warning.className='voltage-divider-warning';warning.setAttribute('role','alert');warning.textContent='ADC output exceeds 3.3 V. Change the divider before connecting it.';group.append(maximum,warning);
 const update=()=>{const data=Object.fromEntries(Object.entries(fields).map(([key,field])=>[key,Number(field.value)])),result=voltageDividerMaximum(data);maximum.textContent=`MAX: ${result.output.toFixed(2)} V`;maximum.classList.toggle('voltage-divider-unsafe',result.unsafe);warning.hidden=!result.unsafe;return voltageDividerSettings(data);};
 for(const [key,field] of Object.entries(fields))field.addEventListener('change',()=>{
  if(!field.checkValidity())return;
  const values=update(),result=voltageDividerMaximum(values);
  // The existing battery monitor has one shared divider configuration. Keep
  // simultaneous editors consistent so duplicate form fields cannot save stale values.
  for(const editor of document.querySelectorAll('.voltage-divider-controls')){
   if(editor===group)continue;
   for(const [name,value] of Object.entries(values)){const input=editor.querySelector(`[name="battery.${name}"]`);if(input)input.value=String(value);}
   const output=editor.querySelector('.voltage-divider-maximum'),notice=editor.querySelector('.voltage-divider-warning');
   if(output){output.textContent=`MAX: ${result.output.toFixed(2)} V`;output.classList.toggle('voltage-divider-unsafe',result.unsafe);}
   if(notice)notice.hidden=!result.unsafe;
  }
  onChange(values,key!=='dividerMaxVin');
 });
 update();return group;
}
