#include "../src/static_asset_policy.h"
#include "../src/bounded_buffer_response.h"
#include "../src/bounded_stream_response.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <algorithm>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
struct Reader {std::string bytes;size_t offset=0,calls=0;bool stall=false;size_t read(uint8_t* out,size_t n){calls++;if(stall&&calls%3==0)return RESPONSE_TRY_AGAIN;n=std::min(n,bytes.size()-offset);memcpy(out,bytes.data()+offset,n);offset+=n;return n;}};
void pump(AsyncWebServerResponse& r,AsyncWebServerRequest& q){r._respond(&q);size_t ack=0;for(int i=0;i<200000&&r._state!=RESPONSE_END&&r._state!=RESPONSE_FAILED;i++){size_t now=q.socket.wire.size();r._ack(&q,now-ack,0);ack=now;}assert(r._state==RESPONSE_END||r._state==RESPONSE_FAILED);}
std::string body(const std::string& s){return s.substr(s.find("\r\n\r\n")+4);}
int main(int argc,char** argv){using namespace StaticAssetPolicy;
 if(argc>1&&std::string(argv[1])=="--wire"){
  std::string path;if(!webPath(argv[4],path))return 2;auto e=encodings(argv[3]);std::string base=std::string(argv[2])+path;std::ifstream raw(base,std::ios::binary),gz(base+".gz",std::ios::binary);auto variant=choose(e,bool(raw),bool(gz));bool zipped=variant==Variant::Gzip;std::string data;int status=200;
  if(variant==Variant::Missing){status=404;data="Not found";}else if(variant==Variant::NotAcceptable){status=406;data="Not acceptable";}else{auto& f=zipped?gz:raw;data.assign(std::istreambuf_iterator<char>(f),{});}
  const char* type=path.size()>5&&path.substr(path.size()-5)==".html"?"text/html; charset=utf-8":path.size()>3&&path.substr(path.size()-3)==".js"?"application/javascript; charset=utf-8":path.size()>4&&path.substr(path.size()-4)==".css"?"text/css; charset=utf-8":"application/json";
  AsyncWebServerRequest q;q.verb=std::string(argv[5])=="HEAD"?HTTP_HEAD:HTTP_GET;auto reader=std::make_shared<Reader>();reader->bytes=data;std::unique_ptr<AsyncWebServerResponse> r;
  if(q.verb==HTTP_HEAD||status!=200)r=std::make_unique<BoundedBufferResponse>(data.data(),data.size(),type,status);
  else r=std::make_unique<BoundedStreamResponse<Reader>>(reader,data.size(),type);
  r->addHeader("Vary","Accept-Encoding");if(zipped)r->addHeader("Content-Encoding","gzip");r->addHeader("X-Content-Type-Options","nosniff");pump(*r,q);
#ifdef _WIN32
  _setmode(_fileno(stdout),_O_BINARY);
#endif
  std::cout.write(q.socket.wire.data(),q.socket.wire.size());return 0;
 }
 assert(choose(encodings("gzip"),true,true)==Variant::Gzip);assert(choose(encodings("gzip;q=0, *;q=1"),true,true)==Variant::Identity);assert(choose(encodings("br, gzip;q=0.5"),false,true)==Variant::Gzip);assert(choose(encodings(""),true,true)==Variant::Identity);assert(choose(encodings(""),false,true)==Variant::NotAcceptable);assert(choose(encodings("identity;q=0,gzip;q=0"),true,true)==Variant::NotAcceptable);assert(choose(encodings("*;q=0"),true,true)==Variant::NotAcceptable);assert(choose(encodings("*;q=0,identity;q=1"),true,true)==Variant::Identity);assert(choose(encodings("GZIP ; Q = 1.000"),true,true)==Variant::Gzip);assert(choose(encodings("xgzip"),false,true)==Variant::NotAcceptable);assert(choose(encodings("gzip;q=broken"),false,true)==Variant::NotAcceptable);assert(choose(encodings("gzip;q=1.001"),false,true)==Variant::NotAcceptable);assert(choose(encodings("*;q=0.7"),false,true)==Variant::Gzip);assert(choose(encodings("gzip"),false,false)==Variant::Missing);
 assert(etagMatches("\"other\", W/\"test\"","\"test\""));assert(etagMatches("*","W/\"test\""));assert(!etagMatches("\"test-gz\"","\"test-raw\""));
 std::string path;assert(webPath("/web/",path)&&path=="/web/index.html");assert(webPath("/web/app.js.gz",path)&&path=="/web/app.js");assert(webPath("/web/some%20file.js",path));for(auto bad:{"/api/status","/web/../secret","/web/%2e%2e/secret","/web/%252e%252e/secret","/web/a\\b","/web//x","/web/%00x","/web/x%","/web/./x"})assert(!webPath(bad,path));
 std::string bytes(15000,'x');for(size_t i=0;i<bytes.size();i++)bytes[i]=char(i%256);
 for(size_t partial:{size_t(1),size_t(57),size_t(1460)}){AsyncWebServerRequest q;q.socket.maximum=partial;auto reader=std::make_shared<Reader>();reader->bytes=bytes;reader->stall=true;BoundedStreamResponse<Reader> r(reader,bytes.size(),"application/octet-stream");pump(r,q);assert(body(q.socket.wire)==bytes);assert(!q.socket.closed);}
 {AsyncWebServerRequest q;q.verb=HTTP_HEAD;q.socket.maximum=19;BoundedBufferResponse r("",15000,"application/javascript");pump(r,q);assert(body(q.socket.wire).empty());assert(q.socket.wire.find("Content-Length: 15000")!=std::string::npos);}
 {AsyncWebServerRequest q;auto reader=std::make_shared<Reader>();reader->bytes="short";BoundedStreamResponse<Reader> r(reader,1000,"application/javascript");pump(r,q);assert(q.socket.closed&&r._state==RESPONSE_FAILED);assert(body(q.socket.wire)=="short");}
 {AsyncWebServerRequest q;BoundedBufferResponse r(bytes.data(),bytes.size(),"text/plain");pump(r,q);assert(body(q.socket.wire)==bytes);}
 std::cout<<"PASS gzip negotiation, traversal, variant ETags, HEAD, partial writes, stalled reads and premature EOF\n";
}
