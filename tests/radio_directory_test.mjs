import assert from 'node:assert/strict';
import {createRadioDirectory} from '../web/modules/radio-directory.js';
const values=new Map(),storage={getItem:k=>values.get(k),setItem:(k,v)=>values.set(k,v)};
const countries=[{name:'France',stationcount:1200}],stations=[{name:'FIP',url:'http://icecast.radiofrance.fr/fip-midfi.mp3'}];
let offline=false;const calls=[];
const fetcher=async(url,options)=>{
  calls.push(url);assert(url.startsWith('https://'));assert.equal(options.credentials,'omit');
  if(offline||url.includes('de1.'))return {ok:false,status:503};
  return {ok:true,json:async()=>url.endsWith('/countries')?countries:stations};
};
const directory=createRadioDirectory({fetcher,storage});
assert.deepEqual((await directory.load('/countries')).rows,countries);
assert(calls[0].includes('de1.'));assert(calls[1].includes('all.'));
assert.deepEqual((await directory.load('/stations/bycountry/France')).rows,stations);
offline=true;assert.equal((await directory.load('/countries')).cached,true);
assert.equal((await directory.load('/stations/bycountry/France')).cached,true);
await assert.rejects(directory.load('/stations/bycountry/Unknown'),/Radio directory unavailable/);
const cache=JSON.parse(values.get('elma-radio-directory-v1'));cache['/countries'].saved=0;values.set('elma-radio-directory-v1',JSON.stringify(cache));
assert.equal((await directory.load('/countries')).bundled,true);
let mirrorCalls=[];
const mirror=createRadioDirectory({storage:{getItem:()=>null,setItem:()=>{}},fetcher:async url=>{
 mirrorCalls.push(url);return url.endsWith('/servers')?{ok:true,json:async()=>[{name:'mirror.api.radio-browser.info'},{name:'http://bad.api.radio-browser.info'},{name:'evil.example'}]}:url.includes('mirror.')?{ok:true,json:async()=>countries}:{ok:false,status:503};
}});
assert.deepEqual((await mirror.load('/countries')).rows,countries);
assert(!mirrorCalls.some(url=>url.includes('evil.example')||url.startsWith('http:')));
console.log('PASS directory failover, discovery, HTTPS-only requests, offline cache, stale-cache expiry and error guidance');


const cold=createRadioDirectory({storage:{getItem:()=>null,setItem:()=>{}},fetcher:async()=>({ok:false,status:503})});
assert.equal(cold.fallback('/countries').rows.length,7);
for(const country of cold.fallback('/countries').rows){
 const result=await cold.load('/stations/bycountry/'+encodeURIComponent(country.name));
 assert(result.bundled && result.rows.length);
 assert(result.rows.every(row=>/^https?:\/\//.test(row.url) && ['MP3','AAC'].includes(row.codec)));
}
// A successful response replaces the bundled rows and persists them for outages.
let online=true;const replacement=createRadioDirectory({storage,fetcher:async()=>online?{ok:true,json:async()=>[{name:'New France Station',url:'https://example.com/live.mp3'}]}:{ok:false,status:503}});
assert.equal((await replacement.load('/stations/bycountry/France')).bundled,undefined);
online=false;assert.equal((await replacement.load('/stations/bycountry/France')).rows[0].name,'New France Station');
console.log('PASS seven-country bundled fallback, MP3/AAC URLs, cache preference and successful server replacement');

assert.equal(cold.fallback('/stations/bycountry/Azerbaijan').rows[0].name,'AvtoFM');
assert.equal(cold.fallback('/stations/bycountry/Russia').rows.length,2);

assert(replacement.fallback('/countries').rows.some(row=>row.name==='Azerbaijan'));
assert(replacement.fallback('/countries').rows.some(row=>row.name==='Russia'));
