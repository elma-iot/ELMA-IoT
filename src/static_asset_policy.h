#pragma once
#include <string>
#include <string_view>
#include <cstdint>
namespace StaticAssetPolicy {
inline std::string_view trim(std::string_view s){while(!s.empty()&&(s.front()==' '||s.front()=='\t'))s.remove_prefix(1);while(!s.empty()&&(s.back()==' '||s.back()=='\t'))s.remove_suffix(1);return s;}
inline bool equal(std::string_view a,std::string_view b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();i++){char c=a[i];if(c>='A'&&c<='Z')c+=32;if(c!=b[i])return false;}return true;}
inline int quality(std::string_view s){s=trim(s);if(s.empty()||(s.front()!='0'&&s.front()!='1'))return 0;int n=s.front()=='1'?1000:0;if(s.size()==1)return n;if(s[1]!='.'||s.size()>5)return 0;int factor=100;for(size_t i=2;i<s.size();i++,factor/=10){if(s[i]<'0'||s[i]>'9'||(n==1000&&s[i]!='0'))return 0;if(s.front()=='0')n+=(s[i]-'0')*factor;}return n;}
struct Encoding {int gzip=0,identity=1000;};
inline Encoding encodings(std::string_view header){int gz=-1,id=-1,wild=-1;
 while(!header.empty()){size_t comma=header.find(',');auto item=trim(header.substr(0,comma));size_t semi=item.find(';');auto name=trim(item.substr(0,semi));int q=1000;
  if(semi!=std::string_view::npos){auto parameter=trim(item.substr(semi+1));auto eq=parameter.find('=');q=eq!=std::string_view::npos&&equal(trim(parameter.substr(0,eq)),"q")?quality(parameter.substr(eq+1)):0;}
  if(equal(name,"gzip"))gz=q;else if(equal(name,"identity"))id=q;else if(name=="*")wild=q;
  if(comma==std::string_view::npos)break;header.remove_prefix(comma+1);
 }
 return {gz>=0?gz:wild>=0?wild:0,id>=0?id:wild==0?0:1000};
}
enum class Variant { Missing, Gzip, Identity, NotAcceptable };
inline Variant choose(Encoding e,bool raw,bool gzip){if(gzip&&e.gzip>0)return Variant::Gzip;if(raw&&e.identity>0)return Variant::Identity;return raw||gzip?Variant::NotAcceptable:Variant::Missing;}
inline bool etagMatches(std::string_view header,std::string_view tag){if(tag.substr(0,2)=="W/")tag.remove_prefix(2);while(!header.empty()){size_t comma=header.find(',');auto item=trim(header.substr(0,comma));if(item=="*")return true;if(item.substr(0,2)=="W/")item.remove_prefix(2);if(item==tag)return true;if(comma==std::string_view::npos)break;header.remove_prefix(comma+1);}return false;}
inline int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
inline bool webPath(std::string_view url,std::string& path){path.clear();if(url.size()>512)return false;for(size_t i=0;i<url.size();i++){unsigned char c=url[i];if(c=='%'){if(i+2>=url.size())return false;int hi=hex(url[i+1]),lo=hex(url[i+2]);if(hi<0||lo<0)return false;c=hi*16+lo;i+=2;}if(c<32||c==127||c=='\\'||c=='%'||c=='?'||c=='#'||c==':')return false;path+=char(c);}
 if(path=="/web"||path=="/web/")path="/web/index.html";
 if(path.rfind("/web/",0)!=0)return false;
 size_t start=1;while(start<path.size()){size_t end=path.find('/',start);auto segment=path.substr(start,end==std::string::npos?end:end-start);if(segment.empty()||segment=="."||segment=="..")return false;if(end==std::string::npos)break;start=end+1;}
 if(path.size()>3&&path.substr(path.size()-3)==".gz")path.resize(path.size()-3);
 return path.size()>5&&path.back()!='/';
}
}
