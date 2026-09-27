// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace ui {
class SightlineJob;
namespace platform {
bool runSightlineJob(SightlineJob &, void *client, void *http);
}
} // namespace ui
