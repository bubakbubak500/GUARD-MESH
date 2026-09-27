#pragma once

#include <cstdint>

namespace ui {

// Time comparisons are wrap-safe for deadlines shorter than INT32_MAX ms.
class ScreenPolicy {
public:
    enum class IdleAction { None, Dim, Lock };
    enum class PeekAction { None, Hide, Fade, Dim };

    ScreenPolicy();

    bool screenOff() const;
    bool manualLocked() const;
    uint32_t timeoutMs() const;
    uint32_t lastInputMs() const;

    void begin(uint32_t now, uint32_t timeoutMs);
    void setTimeout(uint32_t timeoutMs, uint32_t now);
    // Returns a wake request; the host commits wake effects after hardware work.
    bool noteActivity(uint32_t now, bool wakeOnInput = true);
    void recordActivity(uint32_t now);
    void wake(uint32_t now);
    void lockDark();
    void lockLit(uint32_t now);
    void sleep();
    void unlock(uint32_t now);
    void reveal(uint32_t now);
    void armLockWindow(uint32_t now);
    bool lockDimDue(uint32_t now) const;
    void wakeForGlance(uint32_t now, bool armLock);
    IdleAction idleAction(uint32_t now, bool lockOnIdle,
                          bool canHardLock = true) const;

    void startNotify(uint32_t now);
    PeekAction pollNotify(uint32_t now);
    void startGlance(uint32_t now);
    bool glanceActive() const;
    void cancelGlance();
    PeekAction pollGlance(uint32_t now);

private:
    bool screenOff_;
    bool manualLocked_;
    uint32_t timeoutMs_;
    uint32_t lastInputMs_;

    bool lockWindowArmed_;
    uint32_t lockWindowStartMs_;

    bool notifyActive_;
    uint32_t notifyStartMs_;

    bool glanceActive_;
    uint32_t glanceStartMs_;
    bool glanceFadeSent_;
};

// Namespace aliases keep policy values convenient for callers that prefer them.
using IdleAction = ScreenPolicy::IdleAction;
using PeekAction = ScreenPolicy::PeekAction;

} // namespace ui
