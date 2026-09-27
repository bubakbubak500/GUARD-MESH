// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace ui { class AppStoreJobs; namespace platform {
// Opaque because WiFiClient is a class, alias or board-specific macro depending
// on the target. The existing network worker owns both client objects.
bool runAppStoreJob(AppStoreJobs&, void* client, void* http);
} }
