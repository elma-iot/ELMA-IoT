import os,sys,json,threading,http.server,functools,pathlib
os.environ['QT_QPA_PLATFORM']='offscreen';os.environ['QTWEBENGINE_CHROMIUM_FLAGS']='--disable-gpu';os.environ['QT_QUICK_BACKEND']='software'
from PySide6.QtCore import QTimer,QUrl
from PySide6.QtWidgets import QApplication
from PySide6.QtWebEngineWidgets import QWebEngineView
root=pathlib.Path('../Android/app/src/main/assets/site').resolve()
class Handler(http.server.SimpleHTTPRequestHandler):
 def log_message(self,*args):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(root)));threading.Thread(target=server.serve_forever,daemon=True).start()
app=QApplication([]);view=QWebEngineView();view.resize(360,800)
cases=[(360,800,'light','gpio'),(360,800,'dark','gpio'),(412,915,'dark','wifi'),(800,360,'light','mqtt'),(673,841,'dark','device'),(960,720,'light','gpio'),(360,800,'dark','firmware'),(360,800,'dark','info')];results=[];index=0
script='''JSON.stringify({ready:!!document.getElementById('gpioBoardSelector')?.options.length,boards:document.getElementById('gpioBoardSelector')?.options.length,bg:getComputedStyle(document.body).backgroundColor,overflow:[...document.querySelectorAll('input,select,button')].filter(e=>{const r=e.getBoundingClientRect();return r.width>0&&r.height>0&&!e.closest('.tabs')&&(r.left< -1||r.right>innerWidth+1)}).map(e=>e.id||e.className),title:document.getElementById('deviceTitle')?.textContent,selected:document.getElementById('gpioBoardSelector')?.value,audio:document.getElementById('peripheralAudioProfile')?.value,pins:['audio.wsPin','audio.bclkPin','audio.doutPin'].map(name=>document.querySelector('[name="'+name+'"]')?.value)})'''
def inspect(text):
 global index
 if not text:return
 data=json.loads(text)
 if not data['ready']:QTimer.singleShot(1000,nextcase);return
 data['case']=cases[index];results.append(data);view.grab().save(f'.elma-flasher-build/android-{index}.png');index+=1
 if index==len(cases):app.quit()
 else:QTimer.singleShot(100,nextcase)
def nextcase():
 if index>=len(cases):return
 w,h,theme,tab=cases[index];view.resize(w,h)
 view.page().runJavaScript(f"document.documentElement.dataset.elmaTheme='{theme}';document.querySelector('[data-tab=\"{tab}\"]')?.click()")
 QTimer.singleShot(1000,lambda:view.page().runJavaScript(script,inspect))
def seed():
 view.page().runJavaScript("""(async()=>{const c=await fetch('/api/settings').then(r=>r.json());c.ui.gpioBoardAutodetect=false;c.ui.gpioBoardSelection='wemos-lolin32-mini';c.ui.peripheralProfiles.audioProfile='pcm5102-i2s-dac';c.ui.peripheralProfiles.audioProfiles=['pcm5102-i2s-dac'];c.audio.enabled=true;c.audio.wsPin=25;c.audio.bclkPin=26;c.audio.doutPin=22;await fetch('/api/settings',{method:'POST',body:JSON.stringify(c)});window.savedConfig=true;})()""")
 QTimer.singleShot(1500,reload_saved)
def reload_saved():
 view.reload();QTimer.singleShot(4000,nextcase)
view.setUrl(QUrl(f'http://127.0.0.1:{server.server_port}/index.html?elmaRuntime=pc-designer'));view.show();QTimer.singleShot(4000,seed);QTimer.singleShot(60000,app.quit)
app.exec();server.shutdown();pathlib.Path('.elma-flasher-build/android-layout-results.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2));view.close()
sys.exit(0 if len(results)==len(cases) and all(not r['overflow'] and r['selected']=='wemos-lolin32-mini' and r['audio']=='pcm5102-i2s-dac' and r['pins']==['25','26','22'] for r in results) else 1)
