// SPDX-License-Identifier: GPL-3.0-or-later
#include "FirmwareUpdateTransport.h"
#include "../../services/FirmwareUpdateJobs.h"
#include <cstdio>
namespace ui {
namespace platform {
// Keep queued legacy calls harmless even if a caller bypasses the About UI.
// No socket, filesystem or Update writer is touched. GitHub automatic installs
// remain unavailable until their transport and asset validation are implemented.
bool runFirmwareCheck(FirmwareUpdateJobs &jobs, void *client, void *http) {
  (void)client;
  (void)http;
  return jobs.runCheck({nullptr, [](void *, bool) { return -1; }, nullptr});
}
bool runFirmwareInstall(FirmwareUpdateJobs &jobs, void *client, void *http) {
  (void)client;
  (void)http;
  return jobs.runInstall({nullptr, nullptr,
                          [](void *, const FirmwareUpdateJobs::InstallRequest &,
                             FirmwareUpdateJobs::Progress, FirmwareUpdateJobs::InstallResult &result) {
                            result.ok = false;
                            snprintf(result.message, sizeof result.message,
                                     "Automatic install unavailable; use GUARD-MESH GitHub releases");
                          }});
}
} // namespace platform
} // namespace ui
