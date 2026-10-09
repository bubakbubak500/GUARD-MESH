// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
namespace ui {
class GeneralSettings {
public:
  enum class Storage { Internal, Sd, MissingCard, MissingFullData, MigrationBlocked, MountFailed };
  enum class Action { Setup, Reboot, Recover, ConsoleOn, ConsoleOff };
  struct StorageSnapshot {
    bool contactsOnSd = false, cardPresent = false, migrationBlocked = false;
  };
  struct Capabilities {
    bool sd, storageStatus, recovery, resumeRecovery, console;
  };
  struct Host {
    void *context;
    void (*storage)(void *, StorageSnapshot &);
    bool (*advert)(void *);
    void (*action)(void *, Action);
    Capabilities capabilities;
    void (*heardNames)(void*, unsigned&, unsigned&) = nullptr;
    bool (*clearHeardNames)(void*) = nullptr;
  };
  struct State {
    unsigned history, fallback;
    bool sd, console;
    Storage storage;
  };
  explicit GeneralSettings(Host host) : _host(host) {}
  State read() const;
  static Storage storageStatus(bool wantSd, const StorageSnapshot &);
  Capabilities capabilities() const { return _host.capabilities; }
  bool setHistory(unsigned index);
  bool setFallback(unsigned index);
  bool setSd(bool);
  bool setConsole(bool);
  bool advert();
  bool action(Action);
  void heardNames(unsigned& count, unsigned& capacity) const;
  bool clearHeardNames();

private:
  Host _host;
};
} // namespace ui
