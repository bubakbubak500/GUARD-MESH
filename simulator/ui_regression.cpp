// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_regression.h"
#include "SimPlatform.h"
#include "platform/MeshRadioTransport.h"
#include "screens/ConfirmDialog.h"
#include "screens/QuickRepliesScreen.h"
#include "screens/ContactsScreen.h"
#include "screens/SettingsScreen.h"
#include "theme/Fonts.h"
#include "theme/Theme.h"
#include "i18n.h"
#include <cstring>
#include <cstdio>
#include <stdexcept>

namespace {
int accepted = 0, stale_accepted = 0, alerts = 0;
void require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
lv_obj_t* label(lv_obj_t* object, const char* text) {
  if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text)) return object;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    if (auto* found = label(lv_obj_get_child(object, i), text)) return found;
  return nullptr;
}
lv_obj_t* field(lv_obj_t* object) {
  if (lv_obj_check_type(object, &lv_textarea_class)) return object;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    if (auto* found = field(lv_obj_get_child(object, i))) return found;
  return nullptr;
}
void clickLabel(lv_obj_t* root, const char* text) {
  auto* found = label(root, text);
  require(found != nullptr, "Dialog button missing");
  lv_event_send(lv_obj_get_parent(found), LV_EVENT_CLICKED, nullptr);
}
void closeAsync(lv_obj_t** root) { lv_obj_del_async(*root); *root = nullptr; }

struct TestMesh {
  struct Clock { uint32_t getCurrentTimeUnique() { return 10; } } clock;
  ContactInfo contact{};
  ChannelDetails group{};
  bool fail = false, change_channel = false;
  int sends = 0, pops = 0, registrations = 0, reads = 0;
  uint8_t route = 0, registered_key[32] = {};
  TestMesh() { contact.id.pub_key[0] = 7; contact.out_path_len = 3; strcpy(group.name, "group"); }
  Clock* getRTCClock() { return &clock; }
  bool getChannel(int slot, ChannelDetails& out) {
    out = group;
    if (change_channel && ++reads > 1) strcpy(out.name, "different");
    return slot == 2;
  }
  ContactInfo* lookupContactByPubKey(const uint8_t* key, int size) {
    return !memcmp(key, contact.id.pub_key, size) ? &contact : nullptr;
  }
  bool pushChannelScope(const char*) { return true; }
  void popChannelScope() { ++pops; }
  template<class Channel> bool sendGroupMessage(uint32_t, const Channel&, const char*, const char*, int) { ++sends; return !fail; }
  int sendMessage(ContactInfo& target, uint32_t, uint8_t, const char*, uint32_t& ack, uint32_t&) {
    ++sends; route = target.out_path_len; ack = 42; return fail ? MSG_SEND_FAILED : MSG_SEND_SENT_FLOOD;
  }
  bool getLastTxtTxHash4(uint32_t& hash) { hash = 9; return true; }
  uint32_t uiLastSentFp() { return 77; }
  void uiRegisterExpectedAck(uint32_t ack, const uint8_t* key) {
    require(ack == 42, "Wrong registered ACK");
    ++registrations; memcpy(registered_key, key, sizeof registered_key);
  }
};
void radioAdapterRegression() {
  TestMesh mesh;
  ui::platform::MeshRadioTransport<TestMesh> adapter(mesh);
  ui::RadioService radio;
  using Result = ui::RadioSendResult;
  require(radio.sendChannel(adapter, 2, "wrong", "me", "test").status == Result::MissingChannel && !mesh.sends,
          "Stale channel slot transmitted with another channel's key");
  mesh.change_channel = true;
  require(radio.sendChannel(adapter, 2, "group", "me", "test").status == Result::MissingChannel && !mesh.sends,
          "Changed channel was not rechecked at send time");
  mesh.change_channel = false; mesh.fail = true;
  require(radio.sendChannel(adapter, 2, "group", "me", "test").status == Result::Failed && mesh.pops == 1,
          "Failed group send retained its region scope");
  auto result = radio.sendDirect(adapter, mesh.contact.id.pub_key, "test");
  require(result.status == Result::Failed && !mesh.registrations, "Failed DM registered an ACK");
  mesh.fail = false;
  result = radio.sendDirect(adapter, mesh.contact.id.pub_key, "test");
  require(result.status == Result::Sent && result.fingerprint == 77 && result.ack == 42,
          "Radio adapter lost the send result");
  require(mesh.route == OUT_PATH_UNKNOWN && mesh.contact.out_path_len == 3 && mesh.registrations == 1 &&
          !memcmp(mesh.registered_key, mesh.contact.id.pub_key, 32), "Radio adapter changed routing or ACK identity");
}

void screenLifetimeRegression() {
  using Contacts = ui::screens::ContactsScreen;
  ui::ContactEntry entry{};
  strcpy(entry.name, "Alpha"); entry.key6[0] = 1;
  auto* first = lv_obj_create(lv_layer_top());
  auto* second = lv_obj_create(lv_layer_top());
  {
    Contacts contacts({[](const uint8_t*){ return false; },
      [](const lv_font_t*, char* out, size_t cap, const char* text){ snprintf(out, cap, "%s", text); },
      [](char* out, size_t cap, uint32_t age){ snprintf(out, cap, "%lu", static_cast<unsigned long>(age)); },
      [](char* out, size_t cap, double, double, int32_t, int32_t){ snprintf(out, cap, "1km"); },
      [](lv_event_t*){}});
    contacts.render(first, &entry, 1, 100, 0, 0, false);
    lv_obj_update_layout(first); // LONG_DOT temporarily shows dots until layout resolves
    require(label(first, entry.name), "Contact snapshot was not rendered");
    contacts.render(first, &entry, 1, 100, 0, 0, false);
    require(contacts.rowCount() == 1 && lv_obj_get_child_cnt(first) == 2 &&
            lv_obj_has_flag(lv_obj_get_child(first, 0), LV_OBJ_FLAG_HIDDEN),
            "Repeated contact render retained stale rows beyond its hidden content pool");
    contacts.render(second, &entry, 1, 100, 0, 0, false);
    lv_obj_update_layout(second);
    require(!lv_obj_get_child_cnt(first), "Replaced contact screen retained borrowed callback data");
    lv_obj_del(first);
    require(contacts.refresh(200, [](const uint8_t* key, ui::ContactEntry& out){
      require(key[0] == 1, "Contact row identity was lost");
      strcpy(out.name, "Bravo"); out.last_heard = 190; return true;
    }) && label(second, "Bravo"), "Old deletion invalidated the new contact screen");
    lv_obj_del(second);
    require(!contacts.refresh(300, [](const uint8_t*, ui::ContactEntry&){ return true; }),
            "Deleted contact root retained cached labels");
    second = lv_obj_create(lv_layer_top());
    contacts.render(second, &entry, 1, 100, 0, 0, false);
  }
  require(!lv_obj_get_child_cnt(second), "Destroyed contact controller retained row callbacks");
  lv_obj_del(second);

  first = lv_obj_create(lv_layer_top()); second = lv_obj_create(lv_layer_top());
  {
    ui::screens::SettingsScreen settings;
    const ui::screens::SettingsScreen::Category categories[] = {{"Profile", LV_SYMBOL_HOME}, {"About", LV_SYMBOL_SETTINGS}};
    settings.build(first, categories, 2, nullptr, [](lv_event_t*){}, [](lv_obj_t*){}, 1);
    require(settings.badge(), "Settings badge missing");
    settings.build(second, categories, 2, nullptr, [](lv_event_t*){}, [](lv_obj_t*){}, 1);
    require(!lv_obj_get_child_cnt(first), "Settings rebuild left its old root behind");
    lv_obj_del(first);
    settings.hide(0, true); settings.hide(-1, true);
    require(settings.badge(), "Old settings deletion cleared new state");
    lv_obj_del(second);
    require(!settings.badge(), "Settings retained a deleted badge");
  }
}
}

void runHistoryRegression();
void runFileOperationsRegression();
void runAppInventoryRegression();
void runStoreInstallRegression();
void runStoreDataRegression();
void runTerminalRegression(void (*pump)(unsigned));
void runFileScreenRegression(void (*pump)(unsigned));
void runStoreScreenRegression(void (*pump)(unsigned));
void runKeyboardBindingRegression();
void runChatTimelineRegression();
void runMessageMenuRegression(void (*pump)(unsigned));
void runMessageInfoRegression(void (*pump)(unsigned));
void runFocusTargetsRegression();
void runFocusNavigationRegression();
void runSpatialNavigationRegression();
void runLanguageFileRegression();
void runThreadListRegression();
void runHomeScreenRegression();
void runPingReplyRegression(void (*pump)(unsigned), void (*capture)(const char*) = nullptr);
void runGuardianAppRegression(void (*capture)(const char*) = nullptr);
void runThreadMenuRegression();
void runGlyphPickerRegression();
void runQuickReplyPickerRegression();
void runMentionPickerRegression();
void runAccentPickerRegression();
void runAccentCycleRegression();
void runTextSelectionRegression();
void runTextEditMenuRegression();
void runChatComposerRegression();
void runSightlineRegression();
void runSystemInfoRegression();
void runFirmwarePanelRegression();
void runClockSettingsRegression(void (*pump)(unsigned));
void runGpsSettingsRegression();
void runBatterySettingsRegression();
void runSoundSettingsRegression(void (*pump)(unsigned));
void runKeyboardSettingsRegression(void (*pump)(unsigned));
void runDisplaySettingsRegression(void (*pump)(unsigned));
void runAppearanceRegression(void (*pump)(unsigned));
void runGeneralSettingsRegression(void (*pump)(unsigned));
void runBackupScreenRegression(void (*pump)(unsigned));
void runBackupOperationsRegression();
void runAdminSessionRegression(void (*pump)(unsigned));
void runFocusContextRegression(void (*pump)(unsigned));
void runWifiFormsRegression(void (*pump)(unsigned));
void runBluetoothSettingsRegression(void (*pump)(unsigned));
void runLockScreenRegression(void (*pump)(unsigned));
void runSetupWizardRegression(void (*pump)(unsigned));
void runBlockedUsersRegression(void (*pump)(unsigned));
void runContactActionsRegression(void (*pump)(unsigned));
void runBatteryHistoryRegression(void (*pump)(unsigned));
void runMapRegression();
void runMapScreenRegression(void (*pump)(unsigned));
void runUiLifetimeRegression(void (*pump)(unsigned)) {
  runHistoryRegression();
  puts("Stage: file operations"); fflush(stdout);
  runFileOperationsRegression();
  puts("Stage: inventory"); fflush(stdout);
  runAppInventoryRegression();
  puts("Stage: install"); fflush(stdout);
  runStoreInstallRegression();
  puts("Stage: store data"); fflush(stdout);
  runStoreDataRegression();
  puts("Stage: language"); fflush(stdout);
  runLanguageFileRegression();
  puts("Stage: threads"); fflush(stdout);
  runThreadListRegression();
  runHomeScreenRegression();
  runGuardianAppRegression();
  runPingReplyRegression(pump);
  puts("Stage: map"); fflush(stdout);
  runMapRegression();
  puts("Stage: radio"); fflush(stdout);
  radioAdapterRegression();
  puts("Stage: lifetime"); fflush(stdout);
  const auto roots_before = lv_obj_get_child_cnt(lv_layer_top());
  screenLifetimeRegression();
  puts("Stage: confirmations"); fflush(stdout);
  require(lv_obj_get_child_cnt(lv_layer_top()) == roots_before, "Screen controllers leaked roots");
  ui::screens::ConfirmDialog dialog({[]{ return lv_coord_t(24); }, closeAsync, nullptr});
  for (int i = 0; i < 12; ++i) {
    dialog.show("Old operation", "OLD", []{ ++stale_accepted; }, true);
    auto* old = lv_obj_get_child(lv_layer_top(), -1);
    auto* old_button = lv_obj_get_parent(label(old, "OLD"));
    dialog.show("New operation", "NEW", []{ ++accepted; }, true);
    auto* current = lv_obj_get_child(lv_layer_top(), -1);
    // Old tree is still alive until the deferred delete tick. It must not be
    // able to fire the replacement operation, and its DELETE must not clear it.
    lv_event_send(old_button, LV_EVENT_CLICKED, nullptr);
    require(accepted == i && stale_accepted == 0, "Stale confirmation fired");
    pump(40);
    require(dialog.isOpen(), "Old tree deletion cleared the current dialog");
    clickLabel(current, "NEW");
    require(accepted == i + 1 && !dialog.isOpen(), "Confirmation did not close before callback");
    pump(40);
  }
  dialog.show("Cancel operation", "OK", []{ ++stale_accepted; }, false);
  auto* root = lv_obj_get_child(lv_layer_top(), -1);
  clickLabel(root, TR("Cancel"));
  pump(40);
  require(!dialog.isOpen() && stale_accepted == 0, "Cancel invoked confirmation");
  dialog.show("External deletion", "OK", []{ ++stale_accepted; }, false);
  lv_obj_del(lv_obj_get_child(lv_layer_top(), -1));
  require(!dialog.isOpen(), "External deletion left a stale dialog root");
  dialog.dismiss();
  require(lv_obj_get_child_cnt(lv_layer_top()) == roots_before, "Confirmation leaked an overlay");
  lv_obj_t* detached_button = nullptr;
  {
    ui::screens::ConfirmDialog short_lived({[]{ return lv_coord_t(24); }, closeAsync, nullptr});
    short_lived.show("Controller destruction", "OK", []{ ++stale_accepted; }, false);
    detached_button = lv_obj_get_parent(label(lv_obj_get_child(lv_layer_top(), -1), "OK"));
  }
  lv_event_send(detached_button, LV_EVENT_CLICKED, nullptr);
  pump(40);
  require(stale_accepted == 0 && lv_obj_get_child_cnt(lv_layer_top()) == roots_before,
          "Deferred tree retained a destroyed dialog controller");

  ui::screens::QuickRepliesScreen replies({[]{}, [](lv_obj_t*){}, [](const char*, int){ ++alerts; }});
  char previous[TOUCH_QUICK_REPLY_MAXLEN]{};
  touchPrefsGetQuickReply(0, previous, sizeof previous);
  auto* first = lv_obj_create(lv_layer_top());
  replies.build(first);
  auto* first_save = lv_obj_get_parent(label(first, TR("Save quick replies")));
  lv_textarea_set_text(field(first), "stale reply");
  auto* second = lv_obj_create(lv_layer_top());
  replies.build(second);
  lv_textarea_set_text(field(second), "Modular UI reply");
  lv_event_send(first_save, LV_EVENT_CLICKED, nullptr);
  require(alerts == 0, "Old quick-reply screen saved the new fields");
  lv_obj_del(first);
  clickLabel(second, TR("Save quick replies"));
  char saved[TOUCH_QUICK_REPLY_MAXLEN]{};
  touchPrefsGetQuickReply(0, saved, sizeof saved);
  require(alerts == 1 && !std::strcmp(saved, "Modular UI reply"), "Quick replies were not saved through shared preferences");
  lv_obj_del(second);
  touchPrefsSetQuickReply(0, previous);
  require(lv_obj_get_child_cnt(lv_layer_top()) == roots_before, "Quick-reply screen leaked a tree");
  runTerminalRegression(pump);
  runMapScreenRegression(pump);
  runFileScreenRegression(pump);
  runStoreScreenRegression(pump);
  runKeyboardBindingRegression();
  runChatTimelineRegression();
  runMessageMenuRegression(pump);
  runMessageInfoRegression(pump);
  runFocusTargetsRegression();
  runFocusNavigationRegression();
  runSpatialNavigationRegression();
  runThreadMenuRegression();
  runGlyphPickerRegression();
  runQuickReplyPickerRegression();
  runMentionPickerRegression();
  runAccentPickerRegression();
  runAccentCycleRegression();
  runTextSelectionRegression();
  runTextEditMenuRegression();
  runChatComposerRegression();
  runSightlineRegression();
  runSystemInfoRegression();
  runFirmwarePanelRegression();
  runClockSettingsRegression(pump);
  runGpsSettingsRegression();
  runBatterySettingsRegression();
  runSoundSettingsRegression(pump);
  runKeyboardSettingsRegression(pump);
  runDisplaySettingsRegression(pump);
  runAppearanceRegression(pump);
  runGeneralSettingsRegression(pump);
  runBackupScreenRegression(pump);
  runBackupOperationsRegression();
  runContactActionsRegression(pump);
  runAdminSessionRegression(pump);
  runFocusContextRegression(pump);
  runWifiFormsRegression(pump);
  runBluetoothSettingsRegression(pump);
  runLockScreenRegression(pump);
  runSetupWizardRegression(pump);
  runBlockedUsersRegression(pump);
  runBatteryHistoryRegression(pump);
}
