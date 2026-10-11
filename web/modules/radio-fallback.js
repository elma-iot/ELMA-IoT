// Broadcaster streams (AvtoFM uses the device-tested saved URL) checked 2026-10-11; no directory required.
const groups = [
  ['Azerbaijan', 'AZ', [['AvtoFM', 'http://5.191.241.101:8000/avtofm']]],
  ['Russia', 'RU', [['Radio Record', 'https://radiorecord.hostingradio.ru/rr_main64.aacp', 'AAC', 64], ['Record Russian Mix', 'https://radiorecord.hostingradio.ru/rus64.aacp', 'AAC', 64]]],
  ['France', 'FR', [['FIP', 'https://icecast.radiofrance.fr/fip-midfi.mp3'], ['France Inter', 'https://icecast.radiofrance.fr/franceinter-midfi.mp3']]],
  ['Germany', 'DE', [['Deutschlandfunk', 'https://st01.sslstream.dlf.de/dlf/01/128/mp3/stream.mp3']]],
  ['Netherlands', 'NL', [['NPO Klassiek', 'https://icecast.omroep.nl/radio4-bb-mp3'], ['NPO Radio 1', 'https://icecast.omroep.nl/radio1-bb-mp3']]],
  ['Switzerland', 'CH', [['Radio Swiss Jazz', 'https://stream.srg-ssr.ch/srgssr/rsj/mp3/128'], ['Radio Swiss Classic', 'https://stream.srg-ssr.ch/srgssr/rsc_de/mp3/128']]],
  ['United States', 'US', [['SomaFM Groove Salad', 'https://ice5.somafm.com/groovesalad-128-mp3'], ['SomaFM Drone Zone', 'https://ice5.somafm.com/dronezone-128-mp3']]],
];

export function bundledRadioRows(path) {
  if (path === '/countries') return groups.map(([name, code, stations]) => ({ name, iso_3166_1: code, stationcount: stations.length }));
  const match = /^\/stations\/bycountry\/([^?]+)/.exec(path);
  if (!match) return [];
  let country;
  try { country = decodeURIComponent(match[1]); } catch { return []; }
  const group = groups.find(([name]) => name.toLowerCase() === country.toLowerCase());
  return group ? group[2].map(([name, url, codec = 'MP3', bitrate = 128]) => ({ name, url, url_resolved: url, codec, bitrate })) : [];
}
