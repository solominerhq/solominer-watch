#include <cassert>
#include "ui/wifi_display_wake.h"
int main(){
 ui::WifiDisplayWake w;
 assert(!w.update(0,100,true));
 assert(w.update(1,1000,true));
 assert(!w.update(1,2000,true));
 assert(!w.update(2,2000,true)); // Flapping cannot extend the timeout.
 assert(!w.update(2,40000,true)); // Suppressed events never replay.
 assert(w.update(3,41000,true));
 assert(!w.update(4,80000,false)); // Setup, awake, manual-off or Saver idle.
 assert(!w.update(4,90000,true));
 assert(w.update(5,91000,true));
 ui::WifiDisplayWake wrap;
 assert(wrap.update(1,0xfffffff0u,true));
 assert(!wrap.update(2,20,true));
 assert(wrap.update(3,30000,true));
}
