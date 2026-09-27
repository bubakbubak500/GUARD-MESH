// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "widgets/ObjectRef.h"
#include <cstddef>
#include <cstdint>

namespace ui { namespace screens {

// UI-thread owner for the four-step first-boot flow. Radio/BLE boot policy,
// persistent settings, status bar, keyboard, and navigation remain with Host.
class SetupWizardScreen {
public:
  static constexpr size_t CommittedNameCapacity = 32; // NodePrefs storage shape
  static constexpr size_t DraftCapacity = 121; // 30 UTF-8 code points x 4 bytes + NUL
  static constexpr unsigned PresetCapacity = 32;
  enum class Step : uint8_t { Welcome, Name, Region, Connectivity };
  struct Committed {
    char name[CommittedNameCapacity] = {};
    int regionIndex = -1;
  };
  struct Host {
    void *context = nullptr;
    bool (*readCommitted)(void *, Committed &) = nullptr;
    // Labels belong to an immutable process-lifetime preset catalog. Both
    // callbacks are pure and never reenter this owner.
    unsigned (*presetCount)(void *) = nullptr;
    const char *(*presetLabel)(void *, unsigned) = nullptr;
    bool (*applyName)(void *, const char *) = nullptr;
    bool (*applyRegion)(void *, unsigned) = nullptr;
    bool (*markDone)(void *, bool skipped) = nullptr;
    void (*hideKeyboard)(void *) = nullptr;
    void (*syncKeyboard)(void *) = nullptr;
    void (*attachTextArea)(void *, lv_obj_t *) = nullptr;
    void (*statusBarHidden)(void *, bool) = nullptr;
    void (*resetNav)(void *) = nullptr;
    void (*navDetachBeforeTreeMutation)(void *) = nullptr;
    void (*navDirty)(void *) = nullptr;
    void (*focusNext)(void *, lv_obj_t *) = nullptr;
    void (*showAlert)(void *, const char *, unsigned) = nullptr;
    // May defer deletion; consumes and nulls the passed retired root.
    void (*closeRoot)(void *, lv_obj_t **) = nullptr;
  };

  explicit SetupWizardScreen(Host host);
  ~SetupWizardScreen();
  SetupWizardScreen(const SetupWizardScreen &) = delete;
  SetupWizardScreen &operator=(const SetupWizardScreen &) = delete;

  void show(); // idempotent while visible
  void hide();
  void back(); // used by hardware Back as well as the on-screen button
  bool visible() const { return _root.get() != nullptr; }
  lv_obj_t *root() const { return _root.get(); }
  Step step() const { return _step; }

private:
  static void rootDeleted(lv_event_t *);
  static void skipClicked(lv_event_t *);
  static void startedClicked(lv_event_t *);
  static void backClicked(lv_event_t *);
  static void nameNextClicked(lv_event_t *);
  static void regionNextClicked(lv_event_t *);
  static void finishClicked(lv_event_t *);
  static void regionClicked(lv_event_t *);
  bool still(uint32_t, lv_obj_t *) const;
  bool readCommitted(Committed &);
  void detachOwned(lv_obj_t *, bool rootDeleting = false);
  void clear(lv_obj_t *retiring, bool externalDelete);
  void showStep(Step);
  void fillRegions();
  void recolorRegions();
  lv_obj_t *button(const char *, lv_event_cb_t, bool, lv_coord_t, lv_coord_t, lv_coord_t);
  int header(const char *, const char *, const char *);
  void nameNext();
  void regionNext();
  void complete(bool skipped);
  void alert(const char *, unsigned);

  Host _host;
  widgets::ObjectRef _root, _nameField, _regionList, _regionNext;
  Step _step = Step::Welcome;
  int _selectedRegion = -1;
  char _draftName[DraftCapacity] = {};
  uint32_t _generation = 0;
  bool _destroying = false;
};

} } // namespace ui::screens
