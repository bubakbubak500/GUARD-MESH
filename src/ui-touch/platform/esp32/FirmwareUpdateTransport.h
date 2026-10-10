// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace ui {
class FirmwareUpdateJobs;
namespace platform {
// Legacy executor entry points reject checks/installs without using the opaque
// socket handles. Manual GUARD-MESH GitHub downloads are the supported path.
bool runFirmwareCheck(FirmwareUpdateJobs &, void *client, void *http);
bool runFirmwareInstall(FirmwareUpdateJobs &, void *client, void *http);
} // namespace platform
} // namespace ui
