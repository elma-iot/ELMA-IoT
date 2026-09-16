import os,sys,json,threading,http.server,functools,pathlib
os.environ['QT_QPA_PLATFORM']='offscreen';os.environ['QTWEBENGINE_CHROMIUM_FLAGS']='--disable-gpu'
from PySide6.QtCore import QTimer,QUrl
from PySide6.QtWidgets import QApplication
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWebEngineCore import QWebEngineScript
root=pathlib.Path('../Android/app/src/main/assets/site').resolve()
class Handler(http.server.SimpleHTTPRequestHandler):
 def log_message(self,*args):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(root)));threading.Thread(target=server.serve_forever,daemon=True).start()
app=QApplication([]);view=QWebEngineView();view.resize(360,800)
inject=QWebEngineScript();inject.setInjectionPoint(QWebEngineScript.DocumentCreation);inject.setWorldId(QWebEngineScript.MainWorld);inject.setSourceCode('''window.ElmaAndroidConfig={read:()=>JSON.stringify({ui:{gpioBoardSelection:'esp32-c3'}})};window.ElmaAndroidJobs={request:(id,json)=>setTimeout(()=>window.elmaNativeReply(id,JSON.parse(json).op==='usbList'?[]:null,null),0)};''');view.page().scripts().insert(inject)
cases=[(360,800,'light'),(360,800,'dark'),(800,360,'dark'),(673,841,'light')];results=[];index=0
check='''JSON.stringify({ready:document.getElementById('configurationLabel').textContent.includes('esp32-c3'),overflow:[...document.querySelectorAll('button,input,select')].filter(e=>{const r=e.getBoundingClientRect();return r.width&&r.height&&(r.left<0||r.right>innerWidth+1)}).map(e=>e.id)})'''
def inspect(text):
 global index
 data=json.loads(text);results.append(data);view.grab().save(f'.elma-flasher-build/android-build-{index}.png');index+=1
 if index==len(cases):app.quit()
 else:nextcase()
def nextcase():
 w,h,theme=cases[index];view.resize(w,h);view.page().runJavaScript(f"document.documentElement.dataset.elmaTheme='{theme}'");QTimer.singleShot(500,lambda:view.page().runJavaScript(check,inspect))
view.setUrl(QUrl(f'http://127.0.0.1:{server.server_port}/build.html'));view.show();QTimer.singleShot(2000,nextcase);QTimer.singleShot(20000,app.quit);app.exec();server.shutdown();print(results);sys.exit(0 if len(results)==4 and all(r['ready'] and not r['overflow'] for r in results) else 1)
