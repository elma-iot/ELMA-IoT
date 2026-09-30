#include "web_server.h"
#ifndef APP_DISABLE_WEB_UI
void WebServerManager::registerSecurityRoutes(){
    server_.on("/api/security",HTTP_GET,[this](AsyncWebServerRequest* request){
        if(!ensureAuthorized(request))return;
        JsonDocument response;security_.status(response.to<JsonObject>());sendJson(request,response);
    });
    server_.on("/api/security",HTTP_POST,[this](AsyncWebServerRequest* request){
        if(!ensureAuthorized(request))return;
        // Custom header + strict Origin comparison stop cross-site form/fetch
        // submissions. No CORS permission is granted for this endpoint.
        if(!request->hasHeader("X-ELMA-Security")||request->header("X-ELMA-Security")!="1" ||
           (request->hasHeader("Origin")&&request->header("Origin")!="http://"+request->host()&&request->header("Origin")!="https://"+request->host())){
            request->send(403,"application/json","{\"error\":\"Same-origin security request required\"}");return;
        }
        if(!request->contentType().equalsIgnoreCase("application/json")||request->contentLength()>512||!request->_tempObject){
            request->send(400,"application/json","{\"error\":\"Invalid security request\"}");return;
        }
        JsonDocument input,response;
        if(deserializeJson(input,static_cast<const char*>(request->_tempObject))||!input.is<JsonObject>()){
            request->send(400,"application/json","{\"error\":\"Invalid security request\"}");return;
        }
        int code=security_.command(input.as<JsonVariantConst>(),response.to<JsonObject>());
        // The transport owns/free()s _tempObject; wipe PIN material first.
        memset(request->_tempObject,0,request->contentLength());
        sendJson(request,response,code);
    },nullptr,[](AsyncWebServerRequest* request,uint8_t* data,size_t length,size_t index,size_t total){
        if(!total||total>512)return;
        if(index==0&&!request->_tempObject)request->_tempObject=calloc(total+1,1);
        if(!request->_tempObject||index+length>total){request->abort();return;}
        memcpy(static_cast<uint8_t*>(request->_tempObject)+index,data,length);
    });
}
#endif
