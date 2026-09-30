"""Exercise real security UI in two independent browser frames against a mock API.

This checks browser protocol/presentation, not ESP NVS or cryptographic execution.
Run with the Windows application's Python environment; no firmware build required.
"""
import json
import re
import pathlib
import threading
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from PySide6.QtWidgets import QApplication
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWebEngineCore import QWebEnginePage
from PySide6.QtCore import QTimer, QUrl

WEB=pathlib.Path(__file__).resolve().parents[1]/'web'
state=dict(enabled=False,locked=False,available=True,timeoutSeconds=300,retryAfterSeconds=0)
pin='';calls=[]
fixture='''<!doctype html><html><head><link rel="stylesheet" href="/security.css"></head><body>
<section class="hero"><h1 id="deviceTitle">Test device</h1><button id="headerActionsButton">Settings</button><div id="headerActionsMenu"></div></section>
<div class="grid"><section><div id="tab-security"><p data-security-state></p><input data-security-timeout value="300"><button data-security-save-timeout>Save</button><button data-security-action="set">Set PIN</button><button data-security-action="change">Change PIN</button><button data-security-action="disable">Disable</button><button data-security-action="lock">Lock</button></div></section></div>
<script type="module">import {createSecurityInterface} from '/modules/security-tab.js';window.elmaSecurity.destroy();window.elmaSecurity=createSecurityInterface({reload:()=>window.reloaded=(window.reloaded||0)+1});</script></body></html>'''
tab=re.search(r'<button[^>]*data-tab="security"[^>]*>.*?</button>',(WEB/'index.html').read_text(encoding='utf-8'),re.S).group(0)
fixture=fixture.replace('<div class="grid">','<div class="grid">'+tab)
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def send(self,data,kind='application/json',code=200):
        raw=(json.dumps(data) if kind=='application/json' else data).encode()
        self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Content-Length',len(raw));self.end_headers();self.wfile.write(raw)
    def do_GET(self):
        if self.path=='/api/security':return self.send(state)
        if self.path=='/api/status':return self.send({'device':{'friendlyName':'Test device'}})
        if self.path=='/':return self.send('<iframe id="a" src="/fixture"></iframe><iframe id="b" src="/fixture"></iframe>','text/html')
        if self.path=='/fixture':return self.send(fixture,'text/html')
        path=WEB/self.path.lstrip('/')
        if path.is_file() and path.suffix in ('.js','.css'):return self.send(path.read_text(encoding='utf-8'),'text/css' if path.suffix=='.css' else 'text/javascript')
        self.send({},code=404)
    def do_POST(self):
        global pin
        data=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        if self.path=='/mock-retry':state['retryAfterSeconds']=60;return self.send(state)
        assert self.headers.get('X-ELMA-Security')=='1'
        action=data['action'];calls.append(action)
        if action=='set':
            assert len(data['newPin'])==4 and data['newPin']==data['confirmPin'];pin=data['newPin'];state.update(enabled=True,locked=False)
        elif action=='lock':state['locked']=True
        elif action=='verify':
            if data['pin']!=pin:return self.send(dict(state,error='Incorrect PIN'),code=403)
            return self.send(dict(state,ticket='test-one-use-ticket'))
        elif action=='change':
            assert data['ticket']=='test-one-use-ticket' and data['newPin']==data['confirmPin'];pin=data['newPin'];state['locked']=False
        elif action in ('unlock','disable'):
            if data['pin']!=pin:return self.send(dict(state,error='Incorrect PIN'),code=403)
            state['locked']=False
            if action=='disable':state['enabled']=False;pin=''
        self.send(state)

SCRIPT=r'''(async()=>{
 const wait=async f=>{for(let i=0;i<160;i++){if(f())return;await new Promise(r=>setTimeout(r,40));}throw Error('Timed out: '+f);};
 const a=()=>document.querySelector('#a').contentWindow,b=()=>document.querySelector('#b').contentWindow;
 const click=(w,action)=>w.document.querySelector(`[data-security-action="${action}"]`).click();
 const submit=(w,value)=>{w.document.querySelector('[data-pin]').value=value;w.document.querySelector('.security-dialog').requestSubmit();};
 const unlocked=w=>w.elmaSecurity?.state?.locked===false;
 try{
  await wait(()=>unlocked(a())&&unlocked(b()));
  if(a().document.querySelector('[data-tab="security"]').textContent.trim())throw Error('Security tab must be icon only');
  const openPath=a().document.querySelector('[data-security-shackle]').getAttribute('d');
  click(a(),'set');submit(a(),'0123');await wait(()=>a().document.querySelector('[data-tip]').textContent.includes('again'));submit(a(),'0123');await wait(()=>a().elmaSecurity.state.enabled&&a().document.querySelector('.security-mask').hidden);
  if(a().document.querySelector('[data-security-shackle]').getAttribute('d')===openPath)throw Error('PIN set did not close lock icon');
  if(a().elmaSecurity.state.locked)throw Error('Icon test needs enabled but unlocked state');
  click(a(),'lock');await wait(()=>b().elmaSecurity.state.locked);
  if(!b().document.querySelector('#headerActionsButton').disabled)throw Error('Gear enabled when locked');
  if(!b().document.querySelector('.grid > section').inert)throw Error('Lower frame not inert');
  if(getComputedStyle(b().document.querySelector('.hero')).filter!=='none')throw Error('Header was blurred');
  submit(b(),'9999');await wait(()=>b().document.querySelector('[data-error]').textContent.includes('Incorrect'));
  submit(b(),'0123');await wait(()=>unlocked(a())&&unlocked(b()));
  click(a(),'change');await wait(()=>a().document.querySelector('[data-tip]').textContent.includes('old'));await wait(()=>!a().document.querySelector('[data-pin]').disabled);submit(a(),'0123');await wait(()=>a().document.querySelector('[data-tip]').textContent.includes('new four'));submit(a(),'4567');await wait(()=>a().document.querySelector('[data-tip]').textContent.includes('again'));submit(a(),'4567');await wait(()=>unlocked(a())&&a().document.querySelector('.security-mask').hidden);
  click(a(),'disable');await wait(()=>!a().document.querySelector('[data-pin]').disabled);submit(a(),'4567');await wait(()=>!a().elmaSecurity.state.enabled&&!a().elmaSecurity.state.locked);
  if(a().document.querySelector('[data-security-shackle]').getAttribute('d')!==openPath)throw Error('Disabling PIN did not reopen lock icon');
  click(a(),'set');submit(a(),'0123');await wait(()=>a().document.querySelector('[data-tip]').textContent.includes('again'));submit(a(),'0123');await wait(()=>a().elmaSecurity.state.enabled&&a().document.querySelector('.security-mask').hidden);click(a(),'lock');await wait(()=>a().elmaSecurity.state.locked);
  await fetch('/mock-retry',{method:'POST',body:'{}'});await a().elmaSecurity.refresh();
  if(!a().document.querySelector('[data-submit]').disabled||!a().document.querySelector('[data-error]').textContent.includes('1:00'))throw Error('Cooldown not enforced by UI');
  window.testResult={ok:true};
 }catch(error){window.testResult={ok:false,error:error.message};}
})();'''

app=QApplication([]);server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
view=QWebEngineView();view.resize(1100,800);view.setWindowOpacity(0);view.show();results=[]
def loaded(ok):
    if ok:view.page().runJavaScript(SCRIPT)
def poll():
    def receive(value):
        if value and value!='null':results.append(json.loads(value));app.quit()
    view.page().runJavaScript('JSON.stringify(window.testResult||null)',receive)
view.loadFinished.connect(loaded);view.load(QUrl(f'http://127.0.0.1:{server.server_port}/'))
timer=QTimer();timer.timeout.connect(poll);timer.start(250);QTimer.singleShot(30000,app.quit)
try:app.exec()
finally:view.close();server.shutdown()
assert results and results[0]['ok'],results
assert 'activity' not in calls,'Background polling must not refresh inactivity'
print('Two-browser security UI: set, lock, wrong PIN, unlock, change, disable, cooldown and header isolation passed')
