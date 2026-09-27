// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "UIMessageEvent.h"
#include "ui-touch/models/MessageTypes.h"

#include <stddef.h>
#include <stdint.h>

namespace ui {

// Pure, owned preparation of one synchronous mesh delivery. Host callbacks are
// lookup-only and may reenter prepare or mutate the source event.
class MessageIngress : public MessageTypes {
public:
  struct Host {
    void *context = nullptr;
    bool (*ignoredPub)(void *, const uint8_t *) = nullptr;
    bool (*ignoredName)(void *, const char *) = nullptr;
    bool ignoreTiny = false;
  };

  struct Prepared {
    UIMessageEvent event{};
    char sender[MAX_SENDER_NAME + 1]{};
    size_t bodyOffset = 0;
    uint8_t metaFlags = 0;

    bool channel() const { return event.kind == UIEventType::channelMessage; }
    bool room() const { return event.kind == UIEventType::roomMessage; }
    const char *thread() const;
    const char *body() const;
  };

  static bool prepare(const UIMessageEvent &input, const Host &host, Prepared &output);
};

}  // namespace ui
