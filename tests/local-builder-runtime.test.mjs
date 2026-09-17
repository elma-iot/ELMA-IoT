import fs from 'node:fs';
import assert from 'node:assert/strict';
import test from 'node:test';
const path=new URL('../web/modules/local-builder.js',import.meta.url);
const source=fs.readFileSync(path,'utf8').replace(/^import .*?;\s*/, 'const boardChipFamily=()=>"esp32s3",applyBoardFilter=()=>{};');
const {createLocalBuilder}=await import('data:text/javascript;base64,'+Buffer.from(source).toString('base64'));
const widget=()=>({value:'usb',checked:true,style:{},classList:{toggle(){}},options:[],closest(){return this},querySelector(){return null},appendChild(x){this.options.push(x)},addEventListener(){}});
function setup(host,search='',active=false){
 globalThis.window={location:{hostname:host,search}};globalThis.document={body:{classList:{add(){}}},getElementById(){return null},querySelector(){return null}};globalThis.Option=class{constructor(text,value){this.text=text;this.value=value}};
 const elements=new Proxy({}, {get(target,key){return target[key]||=(widget())}});const requests=[];
 globalThis.fetch=async path=>{requests.push(path);return {ok:true,json:async()=>path.endsWith('status')?{active,version:'test'}:{ports:[]}}};
 return {elements,requests,builder:createLocalBuilder({elements,currentSettingsSnapshot:()=>({}),setMessage(){},toast(){}})};
}
test('live device and Android do not probe desktop-only builder API',async()=>{for(const host of ['192.168.1.150','appassets.androidplatform.net']){const {builder,requests}=setup(host);await builder.initialize();assert.deepEqual(requests,[])}});
test('desktop designer activates builder and retains returned version',async()=>{const {builder,elements,requests}=setup('127.0.0.1','?elmaRuntime=pc-designer',true);await builder.initialize();assert.equal(elements.localBuilderPanel.hidden,false);assert.equal(elements.localBuilderBadge.textContent,'Local compiler test');assert.deepEqual(requests,['/api/builder/status','/api/builder/ports'])});
