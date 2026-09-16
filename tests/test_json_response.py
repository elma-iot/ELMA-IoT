"""Exercise real response code with partial writes, delayed ACKs and backpressure."""
import pathlib, shutil, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
VCVARS=pathlib.Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
class JsonResponseTests(unittest.TestCase):
 @unittest.skipUnless(VCVARS.is_file(),'MSVC compiler unavailable')
 def test_large_response_and_partial_reads(self):
  with tempfile.TemporaryDirectory() as folder:
   work=pathlib.Path(folder)
   for name in ('json_buffer_response.h','bounded_buffer_response.h'):shutil.copyfile(ROOT/'src'/name,work/name)
   (work/'ESPAsyncWebServer.h').write_text(r'''
#pragma once
#include <cstdint>
#include <string>
#include <algorithm>
using String=std::string;
constexpr uint8_t ASYNC_WRITE_FLAG_COPY=1;
enum { RESPONSE_HEADERS, RESPONSE_CONTENT, RESPONSE_END };
struct AsyncClient {
 size_t capacity=5744,limit=137,borrowed=0; std::string output;
 size_t space(){return capacity;}
 void setNoDelay(bool){}
 size_t write(const char* data,size_t n,uint8_t flags=1){n=std::min(n,limit);output.append(data,n);if(!flags)borrowed+=n;return n;}
};
struct AsyncWebServerRequest { AsyncClient tcp; AsyncClient* client(){return &tcp;} uint8_t version(){return 1;} };
class AsyncWebServerResponse {
public:
 virtual ~AsyncWebServerResponse()=default;
 virtual bool _sourceValid() const=0;
 virtual void _respond(AsyncWebServerRequest*)=0;
 virtual size_t _ack(AsyncWebServerRequest*,size_t,uint32_t)=0;
 void addHeader(const char*,const char*,bool){}
 void _assembleHead(String& out,uint8_t){out="HEADERS\r\n\r\n";}
 int _code=0,_state=RESPONSE_HEADERS; String _contentType;
 size_t _contentLength=0,_sentLength=0,_ackedLength=0,_writtenLength=0;
 bool _sendContentLength=false,_chunked=true;
};
''')
   (work/'main.cpp').write_text(r'''
#include "json_buffer_response.h"
#include <cassert>
void transfer(BoundedBufferResponse& response,const std::string& expected,bool flash=false){
 AsyncWebServerRequest request;
 request.tcp.capacity=0;response._respond(&request);assert(request.tcp.output.empty());
 request.tcp.capacity=5744;request.tcp.limit=0;assert(response._ack(&request,0,0)==0);
 request.tcp.limit=137;response._ack(&request,0,0);
 for(int i=0;response._state!=RESPONSE_END && i<10000;++i){
  size_t pending=response._writtenLength-response._ackedLength;
  assert(pending<=2920);
  if(pending){
   const size_t ack=std::min(pending,size_t(1+(i*73)%2000));
   request.tcp.limit=1+(i*37)%2000;
   response._ack(&request,ack,0);
  } else {request.tcp.limit=1460;response._ack(&request,0,0);}
  assert(response._writtenLength-response._ackedLength<=2920);
  const auto before=request.tcp.output.size();
  request.tcp.capacity=0;response._ack(&request,0,0);assert(request.tcp.output.size()==before);
  request.tcp.capacity=5744;
 }
 assert(response._state==RESPONSE_END);
 assert(request.tcp.output=="HEADERS\r\n\r\n"+expected);
 assert(request.tcp.borrowed==(flash?expected.size():0));
 assert(response._ack(&request,0,0)==0);
}
int main(){
 std::string expected(124547,'a');for(size_t i=0;i<expected.size();++i)expected[i]=char(i%251);
 BoundedBufferResponse asset(expected.data(),expected.size(),"application/javascript",200,true);transfer(asset,expected,true);
 char* data=static_cast<char*>(malloc(expected.size()+1));memcpy(data,expected.c_str(),expected.size()+1);
 JsonBufferResponse json(data,expected.size(),200);assert(json._sourceValid());transfer(json,expected);
 BoundedBufferResponse empty("",0,"text/plain");transfer(empty,"");
}
''')
   (work/'build.cmd').write_text(f'@call "{VCVARS}" >nul\ncl /nologo /std:c++17 /EHsc /I. main.cpp /Fe:response-test.exe\nif errorlevel 1 exit /b 1\nresponse-test.exe\n')
   r=subprocess.run(['cmd.exe','/c',str(work/'build.cmd')],cwd=work,capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
