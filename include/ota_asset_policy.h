#pragma once
#include <cstdio>
#include <cstring>
// Exact board identity is required; matching ESP family alone is insufficient.
inline bool matchesPublishedBoardAsset(const char* asset,const char* chip,unsigned board,const char* version){
    if(!board||!asset||!chip||!version||!*version)return false;
    char expected[192];int size=std::snprintf(expected,sizeof(expected),"elma-%s-board%u-%s.bin",chip,board,version);
    return size>0&&size<int(sizeof(expected))&&std::strcmp(asset,expected)==0;
}

// SemVer precedence: a stable version supersedes its test build; build metadata is ignored.
inline int comparePublishedVersions(const char* left,const char* right){
    if(*left=='v'||*left=='V')++left;
    if(*right=='v'||*right=='V')++right;
    auto number=[](const char*& p){unsigned long n=0;while(*p>='0'&&*p<='9')n=n*10+(*p++-'0');return n;};
    for(int i=0;i<3;++i){
        auto l=number(left),r=number(right);if(l!=r)return l<r?-1:1;
        if(*left=='.')++left;if(*right=='.')++right;
    }
    bool lp=*left=='-',rp=*right=='-';
    if(lp!=rp)return lp?-1:1;
    if(!lp)return 0;
    ++left;++right;
    while(true){
        const char* le=left;const char* re=right;bool ln=true,rn=true;
        while(*le&&*le!='.'&&*le!='+'){ln=ln&&*le>='0'&&*le<='9';++le;}
        while(*re&&*re!='.'&&*re!='+'){rn=rn&&*re>='0'&&*re<='9';++re;}
        auto ll=le-left,rl=re-right;
        if(!ll||!rl)return ll==rl?0:(ll?1:-1);
        if(ln!=rn)return ln?-1:1;
        if(ln&&ll!=rl)return ll<rl?-1:1;
        int c=std::strncmp(left,right,ll<rl?ll:rl);if(c)return c<0?-1:1;
        if(ll!=rl)return ll<rl?-1:1;
        bool lm=*le=='.',rm=*re=='.';if(lm!=rm)return lm?1:-1;if(!lm)return 0;
        left=le+1;right=re+1;
    }
}
