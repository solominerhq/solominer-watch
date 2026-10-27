#include <cassert>
#include "ui/hashrate_sample.h"
int main(){
 ui::HashrateSample s;
 assert(s.update(1,true,900,1000)==0);
 assert(s.update(1,true,125,1000)==125);
 assert(s.update(1,true,127,1000)==126);
 assert(s.update(2,true,900,1000)==0); // Discard pre-reconnect work.
 assert(s.update(2,true,10,1000)==10); // No old average leaks.
 assert(s.update(2,false,125,1000)==0);
 assert(s.update(2,true,900,1000)==0); // First interval after pause is discarded.
 assert(s.update(2,true,120,1000)==120);
 assert(s.update(2,true,50,0)==0);
 assert(s.update(2,true,125,1000)==0);
 assert(s.update(2,true,125,1000)==125);
}
