#pragma once
#include <stddef.h>
#include <stdint.h>
namespace diagnostics {
constexpr size_t SegmentBytes=192*1024, BufferBytes=4096;
// Leave space for configuration replacement and filesystem housekeeping.
inline bool canAppend(size_t total,size_t occupied,size_t pending) {
  return occupied<=total && pending<=total-occupied && total-occupied-pending>=256*1024;
}
inline uint32_t crcUpdate(uint32_t crc,const uint8_t* data,size_t n) {
  while(n--) { crc ^= *data++; for(int i=0;i<8;++i) crc=(crc>>1)^((crc&1)?0xEDB88320u:0u); } return crc;
}
inline bool elapsed(uint32_t now,uint32_t then,uint32_t interval) { return uint32_t(now-then)>=interval; }
}
