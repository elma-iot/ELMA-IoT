// The running image is authoritative: board names cannot reveal size-fit cuts.
export function runtimePeripheralOptions(options, capabilities, group, index = 0, selected = 'none', activeProfiles = []) {
  if (!capabilities || capabilities.schema !== 1 || !group) return options;
  const allowed = capabilities.profiles?.[group] || ['none'];
  const maximum = Number(capabilities.maxInstances?.[group] || 0);
  const slots = value => Math.min(maximum, Number(capabilities.profileSlots?.[group]?.[value] ?? maximum));
  const full = value => activeProfiles.filter((profile,slot) => slot !== index && profile === value).length >= Number(capabilities.profileCountLimits?.[group]?.[value] ?? maximum);
  const cost = value => Number(capabilities.resourceCosts?.[group]?.[value] || 0);
  const fits = value => cost(value) <= Number(capabilities.resourceAvailable?.pwmChannels ?? Infinity) + cost(selected);
  return options.filter(option => option.value === 'none' || (index < slots(option.value) && allowed.includes(option.value) && !full(option.value) && fits(option.value)) || option.value === selected)
    .map(option => option.value !== 'none' && (index >= slots(option.value) || !allowed.includes(option.value) || full(option.value) || !fits(option.value))
      ? {...option, disabled: true, label: `${option.label} (unavailable in this firmware)`} : option);
}
