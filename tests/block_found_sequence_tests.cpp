#include <assert.h>
#include <string.h>
#include "ui/block_found_sequence.h"

int main() {
    using namespace ui::block_found;
    assert(landingFrame(0)==0 && landingFrame(899)==0);
    assert(landingFrame(900)==1 && landingFrame(1899)==1);
    assert(landingFrame(1900)==3 && landingFrame(2755)==3);
    assert(landingFrame(2756)==4 && landingFrame(4088)==4);
    assert(landingFrame(4089)==5);
    assert(beatIndex(impactMs)==0 && beatAgeMs(impactMs)==0);
    assert(beatIndex(impactMs+666)==0 && beatIndex(impactMs+667)==1);
    assert(beatIndex(impactMs+2000)==3 && beatAgeMs(impactMs+2000)==0);
    assert(beatIndex(impactMs+120000)==180 && beatAgeMs(impactMs+120000)==0);
    assert(headingColor(0,true)==headingOrange);
    assert(headingColor(impactMs,true)==0xFFFFFF);
    assert(headingColor(impactMs+666,true)==0xFFFFFF);
    assert(headingColor(impactMs+667,true)==headingOrange);
    assert(headingColor(impactMs+1334,true)==0xFFFFFF);
    assert(headingColor(impactMs+2000,true)==headingOrange);
    for(uint32_t t=impactMs;t<130176;t+=17) {
        const auto color=headingColor(t,true);
        assert(color==headingOrange || color==0xFFFFFF);
    }
    assert(headingColor(130176,false)==headingOrange);
    assert(headingColor(detailsMs,false)==headingOrange); // Saved / silent playback is steady.
    assert(revealMs==impactMs && celebrationHoldMs==8000 && detailsMs==12089);
    assert(rewardHoldMs==6000);
    assert(!navigationReady(dotsMs-1) && navigationReady(dotsMs));
    Tour tour;
    assert(tour.update(50000,detailsMs-1)==-1);
    assert(tour.update(50001,detailsMs)==1);
    assert(tour.update(56000,detailsMs+5999)==-1);
    assert(tour.update(56001,detailsMs+6000)==2);
    assert(tour.update(99999,130176)==-1); // QR holds, never loops.
    tour={};tour.manual=true;assert(tour.update(50000,detailsMs+1)==-1);
    tour={};assert(tour.update(50000,detailsMs)==1);tour.manual=true;
    assert(tour.update(60000,detailsMs+10000)==-1); // Manual navigation cancels mid-tour too.
    tour={};assert(tour.update(0xFFFFF000,detailsMs)==1);
    assert(tour.update(uint32_t(0xFFFFF000+5999ULL),detailsMs)==-1);
    assert(tour.update(uint32_t(0xFFFFF000+6000ULL),detailsMs)==2);
    // A delayed UI update still gives the reward a full six visible seconds.
    tour={};assert(tour.update(50000,detailsMs+20000)==1);
    assert(tour.update(55999,detailsMs+25999)==-1);
    assert(tour.update(56000,detailsMs+26000)==2);
    assert(nextPage(0,true)==1 && nextPage(1,true)==2 && nextPage(2,true)==0);
    assert(nextPage(0,false)==2 && nextPage(2,false)==1 && nextPage(1,false)==0);
    assert(!validHash(nullptr) && !validHash("") && !validHash("0123"));
    char hash[66];memset(hash,'a',64);hash[64]=0;assert(validHash(hash));
    hash[20]='z';assert(!validHash(hash));hash[20]='A';assert(validHash(hash));
    hash[64]='1';hash[65]=0;assert(!validHash(hash));
}
