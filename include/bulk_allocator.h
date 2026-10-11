#pragma once
#include <esp_heap_caps.h>
#include <memory>
template<class T> struct BulkAllocator {
 using value_type=T;
 BulkAllocator()=default;
 template<class U> BulkAllocator(const BulkAllocator<U>&){}
 T* allocate(size_t count){void* p=heap_caps_malloc(count*sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!p)p=heap_caps_malloc(count*sizeof(T),MALLOC_CAP_8BIT);if(!p)std::__throw_bad_alloc();return static_cast<T*>(p);}
 void deallocate(T* p,size_t){heap_caps_free(p);}
 template<class U> bool operator==(const BulkAllocator<U>&)const{return true;}
 template<class U> bool operator!=(const BulkAllocator<U>&)const{return false;}
};
