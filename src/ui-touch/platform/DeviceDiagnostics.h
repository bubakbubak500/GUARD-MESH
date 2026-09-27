// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/SystemDiagnostics.h"
#include "../services/StorageUsage.h"
namespace ui {
namespace platform {
// UI thread. Expensive immutable flash-image verification is cached by adapter.
const char *resetReason();
void readHardwareDiagnostics(diagnostics::Hardware &, bool details);
// Worker only. Lifecycle must hold the storage mount until this returns.
StorageUsage::Snapshot readSdUsage(void *);
} // namespace platform
} // namespace ui
