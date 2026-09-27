// SPDX-License-Identifier: GPL-3.0-or-later
#include "../OtaCapability.h"
#if defined(GUARD_SIMULATOR)
namespace ui {
namespace platform {
bool hasOtaUpdateSlot() { return false; }
} // namespace platform
} // namespace ui
#endif
