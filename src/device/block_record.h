#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ui/block_found_sequence.h"

namespace block_record {
// The most recent pool-accepted block; reward is subsidy, not a payout receipt.
struct Record {
    uint32_t height=0, time=0, count=0;
    uint64_t subsidy=0;
    char hash[65]{};
    bool unread=false;
    bool valid() const { return count && ui::block_found::validHash(hash); }
};
constexpr size_t EncodedSize=93;
inline void put(uint8_t* p,uint64_t v,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i) p[i]=uint8_t(v>>(8*i));
}
inline uint64_t get(const uint8_t* p,unsigned bytes) {
    uint64_t v=0;for(unsigned i=0;i<bytes;++i)v|=uint64_t(p[i])<<(8*i);return v;
}
inline uint32_t checksum(const uint8_t* p,size_t n) {
    uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h;
}
inline void encode(const Record& r,uint8_t* p) {
    memcpy(p,"SMB1",4);put(p+4,r.height,4);put(p+8,r.time,4);put(p+12,r.count,4);
    put(p+16,r.subsidy,8);p[24]=r.unread;memcpy(p+25,r.hash,64);
    put(p+89,checksum(p,89),4);
}
inline bool decode(const uint8_t* p,size_t n,Record& result) {
    if(n!=EncodedSize || memcmp(p,"SMB1",4) || p[24]>1 || get(p+89,4)!=checksum(p,89))return false;
    Record r;r.height=get(p+4,4);r.time=get(p+8,4);r.count=get(p+12,4);
    r.subsidy=get(p+16,8);r.unread=p[24];memcpy(r.hash,p+25,64);
    if(!r.valid()) {
        return false;
    }
    result=r;
    return true;
}
// Backend writes one committed blob. Callers serialize access; no raw structs
// or padding are persisted. Failed writes retain a dirty in-memory record.
template<class Backend> class Store {
    Backend& backend_;Record current_;bool loaded_=false,dirty_=false;
public:
    explicit Store(Backend& backend):backend_(backend){}
    void init() {
        if(loaded_) {
            return;
        }
        uint8_t bytes[EncodedSize];
        size_t n=sizeof(bytes);
        if(backend_.load(bytes,n))decode(bytes,n,current_);
        loaded_=true;
    }
    Record snapshot() const { return current_; }
    bool accept(uint32_t height,uint32_t time,uint64_t subsidy,const char* hash,uint32_t count) {
        init();if(!ui::block_found::validHash(hash))return false;
        if(current_.valid() && !strcmp(current_.hash,hash))return true;
        Record next;next.height=height;next.time=time;next.subsidy=subsidy;
        next.count=count>current_.count ? count : current_.count+1;
        memcpy(next.hash,hash,65);next.unread=true;current_=next;dirty_=true;
        flush();return true;
    }
    void review(uint32_t count) {
        if(current_.valid() && current_.count==count && current_.unread){current_.unread=false;dirty_=true;}
    }
    bool flush() {
        if(!dirty_) {
            return true;
        }
        uint8_t bytes[EncodedSize];
        encode(current_,bytes);
        if(!backend_.save(bytes,sizeof(bytes))) {
            return false;
        }
        dirty_=false;
        return true;
    }
    bool dirty() const { return dirty_; }
};
void initialize();
Record snapshot();
uint32_t accept(uint32_t height,uint32_t time,uint64_t subsidy,const char* hash,uint32_t count);
void review(uint32_t count); // RAM only; service commits outside the LVGL lock.
void service();
}
