#pragma once
#include <map>
#include <string>
#include <memory>
#include <vector>
#include <cstring>
#include <algorithm>
class File {
 std::shared_ptr<std::vector<uint8_t>> bytes_;size_t pos_=0;
public:
 File()=default;explicit File(std::shared_ptr<std::vector<uint8_t>> bytes):bytes_(bytes){}
 explicit operator bool()const{return !!bytes_;}
 bool isDirectory()const{return false;}
 size_t size()const{return bytes_?bytes_->size():0;}
 bool seek(size_t n){if(!bytes_||n>size())return false;pos_=n;return true;}
 size_t read(uint8_t* dest,size_t n){if(!bytes_)return 0;n=std::min(n,size()-pos_);memcpy(dest,bytes_->data()+pos_,n);pos_+=n;return n;}
 size_t write(const uint8_t* data,size_t n){if(!bytes_)return 0;bytes_->resize(std::max(size(),pos_+n));memcpy(bytes_->data()+pos_,data,n);pos_+=n;return n;}
};
namespace fs {class FS {
public:
 std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> files;
 File open(const char* path,const char*) {auto it=files.find(path);return it==files.end()?File():File(it->second);}
};}
