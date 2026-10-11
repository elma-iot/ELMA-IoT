#pragma once
#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstddef>
constexpr size_t RESPONSE_TRY_AGAIN=size_t(-1);
constexpr int ASYNC_WRITE_FLAG_COPY=1,HTTP_GET=1,HTTP_HEAD=2;
constexpr int RESPONSE_HEADERS=1,RESPONSE_CONTENT=2,RESPONSE_END=3,RESPONSE_FAILED=4;
class String:public std::string {public:using std::string::string;using std::string::operator=;String(const std::string& s):std::string(s){};};
struct Client {std::string wire;size_t maximum=1460;bool closed=false;size_t space()const{return 4096;}void setNoDelay(bool){}size_t write(const char* data,size_t n,int=0){n=std::min(n,maximum);wire.append(data,n);return n;}void close(bool){closed=true;}};
struct AsyncWebServerRequest {int verb=HTTP_GET;Client socket;int method()const{return verb;}int version()const{return 1;}Client* client(){return &socket;}};
class AsyncWebServerResponse {public:int _code=200,_state=0;String _contentType;size_t _contentLength=0,_sentLength=0,_ackedLength=0,_writtenLength=0;bool _sendContentLength=true,_chunked=false;std::vector<std::pair<std::string,std::string>> headers;
 virtual ~AsyncWebServerResponse()=default;virtual bool _sourceValid()const{return true;}virtual void _respond(AsyncWebServerRequest*)=0;virtual size_t _ack(AsyncWebServerRequest*,size_t,uint32_t)=0;
 void addHeader(const char* key,const char* value,bool=true){headers.emplace_back(key,value);}void addHeader(const char* key,const String& value,bool=true){headers.emplace_back(key,value);}
 void _assembleHead(String& output,int){output="HTTP/1.1 "+std::to_string(_code)+" OK\r\nContent-Type: "+_contentType+"\r\nContent-Length: "+std::to_string(_contentLength)+"\r\n";for(auto& h:headers)output+=h.first+": "+h.second+"\r\n";output+="\r\n";}
};
