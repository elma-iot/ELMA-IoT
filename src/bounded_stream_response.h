#pragma once
#include <ESPAsyncWebServer.h>
#include <algorithm>
#include <memory>
// Copy at most two TCP packets in flight. A fixed staging packet retains
// unread bytes if TCP accepts only part of a storage-reader block.
template<class Reader> class BoundedStreamResponse : public AsyncWebServerResponse {
 std::shared_ptr<Reader> reader_;String head_;size_t headerOffset_=0,pending_=0,offset_=0;uint8_t packet_[1460];
 public:
 BoundedStreamResponse(std::shared_ptr<Reader> reader,size_t length,const char* type):reader_(std::move(reader)){_code=200;_contentType=type;_contentLength=length;_sendContentLength=true;_chunked=false;}
 bool _sourceValid()const override{return bool(reader_);}
 void _respond(AsyncWebServerRequest* request)override{addHeader("Connection","close",false);_assembleHead(head_,request->version());_state=RESPONSE_HEADERS;_ack(request,0,0);}
 size_t _ack(AsyncWebServerRequest* request,size_t length,uint32_t)override{
  _ackedLength+=length;size_t total=0;if(_state==RESPONSE_END||_state==RESPONSE_FAILED)return 0;
  for(;;){
   if(_state==RESPONSE_HEADERS&&headerOffset_==head_.length()){head_="";_state=RESPONSE_CONTENT;}
   if(_state==RESPONSE_CONTENT&&_sentLength==_contentLength){if(_ackedLength>=_writtenLength)_state=RESPONSE_END;return total;}
   bool headers=_state==RESPONSE_HEADERS;size_t inFlight=_writtenLength-std::min(_ackedLength,_writtenLength);size_t room=std::min(request->client()->space(),size_t(2920)-std::min(size_t(2920),inFlight));if(!room)return total;
   if(!headers&&offset_==pending_){pending_=reader_->read(packet_,std::min(sizeof(packet_),_contentLength-_sentLength));offset_=0;if(pending_==RESPONSE_TRY_AGAIN){pending_=0;return total;}if(!pending_){_state=RESPONSE_FAILED;request->client()->close(true);return total;}}
   const char* source=headers?head_.c_str()+headerOffset_:reinterpret_cast<const char*>(packet_+offset_);size_t remaining=headers?head_.length()-headerOffset_:pending_-offset_;size_t n=std::min(size_t(1460),std::min(room,remaining));size_t written=request->client()->write(source,n,ASYNC_WRITE_FLAG_COPY);_writtenLength+=written;total+=written;if(headers)headerOffset_+=written;else{offset_+=written;_sentLength+=written;}if(written<n)return total;
  }
 }
};
