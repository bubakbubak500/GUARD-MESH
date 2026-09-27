// SPDX-License-Identifier: GPL-3.0-or-later
#include "SimPlatform.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
void check(bool okay, const char* message) { if (!okay) throw std::runtime_error(message); }
int find(UITask& task, const char* wanted, bool channel) {
  int indexes[UITask::MAX_UI_THREADS];
  int count = task.getThreadCount(channel, indexes, UITask::MAX_UI_THREADS);
  for (int i = 0; i < count; ++i) {
    bool kind; uint16_t unread; uint32_t timestamp; char name[UITask::MAX_THREAD_NAME + 1];
    if (task.getThreadInfo(indexes[i], kind, unread, timestamp, name, sizeof(name)) && !strcmp(name, wanted))
      return indexes[i];
  }
  return -1;
}
}

void runThreadRefreshRegression(UITask& task, void (*pump)(unsigned)) {
  const size_t oldSize = the_mesh.contacts.size();
  const int selected = task.activeThreadIdx();
  const bool selectedChannel = task.activeThreadIsChannel();
  ContactInfo contact{};
  strcpy(contact.name, "perf-directory");
  memset(contact.id.pub_key, 0xB7, sizeof(contact.id.pub_key));
  the_mesh.contacts.push_back(contact);
  task.appSentMsgToContact(contact.id.pub_key, contact.name, "directory fixture", 0);
  const int thread = find(task, contact.name, false);
  check(thread >= 0, "Directory performance: fixture missing");
  task.refreshThreadsFromMesh();
  check(!task.refreshThreadsFromMesh(), "Directory performance: unchanged directory reports changes");
  uint32_t before = task.meshDirectoryRefreshCount();
  pump(4300);
  check(task.meshDirectoryRefreshCount() == before,
        "Directory performance: unchanged directory still scans on old four-second deadline");

  for (int i = 0; i < 12; ++i) {
    ++the_mesh.contacts[oldSize].last_advert_timestamp;
    task.discoveredContact(the_mesh.contacts[oldSize], false, 0);
  }
  pump(40);
  check(task.meshDirectoryRefreshCount() == before,
        "Directory performance: age-only adverts trigger directory scans");

  strcpy(the_mesh.contacts[oldSize].name, "perf-renamed");
  task.discoveredContact(the_mesh.contacts[oldSize], false, 0);
  pump(40);
  check(task.meshDirectoryRefreshCount() == before + 1 && find(task, "perf-renamed", false) == thread,
        "Directory performance: advertised rename did not update bound conversation promptly");
  before = task.meshDirectoryRefreshCount();
  for (int i = 0; i < 10; ++i) task.onThreadsChanged();
  pump(40);
  check(task.meshDirectoryRefreshCount() == before + 1,
        "Directory performance: explicit mutations were not coalesced into one pass");

  before = task.meshDirectoryRefreshCount();
  the_mesh.contacts.resize(oldSize);
  pump(40);
  check(task.meshDirectoryRefreshCount() == before + 1,
        "Directory performance: contact count change without callback was missed");
  check(!task.refreshThreadsFromMesh(), "Directory performance: deleted directory never becomes stable");
  task.removeThread(thread);
  if (selected >= 0) task.enterThread(selectedChannel, selected);
  std::puts("Directory performance: idle 4.3s = 0 scans; 12 age adverts = 0; rename = 1; 10 invalidations = 1; deletion = 1. PASS");
}
