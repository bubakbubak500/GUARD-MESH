// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../widgets/ObjectRef.h"
#include <cstdint>

namespace ui { namespace screens {

// Owns only the blocked-users LVGL page. Persistence and contact lookup live
// in Host. All methods and callbacks run on the UI thread.
class BlockedUsersScreen {
public:
  static constexpr unsigned KeyCapacity = 32;
  static constexpr unsigned NameCapacity = 16;
  static constexpr unsigned KeyBytes = 6;
  static constexpr unsigned NameBytes = 32;

  struct KeyEntry {
    uint8_t key[KeyBytes] = {};
    char display[40] = {}; // resolved contact name; blank uses hex key fallback
  };
  struct NameEntry { char name[NameBytes] = {}; };
  struct Snapshot {
    KeyEntry keys[KeyCapacity] = {};
    NameEntry names[NameCapacity] = {};
    unsigned keyCount = 0, nameCount = 0;
  };
  struct Host {
    void *context = nullptr;
    // One bounded copy of current prefs and resolved display names.
    bool (*readSnapshot)(void *, Snapshot &) = nullptr;
    // Commands receive a copied value. Adapter re-resolves its paired contact
    // at action time. These are void: prefs setters return new membership,
    // where false is the expected state after successful removal.
    void (*unblockKey)(void *, const uint8_t key[KeyBytes]) = nullptr;
    void (*unblockName)(void *, const char *name) = nullptr;
    // Activate page/tall status bar and return its actual current height.
    int (*beginPage)(void *, const char *title) = nullptr;
    void (*endPageIfOwned)(void *) = nullptr;
    void (*bringStatusBarFront)(void *) = nullptr;
    void (*navDirty)(void *) = nullptr;
    void (*closeRoot)(void *, lv_obj_t **) = nullptr; // may defer deletion
  };

  explicit BlockedUsersScreen(Host host);
  ~BlockedUsersScreen();
  BlockedUsersScreen(const BlockedUsersScreen &) = delete;
  BlockedUsersScreen &operator=(const BlockedUsersScreen &) = delete;

  void show();
  void close();
  void rebuild();
  bool isOpen() const { return _root.get() != nullptr; }
  lv_obj_t *root() const { return _root.get(); }

private:
  enum class Kind : uint8_t { Key, Name };
  struct RowContext {
    BlockedUsersScreen *owner;
    uint32_t generation;
    Kind kind;
    uint8_t key[KeyBytes];
    char name[NameBytes];
  };
  static constexpr unsigned RowCapacity = KeyCapacity + NameCapacity;

  static void rootDeleted(lv_event_t *);
  static void rowClicked(lv_event_t *);
  bool still(uint32_t generation, lv_obj_t *root) const;
  void retire(bool deleting, lv_obj_t *deletingRoot = nullptr);
  void detachRows(lv_obj_t *root, RowContext *rows, unsigned count);
  void fillRows(lv_obj_t *root, uint32_t generation, lv_coord_t top);
  void appendRow(lv_obj_t *list, const char *label, RowContext &row);

  Host _host;
  widgets::ObjectRef _root;
  RowContext *_rows = nullptr; // allocated only while page rows exist
  unsigned _rowCount = 0;
  uint32_t _generation = 0;
  bool _destroying = false;
};

} } // namespace ui::screens
