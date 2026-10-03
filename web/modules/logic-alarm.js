const tr=text=>window.ElmaI18n?.t?.(text)||window.ElmaFirmwareI18n?.t?.(text)||text;
export const alarmLabels={clockSource:'Clock source',schedule:'Schedule',alarmDate:'Alarm date',alarmTime:'Alarm time',weekdays:'Weekdays',manualDate:'Set clock date',manualTime:'Set clock time'};
export function alarmField(node,key,value,editor){
 if(node.type!=='clock.alarm'||!Object.hasOwn(alarmLabels,key))return null;
 const save=value=>{editor.begin();node.parameters[key]=value;editor.changed();};let field;
 if(key==='clockSource'||key==='schedule'){
  field=document.createElement('select');
  const options=key==='clockSource'?[['utc','UTC / NTP'],['manual','Manual'],['rtc','DS3231 RTC']]:[['once','Specific date'],['weekdays','Weekdays']];
  for(const [id,label] of options)field.add(new Option(tr(label),id));field.value=value;field.onchange=()=>save(field.value);
 }else if(key==='weekdays'){
  field=document.createElement('span');field.style.display='grid';field.style.gridTemplateColumns='repeat(4,minmax(0,1fr))';field.style.gap='4px';
  const checks=[];for(const [i,day] of ['Mon','Tue','Wed','Thu','Fri','Sat','Sun'].entries()){
   const label=document.createElement('label'),check=document.createElement('input');check.type='checkbox';check.checked=value[i]==='1';check.disabled=node.parameters.schedule!=='weekdays';checks.push(check);check.onchange=()=>save(checks.map(c=>c.checked?'1':'0').join(''));label.append(check,tr(day));field.append(label);
  }
 }else{
  field=document.createElement('input');field.type=key.endsWith('Date')?'date':'time';field.value=value;
  if(field.type==='date'){field.min='2000-01-01';field.max='2099-12-31';}else field.step='1';
  field.disabled=key==='alarmDate'?node.parameters.schedule!=='once':key.startsWith('manual')&&node.parameters.clockSource==='utc';
  field.onchange=()=>save(field.type==='time'&&field.value.length===5?field.value+':00':field.value);
 }
 field.classList.add('logic-alarm-field');field.setAttribute('aria-label',tr(alarmLabels[key]));return field;
}
export function validAlarm(p){
 const date=value=>/^20\d\d-\d\d-\d\d$/.test(value)&&Number.isFinite(Date.parse(value+'T00:00:00Z'))&&new Date(value+'T00:00:00Z').toISOString().slice(0,10)===value;
 return ['utc','manual','rtc'].includes(p.clockSource)&&/^([01]\d|2[0-3]):[0-5]\d:[0-5]\d$/.test(p.alarmTime)&&(p.schedule==='once'?date(p.alarmDate):p.schedule==='weekdays'&&/^[01]{7}$/.test(p.weekdays)&&p.weekdays.includes('1'));
}
