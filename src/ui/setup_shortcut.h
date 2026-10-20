#pragma once
#include <cstdint>

namespace ui {
struct SetupShortcutEvent {
    bool consume = false;
    bool started = false;
    bool armed = false;
    bool restart = false;
};

// Both buttons must remain down together for five seconds. Consume the entire
// gesture (including cancelled holds and staggered releases), then wait for a
// stable release before restarting: BOOT is also an ESP32 boot-strapping pin.
class SetupShortcut {
public:
    SetupShortcutEvent update(bool boot, bool power, uint32_t now) {
        SetupShortcutEvent event;
        const bool any = boot || power;
        const bool both = boot && power;
        if (state_ == State::Initial) {
            if (!any) state_ = State::Idle;
            event.consume = any;
            return event;
        }
        if (state_ == State::Idle) {
            if (!both) return event;
            state_ = State::Holding;
            since_ = now;
            event.started = true;
        }
        event.consume = true;
        switch (state_) {
            case State::Holding:
                if (!both) state_ = any ? State::Cancelled : State::Idle;
                else if (uint32_t(now - since_) >= 5000) {
                    state_ = State::Armed;
                    event.armed = true;
                }
                break;
            case State::Cancelled:
                if (!any) state_ = State::Idle;
                break;
            case State::Armed:
                if (!any) {
                    state_ = State::Releasing;
                    since_ = now;
                }
                break;
            case State::Releasing:
                if (any) state_ = State::Armed;
                else if (uint32_t(now - since_) >= 50) {
                    state_ = State::Idle;
                    event.restart = true;
                }
                break;
            default: break;
        }
        return event;
    }
private:
    enum class State { Initial, Idle, Holding, Cancelled, Armed, Releasing };
    State state_ = State::Initial;
    uint32_t since_ = 0;
};
}  // namespace ui
