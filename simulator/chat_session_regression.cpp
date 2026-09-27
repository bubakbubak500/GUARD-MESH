// SPDX-License-Identifier: GPL-3.0-or-later
#include "SimPlatform.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
UITask* sendingTask = nullptr;
int nextThread = -1;
void check(bool okay, const char* message) {
  if (!okay) throw std::runtime_error(message);
}
int findThread(UITask& task, const char* wanted) {
  int indexes[UITask::MAX_UI_THREADS];
  const int count = task.getThreadCount(false, indexes, UITask::MAX_UI_THREADS);
  for (int i = 0; i < count; ++i) {
    char name[UITask::MAX_THREAD_NAME + 1];
    bool channel = false;
    uint16_t unread = 0;
    uint32_t timestamp = 0;
    if (task.getThreadInfo(indexes[i], channel, unread, timestamp, name, sizeof name) &&
        std::strcmp(name, wanted) == 0) return indexes[i];
  }
  return -1;
}
bool hasText(UITask& task, int thread, const char* wanted) {
  int indexes[32];
  const int count = task.getThreadMessageIndexes(thread, indexes, 32, true);
  for (int i = 0; i < count; ++i) {
    UITask::UIMessage message{};
    if (task.getMessageByIndex(indexes[i], message) && std::strcmp(message.text, wanted) == 0)
      return true;
  }
  return false;
}
void switchChatDuringSend() {
  sendingTask->enterThread(false, nextThread);
  sendingTask->composerReset();
  sendingTask->composerAppendText("new conversation draft");
}
}

void runChatSessionIntegration(UITask& task) {
  const auto originalSize = the_mesh.contacts.size();
  const std::string originalDraft = task.getComposerBuffer();
  const auto originalAck = the_mesh.pendingAck;
  const auto originalAckDue = the_mesh.ackDue;
  ContactInfo first{}, second{};
  std::strcpy(first.name, "session-alpha");
  std::strcpy(second.name, "session-beta");
  std::memset(first.id.pub_key, 0xE1, sizeof(first.id.pub_key));
  std::memset(second.id.pub_key, 0xE2, sizeof(second.id.pub_key));
  the_mesh.contacts.push_back(first);
  the_mesh.contacts.push_back(second);
  task.appSentMsgToContact(first.id.pub_key, first.name, "session seed", 0, 0);
  task.appSentMsgToContact(second.id.pub_key, second.name, "session seed", 0, 0);
  const int firstThread = findThread(task, first.name);
  const int secondThread = findThread(task, second.name);
  check(firstThread >= 0 && secondThread >= 0, "Chat session: fixture binding missing");
  task.enterThread(false, firstThread);

  std::swap(the_mesh.contacts[originalSize], the_mesh.contacts[originalSize + 1]);
  check(task.composerSend("recipient after reorder"), "Chat session: send after reorder failed");
  check(std::memcmp(the_mesh.lastRecipient, first.id.pub_key, 32) == 0,
        "Chat session: cached contact index redirected transport");
  std::swap(the_mesh.contacts[originalSize], the_mesh.contacts[originalSize + 1]);

  task.composerReset();
  task.composerAppendText("captured conversation");
  sendingTask = &task;
  nextThread = secondThread;
  the_mesh.onDirectSend = switchChatDuringSend;
  const bool sent = task.composerSend();
  the_mesh.onDirectSend = nullptr;
  check(sent, "Chat session: reentrant send failed");
  check(task.activeThreadIdx() == secondThread &&
        std::strcmp(task.getComposerBuffer(), "new conversation draft") == 0,
        "Chat session: completion cleared a different conversation draft");
  check(hasText(task, firstThread, "captured conversation") &&
        !hasText(task, secondThread, "captured conversation"),
        "Chat session: completion bubble followed live selection");

  task.enterThread(false, firstThread);
  task.notify(UIEventType::contactMessage);
  task.newMsgFromPub(0, second.id.pub_key, first.name, "same-name identity collision", 1);
  uint8_t bound[32]{};
  ContactInfo resolved{};
  check(task.getThreadContactPub(firstThread, bound) && std::memcmp(bound, first.id.pub_key, 32) == 0 &&
        task.lookupActiveContact(resolved) && std::memcmp(resolved.id.pub_key, first.id.pub_key, 32) == 0,
        "Chat session: receive callback repointed a pinned recipient");

  ContactInfo collisionA{}, collisionB{};
  std::strcpy(collisionA.name, "session-first-rx");
  std::strcpy(collisionB.name, "session-first-rx");
  std::memset(collisionA.id.pub_key, 0xE3, sizeof(collisionA.id.pub_key));
  std::memset(collisionB.id.pub_key, 0xE4, sizeof(collisionB.id.pub_key));
  the_mesh.contacts.push_back(collisionA);
  the_mesh.contacts.push_back(collisionB);
  task.newMsgFromPub(0, collisionB.id.pub_key, collisionB.name, "authoritative first receive", 1);
  const int receivedThread = findThread(task, collisionB.name);
  check(receivedThread >= 0 && task.getThreadContactPub(receivedThread, bound) &&
        std::memcmp(bound, collisionB.id.pub_key, 32) == 0,
        "Chat session: name lookup overrode the first received full key");
  task.enterThread(false, receivedThread);
  check(task.composerSend("reply to authoritative sender") &&
        std::memcmp(the_mesh.lastRecipient, collisionB.id.pub_key, 32) == 0,
        "Chat session: same-name directory redirected a reply");
  check(task.removeThread(receivedThread), "Chat session: receive fixture cleanup failed");

  task.enterThread(false, firstThread);
  check(task.removeThread(firstThread), "Chat session: active fixture deletion failed");
  check(!task.hasActiveThread() && !task.activeThreadIsChannel(), "Chat session: deleted selection retained state");
  check(task.removeThread(secondThread), "Chat session: fixture cleanup failed");
  task.composerReset();
  task.composerAppendText(originalDraft.c_str());
  the_mesh.contacts.resize(originalSize);
  the_mesh.pendingAck = originalAck;
  the_mesh.ackDue = originalAckDue;
  sendingTask = nullptr;
  puts("Chat session UITask integration: PASS");
}
