// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/GuardianEmergency.h"
#include "UITask.h"
#include "services/GuardianLink.h"
#include "services/GuardianRpcLink.h"
#include <Arduino.h>
#include "i18n.h"
#include <cstring>
#include <stdexcept>
namespace {
using View = ui::screens::GuardianEmergency;
using Client = guardian::v3::Client;
std::string slots[2];
bool failSave = false, awake = false, locked = true;
unsigned wakes = 0, keeps = 0, beeps = 0;
const char *firstId = "11111111-1111-4111-8111-111111111111";
const char *secondId = "22222222-2222-4222-8222-222222222222";
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
bool readSlot(unsigned slot, std::string &raw) { raw = slots[slot]; return !raw.empty(); }
bool writeSlot(unsigned slot, const std::string &raw) { if (failSave) return false; slots[slot] = raw; return true; }
guardian::v3::StorageHooks hooks() { return {readSlot, writeSlot}; }
View::Host host() { return {[] { ++wakes; awake = true; }, [] { ++keeps; }, [] { ++beeps; }}; }
lv_obj_t *find(lv_obj_t *root, const char *text) {
  if (!root) return nullptr;
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return root;
  for (unsigned i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *value = find(lv_obj_get_child(root, i), text)) return value;
  return nullptr;
}
void markRead(View &view) {
  auto *caption = find(view.root(), TR("Mark as read")); check(caption, "Emergency read action missing");
  lv_event_send(lv_obj_get_parent(caption), LV_EVENT_CLICKED, nullptr);
}
void seed(const std::string &note, unsigned code = 1, bool second = true) {
  slots[0].clear(); slots[1].clear(); failSave = false;
  JsonDocument doc;
  doc["version"] = 1; doc["stream"] = "33333333-3333-4333-8333-333333333333";
  doc["cursor"] = 2; doc["event_ids"].to<JsonArray>(); doc["seen_ids"].to<JsonArray>();
  auto alerts = doc["alerts"].to<JsonArray>();
  for (unsigned i = 0; i < (second ? 2u : 1u); ++i) {
    auto alert = alerts.add<JsonObject>();
    alert["alert_id"] = i ? secondId : firstId; alert["code"] = i ? 2u : code;
    alert["note"] = i ? "Medical help / Lékařská pomoc" : note;
    alert["source"] = i ? "OK2MED" : "OK7PS / Praha";
    alert["radio"] = i ? 2u : 1u; alert["created"] = 1700000000.0 + i;
    alert["forwarded"] = false; alert["seen"] = false;
    alert["origin"]["network"] = "Guardian";
  }
  guardian::v3::DurableStore store; store.configure(hooks());
  check(store.save(guardian::jsonText(doc)), "Cannot seed emergency durable fixture");
}
}
void runGuardianEmergencyRegression(void (*capture)(const char *)) {
  const auto systemRoots = lv_obj_get_child_cnt(lv_layer_sys());
  auto *other = lv_obj_create(lv_layer_sys()); lv_obj_set_size(other, 4, 4);
  auto *lock = lv_obj_create(lv_layer_top());
  auto *lockText = lv_label_create(lock); lv_label_set_text(lockText, TR("Screen locked"));
  wakes = keeps = beeps = 0; awake = false; locked = true;
  const std::string note = i18nGetLang() == LANG_CS ?
    "Potřebujeme pomoc u přehrady. Dva lidé jsou na břehu, jeden potřebuje ošetření.\n\n"
    "Přístup od severní cesty. Vezměte lékárničku a kontaktujte záchrannou skupinu.\n\n"
    "Tento celý text zůstává viditelný i po odpojení Guardianu. Pomoc / Помощь.\n\n"
    "KONEC — označte jako přečtené až po přečtení." :
    "Help needed at the reservoir. Two people are ashore; one needs medical attention.\n\n"
    "Use the northern path. Bring a first aid kit and contact the rescue team.\n\n"
    "The complete message remains available after Guardian disconnects. Help / Помощь.\n\n"
    "END — mark as read after reading the message.";
  seed(note);
  {
    Client model; model.configure(hooks()); View view(model); view.configure(host());
    view.tick(0);
    check(view.visible() && lv_obj_get_parent(view.root()) == lv_layer_sys(), "Emergency did not cover system layer");
    check(awake && locked && wakes == 1 && beeps == 1 && keeps == 1, "Emergency wake, alarm or lock policy wrong");
    auto *fullNote = find(view.root(), note.c_str());
    check(fullNote && lv_label_get_long_mode(fullNote) == LV_LABEL_LONG_WRAP, "Emergency text clipped or ellipsized");
    lv_obj_update_layout(view.root());
    check(lv_obj_get_scroll_bottom(lv_obj_get_parent(fullNote)) > 0, "Long emergency note is not scrollable");
    if (capture) capture("guardian-emergency.png");
    const auto saved = slots[0] + slots[1];
    view.tick(999); view.tick(1000); view.tick(2999); view.tick(3000);
    check(beeps == 2 && wakes == 3 && keeps == 5, "Emergency reminder cadence wrong");
    check(model.emergency() && model.emergency()->alert_id == firstId && !model.emergency()->seen &&
          slots[0] + slots[1] == saved && model.cursor() == 2, "Opening or ticking implicitly marked emergency read");
    lv_event_send(view.root(), LV_EVENT_CLICKED, nullptr);
    check(view.visible(), "Background touch dismissed emergency");
    failSave = true; markRead(view);
    check(view.visible() && find(view.root(), TR("Cannot save read state. Alarm continues.")) &&
          model.emergency() && model.emergency()->alert_id == firstId, "Failed durable read dismissed emergency");
    view.tick(6000); check(beeps == 3, "Alarm stopped after failed save");
    if (capture) capture("guardian-emergency-save-error.png");
    lv_obj_del(view.root());
    check(!view.visible(), "Emergency external deletion left stale ObjectRef");
    view.tick(6001);
    check(view.visible() && model.emergency()->alert_id == firstId, "Unseen emergency did not reopen after navigation");
    failSave = false; markRead(view);
    check(view.visible() && model.emergency() && model.emergency()->alert_id == secondId,
          "Emergencies did not preserve oldest-first read order");
    Client restored; restored.configure(hooks());
    check(restored.emergency() && restored.emergency()->alert_id == secondId,
          "Local emergency read was not durable before dismissal");
    if (capture) capture("guardian-emergency-next.png");
    markRead(view); check(!view.visible() && !model.emergency(), "Last read emergency did not close its overlay");
    restored.configure(hooks()); check(!restored.emergency(), "Read emergency returned after client reload");
    check(lv_obj_is_valid(other) && lv_obj_is_valid(lock) && locked, "Emergency cleanup removed another overlay or manual lock");
  }
  // Ordinary alerts stay in history and never wake or beep.
  seed("Ordinary status", 4, false);
  {
    Client model; model.configure(hooks()); View view(model); view.configure(host());
    const auto previous = beeps; view.tick(10000);
    check(!view.visible() && beeps == previous, "Ordinary alert triggered emergency alarm");
  }
  // Reminder deadlines use elapsed unsigned time across millis() wrap.
  seed("Evacuate / Evakuace", 3, false);
  {
    Client model; model.configure(hooks()); View view(model); view.configure(host());
    const auto previous = beeps; view.tick(UINT32_MAX - 1000); view.tick(1998);
    check(beeps == previous + 1, "Emergency beep fired early across wrap");
    view.tick(1999); check(beeps == previous + 2, "Emergency beep missed wrap deadline");
  }
  lv_obj_del(other); lv_obj_del(lock);
  check(lv_obj_get_child_cnt(lv_layer_sys()) == systemRoots, "Emergency regression leaked a system root");
  puts("Guardian emergency: durable unread, oldest-first, full UTF-8 scroll, wake/alarm cadence, save failure, overlay lifetime and lock preservation PASS");
}
void runGuardianEmergencyInputRegression(UITask& task,void (*click)(int,int),void (*pump)(unsigned)) {
  seed("Emergency on a manually locked T-Deck",1,false);
  guardian::disconnected(); std::string abandoned,error; while(guardian::rpcTake(abandoned,error)) {}
  auto& model=guardian::v3::client(); model=Client{}; model.configure(hooks());
  task.lockScreen(); ui::screens::guardianEmergencyTick(millis()); pump(50);
  check(task.isManualLock() && ui::screens::guardianEmergencyVisible(),"Emergency changed manual lock");
  auto* caption=find(lv_layer_sys(),TR("Mark as read")); check(caption,"Integrated emergency read missing");
  lv_area_t area; lv_obj_get_coords(caption,&area); click((area.x1+area.x2)/2,(area.y1+area.y2)/2);
  check(!model.emergency() && !ui::screens::guardianEmergencyVisible() && task.isManualLock(),"Locked emergency touch failed or unlocked underlying UI");
  task.unlockScreen();
  model=Client{}; model.configure(guardian::v3::deviceStorage());
  puts("Guardian emergency input: actual locked UITask pointer read, durable dismissal and preserved lock PASS");
}
