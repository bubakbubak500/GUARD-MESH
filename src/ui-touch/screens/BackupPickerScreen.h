// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../services/BackupCatalog.h"
#include "../widgets/ObjectRef.h"
#include "ConfirmDialog.h"
#include <cstdint>

namespace ui {
namespace screens {

class BackupPickerScreen {
public:
  struct Host {
    void *context = nullptr;
    services::BackupCatalog::Host catalog{};
    void (*importBackup)(void *, const services::BackupPath &) = nullptr;
    ConfirmDialog::Host confirmation{};
  };

  explicit BackupPickerScreen(Host host);
  ~BackupPickerScreen();
  BackupPickerScreen(const BackupPickerScreen &) = delete;
  BackupPickerScreen &operator=(const BackupPickerScreen &) = delete;

  void open();
  void close();
  void detach();
  bool rootOpen() const { return _root.get() != nullptr; }
  bool confirmationOpen() const { return _confirmation.isOpen(); }
  void dismissConfirmation() { ++_generation; _confirmation.dismiss(); }

private:
  static void deleted(lv_event_t *);
  static void event(lv_event_t *);
  bool owns(lv_event_t *) const;
  void unbind(lv_obj_t *);
  void pick(lv_event_t *);

  widgets::ObjectRef _root;
  widgets::ObjectRef _close;
  widgets::ObjectRef _rows[services::BackupCatalog::MaxEntries];
  Host _host;
  services::BackupCatalog _catalog;
  ConfirmDialog _confirmation;
  uint32_t _generation = 0;
  bool _destroying = false;
};

} // namespace screens
} // namespace ui
