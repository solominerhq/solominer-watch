#pragma once
#include <stdint.h>
namespace clock_validation {
inline bool leap(int y) { return y%4==0 && (y%100!=0 || y%400==0); }
inline bool valid(int y,int m,int d,int h,int min,int sec) {
    static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(y<2024 || y>2099 || m<1 || m>12 || d<1 || h<0 || h>23 ||
       min<0 || min>59 || sec<0 || sec>59) return false;
    return d<=days[m-1]+(m==2 && leap(y));
}
inline bool bcd(uint8_t v) { return (v&15)<=9 && (v>>4)<=9; }
// PCF85063 seconds bit 7 indicates a stopped/unreliable oscillator.
inline bool registers(const uint8_t* r) {
    if((r[0]&0x80) || (r[1]&0x80) || (r[2]&0xC0) ||
       (r[3]&0xC0) || r[4]>6 || (r[5]&0xE0)) return false;
    const int fields[]={0,1,2,3,5,6};
    for(int i: fields) if(!bcd(r[i])) return false;
    const auto dec=[](uint8_t v){return (v>>4)*10+(v&15);};
    return valid(2000+dec(r[6]),dec(r[5]),dec(r[3]),dec(r[2]),dec(r[1]),dec(r[0]));
}
}
