// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppStoreScreen.h"
#include "../models/AppVisibility.h"
#include "../services/AppStoreData.h"
#include "../services/AppStoreJobs.h"
#include "../services/AppInventory.h"
#include "../device_caps.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
namespace ui { namespace screens { namespace store {
using namespace ui::theme;
using namespace ui::app_visibility;
using namespace ui::widgets;
static Host host{};
static uint32_t inventoryRevision = 0;
static char installedIds[AppInventory::Capacity][sizeof(InstalledApp::id)]{};
static int installedCount = 0;
// ---- the Store page ----
static lv_obj_t* s_luastore_root = nullptr;
static lv_obj_t* s_luastore_list = nullptr;
static lv_timer_t* s_luastore_poll = nullptr;
static lv_coord_t s_luastore_w = 240;   // list width, set at open — pre-layout get_width() reads 0
static bool s_luastore_busy = false;      // an install/remove is in flight
static int  s_luastore_tab = 0;           // 0 = Apps, 1 = Built-in, 2 = Languages (kept across opens)
static lv_obj_t* s_luastore_tabbtn[3] = { nullptr, nullptr, nullptr };

static void luaStoreRebuildList();        // fwd
static void langRebootWithNotice(const char* msg);   // fwd (Languages tab actions)

static void publishNavigation() {
  Navigation nav{s_luastore_list,{s_luastore_tabbtn[0],s_luastore_tabbtn[1],s_luastore_tabbtn[2]},
                 s_luastore_tab>=0 && s_luastore_tab<3 ? s_luastore_tabbtn[s_luastore_tab] : nullptr};
  host.navigation(nav);
}
static void retire() {
  if (s_luastore_poll) { lv_timer_del(s_luastore_poll); s_luastore_poll=nullptr; }
  s_luastore_list=nullptr;
  for (auto& tab:s_luastore_tabbtn) tab=nullptr;
  publishNavigation();
  host.end(close);
}
void close() {
  if (!s_luastore_root) return;
  lv_obj_t* old=s_luastore_root; s_luastore_root=nullptr;
  retire(); host.popupClose(&old); host.drawerChanged();
}
static bool accepts(lv_event_t* event) {
  for (lv_obj_t* object=lv_event_get_target(event); object; object=lv_obj_get_parent(object))
    if (object==s_luastore_root) return true;
  return false;
}
static void deleted(lv_event_t* event) {
  if (lv_event_get_target(event)!=s_luastore_root) return;
  s_luastore_root=nullptr; retire();
}

// Empty requirement = runs anywhere. Unknown requirement = allow: a newer
// catalog must not disable apps on older firmware that has never heard of the
// name, which would be the wrong way round.
static bool luaStoreBoardMeets(const char* req) {
  if (!req || !req[0]) return true;
  if (!strcmp(req, "sdk_ext")) return CAP_LUA_SDK_EXT != 0;
  if (!strcmp(req, "sd"))      return CAP_SD != 0;
  return true;
}

// Turn an action button into a "working on it" state. A download over a weak link
// takes tens of seconds and the only feedback used to be a 1.2 s toast, so the tap
// read as if nothing had happened and people pressed again.
//
// The caption is HIDDEN rather than deleted: this runs inside the button's own
// click event, and deleting objects mid-dispatch is exactly how the popup
// use-after-frees happened. Adding the spinner as a new child is safe.
static void luaStoreMarkBtnBusy(lv_obj_t* b) {
  if (!b) return;
  const uint32_t n = lv_obj_get_child_cnt(b);
  for (uint32_t k = 0; k < n; k++) lv_obj_add_flag(lv_obj_get_child(b, k), LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_color(b, lv_color_hex(themeRole(0x39404C, colors().COLOR_SECONDARY_ACTION)), LV_PART_MAIN);
#if LV_USE_SPINNER
  lv_obj_t* sp = lv_spinner_create(b, 800, 60);
  const lv_coord_t d = lv_font_get_line_height(&font12()) + 2;
  lv_obj_set_size(sp, d, d);
  lv_obj_center(sp);
  lv_obj_clear_flag(sp, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(sp, 2, LV_PART_MAIN);
  lv_obj_set_style_arc_width(sp, 2, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(sp, lv_color_hex(0x555E6B), LV_PART_MAIN);
  lv_obj_set_style_arc_color(sp, lv_color_hex(colors().COLOR_ACCENT), LV_PART_INDICATOR);
#else
  lv_obj_t* dots = lv_label_create(b);      // no spinner widget: a static tell still beats nothing
  lv_label_set_text(dots, "\xE2\x80\xA6");
  lv_obj_set_style_text_font(dots, &font12(), LV_PART_MAIN);
  lv_obj_center(dots);
#endif
}

static void luaStoreInstallBtnCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_luastore_busy) return;
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (idx < 0 || idx >= host.data->appCount()) return;
  if (!host.jobs->requestDownload(false,host.data->apps()[idx].id,host.data->apps()[idx].ver)) return;
  s_luastore_busy = true;
  if (!host.startWorker()) host.jobs->failQueuedDownloads("Not enough memory");
  luaStoreMarkBtnBusy(lv_event_get_target(e));
  host.alert(TR("Installing\xE2\x80\xA6"), 1200);
}

static void luaStoreRemoveBtnCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_luastore_busy) return;
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (index < 0 || index >= installedCount) return;
  char id[sizeof(InstalledApp::id)];
  snprintf(id,sizeof id,"%s",installedIds[index]);
  host.removeApp(id);
  luaStoreRebuildList();
}

void poll() {
  if (!s_luastore_root) return;
  ui::AppStoreJobs::CatalogResult catalog{};
  if (host.jobs->takeCatalog(ui::AppStoreJobs::Apps,catalog)) {
    host.data->parseApps(catalog.ok ? catalog.json : nullptr);
    luaStoreRebuildList();
  }
  host.inventory->poll();
  if (inventoryRevision != host.inventory->revision()) {
    luaStoreRebuildList();       // worker finished the card re-listing
  }
  if (host.jobs->takeCatalog(ui::AppStoreJobs::Languages,catalog)) {
    host.data->parseLanguages(catalog.ok ? catalog.json : nullptr);
    luaStoreRebuildList();
  }
  ui::AppStoreJobs::DownloadResult download{};
  if (host.jobs->takeDownload(true,download)) {
    s_luastore_busy = host.jobs->downloadsActive();
    // "Download failed" alone sent a reporter to GitHub with nothing to go on, because
    // every failure path in the worker is silent (#274). Name the cause when we know it.
    auto langDlFailText = [&download]() -> const char* {
      static char msg[72];
      const char* why = download.error;
      if (!why[0]) return TR("Download failed");
      snprintf(msg, sizeof msg, "%s\n%s", TR("Download failed"), TR(why));
      return msg;
    };
    if (download.ok && download.request.reboot) {   // updated the active language
      langRebootWithNotice(TR("Language updated - restarting to apply it\xE2\x80\xA6"));
      return;
    }
    if (download.ok) host.data->scanLanguages();
    luaStoreRebuildList();
    if (!download.request.silent) host.alert(
        download.ok ? TR("Downloaded - tap Use to switch") : langDlFailText(),
        download.ok ? 1800 : 3200);
  }
  if (host.jobs->takeDownload(false,download)) {
    s_luastore_busy = host.jobs->downloadsActive();
    // Reflect the install from what we just downloaded rather than trusting a
    // re-listing of the directory we only now wrote (FAT dir entries can lag a
    // write on some cards, which left the button reading "Get" until reopen).
    if (download.ok) {
      const char* name=download.request.id;
      for (int c=0;c<host.data->appCount();++c)
        if (!strcmp(host.data->apps()[c].id,download.request.id)) { name=host.data->apps()[c].name; break; }
      host.inventory->installed(download.request.id,name,download.request.version);
    }
    luaStoreRebuildList();
    {
      char message[160];
      snprintf(message,sizeof message,"%s\n%s",TR("Install failed (network?)"),TR(download.error));
      host.alert(download.ok ? TR("Installed") : message,download.ok ? 1400 : 3200);
    }
  }
}

static void luaStoreHideSwitchCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  uint32_t bit = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  uint32_t mask = touchPrefsGetAppHide();
  // switch ON = app visible -> bit cleared
  if (lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)) mask &= ~bit;
  else                                                            mask |= bit;
  touchPrefsSetAppHide(mask);
  host.hiddenChanged();     // MQTT lives in Settings, not the drawer — apply now
}

static void langRebootWithNotice(const char* msg) {
  s_luastore_busy = true;                   // swallow further taps on the way out
  host.rebootWithNotice(msg);
}
// Switching language reboots (same as the Settings picker) so the whole UI —
// fonts, keyboard layout, every built label — re-renders consistently.
static void luaStoreLangBuiltinCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  uint8_t l = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  if (l >= LANG_COUNT) return;
  touchPrefsSetLangFile("");
  touchPrefsSetUiLang(l);
  i18nSetLang(l);
  host.languageKeyboard(l);
  langRebootWithNotice(TR("Restarting to apply the language\xE2\x80\xA6"));
}

static void luaStoreLangUseCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= host.data->installedCount()) return;
  // The file's "# base:" names the built-in column missing keys fall back to
  // (a customized nl file degrades to built-in Dutch, not to English).
  uint8_t base_lang = LANG_EN;
  char name[28], base[12];
  if (host.data->languageMetadata(host.data->installedLanguages()[i].code, name, sizeof name, base, sizeof base) && base[0]) {
    for (uint8_t l = 0; l < LANG_COUNT; l++)
      if (strcmp(base, kUiLangCodes[l]) == 0) { base_lang = l; break; }
  }
  touchPrefsSetUiLang(base_lang);
  touchPrefsSetLangFile(host.data->installedLanguages()[i].code);
  host.languageKeyboard(base_lang);
  langRebootWithNotice(TR("Restarting to apply the language\xE2\x80\xA6"));
}

static void luaStoreLangGetCb(lv_event_t* e) {
  if (!accepts(e)) return;   // Get AND Update (same download)
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_luastore_busy) return;
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= host.data->languageCount()) return;
  char cur[12]; touchPrefsGetLangFile(cur,sizeof cur);
  if (!host.jobs->requestDownload(true,host.data->languages()[i].code,host.data->languages()[i].ver,
                                    cur[0] && !strcmp(cur,host.data->languages()[i].code),false)) return;
  s_luastore_busy = true;
  if (!host.startWorker()) host.jobs->failQueuedDownloads("Not enough memory");
  luaStoreMarkBtnBusy(lv_event_get_target(e));
  host.alert(TR("Downloading\xE2\x80\xA6"), 1200);
}

static void luaStoreLangRemoveCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_luastore_busy) return;
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= host.data->installedCount()) return;
  if (!host.data->removeLanguage(host.data->installedLanguages()[i].code)) return;
  char cur[12];
  touchPrefsGetLangFile(cur, sizeof cur);
  if (strcmp(cur, host.data->installedLanguages()[i].code) == 0) touchPrefsSetLangFile("");   // deleted the active one
  host.data->scanLanguages();
  luaStoreRebuildList();
}

static void luaStoreStyleTabs() {

  for (int t = 0; t < 3; t++) {
    lv_obj_t* b = s_luastore_tabbtn[t];
    if (!b || !lv_obj_is_valid(b)) continue;
    const bool on = (t == s_luastore_tab);

    lv_obj_set_style_bg_color(b, lv_color_hex(on ? colors().COLOR_ACCENT : colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t* l = lv_obj_get_child(b, 0);
    if (l) lv_obj_set_style_text_color(l,
      lv_color_hex(on ? themeRole(0x0E1216, colors().COLOR_ON_ACCENT) : colors().COLOR_SUB), LV_PART_MAIN);
  }
}

static void luaStoreTabCb(lv_event_t* e) {
  if (!accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int t = (int)(intptr_t)lv_event_get_user_data(e);
  if (t == s_luastore_tab) return;
  s_luastore_tab = t;
  if (t == 2) {
    host.data->scanLanguages();                          // tiny dir; sync is fine here
    if (host.data->languageCount() <= 0) {                       // no lang catalog yet -> fetch
      host.data->setCatalogState(ui::AppStoreJobs::Languages, 0);
      if (!host.jobs->requestCatalog(ui::AppStoreJobs::Languages) && !host.jobs->catalogActive(ui::AppStoreJobs::Languages)) host.data->setCatalogState(ui::AppStoreJobs::Languages, -1);
      if (!host.startWorker()) {
        host.jobs->cancelCatalog(ui::AppStoreJobs::Languages);
        host.data->setCatalogState(ui::AppStoreJobs::Languages, -1);
      }
    }
  }
  luaStoreRebuildList();
}

static void luaStoreRebuildList() {
  if (!s_luastore_list) return;
  inventoryRevision = host.inventory->revision();
  installedCount = host.inventory->count();
  for (int i=0; i<installedCount; ++i)
    snprintf(installedIds[i],sizeof installedIds[i],"%s",host.inventory->rows()[i].id);
  // Keep the reader where they were. This runs from ASYNC completions -- the
  // catalog fetch, the card scan, a language catalog -- which land a second or
  // two after the Store opens, i.e. exactly while someone is scrolling down it.
  // Rebuilding threw them back to the top every time, which also hid whatever
  // they had scrolled to (an app near the bottom could look like it had no
  // button at all). Restored after the rows exist, so the value is clamped
  // against the NEW content height rather than the old one.
  const lv_coord_t keep_scroll_y = lv_obj_get_scroll_y(s_luastore_list);
  host.beforeRebuild();
  lv_obj_clean(s_luastore_list);
  const uint32_t hide = touchPrefsGetAppHide();
  const lv_coord_t W = s_luastore_w;
  const lv_coord_t lh14 = lv_font_get_line_height(&font14());
  const lv_coord_t lh12 = lv_font_get_line_height(&font12());
  // The store was tuned on the 320 px T-Deck; the V4 / M9 panels are 240 px
  // portrait, where a fixed 100 px button gutter eats half the text column.
  const bool narrow = (W < 260);
  const lv_coord_t act_w = narrow ? 62 : 78;      // Get / Update / Remove button
  const lv_coord_t act_gut = act_w + (narrow ? 12 : 22);   // text column stops here

  // Height a wrapped string actually needs in a column `w` wide. The cards used
  // to hardcode "title + two description lines", which only held for the 320 px
  // T-Deck the store was designed on: on the P4's 568 px portrait panel, and for
  // any language whose translation runs longer than the English, the description
  // wraps to three or four lines and the fixed card height cut it off. Measure
  // the text and let the card follow it. lv_txt_get_size is a pure calculation,
  // so this is safe before layout (reading lv_obj_get_height here returns 0).
  auto wrapH = [](const char* txt, const lv_font_t* f, lv_coord_t w) -> lv_coord_t {
    if (!txt || !txt[0] || w <= 0) return 0;
    lv_point_t sz;
    lv_txt_get_size(&sz, txt, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    return sz.y;
  };

  auto section = [&](const char* txt) {
    lv_obj_t* l = lv_label_create(s_luastore_list);
    char up[40];
    snprintf(up, sizeof up, "%s", txt);
    for (char* q = up; *q; ++q) if (*q >= 'a' && *q <= 'z') *q -= 32;   // small-caps header
    lv_label_set_text(l, up);
    lv_obj_set_style_text_font(l, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(l, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_top(l, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_left(l, 2, LV_PART_MAIN);
  };

  luaStoreStyleTabs();
  publishNavigation();

  if (s_luastore_tab == 1) {
    // ==== tab: Built-in — show/hide the stock apps ====
    section(TR("Show in the app drawer"));
    struct { const char* name; uint32_t bit; } builtins[] = {
      { "Spectrum", APPHIDE_SPECTRUM }, { "Discover", APPHIDE_DISCOVER },
#if !defined(HAS_TANMATSU)
#endif
      { "Signal", APPHIDE_SIGNAL }, { "Mentions", APPHIDE_MENTIONS },
#if defined(HAS_TOUCH_UI)
      { "Terminal", APPHIDE_TERMINAL },
#endif
#if CAP_FILESYSTEM || defined(TLORA_PAGER)
      { "Files", APPHIDE_FILES },
#endif
#if WADA_WEB_FILE_TRANSFER
  { "Transfer", APPHIDE_FILE_TRANSFER },
#endif
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
      { "MQTT bridge", APPHIDE_MQTT },   // a Settings section, not a drawer tile
#endif
    };
    for (auto& bi : builtins) {
      lv_obj_t* row = lv_obj_create(s_luastore_list);
      lv_obj_remove_style_all(row);
      lv_obj_set_size(row, W - 8, 34);
      lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t* nm = lv_label_create(row);
      lv_label_set_text(nm, bi.name);
      lv_obj_set_style_text_font(nm, &font14(), LV_PART_MAIN);
      lv_obj_set_style_text_color(nm, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
      lv_obj_align(nm, LV_ALIGN_LEFT_MID, 2, 0);
      lv_obj_t* sw = lv_switch_create(row);
      lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
      if (!(hide & bi.bit)) lv_obj_add_state(sw, LV_STATE_CHECKED);
      lv_obj_add_event_cb(sw, luaStoreHideSwitchCb, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)bi.bit);
    }
    return;
  }

  if (s_luastore_tab == 2) {
    // ==== tab: Languages — built-in picks + downloadable/custom .lang files ====
    char curFile[12];
    touchPrefsGetLangFile(curFile, sizeof curFile);
    const uint8_t curLang = i18nGetLang();

    auto langRow = [&](const char* name, const char* sub) {
      lv_obj_t* row = lv_obj_create(s_luastore_list);
      lv_obj_remove_style_all(row);
      lv_obj_set_size(row, W - 8, 38);
      lv_obj_set_style_bg_color(row, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_radius(row, 10, LV_PART_MAIN);
      lv_obj_set_style_border_color(row, lv_color_hex(themeRole(0x232830, colors().COLOR_BORDER)), LV_PART_MAIN);
      lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
      lv_obj_set_style_pad_hor(row, 8, LV_PART_MAIN);
      lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t* nm = lv_label_create(row);
      lv_label_set_text(nm, name);
      lv_obj_set_style_text_font(nm, &font14(), LV_PART_MAIN);
      lv_obj_set_style_text_color(nm, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
      lv_obj_set_width(nm, W - (narrow ? 108 : 150));   // room for [trash][Use]
      lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
      lv_obj_set_height(nm, lh14);   // one line: LONG_DOT only ellipsizes when the
                                     // height is bounded, else it silently wraps
      lv_obj_align(nm, LV_ALIGN_LEFT_MID, 0, sub ? -8 : 0);
      if (sub) {
        lv_obj_t* sb = lv_label_create(row);
        lv_label_set_text(sb, sub);
        lv_obj_set_style_text_font(sb, &font12(), LV_PART_MAIN);
        lv_obj_set_style_text_color(sb, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
        lv_obj_align(sb, LV_ALIGN_LEFT_MID, 0, 8);
      }
      return row;
    };
    auto actionBtn = [&](lv_obj_t* row, const char* txt, uint32_t col,
                         lv_event_cb_t cb, intptr_t ud, int xoff, int bw) {
      lv_obj_t* b = lv_btn_create(row);
      lv_obj_set_size(b, bw, 28);
      lv_obj_align(b, LV_ALIGN_RIGHT_MID, xoff, 0);
      lv_obj_set_style_radius(b, 8, LV_PART_MAIN);
      const bool danger = col == 0x8A4444;
      const bool info = col == 0x4F9DF7;
      lv_obj_set_style_bg_color(b,
          lv_color_hex(danger ? themeRole(col, colors().COLOR_STATUS_DANGER)
                              : info ? colors().COLOR_STATUS_INFO : col), LV_PART_MAIN);
      lv_obj_t* bl = lv_label_create(b);
      lv_label_set_text(bl, txt);
      lv_obj_set_style_text_font(bl, &font12(), LV_PART_MAIN);
      if (isDay()) {
        lv_obj_set_style_text_color(bl,
            lv_color_hex(danger ? colors().COLOR_ON_STATUS_DANGER
                                : info ? colors().COLOR_ON_STATUS_INFO : colors().COLOR_ON_ACCENT), LV_PART_MAIN);
      }
      lv_obj_center(bl);
      lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)ud);
      return b;
    };
    auto activeTag = [&](lv_obj_t* row, int xoff) {
      lv_obj_t* a = lv_label_create(row);
      lv_label_set_text(a, TR("Active"));
      lv_obj_set_style_text_font(a, &font12(), LV_PART_MAIN);
      lv_obj_set_style_text_color(a, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
      lv_obj_align(a, LV_ALIGN_RIGHT_MID, xoff, 0);
    };

    lv_obj_t* hint = lv_label_create(s_luastore_list);
#if CAP_BUILTIN_LANGS
    lv_label_set_text(hint, TR("Tap Use to switch - the device reboots to apply."));
#else
    lv_label_set_text(hint, TR("Tap Use to switch - the device reboots to apply. Language files live in /lang on the storage; edit them or add your own."));
#endif
    lv_obj_set_style_text_font(hint, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_width(hint, W - 12);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);

#if CAP_BUILTIN_LANGS
    // All translations ship in the image on this board: a picker, no downloads.
    for (uint8_t l = 0; l < LANG_COUNT; ++l) {
      lv_obj_t* row = langRow(kUiLangNames[l], nullptr);
      if (!curFile[0] && l == curLang) activeTag(row, -6);
      else actionBtn(row, TR("Use"), 0x2A5A8A, luaStoreLangBuiltinCb, (intptr_t)l, 0,
                     narrow ? 52 : 58);
    }
    return;
#endif
    section(TR("Installed"));
    {
      // English is always here (it's the source keys). Every other language is
      // a downloaded .lang file — the built-in table only backs the files up.
      lv_obj_t* row = langRow("English", nullptr);
      if (!curFile[0] && curLang == LANG_EN) activeTag(row, -6);
      else actionBtn(row, TR("Use"), 0x2A5A8A, luaStoreLangBuiltinCb, (intptr_t)LANG_EN, 0, narrow ? 52 : 58);
    }
    {
      // Guardian Czech is built in on every board, including offline T-Decks.
      lv_obj_t* row = langRow(kUiLangNames[LANG_CS], nullptr);
      if (!curFile[0] && curLang == LANG_CS) activeTag(row, -6);
      else actionBtn(row, TR("Use"), 0x2A5A8A, luaStoreLangBuiltinCb, (intptr_t)LANG_CS, 0, narrow ? 52 : 58);
    }
    for (int i = 0; i < host.data->installedCount(); i++) {
      // A newer catalog ver for this installed file -> offer Update in place.
      int cat = -1;
      for (int c = 0; c < host.data->languageCount(); c++)
        if (strcmp(host.data->languages()[c].code, host.data->installedLanguages()[i].code) == 0) { cat = c; break; }
      // #247: != treated ANY difference as "update available" — including an
      // installed file NEWER than the catalog (a translator side-loading their
      // work-in-progress), and clicking Update then DOWNGRADED it. Offer the
      // update only when the catalog is numerically newer; non-numeric vers
      // (atof 0 on both) fall back to the old inequality.
      bool stale = false;
      if (cat >= 0) {
        const double cv = atof(host.data->languages()[cat].ver), iv = atof(host.data->installedLanguages()[i].ver);
        stale = (cv > 0.0 || iv > 0.0) ? (cv > iv + 1e-9)
                                       : strcmp(host.data->languages()[cat].ver, host.data->installedLanguages()[i].ver) != 0;
      }
      char sub[24];
      snprintf(sub, sizeof sub, "%s.lang  v%s", host.data->installedLanguages()[i].code, host.data->installedLanguages()[i].ver);
      lv_obj_t* row = langRow(host.data->installedLanguages()[i].name, sub);
      const bool act = curFile[0] && strcmp(curFile, host.data->installedLanguages()[i].code) == 0;
      const lv_coord_t tw = narrow ? 30 : 34;          // trash
      const lv_coord_t uw = narrow ? 52 : 58;          // Use
      const lv_coord_t upw = narrow ? 58 : 66;         // Update
      const bool dl = host.jobs->downloading(true,host.data->installedLanguages()[i].code);   // this one is downloading
      actionBtn(row, LV_SYMBOL_TRASH, 0x8A4444, luaStoreLangRemoveCb, (intptr_t)i, 0, tw);
      if (stale) {
        lv_obj_t* ub = actionBtn(row, TR("Update"), 0x4F9DF7, luaStoreLangGetCb, (intptr_t)cat, -(tw + 4), upw);
        if (dl) luaStoreMarkBtnBusy(ub);      // survive a rebuild landing mid-download
      }
      else if (act)  activeTag(row, -(tw + 8));
      else           actionBtn(row, TR("Use"), 0x2A5A8A, luaStoreLangUseCb, (intptr_t)i, -(tw + 4), uw);
    }
    section(TR("Available"));
    for (int i = 0; i < host.data->languageCount(); i++) {
      bool have = false;
      for (int j = 0; j < host.data->installedCount(); j++)
        if (strcmp(host.data->installedLanguages()[j].code, host.data->languages()[i].code) == 0) { have = true; break; }
      if (have) continue;
      lv_obj_t* row = langRow(host.data->languages()[i].name, host.data->languages()[i].code);
      lv_obj_t* gb = actionBtn(row, TR("Get"), colors().COLOR_ACCENT, luaStoreLangGetCb, (intptr_t)i, 0, narrow ? 52 : 58);
      if (host.jobs->downloading(true,host.data->languages()[i].code))
        luaStoreMarkBtnBusy(gb);              // survive a rebuild landing mid-download
    }
    if (host.data->languageCount() <= 0) {
      lv_obj_t* h = lv_label_create(s_luastore_list);
      lv_label_set_text(h, host.data->languageCount() < 0
          ? TR("Language catalog unavailable - check Wi-Fi, then reopen this tab.")
          : TR("Loading the language catalog\xE2\x80\xA6"));
      lv_obj_set_style_text_font(h, &font12(), LV_PART_MAIN);
      lv_obj_set_style_text_color(h, lightSurfaceTextColor(host.data->languageCount() < 0 ? 0xE08080 : colors().COLOR_SUB), LV_PART_MAIN);
      lv_obj_set_width(h, W - 12);
      lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    }
    return;
  }

  // ==== tab: Apps (the catalog) ====
  if (host.data->appCount() <= 0) {
    lv_obj_t* card = lv_obj_create(s_luastore_list);
    lv_obj_remove_style_all(card);
    // Height from the wrapped hint, not a one-line guess — on a 240 px panel
    // the second/third line used to be cut off by the card's own bottom edge.
    lv_obj_set_size(card, W - 8, lh14 + lh12 * (narrow ? 3 : 2) + 22);
    lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* t = lv_label_create(card);
    lv_label_set_text(t, host.data->appCount() < 0 ? TR("Catalog unavailable") : TR("Loading catalog\xE2\x80\xA6"));
    lv_obj_set_style_text_font(t, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(t, lightSurfaceTextColor(host.data->appCount() < 0 ? 0xE08080 : colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(t, 0, 0);
    lv_obj_t* h = lv_label_create(card);
    useChainedFont(h);
    const auto memory=host.workerMemory();
    if (host.data->appCount() < 0 && !memory.running) {
      // The network worker could not start: report the numbers that decide it
      // (it needs one contiguous 8 KB internal block) right on screen, so this
      // is diagnosable from the device instead of a serial cable.
      char mem[128];
      snprintf(mem, sizeof mem,
               "%s\nfree %u KB, largest block %u KB (needs 8 KB)",
               TR("Not enough free memory to fetch it. Reboot and try again."),
               memory.freeKb,
               memory.largestKb);
      lv_label_set_text(h, mem);
    } else {
      lv_label_set_text(h, host.data->appCount() < 0
          ? TR("Turn Wi-Fi on, then tap refresh. Apps already installed still work.")
          : TR("Fetching the app list from firmware.wadamesh.com"));
    }
    lv_obj_set_style_text_font(h, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(h, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_width(h, W - 26);
    lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(h, 0, lh14 + 3);
    // Size to whatever the (possibly translated, possibly multi-line diagnostic)
    // body actually needs — read the text back so this can't drift from the
    // branches above.
    lv_obj_set_height(card, lh14 + 3 + wrapH(lv_label_get_text(h), &font12(), W - 26) + 16);
  }
  const int  chip_h  = lh14 + lh12 - 2;                  // icon chip (square)
  const lv_coord_t pad_v = 14;                           // pad_all 7, top + bottom
  for (int i = 0; i < host.data->appCount(); i++) {
    // Per card, because every description wraps differently. Content is the
    // title line plus the wrapped description, but never shorter than the icon
    // chip or the action button or those would poke out of a short card.
    char title[64];
    snprintf(title, sizeof title, "%s  %s", host.data->apps()[i].name, host.data->apps()[i].ver);
    // Measure the TITLE too, don't assume one line. A long name plus its version
    // wraps in the text column, and the description used to sit at a fixed
    // lh14 + 2 regardless — so the second title line landed on top of it.
    const lv_coord_t dsw   = W - (chip_h + 8) - act_gut;
    const lv_coord_t ttlH  = wrapH(title, &font14(), dsw);
    const lv_coord_t textH = ttlH + 2 + wrapH(host.data->apps()[i].desc, &font12(), dsw);
    lv_coord_t contentH = textH;
    if (contentH < chip_h) contentH = chip_h;
    if (contentH < 32)     contentH = 32;                // the Get/Update/Remove button
    lv_obj_t* row = lv_obj_create(s_luastore_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, W - 8, contentH + pad_v);
    lv_obj_set_style_bg_color(row, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 10, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(themeRole(0x232830, colors().COLOR_BORDER)), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 7, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    const ui::InstalledApp* inst = host.inventory->find(host.data->apps()[i].id);
    // Squircle icon chip with the app's initial, matching the drawer's look.
    const int chip = chip_h;
    lv_obj_t* ico = lv_obj_create(row);
    lv_obj_remove_style_all(ico);
    lv_obj_clear_flag(ico, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ico, chip, chip);
    // Top-aligned, not centred: once a card can be four lines tall a centred chip
    // drifts away from the title it belongs to.
    lv_obj_align(ico, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(ico, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ico, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_color(ico, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_border_width(ico, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(ico, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(ico, chip * 24 / 100, LV_PART_MAIN);
    lv_obj_t* il = lv_label_create(ico);
    char initial[2] = { host.data->apps()[i].name[0], 0 };
    lv_label_set_text(il, initial);
    lv_obj_set_style_text_font(il, &font16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(il, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_center(il);
    const int tx = chip + 8;                        // text column starts past the chip
    lv_obj_t* nm = lv_label_create(row);
    lv_label_set_text(nm, title);
    lv_obj_set_style_text_font(nm, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(nm, lv_color_hex(inst ? colors().COLOR_ACCENT : colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_width(nm, W - tx - act_gut);         // clear of the action button
    lv_label_set_long_mode(nm, LV_LABEL_LONG_WRAP);   // WRAP, not DOT: the card grew to fit it
    lv_obj_set_pos(nm, tx, 0);
    // Description gets its own full-width line — it used to share the row with
    // the version badge and was squeezed into an unreadable sliver.
    lv_obj_t* ds = lv_label_create(row);
    lv_label_set_text(ds, host.data->apps()[i].desc);
    lv_obj_set_style_text_font(ds, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ds, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_width(ds, W - tx - act_gut);   // same column as the title: never under the button
    lv_label_set_long_mode(ds, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(ds, tx, ttlH + 2);         // below the title's ACTUAL height, however it wrapped
    lv_obj_t* b = lv_btn_create(row);
    lv_obj_set_size(b, act_w, 32);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(b, 8, LV_PART_MAIN);
    lv_obj_t* bl = lv_label_create(b);
    const bool cur = inst && strcmp(inst->ver, host.data->apps()[i].ver) == 0;
    if (!inst) lv_obj_set_style_bg_color(b, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
    else if (!cur) lv_obj_set_style_bg_color(b, lv_color_hex(colors().COLOR_STATUS_INFO), LV_PART_MAIN);
    const bool runnable = luaStoreBoardMeets(host.data->apps()[i].requires_cap);
    lv_label_set_text(bl, !runnable ? TR("N/A") : !inst ? TR("Get") : cur ? TR("Remove") : TR("Update"));
    lv_obj_set_style_text_font(bl, &font12(), LV_PART_MAIN);
    lv_obj_center(bl);
    if (!runnable && !inst) {
      // Greyed and inert, with the reason on the row rather than a toast after
      // a pointless download.
      lv_obj_set_style_bg_color(b, lv_color_hex(themeRole(0x39404C, colors().COLOR_SECONDARY_ACTION)), LV_PART_MAIN);
      lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_text_color(ds, lightSurfaceTextColor(0xD7574E), LV_PART_MAIN);
      lv_label_set_text(ds, TR("This board cannot run this app."));
    } else if (inst && cur) {
      // Remove needs the INSTALLED index
      int ii = (int)(inst - host.inventory->rows());
      lv_obj_set_style_bg_color(b, lv_color_hex(themeRole(0x8A4444, colors().COLOR_STATUS_DANGER)), LV_PART_MAIN);
      lv_obj_add_event_cb(b, luaStoreRemoveBtnCb, LV_EVENT_CLICKED, (void*)(intptr_t)ii);
    } else {
      lv_obj_add_event_cb(b, luaStoreInstallBtnCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    if (isDay()) {
      lv_obj_set_style_text_color(bl,
          lv_color_hex(!runnable && !inst ? colors().COLOR_TEXT
                       : inst && cur ? colors().COLOR_ON_STATUS_DANGER
                       : inst ? colors().COLOR_ON_STATUS_INFO : colors().COLOR_ON_ACCENT), LV_PART_MAIN);
    }
    // A rebuild can land mid-download (a card scan or the catalog fetch finishing)
    // and would wipe the spinner set on tap, so repaint it from the in-flight id.
    if (host.jobs->downloading(false,host.data->apps()[i].id))
      luaStoreMarkBtnBusy(b);
  }

  // ---- installed-but-not-in-catalog (sideloaded from the card) ----
  {
    bool any_side = false;
    for (int i = 0; i < host.inventory->count(); i++) {
      bool in_cat = false;
      for (int c = 0; c < host.data->appCount(); c++)
        if (strcmp(host.data->apps()[c].id, host.inventory->rows()[i].id) == 0) { in_cat = true; break; }
      if (!in_cat) { any_side = true; break; }
    }
    if (any_side) section(TR("Your own apps"));
  }
  for (int i = 0; i < host.inventory->count(); i++) {
    bool in_cat = false;
    for (int c = 0; c < host.data->appCount(); c++)
      if (strcmp(host.data->apps()[c].id, host.inventory->rows()[i].id) == 0) { in_cat = true; break; }
    if (in_cat) continue;
    lv_obj_t* row = lv_obj_create(s_luastore_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, W - 8, 40);
    lv_obj_set_style_bg_color(row, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 10, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(themeRole(0x232830, colors().COLOR_BORDER)), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 7, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* nm = lv_label_create(row);
    char head[64];
    snprintf(head, sizeof head, "%s  %s", host.inventory->rows()[i].name, host.inventory->rows()[i].ver);
    lv_label_set_text(nm, head);
    lv_obj_set_style_text_font(nm, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(nm, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_width(nm, W - act_gut);    // clear of the Remove button
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_set_height(nm, lh12);          // one line (see the Languages rows above)
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* b = lv_btn_create(row);
    lv_obj_set_size(b, act_w, 28);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(themeRole(0x8A4444, colors().COLOR_STATUS_DANGER)), LV_PART_MAIN);
    lv_obj_t* bl = lv_label_create(b);
    lv_label_set_text(bl, TR("Remove"));
    lv_obj_set_style_text_font(bl, &font12(), LV_PART_MAIN);
    if (isDay())
      lv_obj_set_style_text_color(bl, lv_color_hex(colors().COLOR_ON_STATUS_DANGER), LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(b, luaStoreRemoveBtnCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }

  if (keep_scroll_y > 0) {
    lv_obj_update_layout(s_luastore_list);          // content height must be known first
    lv_obj_scroll_to_y(s_luastore_list, keep_scroll_y, LV_ANIM_OFF);
  }
}

void open() {
  host.data->ready();
  close();
  s_luastore_busy = host.jobs->downloadsActive();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr), sh = lv_disp_get_ver_res(nullptr);
  // lv_layer_top, below the status bar, exactly like the Monitor/Discover pages:
  // created AFTER the app drawer (also on layer_top), so it stacks above it and
  // closing reveals the drawer again.
  s_luastore_root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(s_luastore_root,deleted,LV_EVENT_DELETE,nullptr);
  lv_obj_remove_style_all(s_luastore_root);
  lv_obj_set_size(s_luastore_root, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_luastore_root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_luastore_root, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_luastore_root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_luastore_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(s_luastore_root);

  // Segmented tab bar (fixed above the scrolling list): Apps | Built-in | Languages
  // Follows the font instead of being a hardcoded 30 px: on the P4's high-DPI panel
  // (and at the larger UI scales) a 12 px line box plus padding is taller than 30,
  // so the tab label was clipped inside its own button. Floored at 30 so the S3
  // boards keep exactly the bar they had.
  lv_coord_t bar_h = lv_font_get_line_height(&font12()) + 14;
  if (bar_h < 30) bar_h = 30;
  {
    lv_obj_t* bar = lv_obj_create(s_luastore_root);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, sw - 12, bar_h);
    lv_obj_set_pos(bar, 6, 4);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    static const char* const kStoreTabs[3] = { "Apps", "Built-in", "Languages" };
#if CAP_BUILTIN_LUA_APPS
    // This board ships its apps and languages inside the image, so there is
    // nothing to browse or install — the page is a picker, not a shop.
    const int t_first = 1, t_count = 2;
#else
    const int t_first = 0, t_count = 3;
#endif
    // Cells sized to their text, NOT even thirds. The P4 is 284 px logical and its
    // fonts are scaled up, so an even third is ~90 px — "Languages" cannot fit that
    // at the board's smallest font (uiFitLabelWidth bottoms out at font12(), which
    // is montserrat_16/18/20 there), while "Apps" leaves half its cell empty. Hand
    // out the bar in proportion to what each label actually measures and the long
    // one gets the room the short one was wasting. Self-balancing for translations.
    const lv_coord_t total = sw - 12;
    lv_coord_t need[3] = { 0, 0, 0 };
    int32_t need_sum = 0;
    for (int t = t_first; t < 3; t++) {
      lv_point_t sz;
      lv_txt_get_size(&sz, TR(kStoreTabs[t]), &font12(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      need[t] = sz.x + 16;                      // text plus breathing room
      need_sum += need[t];
    }
    if (need_sum <= 0) need_sum = t_count;      // degenerate: fall back to even
    lv_coord_t x = 0;
    for (int t = t_first; t < 3; t++) {
      // Last cell takes the remainder so rounding can never leave a gap or overrun.
      const lv_coord_t cw = (t == 2) ? (total - x)
                                     : (lv_coord_t)(((int32_t)total * need[t]) / need_sum);
      lv_obj_t* b = lv_btn_create(bar);
      s_luastore_tabbtn[t] = b;

      lv_obj_remove_style_all(b);
      lv_obj_set_size(b, cw - 4, bar_h - 4);
      lv_obj_set_pos(b, x + 2, 0);
      lv_obj_set_style_radius(b, 8, LV_PART_MAIN);
      lv_obj_t* l = lv_label_create(b);
      lv_label_set_text(l, TR(kStoreTabs[t]));
      lv_obj_set_style_text_font(l, &font12(), LV_PART_MAIN);
      // Shrink the font rather than setting a width: a width plus LONG_DOT does NOT
      // ellipsize here (DOT only kicks in when the HEIGHT is bounded), it silently
      // wraps the label onto a second line inside a one-line button.
      uiFitLabelWidth(l, cw - 8);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      lv_obj_center(l);
      lv_obj_add_event_cb(b, luaStoreTabCb, LV_EVENT_CLICKED, (void*)(intptr_t)t);
      x += cw;
    }
  }

  s_luastore_list = lv_obj_create(s_luastore_root);

  lv_obj_remove_style_all(s_luastore_list);
  s_luastore_w = sw - 12;
  lv_obj_set_size(s_luastore_list, s_luastore_w, sh - host.statusHeight() - bar_h - 14);
  lv_obj_set_pos(s_luastore_list, 6, 4 + bar_h + 4);
  lv_obj_set_scroll_dir(s_luastore_list, LV_DIR_VER);
  lv_obj_set_flex_flow(s_luastore_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_luastore_list, 6, LV_PART_MAIN);

#if CAP_BUILTIN_LUA_APPS
  if (s_luastore_tab == 0) s_luastore_tab = 2;   // no Apps tab on this board
#endif
  host.begin(TR("Store"), &close);   // one-line bar; the tab bar is the page header

  // Open instantly from the cached install list; the card re-listing (sideload
  // pickup) runs on the worker and the poll timer repaints when it lands.
  host.inventory->request();
  if (host.data->appCount() <= 0) {                 // no catalog yet this session -> fetch
    host.data->setCatalogState(ui::AppStoreJobs::Apps, 0);
    if (!host.jobs->requestCatalog(ui::AppStoreJobs::Apps) && !host.jobs->catalogActive(ui::AppStoreJobs::Apps)) host.data->setCatalogState(ui::AppStoreJobs::Apps, -1);
  }
  const bool worker_up = host.startWorker();
  // The net worker needs an 8 KB contiguous internal block. When memory is too
  // tight to start it, nothing will ever service the request — so fail visibly
  // instead of spinning on "Loading catalog..." forever.
  if (!worker_up) {
    host.inventory->cancelQueued();
    host.data->setCatalogState(ui::AppStoreJobs::Apps, -1);
    host.jobs->cancelCatalog(ui::AppStoreJobs::Apps);
    host.data->setCatalogState(ui::AppStoreJobs::Languages, -1);
    host.jobs->cancelCatalog(ui::AppStoreJobs::Languages);
  }
  if (s_luastore_tab == 2) {              // reopened straight onto Languages
    host.data->scanLanguages();
    if (worker_up && host.data->languageCount() <= 0) {
      host.data->setCatalogState(ui::AppStoreJobs::Languages, 0);
      if (!host.jobs->requestCatalog(ui::AppStoreJobs::Languages) && !host.jobs->catalogActive(ui::AppStoreJobs::Languages)) host.data->setCatalogState(ui::AppStoreJobs::Languages, -1);
    }
  }
  luaStoreRebuildList();
  s_luastore_poll = lv_timer_create([](lv_timer_t*){poll();}, 250, nullptr);
}

// Settings -> Language lands here: the Store, opened straight onto Languages.
void openLanguages() {
  s_luastore_tab = 2;
  open();
}

void configure(const Host& value) { close(); host=value; }
lv_obj_t* root() { return s_luastore_root; }

} } }
