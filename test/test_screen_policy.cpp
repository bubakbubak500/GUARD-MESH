#include "ui-touch/application/ScreenPolicy.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>

int main() {
    using ui::IdleAction;
    using ui::PeekAction;
    using ui::ScreenPolicy;

    ScreenPolicy policy;
    assert(!policy.screenOff());
    assert(!policy.manualLocked());
    assert(policy.lastInputMs() == 0);
    assert(policy.timeoutMs() == 20000);

    policy.begin(100, 1000);
    assert(policy.idleAction(1099, true) == IdleAction::None);
    assert(policy.idleAction(1100, false) == IdleAction::Dim);
    assert(policy.idleAction(1100, true, false) == IdleAction::Dim);
    assert(policy.idleAction(1100, true, true) == IdleAction::Lock);

    policy.lockDark();
    const uint32_t beforeDarkTouch = policy.lastInputMs();
    assert(!policy.noteActivity(2000));
    assert(policy.lastInputMs() == beforeDarkTouch);
    policy.sleep();
    assert(policy.manualLocked());

    policy.begin(3000, 1000);
    policy.sleep();
    assert(policy.noteActivity(3100));
    assert(policy.screenOff());
    assert(policy.lastInputMs() == 3100);
    assert(!policy.noteActivity(3200, false));
    assert(policy.lastInputMs() == 3200);
    policy.wake(3300);
    assert(!policy.screenOff() && !policy.manualLocked());

    policy.lockLit(4000);
    assert(policy.idleAction(5000, true) == IdleAction::Dim);
    const uint32_t lockedDeadline = 4000;
    policy.recordActivity(4500);
    policy.reveal(4700);
    assert(policy.manualLocked());
    assert(!policy.lockDimDue(lockedDeadline + 999));
    assert(policy.lockDimDue(lockedDeadline + 1000));
    policy.sleep();
    policy.reveal(5000);
    assert(!policy.screenOff() && policy.manualLocked());
    assert(!policy.lockDimDue(5999));
    assert(policy.lockDimDue(6000));

    policy.begin(0, 0);
    policy.lockLit(0);
    assert(!policy.lockDimDue(19999));
    assert(policy.lockDimDue(20000));
    policy.unlock(20001);
    assert(!policy.manualLocked());

    policy.begin(100, 5000);
    policy.startNotify(1000);
    assert(policy.pollNotify(10999) == PeekAction::None);
    assert(policy.pollNotify(11000) == PeekAction::Dim);
    assert(policy.pollNotify(11001) == PeekAction::None);

    policy.begin(200, 1000);
    policy.startNotify(500);
    assert(!policy.noteActivity(501, false));
    assert(policy.pollNotify(502) == PeekAction::None);
    assert(policy.pollNotify(10500) == PeekAction::None);

    policy.begin(1000, 1000);
    policy.startGlance(2000);
    policy.wakeForGlance(2100, false);
    assert(policy.pollGlance(6799) == PeekAction::None);
    assert(policy.pollGlance(6800) == PeekAction::Fade);
    assert(policy.pollGlance(6801) == PeekAction::None);
    assert(policy.glanceActive());
    assert(policy.pollGlance(7000) == PeekAction::Dim);
    assert(!policy.glanceActive());

    policy.begin(0, 1000);
    policy.startGlance(100);
    policy.startGlance(200);
    assert(policy.pollGlance(4999) == PeekAction::None);
    assert(policy.pollGlance(5000) == PeekAction::Fade);
    assert(policy.pollGlance(5001) == PeekAction::None);
    assert(policy.pollGlance(5200) == PeekAction::Dim);

    policy.begin(0, 1000);
    policy.startGlance(100);
    policy.recordActivity(101);
    assert(policy.pollGlance(102) == PeekAction::Hide);
    assert(!policy.glanceActive());

    policy.begin(0, 0);
    policy.lockDark();
    policy.wakeForGlance(100, true);
    assert(!policy.screenOff() && policy.manualLocked());
    assert(!policy.lockDimDue(20099));
    assert(policy.lockDimDue(20100));
    policy.wakeForGlance(21000, true);
    assert(policy.lockDimDue(21000));

    policy.begin(0, 1000);
    policy.startGlance(100);
    policy.sleep();
    assert(policy.pollGlance(101) == PeekAction::Hide);

    policy.begin(1000, 100);
    assert(policy.idleAction(900, true) == IdleAction::None);
    policy.begin(0xFFFFFF00u, 1000);
    assert(policy.idleAction(0x000002E7u, false) == IdleAction::None);
    assert(policy.idleAction(0x000002E8u, false) == IdleAction::Dim);

    policy.begin(0xFFFFFF00u, 1000);
    policy.startNotify(0xFFFFFF00u);
    policy.recordActivity(0x00000010u);
    assert(policy.pollNotify(0x00000011u) == PeekAction::None);
    assert(policy.pollNotify(0xFFFFFF00u + 10000u) == PeekAction::None);
    policy.startGlance(0xFFFFFF10u);
    policy.recordActivity(0x00000011u);
    assert(policy.pollGlance(0x00000012u) == PeekAction::Hide);

    policy.begin(0xFFFFFF00u, 1000);
    policy.startGlance(0xFFFFFF00u);
    assert(policy.pollGlance(0xFFFFFF00u + 4799u) == PeekAction::None);
    assert(policy.pollGlance(0xFFFFFF00u + 4800u) == PeekAction::Fade);
    assert(policy.pollGlance(0xFFFFFF00u + 5000u) == PeekAction::Dim);

    // Zero is a valid timestamp, including a wake exactly at millis rollover.
    policy.begin(0, 0);
    assert(policy.idleAction(20000, true) == IdleAction::None);
    policy.startNotify(0);
    policy.recordActivity(0);  // preserve the existing same-millisecond rule
    assert(policy.pollNotify(9999) == PeekAction::None);
    assert(policy.pollNotify(10000) == PeekAction::Dim);
    policy.startGlance(0);
    assert(policy.pollGlance(4799) == PeekAction::None);
    assert(policy.pollGlance(4800) == PeekAction::Fade);
    policy.startGlance(4900);  // a new message cancels the old fade deadline
    assert(policy.pollGlance(5000) == PeekAction::None);
    assert(policy.pollGlance(9700) == PeekAction::Fade);
    assert(policy.pollGlance(9900) == PeekAction::Dim);

    policy.begin(0xFFFFFF00u, 0);
    policy.startNotify(0xFFFFFF00u);
    assert(policy.pollNotify(0xFFFFFF00u + 9999u) == PeekAction::None);
    assert(policy.pollNotify(0xFFFFFF00u + 10000u) == PeekAction::Dim);
    policy.startGlance(0x10u);
    assert(policy.pollGlance(0x0Fu) == PeekAction::None); // stale loop snapshot
    policy.setTimeout(1000, 0x11u);
    assert(policy.pollGlance(0x12u) == PeekAction::Hide);
    assert(policy.idleAction(0x11u + 999u, false) == IdleAction::None);
    assert(policy.idleAction(0x11u + 1000u, false) == IdleAction::Dim);

    ScreenPolicy first;
    ScreenPolicy second;
    first.begin(10, 20);
    second.begin(30, 40);
    first.sleep();
    first.lockDark();
    first.startGlance(50);
    assert(first.screenOff() && first.manualLocked());
    assert(!second.screenOff() && !second.manualLocked());
    assert(second.timeoutMs() == 40 && second.lastInputMs() == 30);
    return 0;
}
