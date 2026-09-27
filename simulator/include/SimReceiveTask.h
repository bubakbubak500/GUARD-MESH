// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "UITask.h"

// Observe the real simulator UI notification path without changing firmware code.
class SimReceiveTask : public UITask {
public:
  using UITask::UITask;

  void (*afterNotify)(UIEventType) = nullptr;
  unsigned messageNotifications = 0;

  void notify(UIEventType type = UIEventType::none) override {
    UITask::notify(type);
    if (type == UIEventType::contactMessage || type == UIEventType::channelMessage ||
        type == UIEventType::roomMessage)
      ++messageNotifications;
    if (afterNotify) afterNotify(type);
  }
};
