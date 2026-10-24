#pragma once
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "segmented_buffer.h"

class SegmentedJsonResponse final : public AsyncWebServerResponse {
    SegmentedBuffer data_;String head_;size_t headerOffset_=0;bool valid_=false,headOnly_=false;
public:
    SegmentedJsonResponse(const JsonDocument& doc,size_t length,int status){
        _code=status;_contentType="application/json";_contentLength=length;_sendContentLength=true;_chunked=false;
        valid_=serializeJson(doc,data_)==length&&data_.size()==length;
    }
    bool _sourceValid()const override{return valid_;}
    void _respond(AsyncWebServerRequest* request)override{
        headOnly_=request->method()==HTTP_HEAD;addHeader("Connection","close",false);
        _assembleHead(head_,request->version());_state=RESPONSE_HEADERS;request->client()->setNoDelay(true);_ack(request,0,0);
    }
    size_t _ack(AsyncWebServerRequest* request,size_t length,uint32_t)override{
        _ackedLength+=length;if(_state==RESPONSE_END)return 0;size_t total=0;
        while(true){
            if(_state==RESPONSE_HEADERS&&headerOffset_==head_.length()){head_="";_state=RESPONSE_CONTENT;}
            if(_state==RESPONSE_CONTENT&&(headOnly_||_sentLength==_contentLength)){
                if(_ackedLength>=_writtenLength)_state=RESPONSE_END;return total;
            }
            const bool headers=_state==RESPONSE_HEADERS;
            const char* source=headers?head_.c_str()+headerOffset_:data_.data();
            size_t remaining=headers?head_.length()-headerOffset_:data_.available();
            size_t inFlight=_writtenLength-std::min(_ackedLength,_writtenLength),room=1024-std::min(size_t(1024),inFlight);
            size_t count=std::min(size_t(512),std::min(remaining,std::min(room,request->client()->space())));if(!count)return total;
            size_t written=request->client()->add(source,count,ASYNC_WRITE_FLAG_COPY);if(written)request->client()->send();
            _writtenLength+=written;total+=written;
            if(headers)headerOffset_+=written;else{_sentLength+=written;data_.consume(written);}
            if(written<count)return total;
        }
    }
};
