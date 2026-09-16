export function freePeripheralPin({candidates,occupied,blocked,preferred=''}) {
  const allowed=candidates.map(String).filter(value=>/^\d+$/.test(value));
  const free=value=>!occupied.has(Number(value))&&!blocked.has(Number(value));
  const value=String(preferred);
  return allowed.includes(value)&&free(value)?value:allowed.find(free)||'';
}

export function occupiedPeripheralPins({roles,ownRoles=new Set(),bindings={},ownSlot=''}) {
  const occupied=new Set(),add=value=>{if(value!==null&&value!==undefined&&value!==''&&Number.isInteger(Number(value))&&Number(value)>=0)occupied.add(Number(value));};
  for(const [role,pin] of roles)if(!ownRoles.has(role))add(pin);
  for(const [slot,signals] of Object.entries(bindings))if(slot!==ownSlot)for(const [signal,pin] of Object.entries(signals||{})){
    if(!['CONTACT','SOURCE','MAIN_CONTROL','INPUT_VOLTAGE','OUTPUT_VOLTAGE'].includes(signal))add(pin);
  }
  return occupied;
}
