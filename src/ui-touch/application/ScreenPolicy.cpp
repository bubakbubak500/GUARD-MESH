#include "ui-touch/application/ScreenPolicy.h"

namespace ui {
namespace {

int32_t signedElapsed(uint32_t now, uint32_t then) {
    return static_cast<int32_t>(now - then);
}

bool elapsedAtLeast(uint32_t now, uint32_t then, uint32_t duration) {
    const int32_t elapsed = signedElapsed(now, then);
    return elapsed >= 0 && static_cast<uint32_t>(elapsed) >= duration;
}

bool newerThan(uint32_t candidate, uint32_t reference) {
    return signedElapsed(candidate, reference) > 0;
}

} // namespace

ScreenPolicy::ScreenPolicy()
    : screenOff_(false), manualLocked_(false), timeoutMs_(20000),
      lastInputMs_(0), lockWindowArmed_(false), lockWindowStartMs_(0),
      notifyActive_(false), notifyStartMs_(0), glanceActive_(false),
      glanceStartMs_(0), glanceFadeSent_(false) {}

bool ScreenPolicy::screenOff() const { return screenOff_; }

bool ScreenPolicy::manualLocked() const { return manualLocked_; }

uint32_t ScreenPolicy::timeoutMs() const { return timeoutMs_; }

uint32_t ScreenPolicy::lastInputMs() const { return lastInputMs_; }

void ScreenPolicy::begin(uint32_t now, uint32_t timeoutMs) {
    screenOff_ = false;
    manualLocked_ = false;
    timeoutMs_ = timeoutMs;
    lastInputMs_ = now;
    lockWindowArmed_ = false;
    lockWindowStartMs_ = 0;
    notifyActive_ = false;
    notifyStartMs_ = 0;
    glanceActive_ = false;
    glanceStartMs_ = 0;
    glanceFadeSent_ = false;
}

void ScreenPolicy::setTimeout(uint32_t timeoutMs, uint32_t now) {
    timeoutMs_ = timeoutMs;
    lastInputMs_ = now;
}

bool ScreenPolicy::noteActivity(uint32_t now, bool wakeOnInput) {
    if (screenOff_ && manualLocked_) {
        return false;
    }
    lastInputMs_ = now;
    return screenOff_ && wakeOnInput;
}

void ScreenPolicy::recordActivity(uint32_t now) { lastInputMs_ = now; }

void ScreenPolicy::wake(uint32_t now) {
    screenOff_ = false;
    manualLocked_ = false;
    recordActivity(now);
}

void ScreenPolicy::lockDark() {
    screenOff_ = true;
    manualLocked_ = true;
}

void ScreenPolicy::lockLit(uint32_t now) {
    screenOff_ = false;
    manualLocked_ = true;
    recordActivity(now);
    armLockWindow(now);
}

void ScreenPolicy::sleep() { screenOff_ = true; }

void ScreenPolicy::unlock(uint32_t now) {
    screenOff_ = false;
    manualLocked_ = false;
    recordActivity(now);
}

void ScreenPolicy::reveal(uint32_t now) {
    if (!manualLocked_) {
        return;
    }
    if (screenOff_) {
        screenOff_ = false;
        armLockWindow(now);
    }
    recordActivity(now);
}

void ScreenPolicy::armLockWindow(uint32_t now) {
    lockWindowArmed_ = true;
    lockWindowStartMs_ = now;
}

bool ScreenPolicy::lockDimDue(uint32_t now) const {
    if (!manualLocked_ || screenOff_ || !lockWindowArmed_) {
        return false;
    }
    uint32_t duration = timeoutMs_;
    if (duration == 0 || duration > 20000) {
        duration = 20000;
    }
    return elapsedAtLeast(now, lockWindowStartMs_, duration);
}

void ScreenPolicy::wakeForGlance(uint32_t now, bool armLock) {
    const bool wasOff = screenOff_;
    if (wasOff) {
        screenOff_ = false;
        recordActivity(now);
    }
    if (manualLocked_ && armLock && wasOff) {
        armLockWindow(now);
    }
}

IdleAction ScreenPolicy::idleAction(uint32_t now, bool lockOnIdle,
                                    bool canHardLock) const {
    if (screenOff_ || timeoutMs_ == 0 ||
        !elapsedAtLeast(now, lastInputMs_, timeoutMs_)) {
        return IdleAction::None;
    }
    if (lockOnIdle && canHardLock && !manualLocked_) {
        return IdleAction::Lock;
    }
    return IdleAction::Dim;
}

void ScreenPolicy::startNotify(uint32_t now) {
    notifyActive_ = true;
    notifyStartMs_ = now;
}

PeekAction ScreenPolicy::pollNotify(uint32_t now) {
    if (!notifyActive_) {
        return PeekAction::None;
    }
    if (screenOff_ || newerThan(lastInputMs_, notifyStartMs_)) {
        notifyActive_ = false;
        return PeekAction::None;
    }
    if (elapsedAtLeast(now, notifyStartMs_, 10000)) {
        notifyActive_ = false;
        return PeekAction::Dim;
    }
    return PeekAction::None;
}

void ScreenPolicy::startGlance(uint32_t now) {
    glanceActive_ = true;
    glanceStartMs_ = now;
    glanceFadeSent_ = false;
}

bool ScreenPolicy::glanceActive() const { return glanceActive_; }

void ScreenPolicy::cancelGlance() {
    glanceActive_ = false;
    glanceFadeSent_ = false;
}

PeekAction ScreenPolicy::pollGlance(uint32_t now) {
    if (!glanceActive_) {
        return PeekAction::None;
    }
    if (screenOff_ || newerThan(lastInputMs_, glanceStartMs_)) {
        cancelGlance();
        return PeekAction::Hide;
    }
    if (elapsedAtLeast(now, glanceStartMs_, 5000)) {
        cancelGlance();
        return PeekAction::Dim;
    }
    if (!glanceFadeSent_ && elapsedAtLeast(now, glanceStartMs_, 4800)) {
        glanceFadeSent_ = true;
        return PeekAction::Fade;
    }
    return PeekAction::None;
}

} // namespace ui
