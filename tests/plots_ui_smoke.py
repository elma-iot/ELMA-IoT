"""Real browser checks of conditional plots, paginated history and live controls."""
import json,threading
from pathlib import Path
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
from PySide6.QtWidgets import QApplication
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtCore import QUrl,QTimer
WEB=Path(__file__).resolve().parents[1]/'web'
HTML="""<!doctype html><button data-tab="plots" hidden><svg></svg></button><section id="tab-plots"><p data-plot-status></p><div data-plots></div></section>
<script type="module">
import {createPlotsTab} from '/modules/plots-tab.js';
const now=Date.now(),calls=[],intervals=[];let enabled=true,fail=false,pending=true;
const sample=(value,epoch)=>({plot:'Sensors',series:'Temperature',value,epoch,t:1,unit:'C'});
const request=async url=>{calls.push(url);
 if(url==='/api/plots/config')return {plots:enabled?[{plot:'Sensors'},{plot:'Voltage'}]:[],recordings:[{id:'save1',plot:'Sensors',path:'/'}],storage:{available:true}};
 if(url.startsWith('/api/plots/history')){if(pending){pending=false;return {pending:true};}if(fail)throw Error('External storage unavailable');const offset=new URL(url,location.href).searchParams.get('offset');return {samples:[sample(20+Number(offset),now-300000+Number(offset)*1000)],next:Number(offset)+10,eof:offset==='10'};}
 return {boot:1,cursor:1,samples:[sample(23,now)],storage:{available:true}};
};
const api=createPlotsTab({request,timers:{setInterval(f){intervals.push(f);return intervals.length;},clearInterval(){}}});
const wait=async fn=>{for(let i=0;i<150;i++){if(fn())return;await new Promise(r=>setTimeout(r,20));}throw Error('Timed out');};
try {
 await wait(()=>api.cards.size===2);if(document.querySelector('[data-tab="plots"]').hidden)throw Error('Plot tab hidden');
 api.setActive(true);await wait(()=>api.cards.get('Sensors').loaded);const c=api.cards.get('Sensors');
 if(c.history.length!==2||!calls.some(x=>x.includes('offset=10')))throw Error('History pagination missing');
 if(!api.cards.get('Voltage').load.disabled)throw Error('History enabled without Save Data');
 c.scroll.scrollLeft=0;c.scroll.dispatchEvent(new Event('scroll'));if(c.realtime)throw Error('Scrolling did not leave realtime');
 c.live.click();if(!c.realtime)throw Error('Realtime did not resume');
 c.from.value='';c.load.click();if(!c.note.textContent.includes('valid'))throw Error('Invalid dates accepted');
 c.from.value=new Date(now-3600000).toISOString().slice(0,19);fail=true;c.load.click();await wait(()=>c.note.textContent.includes('unavailable'));
 enabled=false;await intervals[0]();if(!document.querySelector('[data-tab="plots"]').hidden||api.cards.size)throw Error('Removed plots still visible');
 api.destroy();window.testResult={ok:true};
}catch(e){window.testResult={ok:false,error:e.stack};}
</script>"""
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  raw=HTML.encode() if self.path=='/' else (WEB/'modules/plots-tab.js').read_bytes()
  self.send_response(200);self.send_header('Content-Type','text/html' if self.path=='/' else 'text/javascript');self.end_headers();self.wfile.write(raw)
app=QApplication([]);server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
view=QWebEngineView();view.resize(900,800);view.setWindowOpacity(0);view.show();results=[]
def poll():
 def receive(value):
  if value and value!='null':results.append(json.loads(value));app.quit()
 view.page().runJavaScript('JSON.stringify(window.testResult||null)',receive)
view.load(QUrl(f'http://127.0.0.1:{server.server_port}/'));timer=QTimer();timer.timeout.connect(poll);timer.start(100);QTimer.singleShot(20000,app.quit)
try:app.exec()
finally:view.close();server.shutdown();server.server_close()
assert results and results[0]['ok'],results
print('Web Plotter: asynchronous history polling, discovery, paging, storage error, scrolling, dates and Realtime passed')
