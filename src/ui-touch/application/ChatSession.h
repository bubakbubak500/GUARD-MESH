#pragma once

#include "ui-touch/models/MessageStore.h"
#include "ui-touch/services/RadioService.h"

#include <cstddef>
#include <cstdint>

namespace ui {

// UI-thread owner of selection and send identity. The store owns history;
// the session pins a full recipient key and returns copied send results.
class ChatSession : public MessageTypes {
public:
  struct Contact {
    char name[MAX_THREAD_NAME + 1]{};
    uint8_t pub[32]{};
  };

  struct Channel {
    char name[MAX_THREAD_NAME + 1]{};
  };

  struct Host {
    // Synchronous directory queries only: must not mutate the session/store.
    // Transport calls may reenter selection; send snapshots survive that.
    void *context = nullptr;
    int (*contactCount)(void *) = nullptr;
    bool (*contact)(void *, int, Contact &) = nullptr;
    int (*channelCount)(void *) = nullptr;
    bool (*channel)(void *, int, Channel &) = nullptr;
  };

  enum class Status { Invalid, Empty, MissingChannel, MissingKey, MissingContact, Failed, Sent, Busy };

  struct Target {
    int thread = -1;
    bool channel = false;
    int16_t slot = -1;
    char name[MAX_THREAD_NAME + 1]{};
    uint8_t pub[32]{};
    uint32_t revision = 0;
  };

  struct SendResult {
    Status status = Status::Invalid;
    Target target{};
    RadioSendResult radio{};
    char sender[MAX_SENDER_NAME + 1]{};
    char text[MAX_MSG_TEXT + 1]{};
  };

  void configure(MessageStore &store, Host host);
  void clear();
  bool select(int idx, bool channel);
  int activeIndex() const;
  bool activeChannel() const;
  uint32_t revision() const;
  bool syncBindings(int idx);
  bool activeContact(Contact &out) const;
  bool bindReceivedContact(int idx, const uint8_t *pub);

  // A nested send is rejected. Completion never reads live selection to decide
  // which conversation sent the message or which text was transmitted.
  SendResult send(RadioTransport &transport, const char *text, const char *sender,
                  size_t maxText, RadioService::Trace trace = nullptr);
  bool matchesSelection(const Target &target) const;
  // Selection may have changed; the original model slot must still match.
  bool canCommit(const Target &target) const;

private:
  MessageStore *_store = nullptr;
  Host _host{};
  int _active_index = -1;
  bool _active_channel = false;
  bool _active_pub_set = false;
  uint8_t _active_pub[32]{};
  uint32_t _revision = 0;
  bool _sending = false;
  RadioService _radio;

  static bool nonzero(const uint8_t *bytes, size_t length);
  static bool equalName(const char *a, const char *b);
  static void copyName(char *out, const char *name);
  int resolveFull(const uint8_t *pub, Contact *out = nullptr) const;
  int resolveKey6(const uint8_t *key6, Contact *out = nullptr) const;
  int resolveName(const char *name, Contact *out = nullptr) const;
  int resolveChannel(const char *name) const;
  bool resolveThreadContact(int idx, Contact &out) const;
  bool adoptActiveKey(int idx, const uint8_t *pub);
  bool makeTarget(Target &out);
};

} // namespace ui
