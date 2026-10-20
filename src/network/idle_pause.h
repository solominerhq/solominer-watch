#pragma once
namespace network {
enum class IdleAction { None, Stop, Resume };
// No timer or connected state: idle Wi-Fi stays off until foreground resumes.
class IdlePause {
  bool idle_ = false;
public:
  IdleAction step(bool idle) {
    if (idle == idle_) return IdleAction::None;
    idle_ = idle;
    return idle ? IdleAction::Stop : IdleAction::Resume;
  }
};
}
