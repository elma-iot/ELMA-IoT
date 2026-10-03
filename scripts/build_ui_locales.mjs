import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const root=path.resolve(process.argv[2]||'.'),out=path.resolve(process.argv[3]||path.join(root,'web'));
const {locales}=await import(pathToFileURL(path.join(root,'web/i18n/locales/index.js')));
import {runtime} from './firmware_i18n_runtime.mjs';

function translationCorpus(){
  const chunks=[];
  const visit=directory=>{
    for(const entry of fs.readdirSync(directory,{withFileTypes:true})){
      if(['node_modules','tests','i18n','.git'].includes(entry.name))continue;
      const file=path.join(directory,entry.name);
      if(entry.isDirectory())visit(file);
      else if(/\.(?:js|mjs|html|cpp|h)$/.test(entry.name)&&!['generated_web_assets.cpp','firmware-i18n.js'].includes(entry.name))chunks.push(fs.readFileSync(file,'utf8'));
    }
  };
  visit(path.join(root,'web'));
  visit(path.join(root,'src'));
  visit(path.join(root,'include'));
  return chunks.join('\n');
}

function firmwareLocale(locale,corpus){
  if(locale.code==='en')return locale;
  const messages=Object.fromEntries(Object.entries(locale.messages).filter(([key])=>corpus.includes(key)));
  console.log(`[firmware-i18n] ${locale.code}: ${Object.keys(messages).length} of ${Object.keys(locale.messages).length} messages used by device firmware`);
  return {...locale,messages};
}

const code=process.env.ELMA_COMPILED_LANGUAGE||'en';
const completeLocale=locales.find(locale=>locale.code===code);
if(!completeLocale)throw new Error('Unsupported firmware language: '+code);
const selected=firmwareLocale(completeLocale,translationCorpus());
fs.mkdirSync(out,{recursive:true});
fs.writeFileSync(path.join(out,'firmware-i18n.js'),runtime(selected).replace('(function(){',"(function(){if(new URLSearchParams(location.search).get('elmaRuntime')==='pc-designer')return;"));
fs.writeFileSync(path.join(out,'desktop-locales.json'),JSON.stringify(locales.map(({code,nativeName,messages})=>({code,nativeName,messages}))));
console.log('Firmware languages: English + '+code);
