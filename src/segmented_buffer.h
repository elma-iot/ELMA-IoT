#pragma once
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdint>

// Fixed-size blocks fit fragmented ESP32 heap; never realloc a growing payload.
class SegmentedBuffer {
    struct Block {Block* next=nullptr;size_t used=0;uint8_t bytes[512];};
    Block *head_=nullptr,*tail_=nullptr;size_t offset_=0,length_=0;
    void* (*allocate_)(size_t);void (*free_)(void*);
public:
    explicit SegmentedBuffer(void* (*allocate)(size_t)=std::malloc,void (*release)(void*)=std::free):allocate_(allocate),free_(release){}
    SegmentedBuffer(const SegmentedBuffer&)=delete;
    ~SegmentedBuffer(){while(head_){auto* next=head_->next;free_(head_);head_=next;}}
    size_t write(uint8_t value){return write(&value,1);}
    size_t write(const uint8_t* data,size_t size){
        size_t copied=0;
        while(copied<size){
            if(!tail_||tail_->used==sizeof(tail_->bytes)){
                auto* block=static_cast<Block*>(allocate_(sizeof(Block)));if(!block)break;
                block->next=nullptr;block->used=0;if(tail_)tail_->next=block;else head_=block;tail_=block;
            }
            size_t n=std::min(size-copied,sizeof(tail_->bytes)-tail_->used);
            std::memcpy(tail_->bytes+tail_->used,data+copied,n);tail_->used+=n;copied+=n;length_+=n;
        }
        return copied;
    }
    size_t size()const{return length_;}
    const char* data()const{return head_?reinterpret_cast<const char*>(head_->bytes+offset_):nullptr;}
    size_t available()const{return head_?head_->used-offset_:0;}
    void consume(size_t size){
        while(size&&head_){size_t n=std::min(size,available());size-=n;offset_+=n;
            if(offset_==head_->used){auto* old=head_;head_=old->next;if(!head_)tail_=nullptr;free_(old);offset_=0;}}
    }
};
