#include "ui/touch_input.h"
#include <cassert>
#include <cstdio>
using namespace ui;
int main() {
    const PageRect button{24,104,362,100};
    TouchGesture g;
    assert(g.update(true,50,130,100,1,&button).kind==TouchKind::None);
    assert(g.pressed());
    auto e=g.update(false,0,0,180,1);
    assert(e.kind==TouchKind::Tap && e.x==50 && e.duration==80);
    assert(!g.pressed());
    // Two taps remain two taps; no double-tap delay or swallowed second action.
    g.update(true,50,130,200,1,&button);
    assert(g.update(false,0,0,250,1).kind==TouchKind::Tap);
    // A deliberate slower tap with small movement still activates on release.
    g.update(true,50,130,300,1,&button);
    g.update(true,58,133,650,1);
    assert(g.update(false,0,0,800,1).kind==TouchKind::Tap);
    g.update(true,50,130,900,1,&button);
    assert(g.update(false,0,0,1600,1).kind==TouchKind::None);
    // Leaving a control cancels, even when returning before release.
    g.update(true,25,130,1700,1,&button);
    g.update(true,22,130,1740,1);
    assert(!g.pressed());
    g.update(true,25,130,1770,1);
    assert(g.update(false,0,0,1800,1).kind==TouchKind::None);
    // One swipe per contact; horizontal navigation starts at 28 px.
    g.update(true,200,200,1900,1);
    assert(g.update(true,173,200,1940,1).kind==TouchKind::None);
    assert(g.update(true,172,200,1960,1).kind==TouchKind::Left);
    assert(g.update(true,80,200,2000,1).kind==TouchKind::None);
    assert(g.update(false,0,0,2020,1).kind==TouchKind::None);
    g.update(true,200,200,2050,1);
    assert(g.update(false,0,0,2100,1).kind==TouchKind::None);
    // Navigation during a contact cancels it; waking requires another contact.
    g.update(true,50,130,2300,1,&button);
    assert(g.update(false,0,0,2350,2).kind==TouchKind::None);
    g.consumeUntilRelease();
    assert(g.update(true,50,130,2400,2).kind==TouchKind::None);
    assert(g.update(false,0,0,2450,2).kind==TouchKind::None);
    g.update(true,50,130,2500,2,&button);
    assert(g.update(false,0,0,2550,2).kind==TouchKind::Tap);
    TouchQueue q; TouchEvent out;
    e.context=2;e.at=3000;
    assert(q.push(e));assert(q.pop(3100,2,out));assert(out.kind==TouchKind::Tap);
    q.push(e);assert(!q.pop(3251,2,out)); // Busy too long: discard, no surprise later.
    q.push(e);assert(!q.pop(3050,3,out)); // Never replay onto another page.
    for(int i=0;i<4;i++) assert(q.push(e));
    assert(!q.push(e));q.clear();assert(!q.pop(3050,2,out));
    e.at=0xfffffff0u;q.push(e);assert(q.pop(25,2,out));
    TouchGesture wrapped;
    wrapped.update(true,50,130,0xfffffff0u,1,&button);
    assert(wrapped.update(false,0,0,25,1).kind==TouchKind::Tap);

    // A short drag entirely inside a Settings row must never activate it.
    const int directions[] = {-1,1};
    for (int direction : directions) {
        TouchGesture row;
        row.update(true,200,150,100,1,&button);
        row.update(true,200+11*direction,150,120,1);
        assert(row.pressed());
        assert(row.update(true,200+12*direction,150,140,1).kind==TouchKind::None);
        assert(!row.pressed());
        row.update(true,200+18*direction,150,160,1);
        assert(row.update(false,0,0,180,1).kind==TouchKind::None);

        // Returning inside the tap radius cannot rearm a canceled button.
        row.update(true,200,150,300,1,&button);
        row.update(true,200+14*direction,150,340,1);
        row.update(true,200,150,380,1);
        assert(row.update(false,0,0,400,1).kind==TouchKind::None);

        row.update(true,200,150,500,1,&button);
        assert(row.update(true,200+27*direction,153,540,1).kind==TouchKind::None);
        assert(row.update(true,200+28*direction,153,560,1).kind==
               (direction>0?TouchKind::Right:TouchKind::Left));
        assert(!row.pressed());
        // Page changes and release after navigation cannot activate another row.
        assert(row.update(true,200+60*direction,153,580,2,&button).kind==TouchKind::None);
        assert(row.update(false,0,0,600,2).kind==TouchKind::None);
    }
    TouchGesture releasedSwipe;
    releasedSwipe.update(true,200,150,100,1,&button);
    assert(releasedSwipe.update(true,175,150,140,1).kind==TouchKind::None);
    assert(releasedSwipe.update(false,0,0,160,1).kind==TouchKind::Left);
    assert(releasedSwipe.update(false,0,0,180,1).kind==TouchKind::None);
    // Vertical dragging cancels a button too; its live swipe threshold is unchanged.
    TouchGesture vertical;
    vertical.update(true,200,150,100,1,&button);
    vertical.update(true,200,162,120,1);
    assert(!vertical.pressed());
    assert(vertical.update(true,200,189,140,1).kind==TouchKind::None);
    assert(vertical.update(true,200,190,160,1).kind==TouchKind::Down);
    assert(vertical.update(false,0,0,180,1).kind==TouchKind::None);
    // Slight diagonal finger drift is not prematurely classified as horizontal.
    TouchGesture diagonal;
    diagonal.update(true,200,150,100,1,&button);
    assert(diagonal.update(true,222,170,140,1).kind==TouchKind::None);
    assert(!diagonal.pressed());
    // Ordinary non-button contacts retain their existing tap tolerance.
    TouchGesture blank;
    blank.update(true,200,150,100,1);
    blank.update(true,215,153,140,1);
    assert(blank.update(false,0,0,160,1).kind==TouchKind::Tap);
    puts("Touch release, forgiveness, cancellation, swipe, wake and queue tests passed.");
}
