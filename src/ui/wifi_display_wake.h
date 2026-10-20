#pragma once
#include <stdint.h>
namespace ui {
// Display-task owned. Consume blocked/cooldown events so they never wake later.
class WifiDisplayWake {
    uint32_t seen=0, lastWake=0;
    bool hasWoken=false;
public:
    bool update(uint32_t connections, uint32_t now, bool eligible) {
        if(connections==seen) return false;
        seen=connections;
        if(!eligible || (hasWoken && uint32_t(now-lastWake)<30000u)) return false;
        hasWoken=true; lastWake=now; return true;
    }
};
}
