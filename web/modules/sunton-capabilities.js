// Connector limits of the 3248S035C, distinct from the ESP32 chip's pin count.
export function fixedOnboardProfile(board,group,selected) {
  return board?.externalPins && board?.builtinProfiles?.[group] || selected;
}
export function suntonProfileFits(group,profile,signals,requirements={}) {
  if(profile==='none')return true;
  if(['sunton-speaker','viewe-onboard-lcd','ldr'].includes(profile))return true;
  if(group==='display')return false; // The firmware display owner is the fitted LCD.
  if(group==='audio')return profile==='buzzer';
  if(group==='storage')return profile==='microsd-spi'; // The fitted SD slot only.
  if(group==='audioIn'||profile==='custom')return false;
  if(group==='power')return true; // Passive supply diagrams consume no GPIO.
  const required=signals.filter(signal=>!['INT','IRQ','DRDY','RST','RESET','I2C_ADDRESS'].includes(signal));
  let outputs=0,analog=0,touch=0,pull=0;
  for(const signal of required){const kind=requirements[signal]||'output';if(kind==='output')outputs++;if(kind==='adc')analog++;if(kind==='touch')touch++;if(kind==='pull-input')pull++;}
  return required.length<=3&&outputs<=2&&analog<=1&&pull<=2&&!touch;
}
export function suntonLogicSupported(type) {
  return !type.startsWith('hardware.hmi.') && !type.startsWith('mainboard.memory.psram.') &&
    !type.startsWith('mainboard.memory.spiffs.') && !['audio.piano','audio.tts','audio.speed','audio.pitch'].includes(type);
}
