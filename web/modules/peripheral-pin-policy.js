// Shared electrical requirements for peripheral selectors and default allocation.
// ESP8266/8285 ADC is a dedicated TOUT pad, not a digital GPIO.
export function adcGpioPins(chip) {
  if (chip === "esp32") return [32,33,34,35,36,39]; // ADC1: ADC2 conflicts with Wi-Fi.
  if (["esp32s2","esp32s3"].includes(chip)) return Array.from({length:20},(_,i)=>i+1);
  if (["esp32c2","esp32c3"].includes(chip)) return [0,1,2,3,4];
  if (chip === "esp32c6") return [0,1,2,3,4,5,6];
  return [];
}
export function peripheralPinRequirement(group, profile, signal) {
  profile=String(profile).toLowerCase(); signal=String(signal).toUpperCase();
  if (signal === "TOUCH" && profile.includes("native-touch")) return "touch";
  if (["ADC","AOUT","VRX","VRY"].includes(signal)
    || (group === "sensor" && profile.includes("voltage-divider") && signal === "SIGNAL")
    || (["OUT","SIGNAL"].includes(signal) && /analog|line-in|electret|max9814|max4466/.test(profile))) return "adc";
  if (group === "expansion" && /40[56][17]/.test(profile) && signal === "SIG") return "adc";
  if (group === "input" && /limit-switch|rotary-encoder|button/.test(profile) && !profile.includes("ttp223")) return "pull-input";
  if (signal === "MISO" || signal === "TACH" || signal === "AUX" || signal === "Q7" || signal === "INT" || signal === "IRQ" || signal === "DRDY"
    || (group === "communication" && signal === "TX")
    || (group === "audioIn" && ["DOUT","SD","DATA","OUT"].includes(signal))
    || (group === "input" && ["SIG","OUT","A","B","SW","COM"].includes(signal))) return "input";
  // I2C/1-Wire/SDMMC bidirectional lines need output capability too.
  return "output";
}
export function safePeripheralPins({chip, inputPins, outputPins, exposedPins, blocked=new Set(), requirement="output", touchPins=[], override=false}) {
  const output=new Set(outputPins), adc=new Set(adcGpioPins(chip)), touch=new Set(touchPins);
  const chipReserved = chip === "esp32" ? [0,1,3,5,12,15] : chip === "esp32s3" ? [0,3,19,20,43,44,45,46]
    : chip === "esp32s2" ? [0,19,20,26,43,44,45,46] : chip === "esp32c3" ? [2,8,9,18,19,20,21]
    : chip === "esp32c2" ? [8,9,19,20] : chip === "esp32c6" ? [4,5,8,9,12,13,15,16,17]
    : [0,1,2,3,15];
  const reserved=new Set([...blocked,...chipReserved]);
  return inputPins.filter(pin=>exposedPins.has(pin) && (override || !reserved.has(pin))
    && (requirement !== "output" || (output.has(pin) && !(chip === "esp32s2" && pin===46)))
    && (requirement !== "adc" || (adc.has(pin) && (override || !(["esp32s2","esp32s3"].includes(chip) && pin>10)))) // Prefer ADC1 while firmware Wi-Fi runs.
    && (requirement !== "touch" || touch.has(pin))
    && (requirement !== "pull-input" || !((chip === "esp32" && pin>=34) || (["esp8266","esp8285"].includes(chip) && pin===16))));
}
export function occupiedPinChoices(pins, assignments, ownKey, selected="") {
  const choices=pins.map(pin=>{
    const owners=[...new Set(assignments.filter(a=>a.key!==ownKey && a.pin===pin).map(a=>a.label))];
    return {value:String(pin),label:owners.length ? `GPIO${pin} (${owners.join(" / ")})` : `GPIO${pin}`,disabled:owners.length>0};
  });
  if (selected!=="" && !choices.some(option=>option.value===String(selected))) choices.unshift({value:String(selected),label:`GPIO${selected} — invalid/reserved for this board (reassign)`,disabled:true});
  return choices;
}
