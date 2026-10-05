// Seven-bit addresses. Unknown/custom devices never receive implicit sharing.
// Reserve all possible addresses when hardware address straps are not declared.
// See I2C-AUDIT.md for sources, driver coverage and electrical requirements.
export const I2C_PROFILES = {
  'display:i2c-oled': [0x3c,0x3d],
  'display:custom': [],
  'audioIn:wm8960-audio-codec': [0x1a],
  'audioIn:es8388-audio-codec': [0x10,0x11],
  'sensor:ds3231-rtc': [0x68],
  'sensor:bno055': [0x28,0x29],
  'sensor:bno085-bno080': [0x4a,0x4b],
  'sensor:mpu6050': [0x68,0x69],
  'expansion:i2c-gpio-expander': [],
  'expansion:mcp23017-16-bit-io-expander': [0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27],
  'expansion:pcf8574-8-bit-io-expander': [0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f],
  'expansion:pcf8575-16-bit-io-expander': [0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27],
  'expansion:ads1115-16-bit-i2c': [0x48,0x49,0x4a,0x4b],
  'expansion:ads1015-12-bit-i2c': [0x48,0x49,0x4a,0x4b],
  'expansion:mcp4725-i2c': [0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67],
  // 0x70 All Call is enabled at power-on; keep it reserved too.
  'expansion:pca9685': Array.from({length:64},(_,i)=>0x40+i),
  'communication:i2c': [],
};
export function i2cAddresses(profile, address) {
  const options=I2C_PROFILES[profile];
  if(!options)return null;
  if(address!==undefined && address!==null && address!=='') {
    const value=Number(address);
    if((profile==='expansion:pca9685'&&value===112)||!Number.isInteger(value)||value<8||value>119||(options.length&&!options.includes(value)))return [];
    return profile==='expansion:pca9685'?[value,0x70]:[value];
  }
  return options;
}
export function i2cCompatible(a,b,first='',second='') {
  const allCall=first==='expansion:pca9685'&&second===first;
  return Boolean(a?.length&&b?.length&&!a.some(address=>b.includes(address)&&!(allCall&&address===112)));
}
export function i2cIssues(settings) {
  const object=value=>typeof value==='string'?JSON.parse(value||'{}'):(value||{});
  const profiles=object(settings.ui?.peripheralProfiles),bindings=object(settings.ui?.peripheralHelperBindings);
  const groups={audioIn:'audioInProfiles',display:'displayProfiles',sensor:'sensors',expansion:'expansions',communication:'communication'};
  const drivers=new Set(['display:i2c-oled','sensor:bno055','sensor:ds3231-rtc']);
  const devices=[],issues=[];
  for(const [group,key] of Object.entries(groups))for(const [index,profile] of (profiles[key]||[]).entries()) {
    const name=group+':'+profile;if(!(name in I2C_PROFILES))continue;
    const pins=bindings[`${group}:${index}`]||{},oled=group==='display'&&index===0&&profile==='i2c-oled';
    const pin=v=>v===undefined||v===null||v===''?-1:Number(v);
    const item={name,sda:pin(oled?settings.oled?.sdaPin:pins.SDA),scl:pin(oled?settings.oled?.sclPin:pins.SCL),addresses:i2cAddresses(name,oled?settings.oled?.i2cAddress:pins.I2C_ADDRESS)};
    if(item.sda<0||item.scl<0||item.sda===item.scl)issues.push(`${name}: assign two different SDA/SCL GPIOs`);
    if(pins.I2C_ADDRESS!==undefined&&pins.I2C_ADDRESS!==''&&!item.addresses?.length)issues.push(`${name}: invalid I2C address`);
    for(const other of devices) {
      const shared=[item.sda,item.scl].some(p=>p>=0&&[other.sda,other.scl].includes(p));
      const pair=item.sda===other.sda&&item.scl===other.scl;
      if((shared||(drivers.has(name)&&drivers.has(other.name)))&&!pair)issues.push(`${name} / ${other.name}: use the same SDA/SCL pair for the shared external I2C bus`);
      if(shared&&pair&&!i2cCompatible(item.addresses,other.addresses,item.name,other.name))issues.push(`${name} / ${other.name}: I2C address collision or unspecified hardware address`);
    }
    devices.push(item);
  }
  return issues;
}
