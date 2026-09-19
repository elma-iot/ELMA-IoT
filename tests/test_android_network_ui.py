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
app=QApplication([]);view=QWebEngineView();view.resize(412,915)
inject=QWebEngineScript();inject.setInjectionPoint(QWebEngineScript.DocumentCreation);inject.setWorldId(QWebEngineScript.MainWorld);inject.setSourceCode('''window.mockMqtt=false;window.calls=[];window.ElmaAndroidConfig={read:()=>'',save:()=>''};window.ElmaAndroidJobs={request:(id,json)=>{const r=JSON.parse(json);window.calls.push(r.op);let result={};if(r.op==='networkStatus')result={wifiConnected:true,mqttConnected:window.mockMqtt,ssid:'test',ip:'192.168.1.2',wifiRssi:-50};if(r.op==='wifiScan')result={started:true,scanning:false,networks:[{ssid:'dual',frequency:2412,rssi:-71,encrypted:true},{ssid:'dual',frequency:5180,rssi:-20,encrypted:true},{ssid:'five-only',frequency:5805,rssi:-40}]};if(r.op==='mqtt'){window.mockMqtt=r.action==='connect';result={ok:true,connected:window.mockMqtt};}setTimeout(()=>window.elmaNativeReply(id,result,null),0);}};''');view.page().scripts().insert(inject)
results={}
def scan():
 view.page().runJavaScript('document.querySelector(\'[data-tab="wifi"]\').click();document.getElementById("scanWifiButton").click()');QTimer.singleShot(2500,checkscan)
def checkscan():
 view.page().runJavaScript('JSON.stringify([...document.getElementById("wifiNetworkList").options].map(o=>o.textContent))',lambda text:results.update(networks=json.loads(text)));view.page().runJavaScript('document.querySelector(\'[data-tab="mqtt"]\').click();document.querySelector(\'[name="mqtt.host"]\').value="127.0.0.1";document.getElementById("mqttConnectButton").click()');QTimer.singleShot(4500,connected)
def connected():
 view.page().runJavaScript('JSON.stringify({label:document.getElementById("mqttConnectButton").textContent,calls:window.calls})',lambda text:results.update(connected=json.loads(text)));QTimer.singleShot(200,disconnect)
def disconnect():
 view.page().runJavaScript('document.getElementById("mqttConnectButton").click()');QTimer.singleShot(3000,finish)
def finish():
 view.page().runJavaScript('document.getElementById("mqttConnectButton").textContent',lambda text:(results.update(disconnected=text),app.quit()))
view.setUrl(QUrl(f'http://127.0.0.1:{server.server_port}/index.html?elmaRuntime=pc-designer'));view.show();QTimer.singleShot(4000,scan);QTimer.singleShot(30000,app.quit);app.exec();server.shutdown();print(json.dumps(results));
sys.exit(0 if len(results.get('networks',[]))==2 and '-71' in results['networks'][1] and results.get('connected',{}).get('label')=='Disconnect MQTT' and results.get('disconnected')=='Test Connection' and results['connected']['calls'].count('mqtt')>=1 else 1)
