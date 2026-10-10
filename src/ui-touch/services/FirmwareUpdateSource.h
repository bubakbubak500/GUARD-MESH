// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace ui {
namespace firmwareUpdate {
// Manual downloads are the supported release path. A future automatic updater
// must validate the GitHub release, exact board asset and digest before writing.
static const char releasesUrl[] = "https://github.com/bubakbubak500/GUARD-MESH/releases";
} // namespace firmwareUpdate
} // namespace ui
