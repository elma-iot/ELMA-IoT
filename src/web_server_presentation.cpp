#include "web_server.h"
#if APP_SUNTON_PANEL == 3 && !defined(APP_DISABLE_WEB_UI)
#include <esp_heap_caps.h>

namespace {
// Deliberately self-contained: no full app, fonts, diagram or status snapshot.
const char presentationHtml[] PROGMEM=R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ELMA controls</title>
<style>body{font:16px system-ui;background:#111820;color:#edf2fa;max-width:38rem;margin:8vh auto;padding:1rem}section{padding:1.5rem;background:#202c39;border-radius:1rem}button,input{font:inherit;padding:.7rem;margin:.4rem 0;border-radius:.5rem}button{cursor:pointer;background:#eea313;color:#111;border:0}p{line-height:1.5}#error{color:#ffc28c}input{max-width:8rem}small{color:#b8c7d8}</style>
<section><h1 id="heading">LCD controls active</h1><p>The LCD and full web controls share memory. Music and wired automations continue during a switch.</p><p id="track">Connecting…</p><p id="state"></p><button id="claim">Use web controls</button> <button id="stop">Stop playback</button><div id="unlock" hidden><label>Unlock PIN <input id="pin" type="password" inputmode="numeric" maxlength="4" autocomplete="off"></label><button id="confirm">Unlock and switch</button></div><p id="error" role="status"></p><small>When the LCD times out, web controls become available. Double-tap the sleeping LCD to switch back.</small></section>
<script>
const $=id=>document.getElementById(id);let switching=false,polling=false;
const candidate=new URLSearchParams(location.search).get('return')||'/';
const back=candidate.startsWith('/')&&!candidate.startsWith('//')&&!candidate.includes('\\')&&!candidate.startsWith('/presentation')?candidate:'/';
async function request(path,options){const r=await fetch(path,{cache:'no-store',...options});let d={};try{d=await r.json()}catch{}if(!r.ok){if(r.status===423)$('unlock').hidden=false;throw Error(d.error||`Device busy (${r.status}); retry shortly.`)}return d}
async function poll(){if(polling)return;polling=true;try{const d=await request('/api/presentation');$('heading').textContent=d.mode==='pending'?'Switching to LCD':'LCD controls active';$('track').textContent=d.title||'No track selected';$('state').textContent=`${d.state||'idle'} · Volume ${d.volume??0}% · ${Math.floor(d.position||0)} s`;$('stop').disabled=!['playing','buffering'].includes(d.state);if(d.mode==='web')location.replace(back)}catch(e){$('error').textContent=e.message}finally{polling=false}}
async function claim(){if(switching)return;switching=true;$('claim').disabled=true;try{await request('/api/presentation/claim',{method:'POST',headers:{'X-ELMA-Presentation':'1'}});$('error').textContent='Releasing LCD memory…';await poll()}catch(e){$('error').textContent=e.message}finally{switching=false;$('claim').disabled=false}}
$('claim').onclick=claim;
$('confirm').onclick=async()=>{try{await request('/api/security',{method:'POST',headers:{'Content-Type':'application/json','X-ELMA-Security':'1'},body:JSON.stringify({action:'unlock',pin:$('pin').value})});$('pin').value='';$('unlock').hidden=true;await claim()}catch(e){$('pin').value='';$('error').textContent=e.message}};
$('stop').onclick=async()=>{try{await request('/api/stop',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});await poll()}catch(e){$('error').textContent=e.message}};
poll();setInterval(poll,1500);
</script></html>)HTML";
}

void WebServerManager::servePresentationPage(AsyncWebServerRequest* request){
    if(!ensureAuthorized(request))return;
    auto* response=request->beginResponse_P(200,"text/html; charset=utf-8",presentationHtml);
    response->addHeader("Cache-Control","no-store");request->send(response);
}

bool WebServerManager::gatePresentation(AsyncWebServerRequest* request){
    if(!presentationState_ || !(presentationState_()&3))return false;
    const String& path=request->url();
    if(path=="/api/presentation" || path=="/api/presentation/claim" || path=="/api/security" || path=="/api/stop" || path=="/api/volume")return false;
    if((request->method()==HTTP_GET || request->method()==HTTP_HEAD) &&
        (path=="/" || path=="/fallback" || path=="/fallback/" || path=="/presentation")){
        servePresentationPage(request);return true;
    }
    auto* response=request->beginResponse(503,"application/json","{\"error\":\"LCD controls active; switch to web controls.\",\"presentation\":\"lcd\"}");
    response->addHeader("X-ELMA-Presentation","lcd");response->addHeader("Retry-After","1");
    response->addHeader("Cache-Control","no-store");request->send(response);return true;
}

void WebServerManager::registerPresentationRoutes(){
    server_.on("/presentation",HTTP_GET,[this](AsyncWebServerRequest* r){servePresentationPage(r);});
    server_.on("/api/presentation",HTTP_GET,[this](AsyncWebServerRequest* r){
        if(!ensureAuthorized(r))return;
        JsonDocument doc;const uint8_t state=presentationState_?presentationState_():0;
        doc["mode"]=(state&3)==0?"web":(state&3)==1?"lcd":"pending";
        doc["lcdEnabled"]=bool(state&4);doc["locked"]=security_.locked();
        doc["cpuMHz"]=ESP.getCpuFreqMHz();doc["heap8"]=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        if(!appState_ || !appState_->appendPlaybackSummary(doc.as<JsonObject>())){
            r->send(503,"application/json","{\"error\":\"Playback status busy; retry shortly.\"}");return;
        }
        sendJson(r,doc);
    });
    server_.on("/api/presentation/claim",HTTP_POST,[this](AsyncWebServerRequest* r){
        if(!ensureAuthorized(r))return;
        if(r->header("X-ELMA-Presentation")!="1" ||
           (r->hasHeader("Origin") && r->header("Origin")!="http://"+r->host() && r->header("Origin")!="https://"+r->host())){
            r->send(403,"application/json","{\"error\":\"Same-origin presentation request required\"}");return;
        }
        if(presentationWeb_)presentationWeb_();
        r->send(202,"application/json","{\"mode\":\"pending\"}");
    });
}
#endif
