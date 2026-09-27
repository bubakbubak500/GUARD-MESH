// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace ui {
class FirmwareUpdateJobs;
namespace platform {
// Shared executor owns its HTTP/socket objects. Opaque handles preserve the
// board-specific WiFiClient alias (including the P4's C6 socket).
bool runFirmwareCheck(FirmwareUpdateJobs &, void *client, void *http);
bool runFirmwareInstall(FirmwareUpdateJobs &, void *client, void *http);
} // namespace platform
} // namespace ui
