// SPDX-License-Identifier: GPL-3.0-or-later
#include "../DeviceDiagnostics.h"
#if defined(GUARD_SIMULATOR)
namespace ui {
namespace platform {
const char *resetReason() { return "Unknown"; }
void readHardwareDiagnostics(diagnostics::Hardware &out, bool) { out = diagnostics::Hardware{}; }
StorageUsage::Snapshot readSdUsage(void *) { return StorageUsage::Snapshot{}; }
} // namespace platform
} // namespace ui
#endif
