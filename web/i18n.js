// Offline localization for the Android WebView. Each language lives in its own
// module under i18n/locales so additional languages can be added independently.
// Firmware/compiler logs stay verbatim to preserve diagnostic accuracy.
import {locales,localeByCode} from './i18n/locales/index.js';

const supported=locales.map(locale=>locale.code);
const english=localeByCode.en.messages;
const phrases=Object.keys(english);
const dictionaries=Object.fromEntries(locales.map(locale=>[
  locale.code,
  new Map(phrases.map(key=>[key,locale.messages[key]||key]))
]));
const fragmentKeys=phrases.filter(key=>key.length>=12&&!/^https?:/i.test(key)).sort((left,right)=>right.length-left.length);
const originals=new WeakMap(),attributeOriginals=new WeakMap();
let current='en',observer;

function normalized(value){
  const code=String(value||'en').toLowerCase().replace('_','-');
  if(code.startsWith('zh'))return'zh';
  return supported.find(item=>code===item||code.startsWith(item+'-'))||'en';
}
function translateText(value){
  const match=value.match(/^(\s*)(.*?)(\s*)$/s),core=match?.[2]||value;
  const position=core.match(/^Device Setup Wizard · (\d+) of (\d+)$/);
  let translated;
  if(position)translated=`${dictionaries[current].get('Device Setup Wizard')} · ${position[1]}/${position[2]}`;
  else if(core==='Device Setup Wizard · Build and flash')translated=`${dictionaries[current].get('Device Setup Wizard')} · ${dictionaries[current].get('Firmware')}`;
  else{
    translated=dictionaries[current].get(core)||core;
    // Template-generated status messages contain live device names, counts or
    // addresses. Translate their stable English fragments while retaining the
    // runtime value verbatim.
    if(translated===core&&current!=='en')for(const key of fragmentKeys){
      const replacement=dictionaries[current].get(key);
      if(replacement&&replacement!==key&&translated.includes(key))translated=translated.split(key).join(replacement);
    }
  }
  return`${match?.[1]||''}${translated}${match?.[3]||''}`;
}
function applyNode(root,refreshOriginal=false){
  if(!root||root.nodeType!==1&&root.nodeType!==9)return;
  const nodes=[];
  if(root.nodeType===1)nodes.push(root);
  root.querySelectorAll?.('*').forEach(node=>nodes.push(node));
  for(const element of nodes){
    if(element.matches?.('script,style,pre,code,textarea,[contenteditable="true"]'))continue;
    for(const child of element.childNodes)if(child.nodeType===Node.TEXT_NODE&&child.nodeValue.trim()){
      if(!originals.has(child))originals.set(child,child.nodeValue);
      else if(refreshOriginal&&child.nodeValue!==originals.get(child)&&child.nodeValue!==translateText(originals.get(child)))originals.set(child,child.nodeValue);
      const nextText=translateText(originals.get(child));
      if(child.nodeValue!==nextText)child.nodeValue=nextText;
    }
    for(const attribute of ['title','aria-label','placeholder','data-tooltip','alt'])if(element.hasAttribute?.(attribute)){
      let saved=attributeOriginals.get(element)||{};
      if(!(attribute in saved)){saved={...saved,[attribute]:element.getAttribute(attribute)};attributeOriginals.set(element,saved);}
      const nextAttribute=translateText(saved[attribute]);
      if(element.getAttribute(attribute)!==nextAttribute)element.setAttribute(attribute,nextAttribute);
    }
  }
}
function setLanguage(value){
  current=normalized(value);
  const locale=localeByCode[current];
  localStorage.setItem('elma.ui.language',current);
  document.documentElement.lang=current;
  document.documentElement.dir=locale.rtl?'rtl':'ltr';
  applyNode(document);
  window.dispatchEvent(new CustomEvent('elma-language-changed',{detail:{language:current}}));
}
window.ElmaI18n={
  supported,
  locales:locales.map(({code,nativeName,rtl})=>({code,nativeName,rtl})),
  phrases,
  t:key=>dictionaries[current].get(key)||key,
  get language(){return current;},
  setLanguage
};
let initial=localStorage.getItem('elma.ui.language')||navigator.language;
try{initial=window.ElmaAndroidConfig?.language?.()||initial;}catch{}
setLanguage(initial);
observer=new MutationObserver(records=>{
  observer.disconnect();
  for(const record of records){
    if(record.type==='characterData')applyNode(record.target.parentElement,true);
    for(const node of record.addedNodes)applyNode(node.nodeType===1?node:node.parentElement,true);
  }
  observer.observe(document.documentElement,{childList:true,characterData:true,subtree:true});
});
observer.observe(document.documentElement,{childList:true,characterData:true,subtree:true});
