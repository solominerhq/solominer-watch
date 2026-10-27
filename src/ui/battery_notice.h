#pragma once
#include <stdint.h>
namespace ui {
class BatteryNotice {
    bool lowPending_=false, lowShown_=false, unknown_=false, unknownShown_=false;
    uint32_t unknownSince_=0;
public:
    void sample(int percent,bool usb,uint32_t now) {
        if(percent<0) {
            if(!unknown_){unknown_=true;unknownSince_=now;}
        } else {unknown_=false;unknownShown_=false;}
        // A recovered level prevents USB flicker from repeating a notice.
        if(percent>=20){lowShown_=false;lowPending_=false;}
        if(usb) lowPending_=false;
        else if(percent>=0 && percent<=10 && !lowShown_) lowPending_=true;
    }
    bool takeLow(bool eligible) {
        if(!eligible || !lowPending_)return false;
        lowPending_=false;lowShown_=true;return true;
    }
    bool unavailable(uint32_t now) const {
        return unknown_ && uint32_t(now-unknownSince_)>=60000;
    }
    bool takeUnavailable(bool eligible,uint32_t now) {
        if(!eligible || unknownShown_ || !unavailable(now))return false;
        unknownShown_=true;return true;
    }
};
}
