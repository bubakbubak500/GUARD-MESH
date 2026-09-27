#include "ui-touch/application/ChatSession.h"

#include <cstring>

namespace ui {
namespace {

struct SendGuard {
  explicit SendGuard(bool &sending) : flag(sending) { flag = true; }
  ~SendGuard() { flag = false; }
  bool &flag;
};

} // namespace

void ChatSession::configure(MessageStore &store, Host host) {
  if (_store) clear();
  _store = &store;
  _host = host;
  _active_index = -1;
  _active_channel = false;
  _active_pub_set = false;
  memset(_active_pub, 0, sizeof(_active_pub));
}

void ChatSession::clear() {
  _active_index = -1;
  _active_channel = false;
  _active_pub_set = false;
  memset(_active_pub, 0, sizeof(_active_pub));
  ++_revision;
}

bool ChatSession::select(int idx, bool channel) {
  if (!_store || idx < 0 || idx >= MAX_UI_THREADS) {
    clear();
    return false;
  }
  const UIThread &thread = _store->thread(idx);
  if (!thread.used || thread.channel != channel) {
    clear();
    return false;
  }
  _active_index = idx;
  _active_channel = channel;
  _active_pub_set = false;
  memset(_active_pub, 0, sizeof(_active_pub));
  if (!channel && nonzero(thread.mesh_contact_pub, sizeof(thread.mesh_contact_pub))) {
    _active_pub_set = true;
    memcpy(_active_pub, thread.mesh_contact_pub, sizeof(_active_pub));
  }
  ++_revision;
  syncBindings(idx);
  return true;
}

int ChatSession::activeIndex() const { return _active_index; }

bool ChatSession::activeChannel() const { return _active_index >= 0 && _active_channel; }

uint32_t ChatSession::revision() const { return _revision; }

bool ChatSession::nonzero(const uint8_t *bytes, size_t length) {
  if (!bytes) return false;
  uint8_t value = 0;
  for (size_t i = 0; i < length; ++i) value |= bytes[i];
  return value != 0;
}

bool ChatSession::equalName(const char *a, const char *b) {
  return a && b && a[0] && b[0] && strncmp(a, b, MAX_THREAD_NAME + 1) == 0;
}

void ChatSession::copyName(char *out, const char *name) {
  if (!name) {
    out[0] = '\0';
    return;
  }
  strncpy(out, name, MAX_THREAD_NAME);
  out[MAX_THREAD_NAME] = '\0';
}

int ChatSession::resolveFull(const uint8_t *pub, Contact *out) const {
  if (!nonzero(pub, 32) || !_host.contactCount || !_host.contact) return -1;
  const int count = _host.contactCount(_host.context);
  for (int i = 0; i < count; ++i) {
    Contact candidate{};
    if (!_host.contact(_host.context, i, candidate) || !nonzero(candidate.pub, 32)) continue;
    candidate.name[MAX_THREAD_NAME] = '\0';
    if (memcmp(candidate.pub, pub, 32) == 0) {
      if (out) *out = candidate;
      return i;
    }
  }
  return -1;
}

int ChatSession::resolveKey6(const uint8_t *key6, Contact *out) const {
  if (!nonzero(key6, 6) || !_host.contactCount || !_host.contact) return -1;
  const int count = _host.contactCount(_host.context);
  int match = -1;
  Contact found{};
  for (int i = 0; i < count; ++i) {
    Contact candidate{};
    if (!_host.contact(_host.context, i, candidate) || !nonzero(candidate.pub, 32) ||
        memcmp(candidate.pub, key6, 6) != 0) continue;
    if (match >= 0) return -1;
    match = i;
    candidate.name[MAX_THREAD_NAME] = '\0';
    found = candidate;
  }
  if (match >= 0 && out) *out = found;
  return match;
}

int ChatSession::resolveName(const char *name, Contact *out) const {
  if (!name || !name[0] || !_host.contactCount || !_host.contact) return -1;
  const int count = _host.contactCount(_host.context);
  int match = -1;
  Contact found{};
  for (int i = 0; i < count; ++i) {
    Contact candidate{};
    if (!_host.contact(_host.context, i, candidate) || !nonzero(candidate.pub, 32)) continue;
    candidate.name[MAX_THREAD_NAME] = '\0';
    if (!equalName(candidate.name, name)) continue;
    if (match >= 0) return -1;
    match = i;
    found = candidate;
  }
  if (match >= 0 && out) *out = found;
  return match;
}

int ChatSession::resolveChannel(const char *name) const {
  if (!name || !name[0] || !_host.channelCount || !_host.channel) return -1;
  const int count = _host.channelCount(_host.context);
  int match = -1;
  for (int i = 0; i < count; ++i) {
    Channel candidate{};
    if (!_host.channel(_host.context, i, candidate)) continue;
    candidate.name[MAX_THREAD_NAME] = '\0';
    if (!equalName(candidate.name, name)) continue;
    if (match >= 0) return -1;
    match = i;
  }
  return match;
}

bool ChatSession::resolveThreadContact(int idx, Contact &out) const {
  if (!_store || idx < 0 || idx >= MAX_UI_THREADS) return false;
  const UIThread &thread = _store->thread(idx);
  if (!thread.used || thread.channel) return false;
  if (nonzero(thread.mesh_contact_pub, 32))
    return resolveFull(thread.mesh_contact_pub, &out) >= 0;
  if (nonzero(thread.mesh_contact_key6, 6))
    return resolveKey6(thread.mesh_contact_key6, &out) >= 0;
  return resolveName(thread.name, &out) >= 0;
}

bool ChatSession::syncBindings(int idx) {
  if (!_store || idx < 0 || idx >= MAX_UI_THREADS) return false;
  const UIThread thread = _store->thread(idx);
  if (!thread.used) return false;
  if (thread.channel) {
    _store->bindChannel(idx, -1);
    const int slot = resolveChannel(thread.name);
    if (slot >= 0) _store->bindChannel(idx, static_cast<int16_t>(slot));
    return true;
  }

  Contact match{};
  int contactIndex = -1;
  const bool hasFull = nonzero(thread.mesh_contact_pub, 32);
  const bool hasKey6 = nonzero(thread.mesh_contact_key6, 6);
  if (idx == _active_index && _active_pub_set) {
    if ((hasFull && memcmp(thread.mesh_contact_pub, _active_pub, 32) != 0) ||
        (!hasFull && hasKey6 && memcmp(thread.mesh_contact_key6, _active_pub, 6) != 0))
      return false;
    // An established full key outranks any remaining weak lookup, including
    // after a legacy binding is restored while this session is still open.
    contactIndex = resolveFull(_active_pub, &match);
    return _store->bindContact(idx, static_cast<int16_t>(contactIndex), _active_pub);
  }
  if (hasFull) {
    if (!adoptActiveKey(idx, thread.mesh_contact_pub)) return false;
    contactIndex = resolveFull(thread.mesh_contact_pub, &match);
    return _store->bindContact(idx, static_cast<int16_t>(contactIndex));
  }
  if (hasKey6) {
    contactIndex = resolveKey6(thread.mesh_contact_key6, &match);
    if (contactIndex >= 0) {
      if (!adoptActiveKey(idx, match.pub)) return false;
      return _store->bindContact(idx, static_cast<int16_t>(contactIndex), match.pub);
    }
    return _store->bindContact(idx, -1, nullptr);
  }
  contactIndex = resolveName(thread.name, &match);
  if (contactIndex >= 0) {
    if (!adoptActiveKey(idx, match.pub)) return false;
    return _store->bindContact(idx, static_cast<int16_t>(contactIndex), match.pub);
  }
  return _store->bindContact(idx, -1, nullptr);
}

bool ChatSession::adoptActiveKey(int idx, const uint8_t *pub) {
  if (idx != _active_index) return true;
  if (!pub || !nonzero(pub, 32)) return false;
  if (_active_pub_set) return memcmp(_active_pub, pub, 32) == 0;
  memcpy(_active_pub, pub, sizeof(_active_pub));
  _active_pub_set = true;
  ++_revision;
  return true;
}

bool ChatSession::activeContact(Contact &out) const {
  if (!_store || _active_index < 0 || _active_index >= MAX_UI_THREADS || _active_channel) return false;
  const UIThread &thread = _store->thread(_active_index);
  if (!thread.used || thread.channel) return false;
  if (_active_pub_set) {
    if (nonzero(thread.mesh_contact_pub, 32) &&
        memcmp(thread.mesh_contact_pub, _active_pub, 32) != 0) return false;
    if (!nonzero(thread.mesh_contact_pub, 32) && nonzero(thread.mesh_contact_key6, 6) &&
        memcmp(thread.mesh_contact_key6, _active_pub, 6) != 0) return false;
    return resolveFull(_active_pub, &out) >= 0;
  }
  return resolveThreadContact(_active_index, out);
}

bool ChatSession::bindReceivedContact(int idx, const uint8_t *pub) {
  if (!_store || !pub || !nonzero(pub, 32) || idx < 0 || idx >= MAX_UI_THREADS) return false;
  const UIThread &thread = _store->thread(idx);
  if (!thread.used || thread.channel) return false;
  const bool hasFull = nonzero(thread.mesh_contact_pub, 32);
  const bool hasKey6 = nonzero(thread.mesh_contact_key6, 6);
  if (hasFull && memcmp(thread.mesh_contact_pub, pub, 32) != 0) return false;
  if (!hasFull && hasKey6 && memcmp(thread.mesh_contact_key6, pub, 6) != 0) return false;
  if (idx == _active_index && _active_pub_set && memcmp(_active_pub, pub, 32) != 0) return false;

  const bool changesActiveKey = idx == _active_index &&
      (!_active_pub_set || memcmp(_active_pub, pub, 32) != 0);
  if (!_store->bindContact(idx, -1, pub)) return false;
  if (idx == _active_index) {
    _active_pub_set = true;
    memcpy(_active_pub, pub, sizeof(_active_pub));
  }
  syncBindings(idx);
  if (changesActiveKey) ++_revision;
  return true;
}

bool ChatSession::makeTarget(Target &out) {
  if (!_store || _active_index < 0 || _active_index >= MAX_UI_THREADS) return false;
  if (!syncBindings(_active_index)) return false;
  const UIThread &thread = _store->thread(_active_index);
  if (!thread.used || thread.channel != _active_channel) return false;
  out.thread = _active_index;
  out.channel = _active_channel;
  out.revision = _revision;
  copyName(out.name, thread.name);
  if (_active_channel) {
    out.slot = thread.mesh_channel_slot;
    return out.slot >= 0;
  }
  // syncBindings is the single resolver; a missing/ambiguous weak identity
  // remains unbound and must not acquire a second, different fallback here.
  if (!_active_pub_set || !nonzero(thread.mesh_contact_pub, 32)) return false;
  memcpy(out.pub, _active_pub, sizeof(out.pub));
  return true;
}

ChatSession::SendResult ChatSession::send(RadioTransport &transport, const char *text,
    const char *sender, size_t maxText, RadioService::Trace trace) {
  SendResult result{};
  if (_sending) {
    result.status = Status::Busy;
    return result;
  }
  if (!_store || _active_index < 0 || _active_index >= MAX_UI_THREADS) return result;
  if (!text || !text[0]) {
    result.status = Status::Empty;
    return result;
  }
  if (!sender) return result;
  SendGuard guard(_sending);

  size_t textLimit = maxText < MAX_MSG_TEXT ? maxText : MAX_MSG_TEXT;
  size_t textLength = 0;
  while (textLength < textLimit && text[textLength]) ++textLength;
  if (!textLength) {
    result.status = Status::Empty;
    return result;
  }
  memcpy(result.text, text, textLength);
  result.text[textLength] = '\0';
  size_t senderLength = 0;
  while (senderLength < MAX_SENDER_NAME && sender[senderLength]) ++senderLength;
  memcpy(result.sender, sender, senderLength);
  result.sender[senderLength] = '\0';

  if (!makeTarget(result.target)) {
    const bool channel = _store->thread(_active_index).used && _store->thread(_active_index).channel;
    result.status = channel ? Status::MissingChannel : Status::MissingKey;
    return result;
  }
  if (result.target.channel) {
    result.radio = _radio.sendChannel(transport, result.target.slot, result.target.name,
                                     result.sender, result.text, trace);
  } else {
    result.radio = _radio.sendDirect(transport, result.target.pub, result.text, trace);
  }
  switch (result.radio.status) {
  case RadioSendResult::Sent: result.status = Status::Sent; break;
  case RadioSendResult::MissingChannel: result.status = Status::MissingChannel; break;
  case RadioSendResult::MissingContact: result.status = Status::MissingContact; break;
  default: result.status = Status::Failed; break;
  }
  return result;
}

bool ChatSession::matchesSelection(const Target &target) const {
  return target.revision == _revision && target.thread == _active_index &&
      target.channel == _active_channel && canCommit(target);
}

bool ChatSession::canCommit(const Target &target) const {
  if (!_store || target.thread < 0 || target.thread >= MAX_UI_THREADS) return false;
  const UIThread &thread = _store->thread(target.thread);
  if (!thread.used || thread.channel != target.channel || !equalName(thread.name, target.name)) return false;
  if (target.channel) return true;
  return nonzero(thread.mesh_contact_pub, 32) && memcmp(thread.mesh_contact_pub, target.pub, 32) == 0;
}

} // namespace ui
