#pragma once
#include <stdint.h>
namespace settings {
class SaveRetry {
    bool attempted_=false, failed_=false;
    uint32_t lastAttempt_=0;
public:
    bool due(uint32_t now,uint32_t changedAt,bool force) const {
        return force || (uint32_t(now-changedAt)>=800 &&
            (!attempted_ || !failed_ || uint32_t(now-lastAttempt_)>=5000));
    }
    void result(uint32_t now,bool success) {
        attempted_=true;failed_=!success;lastAttempt_=now;
    }
};
}
