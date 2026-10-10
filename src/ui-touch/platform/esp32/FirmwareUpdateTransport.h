// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace ui {
class FirmwareUpdateJobs;
namespace platform {
// Worker-only HTTPS fetch/OTA. Owns its verified TLS client; shared plain-HTTP
// handles are deliberately unused. Unsupported boards fail without writing.
bool runFirmwareCheck(FirmwareUpdateJobs &, void *client, void *http);
bool runFirmwareInstall(FirmwareUpdateJobs &, void *client, void *http);
} // namespace platform
} // namespace ui
