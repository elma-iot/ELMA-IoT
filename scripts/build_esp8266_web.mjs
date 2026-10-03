import {build} from 'esbuild';
import fs from 'node:fs';
// Derive chrome from the full interface so icon/header changes stay unified.
const full=fs.readFileSync(new URL('../web/index.html',import.meta.url),'utf8');
let hero=full.slice(full.indexOf('<div class="hero-header">'),full.indexOf('<div class="stats">'));
for(const id of ['headerRebootButton','headerShutdownButton']) hero=hero.replace(new RegExp('<button id="'+id+'"[\\s\\S]*?</button>'),'');
const buttons=[...full.matchAll(/<button\b[^>]*data-tab="([^"]+)"[^>]*>[\s\S]*?<\/button>/g)];
const tabs=['gpio','logics','wifi','mqtt','device','hardware','plots','security','info'].map(id=>{
 const match=buttons.find(m=>m[1]===id); if(!match)throw Error('Missing shared tab '+id);
 return match[0];
}).join('\n');
const template=fs.readFileSync(new URL('../web/esp8266/shell.html',import.meta.url),'utf8');
fs.writeFileSync(new URL('../web/esp8266/index.html',import.meta.url),template.replace('<!--SHARED_HEADER-->',hero).replace('<!--SHARED_TABS-->',tabs));
import {fileURLToPath} from 'node:url';
await build({absWorkingDir:fileURLToPath(new URL('..',import.meta.url)),entryPoints:['web/esp8266/app.js'],bundle:true,minify:true,format:'esm',outfile:'web/esp8266/app.bundle.js'});

import {locales} from '../web/i18n/locales/index.js';
import {runtime} from './firmware_i18n_runtime.mjs';
const folder=new URL('../web/esp8266/locales/',import.meta.url);
fs.mkdirSync(folder,{recursive:true});
const corpus=['../web/esp8266/index.html','../web/esp8266/app.js','../web/esp8266/app.bundle.js'].map(p=>fs.readFileSync(new URL(p,import.meta.url),'utf8')).join('\n');
for(const locale of locales) {
 const messages=Object.fromEntries(Object.entries(locale.messages).filter(([key])=>corpus.includes(key)));
 fs.writeFileSync(new URL(locale.code+'.js',folder),runtime({...locale,messages}));
}
