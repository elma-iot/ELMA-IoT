import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const source = await readFile(new URL('../web/modules/logs-tab.js', import.meta.url), 'utf8');
const {createLogsTab} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

function fixture(request, clipboard) {
  const events = {};
  const button = {addEventListener: (name, fn) => events[name] = fn, focus() {}};
  const terminal = {textContent:'', scrollTop:0, scrollHeight:100, clientHeight:100};
  const status = {textContent:''};
  let copied = '', fallbackCalls = 0;
  const visibility = {};
  const document = {
    hidden:false,
    getElementById: id => ({deviceLogText:terminal,deviceLogStatus:status,deviceLogCopy:button})[id],
    addEventListener: (name,fn) => visibility[name] = fn,
    createElement: () => ({style:{}, select() {copied=this.value;}, remove(){}}),
    body:{append(){}},
    execCommand: () => {fallbackCalls++; return true;},
  };
  const scheduled = new Map();
  let timerId=0;
  const timers = {setTimeout: fn => {scheduled.set(++timerId,fn);return timerId;},clearTimeout:id=>scheduled.delete(id)};
  const tab = createLogsTab({request,document,navigator:{clipboard},timers});
  return {tab,terminal,status,document,events,visibility,scheduled,get copied(){return copied;},get fallbackCalls(){return fallbackCalls;}};
}
const settle = () => new Promise(resolve=>setImmediate(resolve));

test('logs render as literal text, use revision polling, and stop when inactive', async()=>{
  const calls=[];
  const f=fixture(async url=>{calls.push(url);return calls.length===1 ? {text:'<script>unsafe()</script>',revision:'boot:2',notice:'Internal'} : {unchanged:true,revision:'boot:2'};});
  f.tab.setActive(true); await settle();
  assert.equal(f.terminal.textContent,'<script>unsafe()</script>');
  const poll=[...f.scheduled.values()][0]; await poll();
  assert.match(calls[1],/boot%3A2/);
  assert.equal(f.terminal.textContent,'<script>unsafe()</script>');
  f.tab.setActive(false); assert.equal(f.scheduled.size,0);
});
test('reading older output does not jump to the bottom on refresh', async()=>{
  const f=fixture(async()=>({text:'new output',revision:'a'}));
  f.terminal.scrollHeight=1000; f.terminal.clientHeight=100; f.terminal.scrollTop=120;
  f.tab.setActive(true); await settle(); assert.equal(f.terminal.scrollTop,120);
});
test('HTTP clipboard fallback copies the exact displayed text', async()=>{
  const f=fixture(async()=>({text:'boot\nreset=watchdog',revision:'a'}));
  f.tab.setActive(true); await settle(); await f.events.click();
  assert.equal(f.copied,'boot\nreset=watchdog'); assert.equal(f.fallbackCalls,1);
});
test('denied clipboard API falls back to HTTP-compatible copy', async()=>{
  const f=fixture(async()=>({text:'diagnostic',revision:'a'}),{writeText:async()=>{throw new Error('denied');}});
  f.tab.setActive(true); await settle(); await f.events.click(); assert.equal(f.copied,'diagnostic');
});
test('hidden documents stop polling and resume when visible',async()=>{
  let calls=0;
  const f=fixture(async()=>{calls++;return {text:'log',revision:'a'};});
  f.tab.setActive(true); await settle();
  f.document.hidden=true; f.visibility.visibilitychange(); assert.equal(f.scheduled.size,0);
  f.document.hidden=false; f.visibility.visibilitychange(); await settle(); assert.equal(calls,2);
});
test('busy storage retries while preserving the previous output',async()=>{
  let calls=0;
  const f=fixture(async()=>{if(calls++) throw new Error('busy'); return {text:'keep me',revision:'a'};});
  f.tab.setActive(true); await settle(); await [...f.scheduled.values()][0]();
  assert.equal(f.terminal.textContent,'keep me'); assert.match(f.status.textContent,/busy/); assert.equal(f.scheduled.size,1);
});
test('designer has an honest empty state and does not poll device logs',async()=>{
  const f=fixture(async()=>({source:'designer',text:'',notice:'Open device'}));
  f.tab.setActive(true); await settle(); assert.equal(f.scheduled.size,0); assert.match(f.terminal.textContent,/flashed device/);
});
