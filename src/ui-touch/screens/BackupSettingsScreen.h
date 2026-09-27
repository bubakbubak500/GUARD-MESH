// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../services/BackupCatalog.h"
#include "../widgets/ObjectRef.h"
#include "ConfirmDialog.h"
#include <cstdint>

namespace ui {
namespace screens {

class BackupSettingsScreen {
public:
  struct Host {
    void *context = nullptr;
    services::BackupCatalog::Host catalog{};
    void (*openPicker)(void *) = nullptr;
    void (*makeFilename)(void *, char *, std::size_t) = nullptr;
    void (*exportBackup)(void *, const char *) = nullptr;
    bool (*deleteBackup)(void *, const services::BackupPath &) = nullptr;
    void (*factoryReset)(void *) = nullptr;
    void (*alert)(void *, const char *, int) = nullptr;
    bool showImport = false;
    ConfirmDialog::Host confirmation{};
  };

  explicit BackupSettingsScreen(Host host);
  ~BackupSettingsScreen();
  BackupSettingsScreen(const BackupSettingsScreen &) = delete;
  BackupSettingsScreen &operator=(const BackupSettingsScreen &) = delete;

  void build(lv_obj_t *body, lv_coord_t width);
  void detach();
  void scheduleRebuild();
  bool confirmationOpen() const { return _confirmation.isOpen(); }
  void dismissConfirmation() { ++_generation; _confirmation.dismiss(); }

private:
  struct Deferred;
  static void deleted(lv_event_t *);
  static void event(lv_event_t *);
  static void deferred(void *);
  bool owns(lv_event_t *) const;
  void unbind(lv_obj_t *);
  void notify(const char *, int);
  void render();
  void exportBackup();
  void deleteBackup(lv_event_t *);
  void confirmFactoryReset();
  void cancelDeferred();

  widgets::ObjectRef _body;
  widgets::ObjectRef _export;
  widgets::ObjectRef _import;
  widgets::ObjectRef _factoryReset;
  widgets::ObjectRef _deleteButtons[services::BackupCatalog::MaxEntries];
  Host _host;
  services::BackupCatalog _catalog;
  ConfirmDialog _confirmation;
  Deferred *_deferred = nullptr;
  lv_coord_t _width = 0;
  uint32_t _generation = 0;
  uint32_t _request = 0;
  bool _destroying = false;
};

} // namespace screens
} // namespace ui
