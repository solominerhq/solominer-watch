#pragma once
#include <stdint.h>
namespace ui {
// Monitor-owned. A reconnect or unusable interval discards the old average.
class HashrateSample {
    uint32_t generation=0; bool armed=false;
    double samples[10]={}; unsigned count=0,index=0; double sum=0;
public:
    double update(uint32_t link, bool eligible, uint32_t kh, uint32_t elapsed) {
        if(!eligible || !elapsed || link!=generation || !armed) {
            generation=link;armed=eligible && elapsed>0;count=index=0;sum=0;
            return 0; // Discard the interval spanning a connection boundary.
        }
        if(count==10)sum-=samples[index];else ++count;
        samples[index]=double(kh)*1000.0/elapsed;sum+=samples[index];index=(index+1)%10;
        return sum/count;
    }
};
}
