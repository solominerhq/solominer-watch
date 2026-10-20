#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <cmath>
namespace mining_stats {
struct Snapshot {
    double best=0;
    uint32_t mega=0, shares=0, blocks=0, jobs=0;
    uint64_t uptime=0;
    uint32_t remainder=0;
};
constexpr size_t Bytes=44;
inline uint32_t crc(const uint8_t* p,size_t n) {
    uint32_t c=0xFFFFFFFFu;
    while(n--) {c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xEDB88320u:0);}
    return ~c;
}
inline void put(uint8_t* p,uint64_t n,size_t size) {
    for(size_t i=0;i<size;++i){p[i]=uint8_t(n);n>>=8;}
}
inline uint64_t get(const uint8_t* p,size_t size) {
    uint64_t n=0;for(size_t i=0;i<size;++i)n|=uint64_t(p[i])<<(8*i);return n;
}
inline void encode(const Snapshot& s,uint8_t* p) {
    memcpy(p,"SMS1",4);uint64_t bits=0;static_assert(sizeof(s.best)==8,"64-bit double required");
    memcpy(&bits,&s.best,8);put(p+4,bits,8);
    put(p+12,s.mega,4);put(p+16,s.shares,4);put(p+20,s.blocks,4);
    put(p+24,s.jobs,4);put(p+28,s.uptime,8);put(p+36,s.remainder,4);
    put(p+40,crc(p,40),4);
}
inline bool decode(const uint8_t* p,size_t n,Snapshot& out) {
    if(n!=Bytes || memcmp(p,"SMS1",4) || get(p+40,4)!=crc(p,40))return false;
    Snapshot s;uint64_t bits=get(p+4,8);memcpy(&s.best,&bits,8);
    if(!std::isfinite(s.best) || s.best<0)return false;
    s.mega=get(p+12,4);s.shares=get(p+16,4);s.blocks=get(p+20,4);
    s.jobs=get(p+24,4);s.uptime=get(p+28,8);s.remainder=get(p+36,4);
    out=s;return true;
}
}
