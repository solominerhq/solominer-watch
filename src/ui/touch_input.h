#pragma once
#include <stdint.h>
#include "page_controls.h"

namespace ui {
enum class TouchKind { None, Tap, Left, Right, Up, Down };
struct TouchEvent {
    TouchKind kind = TouchKind::None;
    int16_t x = 0, y = 0, endX = 0, endY = 0;
    uint32_t context = 0, at = 0, duration = 0;
};

// Owned by the touch task. No timers, allocation, or display calls.
class TouchGesture {
    bool active_ = false, consume_ = false, tap_ = false, bounded_ = false, swiped_ = false;
    int16_t x_ = 0, y_ = 0, lastX_ = 0, lastY_ = 0;
    uint32_t started_ = 0, context_ = 0, lastSwipe_ = 0;
    PageRect bounds_{0,0,0,0};
    TouchEvent event(TouchKind kind, uint32_t now) const {
        TouchEvent e; e.kind=kind; e.x=x_; e.y=y_; e.endX=lastX_; e.endY=lastY_;
        e.context=context_; e.at=now; e.duration=now-started_; return e;
    }
public:
    void consumeUntilRelease() { active_=false; tap_=false; consume_=true; }
    bool pressed() const { return active_ && tap_; }
    uint32_t context() const { return context_; }
    int16_t startX() const { return x_; }
    int16_t startY() const { return y_; }
    TouchEvent update(bool down, int16_t x, int16_t y, uint32_t now,
                      uint32_t context, const PageRect* target=nullptr) {
        if (consume_) { if (!down) consume_=false; return {}; }
        if (!active_) {
            if (!down) return {};
            if (swiped_ && uint32_t(now-lastSwipe_)<200) { consume_=true; return {}; }
            active_=tap_=true; x_=lastX_=x; y_=lastY_=y; started_=now; context_=context;
            bounded_=target!=nullptr; if(target) bounds_=*target;
            return {};
        }
        if (context!=context_) { consumeUntilRelease(); if(!down)consume_=false; return {}; }
        if (down) { lastX_=x; lastY_=y; }
        const int dx=lastX_-x_, dy=lastY_-y_;
        const int distance2=dx*dx+dy*dy;
        const uint32_t elapsed=now-started_;
        // A drag starting on a button must stop being a tap early, even if
        // the finger stays inside the large row or returns to its start.
        const int tapSlop=bounded_ ? 12 : 24;
        if (distance2>=tapSlop*tapSlop || elapsed>650 || (bounded_ && !bounds_.contains(lastX_,lastY_))) tap_=false;
        // Recognize horizontal navigation sooner, including over buttons.
        // Keep vertical gestures, release swipes and minimum velocity unchanged.
        const bool horizontal=dx*dx>dy*dy;
        const bool liveSwipe=horizontal ? dx*dx>=28*28 : distance2>=40*40;
        const bool velocity=elapsed && double(distance2)>=double(elapsed)*elapsed*0.0004;
        if ((down && liveSwipe && velocity) ||
            (!down && !tap_ && distance2>=24*24 && velocity)) {
            const TouchKind kind=horizontal ? (dx>0?TouchKind::Right:TouchKind::Left)
                                                    : (dy>0?TouchKind::Down:TouchKind::Up);
            auto e=event(kind,now); lastSwipe_=now; swiped_=true;
            active_=tap_=false; consume_=down; return e;
        }
        if (!down) { auto e=tap_?event(TouchKind::Tap,now):TouchEvent{};active_=tap_=false;return e; }
        return {};
    }
};

// A short, bounded queue survives a busy display without replaying old input.
// Producer and consumer are the same touch task; LVGL ownership is separate.
class TouchQueue {
    TouchEvent events_[4];
    unsigned head_=0, count_=0;
public:
    bool push(const TouchEvent& e) {
        if(e.kind==TouchKind::None || count_==4)return false;
        events_[(head_+count_)%4]=e;++count_;return true;
    }
    bool pop(uint32_t now,uint32_t context,TouchEvent& out) {
        while(count_) {
            auto e=events_[head_];head_=(head_+1)%4;--count_;
            if(e.context==context && uint32_t(now-e.at)<=250){out=e;return true;}
        }
        return false;
    }
    void clear(){head_=count_=0;}
};
}
