// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/GuardianV3Client.h"
#include "../widgets/ObjectRef.h"
namespace ui { namespace screens {
// UI-thread owner of one system-layer emergency overlay. Read state belongs to
// the durable v3 client; waking the panel never changes the manual lock policy.
class GuardianEmergency {
public:
  struct Host {
    void (*wake)(); void (*keepAwake)(); void (*beep)();
    Host(void (*w)() = nullptr, void (*k)() = nullptr, void (*b)() = nullptr)
      : wake(w), keepAwake(k), beep(b) {}
  };
  explicit GuardianEmergency(guardian::v3::Client &model) : _client(model) {}
  ~GuardianEmergency();
  GuardianEmergency(const GuardianEmergency &) = delete;
  GuardianEmergency &operator=(const GuardianEmergency &) = delete;
  void configure(Host host) { _host = host; }
  void tick(uint32_t now);
  bool visible() const { return _root.get() != nullptr; }
  lv_obj_t *root() const { return _root.get(); }
private:
  void show(const guardian::v3::Alert &);
  void close();
  void read();
  guardian::v3::Client &_client;
  Host _host;
  widgets::ObjectRef _root, _error;
  std::string _id, _signature;
  uint32_t _wakeAt = 0, _beepAt = 0, _now = 0;
  uint32_t _observedRevision = 0;
  bool _started = false;
};
void guardianEmergencyConfigure(GuardianEmergency::Host);
void guardianEmergencyTick(uint32_t now);
bool guardianEmergencyVisible();
} }
