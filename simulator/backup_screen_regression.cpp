// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "services/BackupCatalog.h"
#include "screens/BackupPickerScreen.h"
#include "screens/BackupSettingsScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
using Catalog = ui::services::BackupCatalog;
using Path = ui::services::BackupPath;
using Picker = ui::screens::BackupPickerScreen;
using Settings = ui::screens::BackupSettingsScreen;
using ConfirmHost = ui::screens::ConfirmDialog::Host;

const char *currentScenario = "backup";

void require(bool condition, const char *message) {
  if (!condition) {
    char detail[320];
    std::snprintf(detail, sizeof(detail), "Backup scenario '%s': %s", currentScenario, message);
    throw std::runtime_error(detail);
  }
}

lv_obj_t *findText(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && !std::strcmp(lv_label_get_text(root), text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = findText(lv_obj_get_child(root, i), text))
      return found;
  return nullptr;
}

bool hasText(lv_obj_t *root, const char *text) {
  lv_obj_update_layout(root);
  return findText(root, text) != nullptr;
}

lv_obj_t *findTextContaining(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && std::strstr(lv_label_get_text(root), text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = findTextContaining(lv_obj_get_child(root, i), text))
      return found;
  return nullptr;
}

void collectLabels(lv_obj_t *root, char *out, std::size_t capacity, unsigned &count) {
  if (count >= 32 || capacity == 0)
    return;
  if (lv_obj_check_type(root, &lv_label_class)) {
    const char *label = lv_label_get_text(root);
    const std::size_t used = std::strlen(out);
    if (used < capacity)
      std::snprintf(out + used, capacity - used, "%s%s", used ? " | " : "", label ? label : "(null)");
    ++count;
  }
  const int childCount = static_cast<int>(lv_obj_get_child_cnt(root));
  for (int i = childCount - 1; i >= 0 && count < 32; --i)
    collectLabels(lv_obj_get_child(root, static_cast<uint32_t>(i)), out, capacity, count);
}

lv_obj_t *button(lv_obj_t *root, const char *text) {
  lv_obj_update_layout(root);
  auto *label = findText(root, text);
  if (!label) {
    char labels[768] = {};
    unsigned count = 0;
    collectLabels(root, labels, sizeof(labels), count);
    char detail[1200];
    std::snprintf(detail, sizeof(detail),
                  "Backup scenario '%s': exact control label missing: '%s'; actual labels (%u): %s", currentScenario,
                  text ? text : "(null)", count, labels);
    throw std::runtime_error(detail);
  }
  return lv_obj_get_parent(label);
}

lv_obj_t *buttonContaining(lv_obj_t *root, const char *text) {
  lv_obj_update_layout(root);
  auto *label = findTextContaining(root, text);
  if (!label) {
    char detail[320];
    std::snprintf(detail, sizeof(detail), "Backup scenario '%s': containing control label missing: '%s'",
                  currentScenario, text ? text : "(null)");
    throw std::runtime_error(detail);
  }
  return lv_obj_get_parent(label);
}

void click(lv_obj_t *object) {
  require(object != nullptr, "Backup click target missing");
  lv_event_send(object, LV_EVENT_CLICKED, nullptr);
}

lv_obj_t *pageRoot(lv_obj_t *object) {
  require(object != nullptr, "Backup page object missing");
  while (lv_obj_get_parent(object) && lv_obj_get_parent(object) != lv_layer_top())
    object = lv_obj_get_parent(object);
  return object;
}

void countDelete(lv_event_t *event) {
  ++*static_cast<int *>(lv_event_get_user_data(event));
}

struct Fixture;
Fixture *closingFixture = nullptr;

struct Fixture {
  int scanCount = 0;
  int imports = 0;
  int exports = 0;
  int deletes = 0;
  int resets = 0;
  int alerts = 0;
  bool replacement = false;
  bool deleteOk = true;
  bool alertRebuild = false;
  bool reentrantPicker = false;
  bool reentrantSettings = false;
  char imported[Path::Capacity] = {};
  char deleted[Path::Capacity] = {};
  char exported[48] = {};
  char alertText[64] = {};
  const char *rtcFilename = "meshcore-20260921-120000.json";
  Picker *picker = nullptr;
  Settings *settings = nullptr;
  lv_obj_t *replacementBody = nullptr;

  static void scan(void *context, Catalog &catalog) {
    auto &fixture = *static_cast<Fixture *>(context);
    ++fixture.scanCount;
    if (fixture.replacement) {
      catalog.add("int:/replacement.json", "Replacement backup");
      return;
    }
    catalog.add("int:/meshcore-backup.json", "Primary backup");
    catalog.add("sd:/meshcomod/older.JSON", "SD backup");
  }

  static void importBackup(void *context, const Path &path) {
    auto &fixture = *static_cast<Fixture *>(context);
    ++fixture.imports;
    std::strncpy(fixture.imported, path.c_str(), sizeof(fixture.imported) - 1);
  }

  static void makeFilename(void *context, char *out, std::size_t capacity) {
    auto &fixture = *static_cast<Fixture *>(context);
    std::snprintf(out, capacity, "%s", fixture.rtcFilename);
  }

  static void exportBackup(void *context, const char *filename) {
    auto &fixture = *static_cast<Fixture *>(context);
    ++fixture.exports;
    std::snprintf(fixture.exported, sizeof(fixture.exported), "%s", filename ? filename : "");
  }

  static bool deleteBackup(void *context, const Path &path) {
    auto &fixture = *static_cast<Fixture *>(context);
    ++fixture.deletes;
    std::strncpy(fixture.deleted, path.c_str(), sizeof(fixture.deleted) - 1);
    return fixture.deleteOk;
  }

  static void factoryReset(void *context) { ++static_cast<Fixture *>(context)->resets; }

  static void alert(void *context, const char *text, int) {
    auto &fixture = *static_cast<Fixture *>(context);
    ++fixture.alerts;
    std::snprintf(fixture.alertText, sizeof(fixture.alertText), "%s", text ? text : "");
    if (fixture.alertRebuild && fixture.settings && fixture.replacementBody) {
      fixture.alertRebuild = false;
      fixture.replacement = true;
      fixture.settings->build(fixture.replacementBody, 202);
    }
  }

  Catalog::Host catalogHost() { return {this, scan}; }
  ConfirmHost confirmationHost() {
    return {
        []() -> lv_coord_t { return 24; },
        [](lv_obj_t **root) {
          if (!root || !*root)
            return;
          auto *old = *root;
          *root = nullptr;
          lv_obj_del_async(old);
          auto *fixture = closingFixture;
          if (!fixture)
            return;
          if (fixture->reentrantPicker && fixture->picker) {
            fixture->reentrantPicker = false;
            fixture->picker->open();
          } else if (fixture->reentrantSettings && fixture->settings && fixture->replacementBody) {
            fixture->reentrantSettings = false;
            fixture->replacement = true;
            fixture->settings->build(fixture->replacementBody, 202);
          }
        },
        nullptr};
  }
  Picker::Host pickerHost() { return {this, catalogHost(), importBackup, confirmationHost()}; }
  Settings::Host settingsHost() {
    return {this, catalogHost(), nullptr, makeFilename, exportBackup, deleteBackup, factoryReset, alert, false,
            confirmationHost()};
  }
};

lv_obj_t *body() {
  auto *root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, 210, 240);
  return root;
}

void catalogRegression() {
  currentScenario = "catalog";
  Catalog catalog;
  require(!catalog.add("", "empty"), "Empty backup path accepted");
  char overlong[Path::Capacity + 1];
  std::memset(overlong, 'x', sizeof(overlong) - 1);
  overlong[sizeof(overlong) - 1] = '\0';
  require(!catalog.add(overlong, "too long"), "Overlong full backup path was accepted");
  require(Catalog::isJsonName("meshcore.JSON") && Catalog::isJsonName("sd:/meshcomod/MESHCORE.JsOn") &&
              !Catalog::isJsonName(".meshcore.json") && !Catalog::isJsonName("._meshcore-backup.json") &&
              !Catalog::isJsonName("meshcore.json.bak"),
          "Backup filename filter changed");

  char longDisplay[Catalog::DisplayCapacity + 20];
  std::memset(longDisplay, 'd', sizeof(longDisplay) - 1);
  longDisplay[sizeof(longDisplay) - 1] = '\0';
  require(catalog.add("int:/valid.json", longDisplay), "Valid path rejected for long display text");
  require(std::strlen(catalog.display(0)) == Catalog::DisplayCapacity - 1,
          "Long display text did not use bounded presentation storage");
  require(!catalog.add("int:/valid.json", "duplicate"), "Duplicate backup path accepted");

  Catalog limited;
  char stored[Path::Capacity];
  for (std::size_t i = 0; i < Catalog::MaxEntries; ++i) {
    std::snprintf(stored, sizeof(stored), "int:/entry-%02u.json", static_cast<unsigned>(i));
    require(limited.add(stored, stored), "Backup catalog rejected an entry before its bound");
  }
  require(limited.count() == Catalog::MaxEntries &&
              !limited.add("int:/entry-overflow.json", "overflow"),
          "Backup catalog exceeded its 24-entry bound");
}

void pickerConfirmationRegression(void (*pump)(unsigned)) {
  currentScenario = "picker-confirmation";
  Fixture fixture;
  Picker picker(fixture.pickerHost());
  fixture.picker = &picker;
  closingFixture = &fixture;

  picker.open();
  require(picker.rootOpen() && fixture.scanCount == 1, "Backup picker did not render its catalog");
  currentScenario = "picker-confirmation/initial-row";
  auto *row = button(lv_layer_top(), "Primary backup");
  click(row);
  require(!picker.rootOpen() && picker.confirmationOpen() && fixture.imports == 0,
          "Backup picker imported before confirmation");
  currentScenario = "picker-confirmation/initial-dialog";
  auto *oldAccept = button(lv_layer_top(), TR("Import"));
  click(button(lv_layer_top(), TR("Cancel")));
  // The dialog tree is still alive until the host's deferred DELETE is pumped.
  click(oldAccept);
  require(fixture.imports == 0 && !picker.confirmationOpen(), "Dismissed import confirmation remained live");
  pump(40);

  picker.open();
  currentScenario = "picker-confirmation/replacement-source-row";
  row = button(lv_layer_top(), "Primary backup");
  click(row);
  currentScenario = "picker-confirmation/replacement-dialog";
  oldAccept = button(lv_layer_top(), TR("Import"));
  fixture.replacement = true;
  picker.open();
  // Replacement dismissal must retire the old action before the old tree is deleted.
  click(oldAccept);
  require(fixture.imports == 0 && picker.rootOpen() && !picker.confirmationOpen() &&
              hasText(lv_layer_top(), "Replacement backup"),
          "Old picker confirmation affected a replacement page");
  pump(40);

  currentScenario = "picker-confirmation/replacement-row";
  row = button(lv_layer_top(), "Replacement backup");
  click(row);
  currentScenario = "picker-confirmation/replacement-dialog";
  click(button(lv_layer_top(), TR("Import")));
  require(fixture.imports == 1 && !std::strcmp(fixture.imported, "int:/replacement.json"),
          "Confirmed picker target was not captured by value");
  pump(40);
  closingFixture = nullptr;
}

void pickerExternalDeleteRegression(void (*pump)(unsigned)) {
  currentScenario = "picker-external-delete";
  Fixture fixture;
  Picker picker(fixture.pickerHost());
  closingFixture = &fixture;
  picker.open();
  auto *root = pageRoot(button(lv_layer_top(), "Primary backup"));
  int observers = 0;
  lv_obj_add_event_cb(root, countDelete, LV_EVENT_DELETE, &observers);
  lv_obj_del(root);
  require(observers == 1, "External picker root DELETE skipped a later observer");
  require(!picker.rootOpen() && !picker.confirmationOpen(), "External picker root DELETE left owner state live");
  pump(40);
  require(observers == 1, "External picker root DELETE observer fired more than once");
  closingFixture = nullptr;
}

void pickerLifetimeRegression(void (*pump)(unsigned)) {
  currentScenario = "picker-lifetime";
  Fixture fixture;
  closingFixture = &fixture;
  lv_obj_t *retainedRoot = nullptr;
  lv_obj_t *retainedRow = nullptr;
  lv_obj_t *retainedAccept = nullptr;
  {
    Picker temporary(fixture.pickerHost());
    temporary.open();
    retainedRow = button(lv_layer_top(), "Primary backup");
    retainedRoot = pageRoot(retainedRow);
    click(retainedRow);
    retainedAccept = button(lv_layer_top(), TR("Import"));
  }
  // Both the old row and the old confirmation target remain allocated until pump.
  click(retainedRow);
  click(retainedAccept);
  require(fixture.imports == 0, "Destroyed picker retained row or confirmation callbacks");
  pump(40);
  (void)retainedRoot;
  closingFixture = nullptr;
}

void pickerReentrantConfirmationRegression(void (*pump)(unsigned)) {
  currentScenario = "picker-reentrant-confirmation";
  Fixture fixture;
  Picker picker(fixture.pickerHost());
  fixture.picker = &picker;
  closingFixture = &fixture;
  picker.open();
  click(button(lv_layer_top(), "Primary backup"));
  require(picker.confirmationOpen(), "Picker reentrant confirmation did not open");
  fixture.replacement = true;
  fixture.reentrantPicker = true;
  click(button(lv_layer_top(), TR("Import")));
  require(fixture.imports == 0 && picker.rootOpen() && !picker.confirmationOpen() &&
              hasText(lv_layer_top(), "Replacement backup"),
          "Reentrant picker close applied retired import or lost replacement page");
  picker.close();
  pump(40);
  closingFixture = nullptr;
}

void settingsExternalDeleteRegression(void (*pump)(unsigned)) {
  currentScenario = "settings-external-delete";
  Fixture fixture;
  Settings settings(fixture.settingsHost());
  closingFixture = &fixture;
  auto *root = body();
  settings.build(root, 202);
  int observers = 0;
  lv_obj_add_event_cb(root, countDelete, LV_EVENT_DELETE, &observers);
  lv_obj_del(root);
  require(observers == 1, "External settings root DELETE skipped a later observer");
  pump(40);
  require(observers == 1, "External settings root DELETE observer fired more than once");
  closingFixture = nullptr;
}

void settingsReentrantConfirmationRegression(void (*pump)(unsigned)) {
  currentScenario = "settings-reentrant-confirmation";
  Fixture fixture;
  Settings settings(fixture.settingsHost());
  fixture.settings = &settings;
  closingFixture = &fixture;
  auto *first = body();
  auto *second = body();
  settings.build(first, 202);
  click(buttonContaining(first, TR("Factory reset")));
  require(settings.confirmationOpen() && fixture.resets == 0, "Settings reentrant confirmation did not open");
  fixture.replacement = true;
  fixture.replacementBody = second;
  fixture.reentrantSettings = true;
  click(button(lv_layer_top(), TR("Erase all")));
  require(fixture.resets == 0 && !settings.confirmationOpen() && fixture.scanCount == 2 &&
              hasText(second, "Replacement backup"),
          "Reentrant settings close applied retired reset or lost replacement page");
  lv_obj_del_async(first);
  settings.detach();
  lv_obj_del(second);
  pump(40);
  closingFixture = nullptr;
}

void settingsConfirmationRegression(void (*pump)(unsigned)) {
  currentScenario = "settings-confirmation";
  Fixture fixture;
  Settings settings(fixture.settingsHost());
  fixture.settings = &settings;
  closingFixture = &fixture;
  auto *first = body();
  settings.build(first, 202);
  require(hasText(first, "Primary backup"), "Backup settings rows missing");
  click(button(first, LV_SYMBOL_TRASH));
  require(settings.confirmationOpen() && fixture.deletes == 0, "Delete ran before confirmation");
  auto *oldAccept = button(lv_layer_top(), TR("Delete"));
  auto *second = body();
  fixture.replacement = true;
  settings.build(second, 202);
  click(oldAccept);
  require(fixture.deletes == 0 && hasText(second, "Replacement backup"),
          "Stale delete confirmation changed the replacement page");
  lv_obj_del_async(first);
  pump(40);

  click(button(second, LV_SYMBOL_TRASH));
  click(button(lv_layer_top(), TR("Delete")));
  require(fixture.deletes == 1 && !std::strcmp(fixture.deleted, "int:/replacement.json"),
          "Confirmed delete target was not captured by value");
  require(!settings.confirmationOpen(), "Delete confirmation remained open after acceptance");
  pump(40);
  require(fixture.scanCount == 3 && hasText(second, "Replacement backup"),
          "Deferred delete rebuild did not rescan the current page exactly once");

  settings.detach();
  lv_obj_del(second);
  pump(40);
  closingFixture = nullptr;
}

void settingsExportAndResetRegression(void (*pump)(unsigned)) {
  currentScenario = "settings-export-reset";
  Fixture fixture;
  Settings settings(fixture.settingsHost());
  fixture.settings = &settings;
  closingFixture = &fixture;
  auto *root = body();
  settings.build(root, 202);
  click(buttonContaining(root, TR("Export new backup")));
  require(fixture.exports == 1 && !std::strcmp(fixture.exported, fixture.rtcFilename),
          "Export did not preserve the supplied RTC filename");
  require(fixture.scanCount == 1, "Export rebuilt synchronously");
  pump(40);
  require(fixture.scanCount == 2 && hasText(root, "Primary backup"),
          "Deferred export rebuild did not rescan exactly once");

  click(buttonContaining(root, TR("Factory reset")));
  require(settings.confirmationOpen() && fixture.resets == 0, "Factory reset ran before confirmation");
  click(button(lv_layer_top(), TR("Erase all")));
  require(fixture.resets == 1 && !settings.confirmationOpen(), "Confirmed fake factory reset was not dispatched");
  pump(40);

  settings.detach();
  lv_obj_del(root);
  pump(40);
  closingFixture = nullptr;
}

void settingsDeleteFailureRegression(void (*pump)(unsigned)) {
  currentScenario = "settings-delete-failure";
  Fixture fixture;
  Settings settings(fixture.settingsHost());
  fixture.settings = &settings;
  fixture.deleteOk = false;
  closingFixture = &fixture;
  auto *first = body();
  auto *second = body();
  settings.build(first, 202);
  click(button(first, LV_SYMBOL_TRASH));
  require(settings.confirmationOpen(), "Delete failure confirmation did not open");
  fixture.replacement = true;
  fixture.replacementBody = second;
  fixture.alertRebuild = true;
  click(button(lv_layer_top(), TR("Delete")));
  require(fixture.deletes == 1 && fixture.alerts == 1 && !std::strcmp(fixture.alertText, TR("Delete failed")) &&
              hasText(second, "Replacement backup"),
          "Delete failure did not report through the host alert");
  pump(40);
  require(fixture.scanCount == 2, "Delete failure scheduled a stale refresh after alert replacement");

  lv_obj_del_async(first);
  settings.detach();
  lv_obj_del(second);
  pump(40);
  closingFixture = nullptr;
}

void settingsLifetimeRegression(void (*pump)(unsigned)) {
  currentScenario = "settings-lifetime";
  Fixture fixture;
  closingFixture = &fixture;
  lv_obj_t *retainedBody = nullptr;
  lv_obj_t *retainedDelete = nullptr;
  lv_obj_t *retainedAccept = nullptr;
  {
    Settings temporary(fixture.settingsHost());
    retainedBody = body();
    temporary.build(retainedBody, 202);
    retainedDelete = button(retainedBody, LV_SYMBOL_TRASH);
    click(retainedDelete);
    retainedAccept = button(lv_layer_top(), TR("Delete"));
  }
  // The owner is gone, but both the row and confirmation tree remain until pump.
  click(retainedDelete);
  click(retainedAccept);
  require(fixture.deletes == 0, "Destroyed settings owner retained row or confirmation callbacks");
  pump(40);
  lv_obj_del(retainedBody);
  pump(40);
  closingFixture = nullptr;
}

void settingsDeferredCancellationRegression(void (*pump)(unsigned)) {
  currentScenario = "settings-deferred-cancellation";
  {
    Fixture fixture;
    Settings settings(fixture.settingsHost());
    auto *root = body();
    settings.build(root, 202);
    settings.scheduleRebuild();
    settings.detach();
    pump(40);
    require(fixture.scanCount == 1, "Detached settings page rebuilt after cancellation");
    lv_obj_del(root);
    pump(40);
  }

  {
    Fixture fixture;
    Settings settings(fixture.settingsHost());
    auto *root = body();
    settings.build(root, 202);
    settings.scheduleRebuild();
    lv_obj_del(root);
    pump(40);
    require(fixture.scanCount == 1, "Externally deleted settings page rebuilt after cancellation");
  }

  {
    Fixture fixture;
    auto *root = body();
    {
      Settings settings(fixture.settingsHost());
      settings.build(root, 202);
      settings.scheduleRebuild();
    }
    pump(40);
    require(fixture.scanCount == 1, "Destroyed settings owner rebuilt after cancellation");
    lv_obj_del(root);
    pump(40);
  }

  {
    Fixture fixture;
    Settings settings(fixture.settingsHost());
    auto *first = body();
    auto *second = body();
    settings.build(first, 202);
    settings.scheduleRebuild();
    fixture.replacement = true;
    settings.build(second, 202);
    require(fixture.scanCount == 2 && hasText(second, "Replacement backup"),
            "Replacement settings page did not build");
    lv_obj_del_async(first);
    pump(40);
    require(fixture.scanCount == 2, "Canceled settings rebuild rebuilt the replacement page");
    settings.detach();
    lv_obj_del(second);
    pump(40);
  }
}

} // namespace

void runBackupScreenRegression(void (*pump)(unsigned)) {
  catalogRegression();
  pickerConfirmationRegression(pump);
  pickerExternalDeleteRegression(pump);
  pickerLifetimeRegression(pump);
  pickerReentrantConfirmationRegression(pump);
  settingsExternalDeleteRegression(pump);
  settingsReentrantConfirmationRegression(pump);
  settingsConfirmationRegression(pump);
  settingsExportAndResetRegression(pump);
  settingsDeleteFailureRegression(pump);
  settingsLifetimeRegression(pump);
  settingsDeferredCancellationRegression(pump);
}
