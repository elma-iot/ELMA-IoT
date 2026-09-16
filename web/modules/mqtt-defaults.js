export function applyMqttDefaults(settings) {
  settings.mqtt ||= {};
  const name=String(settings.device?.deviceName || settings.device?.friendlyName || 'elma-device').trim()
    .toLowerCase().replace(/[^a-z0-9_-]+/g,'-').replace(/^-+|-+$/g,'') || 'elma-device';
  if(!String(settings.mqtt.clientId || '').trim())settings.mqtt.clientId=name;
  if(!String(settings.mqtt.baseTopic || '').trim())settings.mqtt.baseTopic=`elma/${name}`;
  if(!settings.mqtt.port)settings.mqtt.port=1883;
  return settings;
}
