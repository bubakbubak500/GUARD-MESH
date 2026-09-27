// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileManagerScreen.h"
#include "../platform/UiPlatform.h"
#include "../UITask.h"
#include "../device_caps.h"
#include <FS.h>
#include <cstring>
#include <strings.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "../services/FileOperations.h"
#include "../services/AudioService.h"
#include "../services/ImageCodec.h"
#include <LvglPsramAlloc.h>
#include "../../helpers/esp32/TouchPrefsStore.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../i18n.h"
namespace ui { namespace screens { namespace files {
using namespace ui::theme;
using namespace ui::widgets;
using namespace ui::images;
static Host host{};
static lv_obj_t* page=nullptr;
static struct { fs::FS* filesystem; char path[200]; bool directory; } pendingDelete{};
static bool accepts(lv_event_t*);
static void watch(lv_obj_t*);
static void fmMarkSdIo();
static bool copyPath(fs::FS* source,const char* from,fs::FS* dest,const char* to,bool dir) {
  if (!source || !dest) return false;
  if (host.isSd(source) || host.isSd(dest)) host.markSdIo();
  return host.operations->copy(*source,from,*dest,to,dir);
}
// ===== File manager =========================================================
static lv_obj_t* s_fm_list      = nullptr;  // scrollable list of entries
static lv_obj_t* s_fm_path_lbl  = nullptr;  // address-bar / current location
static lv_obj_t* s_fm_search_ta = nullptr;  // inline search field (when active)
static lv_obj_t* s_fm_sort_lbl  = nullptr;  // sort button label (shows current mode)
static fs::FS*   s_fm_fs        = nullptr;  // current filesystem (nullptr = roots screen)
static char      s_fm_path[160]  = {0};     // current dir within s_fm_fs (e.g. "/" or "/foo")
// Light the status-bar SD LED on any file-manager microSD access. The fm runs on
// a generic fs::FS*; only &SD is real microSD I/O (Internal = SPIFFS). Browsing
// (fmRefresh) and the file open/save paths call this; mutations re-list via
// fmRefresh, so they blip the LED too.
static char      s_fm_store[12]  = {0};     // storage label ("Internal" / "SD")
static char      s_fm_filter[40] = {0};     // active search filter (empty = none)
static uint8_t   s_fm_sort       = 0;       // 0 Name A-Z, 1 Z-A, 2 Size, 3 Type
struct FmEntry { char name[64]; uint32_t size; uint32_t mtime; bool isdir; };   // mtime: epoch secs, 0 = unknown (#185)
static const int FM_MAX_ENTRIES  = 192;
static FmEntry*  s_fm_entries    = nullptr; // PSRAM-allocated while the file manager is open
static int       s_fm_count      = 0;
static bool      s_fm_show_hidden = false;  // reveal MeshCore system files on flat SPIFFS (toggle in + menu)
static lv_obj_t* s_editor_root   = nullptr; // text editor overlay
static lv_obj_t* s_editor_ta     = nullptr;
static char      s_editor_path[200] = {0};
static lv_obj_t* s_fm_img_root   = nullptr; // read-only image viewer overlay
static uint8_t*  s_fm_img_buf    = nullptr; // decoded RGB565 (PSRAM); freed on close
static lv_img_dsc_t s_fm_img_dsc;           // wraps s_fm_img_buf for the lv_img widget
static lv_obj_t* s_fm_img_widget = nullptr; // the scaled lv_img inside the viewer
static lv_obj_t* s_fm_img_hdr    = nullptr; // filename/size label (windowed only)
static lv_obj_t* s_fm_img_close  = nullptr; // close (X) button (windowed only)
static lv_obj_t* s_fm_img_full   = nullptr; // full-screen toggle button (windowed only)
static lv_obj_t* s_fm_img_hint   = nullptr; // "tap to exit" hint (full-screen only)
static int       s_fm_img_w = 0, s_fm_img_h = 0; // decoded image dimensions
static bool      s_fm_img_fs = false;       // viewer is in full-screen mode
static lv_obj_t* s_fm_fmt_overlay = nullptr; // full-screen "formatting..." notice
static lv_obj_t* s_fm_actions    = nullptr;  // per-entry action sheet
static lv_obj_t* s_fm_prompt     = nullptr;  // text-input modal (rename / new folder)
static lv_obj_t* s_fm_prompt_ta  = nullptr;
static void    (*s_fm_prompt_cb)(const char*) = nullptr;
static char      s_fm_sel_name[64] = {0};    // entry the action sheet targets
static bool      s_fm_sel_isdir  = false;
static int       s_fm_paste_pending = 0;     // deferred copy/move (runs after notice paints)
// Copy/Cut clipboard.
static struct { fs::FS* fs; char path[200]; char name[64]; bool isdir; bool is_cut; bool active; }
  s_fm_clip = {};


// ---- File manager (Phase 1 + header: Back / address bar / Sort / Find) ----
void fmRefresh();
static void fmRender();

static void fmOpenEditor(const char* name);

static const char* k_fm_sort_names[] = { "A-Z", "Z-A", "Size", "Type" };

static void fmFmtSize(size_t bytes, char* out, size_t outsz) {
  if (bytes < 1024)                 snprintf(out, outsz, "%u B", (unsigned)bytes);
  else if (bytes < 1024UL * 1024)   snprintf(out, outsz, "%.1f KB", bytes / 1024.0);
  else                              snprintf(out, outsz, "%.1f MB", bytes / (1024.0 * 1024.0));
}

// Style a list button to the dark palette (matches the command picker).
static void fmStyleRow(lv_obj_t* btn, uint32_t text_color) {
  lv_obj_set_style_text_font(btn, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(btn, lv_color_hex(text_color), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn, lv_color_hex(colors().COLOR_CONTROL_PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(btn, lv_color_hex(colors().COLOR_CONTROL_PRESSED), LV_PART_MAIN);
  lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
  lv_obj_set_style_min_height(btn, 34, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(btn, 5, LV_PART_MAIN);
}

// Case-insensitive substring test for the search filter.
static bool fmContainsCI(const char* hay, const char* needle) {
  if (!needle || !needle[0]) return true;
  size_t nl = strlen(needle);
  for (const char* p = hay; *p; ++p) if (strncasecmp(p, needle, nl) == 0) return true;
  return false;
}

// qsort comparator honouring s_fm_sort. Dirs sort before files for Size/Type.
static int fmCmp(const void* a, const void* b) {
  const FmEntry* x = (const FmEntry*)a;
  const FmEntry* y = (const FmEntry*)b;
  switch (s_fm_sort) {
    case 1: return strcasecmp(y->name, x->name);                       // Name Z-A
    case 2:                                                            // Size (dirs first)
      if (x->isdir != y->isdir) return x->isdir ? -1 : 1;
      if (x->isdir) return strcasecmp(x->name, y->name);
      return (y->size > x->size) ? 1 : (y->size < x->size ? -1 : 0);
    case 3:                                                            // Type (dirs first)
      if (x->isdir != y->isdir) return x->isdir ? -1 : 1;
      return strcasecmp(x->name, y->name);
    default: return strcasecmp(x->name, y->name);                     // Name A-Z
  }
}

// Append `name` onto the current path (handles the root "/" case).
static void fmEnterDir(const char* base) {
  size_t n = strlen(s_fm_path);
  if (n == 0) { snprintf(s_fm_path, sizeof s_fm_path, "/%s", base); }
  else if (s_fm_path[n - 1] == '/') {
    snprintf(s_fm_path + n, sizeof(s_fm_path) - n, "%s", base);
  } else {
    snprintf(s_fm_path + n, sizeof(s_fm_path) - n, "/%s", base);
  }
  fmRefresh();
}

static void fmUp() {
  if (!s_fm_fs) return;                       // already at the roots screen
  if (strcmp(s_fm_path, "/") == 0) { fmShowRoots(); return; }   // root -> roots
  char* slash = strrchr(s_fm_path, '/');
  if (slash == s_fm_path) s_fm_path[1] = '\0';   // keep leading "/"
  else if (slash)         *slash = '\0';
  fmRefresh();
}

// Free the strdup'd basename stashed on a directory row when it's deleted.
static void fmRowFreeCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  void* ud = lv_obj_get_user_data(lv_event_get_target(e));
  if (ud) free(ud);
}
static void fmUpCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmUp();
}

void fmOpenStorage(fs::FS* fs, const char* store, const char* path) {
  s_fm_fs = fs;
  snprintf(s_fm_store, sizeof s_fm_store, "%s", store);
  snprintf(s_fm_path, sizeof s_fm_path, "%s", path);
  fmRefresh();
}
static void fmInternalClickCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmOpenStorage(host.internalFilesystem(), "Internal", "/");
}

// 64-bit size formatter for card capacity (cards routinely exceed 4 GB).
void fmFmtSize64(uint64_t bytes, char* out, size_t outsz) {
  if (bytes < 1024ULL * 1024)              snprintf(out, outsz, "%.0f KB", bytes / 1024.0);
  else if (bytes < 1024ULL * 1024 * 1024)  snprintf(out, outsz, "%.0f MB", bytes / (1024.0 * 1024));
  else                                     snprintf(out, outsz, "%.1f GB", bytes / (1024.0 * 1024 * 1024));
}



// Full-screen "busy" notice (copy/move/format). Pure LVGL — used by the generic paste path too.
void fmShowBusyOverlay(const char* msg) {
  if (s_fm_fmt_overlay) return;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_fm_fmt_overlay = lv_obj_create(lv_layer_top());
  watch(s_fm_fmt_overlay);
  lv_obj_remove_style_all(s_fm_fmt_overlay);
  lv_obj_set_size(s_fm_fmt_overlay, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_fm_fmt_overlay, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_fm_fmt_overlay, lv_color_hex(themeRole(0x0E1216, colors().COLOR_PANEL)), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_fm_fmt_overlay, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_fm_fmt_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = lv_label_create(s_fm_fmt_overlay);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, sw - 36);
  lv_label_set_text(l, msg);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(l, lv_color_hex(themeRole(0xFFCC66, colors().COLOR_STATUS_WARN_TEXT)), LV_PART_MAIN);
  lv_obj_set_style_text_font(l, &font14(), LV_PART_MAIN);
  lv_obj_center(l);
}
void fmHideFormatOverlay() {
  if (s_fm_fmt_overlay) { host.popupClose(&s_fm_fmt_overlay); }
}

// ---- File operations (Phase 4a: delete / rename / new folder) ----
struct FmRowData { char name[64]; bool isdir; };

// Build an absolute path for `name` inside the current directory.
static void fmFullPath(const char* name, char* out, size_t outsz) {
  if (strcmp(s_fm_path, "/") == 0) snprintf(out, outsz, "/%s", name);
  else                             snprintf(out, outsz, "%s/%s", s_fm_path, name);
}

// Recursively delete a file or directory tree. Return false immediately when
// an entry cannot be removed; rewinding onto the same undeletable FAT entry
// forever would hang factory reset with its watchdog intentionally suspended.


// ----- text-input modal (rename / new folder) -----
// A Lua app driving this prompt (wada.ui.input) must hear about EVERY way it
// closes, or it sits waiting on a callback that will never arrive. Set while an
// OK is being delivered so the close below does not also report a cancel.

static bool s_fm_prompt_ok = false;
void fmPromptClose() {
  if (!s_fm_prompt) return;
  if (s_fm_prompt) {
    host.hideKb();
    host.popupClose(&s_fm_prompt);
    s_fm_prompt_ta = nullptr;
    s_fm_prompt_cb = nullptr;
  }
#if CAP_LUA_APPS
  if (!s_fm_prompt_ok) host.cancelPrompt();
#endif
}
static void fmPromptOkCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!s_fm_prompt_ta) { fmPromptClose(); return; }
  host.kbMirrorSyncToReal();
  char buf[80];
  const char* t = lv_textarea_get_text(s_fm_prompt_ta);
  snprintf(buf, sizeof buf, "%s", t ? t : "");
  void (*cb)(const char*) = s_fm_prompt_cb;
  s_fm_prompt_ok = (cb && buf[0]);      // empty text is a cancel, not a delivery
  fmPromptClose();
  s_fm_prompt_ok = false;
  if (cb && buf[0]) cb(buf);
}
static void fmPromptCancelCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmPromptClose();
}
void fmTextPrompt(const char* title, const char* initial, void (*cb)(const char*)) {
  fmPromptClose();
  s_fm_prompt_cb = cb;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_fm_prompt = lv_obj_create(lv_layer_top());
  watch(s_fm_prompt);
  lv_obj_remove_style_all(s_fm_prompt);
  lv_obj_set_size(s_fm_prompt, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_fm_prompt, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_fm_prompt, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_fm_prompt, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_fm_prompt, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* card = lv_obj_create(s_fm_prompt);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, sw - 30, 124);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 8);
  styleSurface(card, colors().COLOR_PANEL, 10);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(themeRole(0x2A2E33, colors().COLOR_BORDER)), LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* tl = lv_label_create(card);
  lv_label_set_text(tl, title);
  lv_obj_set_style_text_color(tl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(tl, &font14(), LV_PART_MAIN);
  lv_obj_align(tl, LV_ALIGN_TOP_LEFT, 2, 0);

  s_fm_prompt_ta = lv_textarea_create(card);
  lv_obj_set_size(s_fm_prompt_ta, sw - 30 - 20, 32);
  lv_obj_align(s_fm_prompt_ta, LV_ALIGN_TOP_MID, 0, 24);
  styleCard(s_fm_prompt_ta);
  lv_textarea_set_one_line(s_fm_prompt_ta, true);
  lv_textarea_set_max_length(s_fm_prompt_ta, 63);
  lv_obj_set_style_text_font(s_fm_prompt_ta, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(s_fm_prompt_ta, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  if (initial) lv_textarea_set_text(s_fm_prompt_ta, initial);
  host.attachSettingsTaEvents(s_fm_prompt_ta);

  lv_obj_t* bc = lv_btn_create(card);
  lv_obj_set_size(bc, 80, 32);
  lv_obj_align(bc, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  styleButton(bc);
  lv_obj_set_style_bg_color(bc, lv_color_hex(colors().COLOR_SECONDARY_ACTION), LV_PART_MAIN);
  lv_obj_add_event_cb(bc, fmPromptCancelCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lc = lv_label_create(bc); lv_label_set_text(lc, TR("Cancel")); lv_obj_center(lc);
  useChainedFont(lc);

  lv_obj_t* bo = lv_btn_create(card);
  lv_obj_set_size(bo, 80, 32);
  lv_obj_align(bo, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  styleButton(bo);
  lv_obj_set_style_bg_color(bo, lv_color_hex(colors().COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_text_color(bo, lv_color_hex(colors().COLOR_ON_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(bo, fmPromptOkCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lo = lv_label_create(bo); lv_label_set_text(lo, "OK"); lv_obj_center(lo);
  useChainedFont(lo);

  if (host.keyboard()) host.kbMirrorBind(s_fm_prompt_ta);
}

// ----- operations (act on s_fm_sel_name in the current dir) -----
static void fmDoDelete() {
  if (!page || !pendingDelete.filesystem) return;
  const auto request = pendingDelete;
  pendingDelete.filesystem = nullptr;
  const bool ok = request.directory ? host.operations->remove(*request.filesystem, request.path)
                                    : request.filesystem->remove(request.path);
  if (host.task()) host.task()->showAlert(ok ? TR("Deleted") : TR("Delete failed"), ok ? 1200 : 1600);
  fmRefresh();
}
static void fmRenameApply(const char* newname) {
  if (!s_fm_fs || !s_fm_sel_name[0] || !newname[0]) return;
  char oldp[200], newp[200];
  fmFullPath(s_fm_sel_name, oldp, sizeof oldp);
  fmFullPath(newname,       newp, sizeof newp);
  bool ok = s_fm_fs->rename(oldp, newp);
  if (host.task()) host.task()->showAlert(ok ? TR("Renamed") : TR("Rename failed"), ok ? 1200 : 1600);
  fmRefresh();
}
static void fmNewFolderApply(const char* name) {
  if (!s_fm_fs || !name[0]) return;
  if (host.isFlat(s_fm_fs)) { if (host.task()) host.task()->showAlert(TR("Internal has no folders"), 1800); return; }
  char p[200];
  fmFullPath(name, p, sizeof p);
  bool ok = s_fm_fs->mkdir(p);
  if (host.task()) host.task()->showAlert(ok ? TR("Folder created") : TR("mkdir failed"), ok ? 1200 : 1600);
  fmRefresh();
}
static void fmNewFileApply(const char* name) {
  if (!s_fm_fs || !name[0]) return;
  char p[200];
  fmFullPath(name, p, sizeof p);
  if (s_fm_fs->exists(p)) { if (host.task()) host.task()->showAlert(TR("Already exists"), 1600); return; }
  File f = s_fm_fs->open(p, "w");          // create an empty file
  if (!f) { if (host.task()) host.task()->showAlert(TR("Create failed"), 1600); return; }
  f.close();
  fmRefresh();
  fmOpenEditor(name);                      // jump straight into the editor
}

// Stream a single file src -> dst (any fs to any fs). feedLoopWDT each chunk so
// a large file can't trip the watchdog mid-copy.

// Recursively copy a file or directory tree src -> dst.

// Perform the pending paste (copy or move) from s_fm_clip into the current dir.
// Returns true on success. Runs from UITask::loop with the loop WDT disabled.
bool fmDoPaste() {
  if (!s_fm_clip.active || !s_fm_fs) return false;

  // Source's parent dir — used to detect "paste into the same folder".
  char srcparent[200];
  snprintf(srcparent, sizeof srcparent, "%s", s_fm_clip.path);
  char* sl = strrchr(srcparent, '/');
  if (sl == srcparent) srcparent[1] = '\0';
  else if (sl)         *sl = '\0';
  const bool same_dir = (s_fm_clip.fs == s_fm_fs) && (strcmp(srcparent, s_fm_path) == 0);
  if (s_fm_clip.is_cut && same_dir) { s_fm_clip.active = false; return true; }   // already here

  // Pick a non-colliding destination name.
  char name[80];
  char dst[200];
  snprintf(name, sizeof name, "%s", s_fm_clip.name);
  fmFullPath(name, dst, sizeof dst);
  for (int k = 1; k < 1000 && s_fm_fs->exists(dst); ++k) {
    snprintf(name, sizeof name, "%s-%d", s_fm_clip.name, k);
    fmFullPath(name, dst, sizeof dst);
  }

  if (host.isSd(s_fm_clip.fs) || host.isSd(s_fm_fs)) host.markSdIo();
  const bool ok = s_fm_clip.is_cut
      ? host.operations->move(*s_fm_clip.fs, s_fm_clip.path, *s_fm_fs, dst, s_fm_clip.isdir)
      : copyPath(s_fm_clip.fs, s_fm_clip.path, s_fm_fs, dst, s_fm_clip.isdir);
  if (ok && s_fm_clip.is_cut) s_fm_clip.active = false; // only a successful move consumes the clipboard
  return ok;
}

// ----- action sheet -----
static lv_obj_t* fmActionBtn(lv_obj_t* parent, const char* text, lv_event_cb_t cb, uint32_t bg) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_width(b, lv_pct(100));
  lv_obj_set_height(b, 34);
  styleButton(b);
  uint32_t fill = bg;
  uint32_t fg = colors().COLOR_TEXT;
  if (isDay()) {
    if (bg == 0x5A2D2D) { fill = colors().COLOR_STATUS_DANGER; fg = colors().COLOR_ON_STATUS_DANGER; }
    else if (bg == 0x2D4A2D) { fill = colors().COLOR_STATUS_OK; fg = colors().COLOR_ON_STATUS_OK; }
    else fill = colors().COLOR_CONTROL;
  }
  lv_obj_set_style_bg_color(b, lv_color_hex(fill), LV_PART_MAIN);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  useChainedFont(l);
  lv_label_set_text(l, TR(text));
  lv_obj_set_style_text_color(l, lv_color_hex(fg), LV_PART_MAIN);
  lv_obj_center(l);
  return b;
}
void fmCloseActions() {
  if (s_fm_actions) { host.popupClose(&s_fm_actions); }
}
static void fmActRenameCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  fmTextPrompt("Rename", s_fm_sel_name, fmRenameApply);
}
static void fmActDeleteCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  if (!s_fm_fs || !s_fm_sel_name[0]) return;
  pendingDelete.filesystem = s_fm_fs;
  pendingDelete.directory = s_fm_sel_isdir;
  fmFullPath(s_fm_sel_name, pendingDelete.path, sizeof pendingDelete.path);
  char m[110];
  snprintf(m, sizeof m, "Delete %s%s?%s", s_fm_sel_isdir ? "folder " : "", s_fm_sel_name,
           s_fm_sel_isdir ? "\n(and everything inside)" : "");
  host.showConfirm(m, TR("Delete"), fmDoDelete);
}
static void fmActNewFolderCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  fmTextPrompt("New folder name", "", fmNewFolderApply);
}
static void fmActNewFileCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  fmTextPrompt("New file name", "note.txt", fmNewFileApply);
}
static void fmClipSet(bool cut) {
  s_fm_clip.fs = s_fm_fs;
  fmFullPath(s_fm_sel_name, s_fm_clip.path, sizeof s_fm_clip.path);
  snprintf(s_fm_clip.name, sizeof s_fm_clip.name, "%s", s_fm_sel_name);
  s_fm_clip.isdir  = s_fm_sel_isdir;
  s_fm_clip.is_cut = cut;
  s_fm_clip.active = true;
  if (host.task()) host.task()->showAlert(cut ? TR("Cut - Paste in a folder") : TR("Copied - Paste in a folder"), 1600);
}
static void fmActCopyCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  fmClipSet(false);
}
static void fmActCutCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  fmClipSet(true);
}
static void fmActPasteCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  if (!s_fm_clip.active || !s_fm_fs) return;
  fmShowBusyOverlay(s_fm_clip.is_cut ? "Moving...\n\nDo NOT power off or\nremove the card."
                                     : "Copying...\n\nDo NOT power off or\nremove the card.");
  s_fm_paste_pending = 2;
}
// name==nullptr -> folder-level menu only (used by the ".." row).
static void fmActToggleHiddenCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  s_fm_show_hidden = !s_fm_show_hidden;
  fmCloseActions();
  fmRefresh();
}

static void fmOpenActions(const char* name, bool isdir) {
  fmCloseActions();
  if (name) { snprintf(s_fm_sel_name, sizeof s_fm_sel_name, "%s", name); s_fm_sel_isdir = isdir; }
  else      { s_fm_sel_name[0] = '\0'; s_fm_sel_isdir = false; }

  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_fm_actions = lv_obj_create(lv_layer_top());
  watch(s_fm_actions);
  lv_obj_remove_style_all(s_fm_actions);
  lv_obj_set_size(s_fm_actions, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_fm_actions, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_fm_actions, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_fm_actions, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_fm_actions, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_fm_actions, [](lv_event_t* ev) {
    if (!accepts(ev)) return;
    if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
    lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
    fmCloseActions();
  }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* card = lv_obj_create(s_fm_actions);
  lv_obj_remove_style_all(card);
  lv_obj_set_width(card, sw - 50);
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  styleSurface(card, colors().COLOR_PANEL, 10);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(themeRole(0x2A2E33, colors().COLOR_BORDER)), LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_set_style_max_height(card, (sh - host.statusHeight()) - 16, LV_PART_MAIN);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 5, LV_PART_MAIN);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);

  lv_obj_t* tl = lv_label_create(card);
  lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(tl, lv_pct(100));
  lv_label_set_text(tl, name ? name : "Folder");
  lv_obj_set_style_text_color(tl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(tl, &font12(), LV_PART_MAIN);

  if (name) {
    fmActionBtn(card, TR("Rename"), fmActRenameCb, 0x2B3440);
    fmActionBtn(card, TR("Copy"), fmActCopyCb, 0x2B3440);
    fmActionBtn(card, TR("Cut"), fmActCutCb, 0x2B3440);
    fmActionBtn(card, TR("Delete"), fmActDeleteCb, 0x5A2D2D);
  } else {
    fmActionBtn(card, TR("New file"), fmActNewFileCb, 0x2B3440);
    fmActionBtn(card, TR("New folder"), fmActNewFolderCb, 0x2B3440);
#if defined(ESP32)
    if (host.isFlat(s_fm_fs))   // Internal only: toggle MeshCore's hidden system files
      fmActionBtn(card, s_fm_show_hidden ? "Hide system files" : "Show system files",
                  fmActToggleHiddenCb, 0x2B3440);
#endif
  }
  if (s_fm_clip.active) {
    fmActionBtn(card, s_fm_clip.is_cut ? TR("Paste (move)") : TR("Paste (copy)"), fmActPasteCb, 0x2D4A2D);
  }
}

// ----- text editor (Phase 5) -----
static const size_t FM_EDIT_MAX = 8192;   // size cap for on-device editing
void fmEditorClose() {
  if (!s_editor_root) return;
  if (host.keyboard()) lv_keyboard_set_textarea(host.keyboard(), nullptr);
  host.popupClose(&s_editor_root);
  s_editor_ta = nullptr;
}
static void fmEditorCancelCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmEditorClose();
}
static void fmEditorSaveCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!s_editor_ta || !s_fm_fs) { fmEditorClose(); return; }
  const char* txt = lv_textarea_get_text(s_editor_ta);
  size_t len = txt ? strlen(txt) : 0;
  fmMarkSdIo();                              // SD file save -> activity LED
  File f = s_fm_fs->open(s_editor_path, "w");
  bool ok = false;
  if (f) { ok = (f.write((const uint8_t*)txt, len) == len); f.close(); }
  if (host.task()) host.task()->showAlert(ok ? TR("Saved") : TR("Save failed"), ok ? 1300 : 1800);
  fmEditorClose();
  fmRefresh();
}
static void fmOpenEditor(const char* name) {
  if (!s_fm_fs || !name || !name[0]) return;
  char path[200];
  fmFullPath(name, path, sizeof path);
  File f = s_fm_fs->open(path, "r");
  if (!f) { if (host.task()) host.task()->showAlert(TR("Cannot open file"), 1500); return; }
  size_t sz = f.size();
  if (sz > FM_EDIT_MAX) { f.close(); if (host.task()) host.task()->showAlert(TR("Too large to edit (>8 KB)"), 2200); return; }
  char* buf = (char*)ui::platform::allocate(sz + 1, true);
  if (!buf) buf = (char*)malloc(sz + 1);
  if (!buf) { f.close(); if (host.task()) host.task()->showAlert(TR("Out of memory"), 1500); return; }
  size_t rd = f.readBytes(buf, sz);
  buf[rd] = '\0';
  f.close();
  if (rd != sz) { free(buf); if (host.task()) host.task()->showAlert(TR("Cannot open file"),1500); return; }
  fmEditorClose();
  snprintf(s_editor_path, sizeof s_editor_path, "%s", path);

  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_editor_root = lv_obj_create(lv_layer_top());
  watch(s_editor_root);
  lv_obj_remove_style_all(s_editor_root);
  lv_obj_set_size(s_editor_root, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_editor_root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_editor_root, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_editor_root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_editor_root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* fn = lv_label_create(s_editor_root);
  lv_label_set_long_mode(fn, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(fn, 6, 9);
  lv_obj_set_width(fn, sw - 6 - 112);
  lv_label_set_text(fn, name);
  lv_obj_set_style_text_font(fn, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(fn, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);

  lv_obj_t* save = lv_btn_create(s_editor_root);
  lv_obj_set_size(save, 58, 28);
  lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -50, 4);
  styleButton(save);
  lv_obj_set_style_bg_color(save, lv_color_hex(colors().COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_text_color(save, lv_color_hex(colors().COLOR_ON_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(save, fmEditorSaveCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* sl = lv_label_create(save); lv_label_set_text(sl, TR("Save"));
  lv_obj_set_style_text_font(sl, &font12(), LV_PART_MAIN); lv_obj_center(sl);

  lv_obj_t* cancel = lv_btn_create(s_editor_root);
  lv_obj_set_size(cancel, 44, 28);
  lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -3, 4);
  styleButton(cancel);
  lv_obj_set_style_bg_color(cancel, lv_color_hex(colors().COLOR_SECONDARY_ACTION), LV_PART_MAIN);
  lv_obj_add_event_cb(cancel, fmEditorCancelCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cl = lv_label_create(cancel); lv_label_set_text(cl, LV_SYMBOL_CLOSE); tanCloseRed(cl);
  lv_obj_set_style_text_font(cl, &font12(), LV_PART_MAIN); lv_obj_center(cl);

  s_editor_ta = lv_textarea_create(s_editor_root);
  lv_obj_set_size(s_editor_ta, sw - 8, (sh - host.statusHeight()) - 38 - 4);
  lv_obj_set_pos(s_editor_ta, 4, 36);
  lv_textarea_set_one_line(s_editor_ta, false);
  lv_textarea_set_max_length(s_editor_ta, FM_EDIT_MAX);
  styleCard(s_editor_ta);
  lv_obj_set_style_text_font(s_editor_ta, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(s_editor_ta, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_textarea_set_text(s_editor_ta, buf);
  free(buf);

  // Bind the physical keyboard directly (no one-line mirror); the on-screen
  // keyboard stays hidden on the T-Deck, handleHwKey routes keys to this ta.
  if (host.keyboard()) {
    host.bindEditor(s_editor_ta);
  }
}

// ---- Read-only image viewer (PNG / JPEG; decoders enabled in lv_conf.h) -----


static bool fmIsImage(const char* name) {
  if (!name) return false;
  const char* dot = strrchr(name, '.');
  if (!dot) return false;
  return !strcasecmp(dot, ".png")  || !strcasecmp(dot, ".jpg") ||
         !strcasecmp(dot, ".jpeg") || !strcasecmp(dot, ".sjpg") ||
         !strcasecmp(dot, ".bmp");
}
#if CAP_SD || defined(TLORA_PAGER)
static bool fmIsAudio(const char* name) {
  if (!name) return false;
  const char* dot = strrchr(name, '.');
  return dot && !strcasecmp(dot, ".wav");
}
#endif

void fmImageClose() {
  // The popup closes asynchronously. Disconnect its image before freeing pixels
  // so a redraw of the old tree cannot use the released or replacement buffer.
  if (s_fm_img_widget) lv_img_set_src(s_fm_img_widget,nullptr);
  if (s_fm_img_root) { host.popupClose(&s_fm_img_root); }  // also deletes the children below
  lv_img_cache_invalidate_src(&s_fm_img_dsc);   // drop the cache entry before the buffer it points at is freed
  if (s_fm_img_buf)  { lvglPsramFree(s_fm_img_buf); s_fm_img_buf = nullptr; }   // decoded RGB565 (lvglPsramAlloc)
  s_fm_img_widget = s_fm_img_hdr = s_fm_img_close = s_fm_img_full = s_fm_img_hint = nullptr;
  s_fm_img_fs = false;
  if (host.statusbar()) lv_obj_clear_flag(host.statusbar(), LV_OBJ_FLAG_HIDDEN);  // ensure it's back
}
static void fmImageCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);   // swallow trailing click
  fmImageClose();
}

static void fmShowObj(lv_obj_t* o, bool show) {
  if (!o) return;
  if (show) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// "Set as lock wallpaper" action shown in the windowed image viewer, plus the
// open image's path / filesystem so the action can persist it. The button is
// CAP_LOCK_SCREEN-gated: on the one filesystem board WITHOUT a lock screen
// (V4-R8) nothing ever renders the wallpaper, so offering to set one — with a
// "Lock wallpaper set" toast — was a dead control.
#if CAP_LOCK_SCREEN
static lv_obj_t* s_fm_img_wall = nullptr;
#endif
static char      s_fm_img_path[208] = {0};
static bool      s_fm_img_on_sd     = false;

// Position the image + chrome for the current mode. Windowed: slim header with
// the close/full buttons, image scaled to fit below it (never upscaled). Full
// screen: chrome hidden, image fitted to the whole display, a "tap to exit"
// hint at the bottom.
static void fmImageRelayout() {
  if (!s_fm_img_root || !s_fm_img_widget) return;
  const int w = s_fm_img_w, h = s_fm_img_h;
  if (w <= 0 || h <= 0) return;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  const bool fs = s_fm_img_fs;

  fmShowObj(s_fm_img_hdr,   !fs);
  fmShowObj(s_fm_img_close, !fs);
  fmShowObj(s_fm_img_full,  !fs);
#if CAP_LOCK_SCREEN
  fmShowObj(s_fm_img_wall,  !fs);
#endif
  fmShowObj(s_fm_img_hint,   fs);
  // The status bar lives on lv_layer_sys (above this overlay), so hide it
  // outright for a true full-screen image; restore it otherwise.
  fmShowObj(host.statusbar(), !fs);

  if (fs) {
    lv_obj_set_pos(s_fm_img_root, 0, 0);
    lv_obj_set_size(s_fm_img_root, sw, sh);
    // Fit the full display, preserving aspect (letterbox); allow upscaling.
    uint32_t zx = (uint32_t)sw * 256u / (uint32_t)w;
    uint32_t zy = (uint32_t)sh * 256u / (uint32_t)h;
    uint32_t zoom = (zx < zy) ? zx : zy;
    if (zoom < 1)    zoom = 1;
    if (zoom > 1024) zoom = 1024;
    lv_img_set_zoom(s_fm_img_widget, (uint16_t)zoom);
    lv_obj_align(s_fm_img_widget, LV_ALIGN_CENTER, 0, 0);
  } else {
    lv_obj_set_pos(s_fm_img_root, 0, host.statusHeight());
    lv_obj_set_size(s_fm_img_root, sw, sh - host.statusHeight());
    const int hdr_h = 30;
    uint32_t zx = (uint32_t)sw * 256u / (uint32_t)w;
    uint32_t zy = (uint32_t)((sh - host.statusHeight()) - hdr_h) * 256u / (uint32_t)h;
    uint32_t zoom = (zx < zy) ? zx : zy;
    if (zoom > 256) zoom = 256;   // never upscale in windowed mode
    if (zoom < 1)   zoom = 1;
    lv_img_set_zoom(s_fm_img_widget, (uint16_t)zoom);
    lv_obj_align(s_fm_img_widget, LV_ALIGN_CENTER, 0, hdr_h / 2);
  }
}
static void fmImageFullCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  s_fm_img_fs = true;
  fmImageRelayout();
}
static void fmImageRootClickCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!s_fm_img_fs) return;          // windowed: background taps do nothing
  s_fm_img_fs = false;               // full screen: any tap returns to windowed
  fmImageRelayout();
}

// Persist the currently-viewed JPEG as the lock-screen wallpaper. By the time the
// viewer is up the file is a confirmed JPEG (PNG is rejected earlier), so it's a
// valid wallpaper. SD paths get the "sd:" prefix the decoder expects.
#if CAP_LOCK_SCREEN
static void fmSetWallpaperCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !s_fm_img_path[0]) return;
  char pref[TOUCH_LOCK_WALLPAPER_MAXLEN];
  if (s_fm_img_on_sd) snprintf(pref, sizeof pref, "sd:%s", s_fm_img_path);
  else                snprintf(pref, sizeof pref, "%s", s_fm_img_path);
  touchPrefsSetLockWallpaper(pref);
  host.wallpaperChanged(pref);
  fmImageClose();
  if (host.task()) host.task()->showAlert(TR("Lock wallpaper set"), 1300);
}
#endif  // CAP_LOCK_SCREEN (fmSetWallpaperCb)

#if CAP_SOUND_FILES
// ---- .wav -> notification-sound chooser (opened from the File Manager) ------
static char      s_fm_snd_path[208] = {0};
static bool      s_fm_snd_on_sd     = false;
static int       s_fm_snd_slot      = TOUCH_SND_MSG; // captured with the displayed file
static lv_obj_t* s_fm_snd_root      = nullptr;
void fmSndClose() { if (s_fm_snd_root) { host.popupClose(&s_fm_snd_root); } }
static void fmSndCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return; if (lv_event_get_code(e) == LV_EVENT_CLICKED) fmSndClose(); }
static void fmSndBuildPref(char* pref, int cap) {
  if (s_fm_snd_on_sd) snprintf(pref, cap, "sd:%s", s_fm_snd_path);
  else                snprintf(pref, cap, "%s", s_fm_snd_path);
}
static void fmSndPlayCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !s_fm_snd_path[0]) return;
  char pref[TOUCH_SOUND_PATH_MAXLEN]; fmSndBuildPref(pref, sizeof pref);
  if (!ui::audio::supportsWav(pref)) { if (host.task()) host.task()->showAlert(TR("Unsupported WAV (need 16-bit PCM)"), 2200); return; }
  ui::audio::previewWav(pref);
}
static void fmSndSetCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !s_fm_snd_path[0]) return;
  char pref[TOUCH_SOUND_PATH_MAXLEN]; fmSndBuildPref(pref, sizeof pref);
  if (!ui::audio::supportsWav(pref)) { if (host.task()) host.task()->showAlert(TR("Unsupported WAV (need 16-bit PCM)"), 2200); return; }
  if (!touchPrefsSetSoundFile(s_fm_snd_slot, pref)) {
    if (host.task()) host.task()->showAlert(TR("Save failed"), 1800);
    return;
  }
  fmSndClose();
  host.closeFullscreenView();             // tear down the File Manager so closing Sound won't reveal it
  host.openSoundSettings();   // back to Settings -> Sound, fresh (shows the new file)
  if (host.task()) host.task()->showAlert(TR("Notification sound set"), 1300);
}
static void fmOpenAudio(const char* name) {
  if (!s_fm_fs || !name || !name[0]) return;
  fmMarkSdIo();
  char path[208]; fmFullPath(name, path, sizeof path);
  strncpy(s_fm_snd_path, path, sizeof s_fm_snd_path - 1);
  s_fm_snd_path[sizeof s_fm_snd_path - 1] = '\0';
  s_fm_snd_on_sd = host.isSd(s_fm_fs);
  s_fm_snd_slot = host.soundSlot ? host.soundSlot() : TOUCH_SND_MSG;
  if (s_fm_snd_slot < 0 || s_fm_snd_slot > 2) s_fm_snd_slot = TOUCH_SND_MSG;
  fmSndClose();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_fm_snd_root = lv_obj_create(lv_layer_top());
  watch(s_fm_snd_root);
  lv_obj_remove_style_all(s_fm_snd_root);
  lv_obj_set_size(s_fm_snd_root, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_fm_snd_root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_fm_snd_root, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_fm_snd_root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_fm_snd_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* fn = lv_label_create(s_fm_snd_root);
  lv_label_set_long_mode(fn, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(fn, 8, 10); lv_obj_set_width(fn, sw - 16);
  lv_label_set_text(fn, name);
  lv_obj_set_style_text_font(fn, &font16(), LV_PART_MAIN);
  lv_obj_set_style_text_color(fn, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  static const char* const kSlot[3] = { "Message", "Direct (DM)", "@ mention" };
  lv_obj_t* sub = lv_label_create(s_fm_snd_root);
  char subt[56]; snprintf(subt, sizeof subt, "Target: %s sound",
                          kSlot[s_fm_snd_slot]);
  lv_label_set_text(sub, subt);
  lv_obj_set_pos(sub, 8, 40);
  lv_obj_set_style_text_font(sub, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(sub, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_t* play = lv_btn_create(s_fm_snd_root);
  lv_obj_set_size(play, sw - 16, 40); lv_obj_set_pos(play, 8, 76); styleButton(play);
  lv_obj_add_event_cb(play, fmSndPlayCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* pl = lv_label_create(play); lv_label_set_text(pl, TR(LV_SYMBOL_PLAY "  Play")); lv_obj_center(pl);
  useChainedFont(pl);
  lv_obj_t* setb = lv_btn_create(s_fm_snd_root);
  lv_obj_set_size(setb, sw - 16, 44); lv_obj_set_pos(setb, 8, 124); styleButton(setb);
  lv_obj_set_style_bg_color(setb, lv_color_hex(0x35C9C9), LV_PART_MAIN);
  lv_obj_add_event_cb(setb, fmSndSetCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* sl = lv_label_create(setb); lv_label_set_text(sl, TR(LV_SYMBOL_OK "  Set as notification sound"));
  useChainedFont(sl);
  lv_obj_set_style_text_color(sl, lv_color_black(), LV_PART_MAIN); lv_obj_center(sl);
  lv_obj_t* close = lv_btn_create(s_fm_snd_root);
  lv_obj_set_size(close, 30, 26); lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -6, 6); styleButton(close);
  lv_obj_add_event_cb(close, fmSndCloseCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cl = lv_label_create(close); lv_label_set_text(cl, LV_SYMBOL_CLOSE); tanCloseRed(cl);
  lv_obj_set_style_text_font(cl, &font12(), LV_PART_MAIN); lv_obj_center(cl);
}
#endif  // HAS_TDECK_GT911 || TLORA_PAGER (.wav notification-sound chooser)

static void fmOpenImage(const char* name) {
  if (!s_fm_fs || !name || !name[0]) return;
  fmMarkSdIo();                          // SD file read -> activity LED
  char path[200];
  fmFullPath(name, path, sizeof path);
  strncpy(s_fm_img_path, path, sizeof s_fm_img_path - 1);
  s_fm_img_path[sizeof s_fm_img_path - 1] = '\0';
  s_fm_img_on_sd = host.isSd(s_fm_fs);
  File f = s_fm_fs->open(path, "r");
  if (!f) { if (host.task()) host.task()->showAlert(TR("Cannot open file"), 1500); return; }
  size_t sz = f.size();
  if (sz == 0 || sz > FM_IMG_MAX) {
    f.close();
    if (host.task()) host.task()->showAlert(sz ? TR("Image too large (>4 MB)") : TR("Empty file"), 2000);
    return;
  }
  uint8_t* enc = (uint8_t*)ui::platform::allocate(sz, true);
  if (!enc) enc = (uint8_t*)malloc(sz);
  if (!enc) { f.close(); if (host.task()) host.task()->showAlert(TR("Out of memory"), 1500); return; }
  size_t rd = f.readBytes((char*)enc, sz);
  f.close();

  // Decode to a full RGB565 buffer up front (LVGL's line-by-line readers can't
  // feed a scaled/zoomed lv_img — that renders black). Route by magic bytes:
  // PNG -> lodepng, BMP -> our reader, else JPEG (SJPG/TJpgDec). Same TRUE_COLOR
  // path the map tiles use, so it scales.
  int dw = 0, dh = 0;
  uint8_t* rgb;
  ui::images::clearError();   // only the JPEG path sets this (e.g. "progressive")
  if (rd >= 4 && enc[0] == 0x89 && enc[1] == 'P' && enc[2] == 'N' && enc[3] == 'G') {
    rgb = decodePngToRgb565(enc, rd, &dw, &dh);
  } else if (rd >= 2 && enc[0] == 'B' && enc[1] == 'M') {
    rgb = decodeBmpToRgb565(enc, rd, &dw, &dh);
  } else {
    rgb = decodeJpegToRgb565(enc, rd, &dw, &dh);
    if (!rgb) rgb = decodeJpegScaledToRgb565(enc, rd, &dw, &dh, 1024);   // > 1024 px JPEG: downscale to fit
  }
  free(enc);             // encoded bytes no longer needed once decoded
  if (!rgb || dw <= 0 || dh <= 0) {
    if (rgb) lvglPsramFree(rgb);
    char emsg[96];
    if (ui::images::lastError()[0]) snprintf(emsg, sizeof emsg, "%s", ui::images::lastError());   // specific JPEG reason (e.g. progressive)
    else                  snprintf(emsg, sizeof emsg, "Can't display image\n(JPEG/PNG/BMP, <= 1024 px)");
    if (host.task()) host.task()->showAlert(emsg, 3600);
    return;
  }

  fmImageClose();        // drop any previous viewer (frees its buffer)
  s_fm_img_buf = rgb;    // decoded RGB565 pixels; freed on close
  memset(&s_fm_img_dsc, 0, sizeof s_fm_img_dsc);
  s_fm_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  s_fm_img_dsc.header.w  = (uint32_t)dw;
  s_fm_img_dsc.header.h  = (uint32_t)dh;
  s_fm_img_dsc.data      = s_fm_img_buf;
  s_fm_img_dsc.data_size = (uint32_t)dw * (uint32_t)dh * sizeof(lv_color_t);
  // s_fm_img_dsc is a reused static, so LVGL's image cache (keyed on the src
  // pointer) would hand back the PREVIOUS image's decoded geometry/data — the
  // "half old, half new" render. Drop the stale entry so this dsc decodes fresh.
  lv_img_cache_invalidate_src(&s_fm_img_dsc);
  const int w = dw, h = dh;

  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_fm_img_root = lv_obj_create(lv_layer_top());
  watch(s_fm_img_root);
  lv_obj_remove_style_all(s_fm_img_root);
  lv_obj_set_size(s_fm_img_root, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_fm_img_root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_fm_img_root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_fm_img_root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_fm_img_root, LV_OBJ_FLAG_SCROLLABLE);
  // A tap on the backdrop leaves full screen (no-op while windowed).
  lv_obj_add_flag(s_fm_img_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_fm_img_root, fmImageRootClickCb, LV_EVENT_CLICKED, nullptr);

  // The image itself; zoom + position are applied by fmImageRelayout().
  lv_obj_t* img = lv_img_create(s_fm_img_root);
  lv_img_set_src(img, &s_fm_img_dsc);
  lv_img_set_antialias(img, true);
  lv_img_set_pivot(img, w / 2, h / 2);            // scale around the image centre
  lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);  // let taps fall through to the backdrop

  // Header: filename + native dimensions (windowed only).
  char hdr[88];
  snprintf(hdr, sizeof hdr, "%.48s   %dx%d", name, w, h);
  lv_obj_t* fn = lv_label_create(s_fm_img_root);
  lv_label_set_long_mode(fn, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(fn, 6, 8);
  lv_obj_set_width(fn, sw - 6 - 96);
  lv_label_set_text(fn, hdr);
  lv_obj_set_style_text_font(fn, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(fn, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);

  // Close (X), top-right.
  lv_obj_t* close = lv_btn_create(s_fm_img_root);
  lv_obj_set_size(close, 30, 26);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -3, 3);
  styleButton(close);
  lv_obj_set_style_bg_color(close, lv_color_hex(colors().COLOR_SECONDARY_ACTION), LV_PART_MAIN);
  lv_obj_add_event_cb(close, fmImageCloseCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cl = lv_label_create(close); lv_label_set_text(cl, LV_SYMBOL_CLOSE); tanCloseRed(cl);
  lv_obj_set_style_text_font(cl, &font12(), LV_PART_MAIN); lv_obj_center(cl);

  // Full-screen toggle, just left of the close (X).
  lv_obj_t* full = lv_btn_create(s_fm_img_root);
  lv_obj_set_size(full, 52, 26);
  lv_obj_align(full, LV_ALIGN_TOP_RIGHT, -3 - 30 - 4, 3);
  styleButton(full);
  lv_obj_set_style_bg_color(full, lv_color_hex(colors().COLOR_SECONDARY_ACTION), LV_PART_MAIN);
  lv_obj_add_event_cb(full, fmImageFullCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* fll = lv_label_create(full); lv_label_set_text(fll, TR("Full"));
  lv_obj_set_style_text_font(fll, &font12(), LV_PART_MAIN); lv_obj_center(fll);

#if CAP_LOCK_SCREEN
  // "Set as lock wallpaper" — bottom-left, windowed chrome only.
  lv_obj_t* wall = lv_btn_create(s_fm_img_root);
  lv_obj_set_height(wall, 30);
  lv_obj_align(wall, LV_ALIGN_BOTTOM_LEFT, 6, -6);
  styleButton(wall);
  lv_obj_set_style_bg_color(wall, lv_color_hex(0x35C9C9), LV_PART_MAIN);
  lv_obj_add_event_cb(wall, fmSetWallpaperCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* wll = lv_label_create(wall);
  lv_label_set_text(wll, TR(LV_SYMBOL_IMAGE "  Set as wallpaper"));
  lv_obj_set_style_text_font(wll, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(wll, lv_color_black(), LV_PART_MAIN);
  lv_obj_center(wll);
  s_fm_img_wall = wall;
#endif  // CAP_LOCK_SCREEN (wallpaper button)

  // "tap to exit" hint, shown only in full-screen mode.
  lv_obj_t* hint = lv_label_create(s_fm_img_root);
  lv_label_set_text(hint, TR("tap to exit full screen"));
  lv_obj_set_style_text_font(hint, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(hint, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(hint, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(hint, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(hint, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(hint, 3, LV_PART_MAIN);
  lv_obj_set_style_radius(hint, 4, LV_PART_MAIN);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

  s_fm_img_widget = img;
  s_fm_img_hdr    = fn;
  s_fm_img_close  = close;
  s_fm_img_full   = full;
  s_fm_img_hint   = hint;
  s_fm_img_w = w; s_fm_img_h = h;
  s_fm_img_fs = false;
  fmImageRelayout();   // apply the windowed layout (zoom, positions, visibility)

  if (host.keyboard()) lv_keyboard_set_textarea(host.keyboard(), nullptr);   // no text entry here
}

// ----- row interaction -----
static void fmRowClickCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  FmRowData* rd = (FmRowData*)lv_obj_get_user_data(lv_event_get_target(e));
  if (!rd) return;
  if (rd->isdir)                fmEnterDir(rd->name);
#if CAP_SOUND_FILES
  else if (fmIsAudio(rd->name)) fmOpenAudio(rd->name);   // .wav -> notification-sound chooser
#endif
  else if (fmIsImage(rd->name)) fmOpenImage(rd->name);   // images -> read-only viewer
  else                          fmOpenEditor(rd->name);  // text -> editor; long-press -> manage
}
static void fmRowLongPressCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);   // swallow trailing click
  FmRowData* rd = (FmRowData*)lv_obj_get_user_data(lv_event_get_target(e));
  if (rd) fmOpenActions(rd->name, rd->isdir);
  else    fmOpenActions(nullptr, false);            // ".." row -> folder actions
}
// Folder-level menu: the "+" header button (CLICKED) and a long-press on the
// list's empty area (LONG_PRESSED). Only meaningful inside a filesystem.
static void fmFolderMenuCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  const lv_event_code_t c = lv_event_get_code(e);
  if (c != LV_EVENT_CLICKED && c != LV_EVENT_LONG_PRESSED) return;
  if (c == LV_EVENT_LONG_PRESSED) { lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a); }
  if (s_fm_fs) fmOpenActions(nullptr, false);
}

// Draw the current entries into the list, applying the active sort + filter.
// (No FS access — works off the cached s_fm_entries so sort/search are instant.)
static void fmRender() {
  if (!s_fm_list) return;
  lv_obj_clean(s_fm_list);
  if (s_fm_path_lbl) {
    char buf[180];
    snprintf(buf, sizeof buf, "%s:%s", s_fm_store, s_fm_path);
    lv_label_set_text(s_fm_path_lbl, buf);
  }

  lv_obj_t* up = lv_list_add_btn(s_fm_list, LV_SYMBOL_LEFT, "..");
  fmStyleRow(up, colors().COLOR_SUB);
  lv_obj_add_event_cb(up, fmUpCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(up, fmRowLongPressCb, LV_EVENT_LONG_PRESSED, nullptr);   // folder actions

  if (s_fm_count > 1) qsort(s_fm_entries, s_fm_count, sizeof(FmEntry), fmCmp);

  int shown = 0;
  for (int i = 0; i < s_fm_count; ++i) {
    FmEntry& en = s_fm_entries[i];
    if (!fmContainsCI(en.name, s_fm_filter)) continue;
    char label[120];
    if (en.isdir) {
      snprintf(label, sizeof label, "%s", en.name);
    } else {
      char sz[16];
      fmFmtSize((size_t)en.size, sz, sizeof sz);
      // Timestamp when the FS provides one and it's sane (> 2001; FAT's 1980
      // epoch / a clockless write shows as garbage, better omitted) (#185).
      char ts[24] = "";
      if (en.mtime > 978307200u) {
        time_t t = (time_t)en.mtime;
        struct tm tmv;
        if (ui::platform::localTime(t, tmv)) strftime(ts, sizeof ts, "  %d %b %H:%M", &tmv);
      }
#if defined(HAS_THINKNODE_M9)
      // The M9's 320 px row cannot show a crash-report filename, size and date
      // at once. Keep the filename on line one and make metadata immediately
      // visible below it instead of hiding the timestamp in the marquee tail.
      if (ts[0]) snprintf(label, sizeof label, "%s\n%s%s", en.name, sz, ts);
      else       snprintf(label, sizeof label, "%s   %s", en.name, sz);
#else
      snprintf(label, sizeof label, "%s   %s%s", en.name, sz, ts);
#endif
    }
    lv_obj_t* row = lv_list_add_btn(s_fm_list, en.isdir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, label);
    fmStyleRow(row, colors().COLOR_TEXT);
    FmRowData* rd = (FmRowData*)malloc(sizeof(FmRowData));
    if (rd) { snprintf(rd->name, sizeof rd->name, "%s", en.name); rd->isdir = en.isdir; }
    lv_obj_set_user_data(row, rd);
    lv_obj_add_event_cb(row, fmRowFreeCb,      LV_EVENT_DELETE,       nullptr);
    lv_obj_add_event_cb(row, fmRowClickCb,     LV_EVENT_CLICKED,      nullptr);
    lv_obj_add_event_cb(row, fmRowLongPressCb, LV_EVENT_LONG_PRESSED, nullptr);
    ++shown;
  }
  if (shown == 0) {
    lv_obj_t* empty = lv_list_add_text(s_fm_list, s_fm_filter[0] ? "(no matches)" : "(empty)");
    lv_obj_set_style_text_color(empty, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  }
}

// Read the current directory into s_fm_entries, then render.
// MeshCore/touch internal data files, hidden from the Internal view by default
// (toggle via the + menu). The /bl/<hash> contact blobs are the bulk of the
// clutter; the rest are a handful of named stores.
static bool fmIsSystemPath(const char* full) {
  if (!full) return false;
  if (!strncmp(full, "/bl/", 4) || !strcmp(full, "/bl")) return true;
  const char* base = strrchr(full, '/'); base = base ? base + 1 : full;
  static const char* const kSys[] = {
    "contacts3", "channels2", "adv_blobs", "com_prefs", "new_prefs", "node_prefs",
    "regions2", "s_contacts", "packet_log", "log", "identity", "ui_chat_history_v1.bin",
  };
  for (unsigned i = 0; i < sizeof(kSys)/sizeof(kSys[0]); ++i)
    if (!strcmp(base, kSys[i])) return true;
  return false;
}
// OS-metadata cruft on removable FAT/exFAT cards: macOS AppleDouble sidecars
// ("._<name>") and dot-files (.DS_Store, .Spotlight-V100, .Trashes, .fseventsd,
// .TemporaryItems…), the Finder "__MACOSX" ZIP folder, and Windows' "System
// Volume Information". Hidden in the file manager unless "Show system files" is
// on — same toggle that reveals MeshCore's own SPIFFS files.
static bool fmIsHiddenName(const char* base) {
  if (!base || !base[0]) return false;
  if (base[0] == '.') return true;                            // ._* AppleDouble + all dot-files
  if (!strcmp(base, "__MACOSX")) return true;
  if (!strcasecmp(base, "System Volume Information")) return true;
  return false;
}
// Append an entry, de-duplicating by (name,isdir) so synthesised virtual folders collapse.
static void fmAddEntry(const char* name, uint32_t size, bool isdir, uint32_t mtime = 0) {
  if (!name[0] || s_fm_count >= FM_MAX_ENTRIES) return;
  for (int i = 0; i < s_fm_count; ++i)
    if (s_fm_entries[i].isdir == isdir && !strcmp(s_fm_entries[i].name, name)) return;
  FmEntry& en = s_fm_entries[s_fm_count++];
  snprintf(en.name, sizeof en.name, "%s", name);
  en.size = size; en.isdir = isdir; en.mtime = mtime;
}

void fmRefresh() {
  if (!s_fm_list) return;
  if (!s_fm_fs) { fmShowRoots(); return; }
  s_fm_count = 0;
  if (!s_fm_entries) { fmRender(); return; }
  const bool flat = host.isFlat(s_fm_fs);
  if (flat) {
    // Prefix of the current virtual folder: "" at root, "lock/" inside /lock, etc.
    char pfx[200];
    if (s_fm_path[0] == '\0' || (s_fm_path[0] == '/' && s_fm_path[1] == '\0')) pfx[0] = '\0';
    else snprintf(pfx, sizeof pfx, "%s/", s_fm_path + 1);
    const size_t pfxlen = strlen(pfx);
    File root = s_fm_fs->open("/");
    if (root) {
      File e = root.openNextFile();
      while (e) {
        const char* full = e.path();   // full path incl. leading '/' (name() is basename-only on this core)
        const uint32_t esz = (uint32_t)e.size();
        const uint32_t emt = (uint32_t)e.getLastWrite();   // 0 when the FS has no mtime (#185)
        if (s_fm_show_hidden || !fmIsSystemPath(full)) {
          const char* rel = (full[0] == '/') ? full + 1 : full;
          if (!strncmp(rel, pfx, pfxlen)) {                 // belongs in the current virtual folder
            const char* sub = rel + pfxlen;
            if (sub[0]) {
              const char* slash = strchr(sub, '/');
              if (slash) {                                  // deeper path -> a sub-folder
                char seg[64]; size_t n = (size_t)(slash - sub);
                if (n >= sizeof seg) n = sizeof seg - 1;
                memcpy(seg, sub, n); seg[n] = '\0';
                fmAddEntry(seg, 0, true);
              } else {
                fmAddEntry(sub, esz, false, emt);           // a file in this folder
              }
            }
          }
        }
        e.close();
        e = root.openNextFile();
      }
      root.close();
    }
  } else {
    fmMarkSdIo();                                           // SD browse -> activity LED
    File dir = s_fm_fs->open(s_fm_path);                    // SD / FAT: real directory listing
    if (dir) {
      File e = dir.openNextFile();
      while (e && s_fm_count < FM_MAX_ENTRIES) {
        const char* full = e.name();
        const char* base = strrchr(full, '/'); base = base ? base + 1 : full;
        if (s_fm_show_hidden || !fmIsHiddenName(base))
          fmAddEntry(base, (uint32_t)e.size(), e.isDirectory(), (uint32_t)e.getLastWrite());
        e.close();
        e = dir.openNextFile();
      }
      dir.close();
    }
  }
  fmRender();
}

// Roots screen: list the available storages.



static void fmSortBtnCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  s_fm_sort = (uint8_t)((s_fm_sort + 1) % 4);
  if (s_fm_sort_lbl) lv_label_set_text(s_fm_sort_lbl, k_fm_sort_names[s_fm_sort]);
  fmRender();
}

static void fmSearchChangedCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  (void)e;
  if (!s_fm_search_ta) return;
  const char* t = lv_textarea_get_text(s_fm_search_ta);
  snprintf(s_fm_filter, sizeof s_fm_filter, "%s", t ? t : "");
  fmRender();
}

// Toggle the inline search field over the address bar.
static void fmToggleSearch() {
  if (s_fm_search_ta) {
    host.hideKb();
    lv_obj_del(s_fm_search_ta);
    s_fm_search_ta = nullptr;
    s_fm_filter[0] = '\0';
    if (s_fm_path_lbl) lv_obj_clear_flag(s_fm_path_lbl, LV_OBJ_FLAG_HIDDEN);
    fmRender();
    return;
  }
  if (!s_fm_path_lbl) return;
  lv_obj_t* parent = lv_obj_get_parent(s_fm_path_lbl);
  lv_coord_t x = lv_obj_get_x(s_fm_path_lbl);
  lv_coord_t y = lv_obj_get_y(s_fm_path_lbl);
  lv_coord_t w = lv_obj_get_width(s_fm_path_lbl);
  lv_obj_add_flag(s_fm_path_lbl, LV_OBJ_FLAG_HIDDEN);

  s_fm_search_ta = lv_textarea_create(parent);
  lv_obj_set_pos(s_fm_search_ta, x, y - 5);
  lv_obj_set_size(s_fm_search_ta, w, 26);
  styleCard(s_fm_search_ta);
  lv_textarea_set_one_line(s_fm_search_ta, true);
  taSetPlaceholder(s_fm_search_ta, TR("search"));
  lv_textarea_set_max_length(s_fm_search_ta, sizeof(s_fm_filter) - 1);
  lv_obj_set_style_text_font(s_fm_search_ta, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(s_fm_search_ta, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_pad_ver(s_fm_search_ta, 2, LV_PART_MAIN);
  lv_obj_add_event_cb(s_fm_search_ta, fmSearchChangedCb, LV_EVENT_VALUE_CHANGED, nullptr);
  host.attachSettingsTaEvents(s_fm_search_ta);
  if (host.keyboard()) host.kbMirrorBind(s_fm_search_ta);
}

static void fmBackCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_fm_search_ta) { fmToggleSearch(); return; }   // close search first
  if (!s_fm_fs) {   // already on the Storage (roots) page: Back == Home
    host.closeFullscreenView();
    host.home();
    return;
  }
  fmUp();
}
static void fmSearchBtnCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE && !accepts(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmToggleSearch();
}

// Write the bundled placeholder into SPIFFS /lock/ once, so the "lock" folder
// exists for the (future) lockscreen and is viewable now. SPIFFS is flat, so
// writing "/lock/placeholder.png" implicitly creates the folder.


void buildFileManager(lv_obj_t* body) {
  close();
  if (!body) return;
  page = body;
  watch(page);
  host.fmSeedLockFolder();   // ensure /lock/placeholder.png exists (once per boot)
  // Keep the entry cache in PSRAM — it's 12 KB, and internal DRAM is tight
  // enough that holding it there can push SD mounting into an OOM abort.
  if (!s_fm_entries) {
    s_fm_entries = (FmEntry*)ui::platform::allocate(sizeof(FmEntry) * FM_MAX_ENTRIES, true);
    if (!s_fm_entries) s_fm_entries = (FmEntry*)ui::platform::allocate(sizeof(FmEntry) * FM_MAX_ENTRIES, false);
  }
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  const lv_coord_t bw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t bh = (lv_disp_get_ver_res(nullptr) - host.statusHeight());
  const lv_coord_t HDR_H = 34, BTN_W = 38, BTN_H = 28, HOME_RES = 48;

  // Back button (far left): closes search if open, else goes up a folder.
  lv_obj_t* back = lv_btn_create(body);
  lv_obj_set_size(back, 30, BTN_H);
  lv_obj_set_pos(back, 3, 3);
  styleButton(back);
  lv_obj_set_style_bg_color(back, lv_color_hex(colors().COLOR_CONTROL), LV_PART_MAIN);
  lv_obj_set_style_pad_all(back, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(back, fmBackCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* backl = lv_label_create(back);
  useChainedFont(backl);
  lv_label_set_text(backl, LV_SYMBOL_LEFT);
  lv_obj_center(backl);

  // "+" button (next to Back): opens the folder menu (New folder / Paste).
  lv_obj_t* add = lv_btn_create(body);
  lv_obj_set_size(add, 30, BTN_H);
  lv_obj_set_pos(add, 36, 3);
  styleButton(add);
  lv_obj_set_style_bg_color(add, lv_color_hex(colors().COLOR_CONTROL), LV_PART_MAIN);
  lv_obj_set_style_pad_all(add, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(add, fmFolderMenuCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* addl = lv_label_create(add);
  useChainedFont(addl);
  lv_label_set_text(addl, LV_SYMBOL_PLUS);
  lv_obj_center(addl);

  // Find (search) button — rightmost before the floating Home.
  const lv_coord_t find_x = bw - HOME_RES - BTN_W - 3;
  lv_obj_t* find = lv_btn_create(body);
  lv_obj_set_size(find, BTN_W, BTN_H);
  lv_obj_set_pos(find, find_x, 3);
  styleButton(find);
  lv_obj_set_style_bg_color(find, lv_color_hex(colors().COLOR_CONTROL), LV_PART_MAIN);
  lv_obj_set_style_pad_all(find, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(find, fmSearchBtnCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* findl = lv_label_create(find);
  lv_label_set_text(findl, TR("Find"));
  lv_obj_set_style_text_font(findl, &font12(), LV_PART_MAIN);
  lv_obj_center(findl);

  // Sort button — cycles A-Z / Z-A / Size / Type, label shows the mode.
  const lv_coord_t sort_x = find_x - BTN_W - 3;
  lv_obj_t* sort = lv_btn_create(body);
  lv_obj_set_size(sort, BTN_W, BTN_H);
  lv_obj_set_pos(sort, sort_x, 3);
  styleButton(sort);
  lv_obj_set_style_bg_color(sort, lv_color_hex(colors().COLOR_CONTROL), LV_PART_MAIN);
  lv_obj_set_style_pad_all(sort, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(sort, fmSortBtnCb, LV_EVENT_CLICKED, nullptr);
  s_fm_sort_lbl = lv_label_create(sort);
  lv_label_set_text(s_fm_sort_lbl, k_fm_sort_names[s_fm_sort]);
  lv_obj_set_style_text_font(s_fm_sort_lbl, &font12(), LV_PART_MAIN);
  lv_obj_center(s_fm_sort_lbl);

  // Address bar (between the +/Back group and Sort), styled like a URL field.
  const lv_coord_t loc_x = 36 + 30 + 4;   // past Back(3+30) and "+"(36+30)
  const lv_coord_t loc_w = sort_x - 4 - loc_x;
  s_fm_path_lbl = lv_label_create(body);
  lv_label_set_long_mode(s_fm_path_lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(s_fm_path_lbl, loc_x, 6);
  lv_obj_set_width(s_fm_path_lbl, loc_w);
  // One line, fixed. LV_LABEL_LONG_DOT ellipsizes against the object's HEIGHT,
  // and a label defaults to LV_SIZE_CONTENT — so with no height set, a path too
  // long for loc_w wrapped and grew downward instead of getting its dots. This
  // text changes every time the user walks into a directory, the field sits at
  // y=6 inside a header only HDR_H (34) tall, and s_fm_list is pinned at HDR_H
  // and painted after it, so the overflow disappeared under the list. Height =
  // one line of font12() plus the 3 px pad_ver either side.
  lv_obj_set_height(s_fm_path_lbl, SC(21));
  lv_obj_set_style_text_font(s_fm_path_lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(s_fm_path_lbl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_fm_path_lbl, lv_color_hex(themeRole(0x101418, colors().COLOR_FIELD)), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_fm_path_lbl, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_fm_path_lbl, lv_color_hex(themeRole(0x2A2E33, colors().COLOR_BORDER)), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_fm_path_lbl, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(s_fm_path_lbl, 5, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(s_fm_path_lbl, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(s_fm_path_lbl, 3, LV_PART_MAIN);
  lv_label_set_text(s_fm_path_lbl, TR("Storage"));

  // Entry list fills the rest.
  s_fm_list = lv_list_create(body);
  lv_obj_set_size(s_fm_list, bw - 8, bh - HDR_H - 2);
  lv_obj_set_pos(s_fm_list, 4, HDR_H);
  lv_obj_set_style_bg_color(s_fm_list, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_fm_list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_fm_list, 0, LV_PART_MAIN);
  // Long-press on the list's empty area opens the folder menu too.
  lv_obj_add_event_cb(s_fm_list, fmFolderMenuCb, LV_EVENT_LONG_PRESSED, nullptr);

  fmShowRoots();
}


static void fmMarkSdIo() { if (host.isSd(s_fm_fs)) host.markSdIo(); }
void configure(Host value) { close(); host=value; }
bool active() { return page!=nullptr; }
fs::FS* currentFilesystem() { return s_fm_fs; }
lv_obj_t* editorInput() { return s_editor_ta; }
static lv_obj_t* popup(unsigned index) {
  switch(index) {
    case 0:return s_fm_img_root; case 1:return s_editor_root;
    case 2:return s_fm_prompt; case 3:return s_fm_actions;
#if CAP_SOUND_FILES
    case 4:return s_fm_snd_root;
#endif
    case 5:return s_fm_fmt_overlay; default:return nullptr;
  }
}
bool popupOpen(unsigned index) { return popup(index)!=nullptr; }
static bool accepts(lv_event_t* event) {
  for (auto* object=lv_event_get_target(event); object; object=lv_obj_get_parent(object)) {
    if (object==page) return true;
    for (unsigned i=0;i<6;++i) if (object==popup(i)) return true;
  }
  return false;
}
static void watch(lv_obj_t* object) {
  lv_obj_add_event_cb(object,[](lv_event_t* event) {
    auto* dead=lv_event_get_target(event);
    if (dead==page) { page=nullptr; close(); return; }
    if (dead==s_fm_prompt) { s_fm_prompt=nullptr; s_fm_prompt_ta=nullptr; s_fm_prompt_cb=nullptr; host.hideKb(); if (!s_fm_prompt_ok) host.cancelPrompt(); }
    if (dead==s_editor_root) { s_editor_root=nullptr; s_editor_ta=nullptr; host.hideKb(); }
    if (dead==s_fm_img_root) { s_fm_img_root=nullptr; fmImageClose(); }
    if (dead==s_fm_actions) s_fm_actions=nullptr;
    if (dead==s_fm_fmt_overlay) s_fm_fmt_overlay=nullptr;
#if CAP_SOUND_FILES
    if (dead==s_fm_snd_root) s_fm_snd_root=nullptr;
#endif
  },LV_EVENT_DELETE,&page);
}
void close() {
  if (!host.task) return;
  pendingDelete.filesystem = nullptr;
  auto* old=page; page=nullptr;
  if (old) lv_obj_remove_event_cb_with_user_data(old,nullptr,&page);
  const bool hadInput=s_fm_search_ta || s_fm_prompt_ta || s_editor_ta;
  fmEditorClose(); fmImageClose(); fmCloseActions(); fmPromptClose();
#if CAP_SOUND_FILES
  fmSndClose();
#endif
  fmHideFormatOverlay();
  s_fm_paste_pending=0;
  s_fm_list=s_fm_path_lbl=s_fm_search_ta=s_fm_sort_lbl=nullptr;
  s_fm_fs=nullptr; s_fm_filter[0]=0;
  free(s_fm_entries); s_fm_entries=nullptr; s_fm_count=0;
  if (hadInput) host.hideKb();
  if (old) lv_obj_clean(old);
}
bool takePendingPaste() { return s_fm_paste_pending && --s_fm_paste_pending==0; }
void fmShowRoots() {
  s_fm_fs=nullptr; s_fm_path[0]=s_fm_store[0]=0; s_fm_count=0;
  if (!s_fm_list) return;
  lv_obj_clean(s_fm_list);
  if (s_fm_path_lbl) lv_label_set_text(s_fm_path_lbl,TR("Storage"));
  char label[80], usedText[16], totalText[16]; uint64_t used=0,total=0;
  host.internalUsage(used,total);
  fmFmtSize(used,usedText,sizeof usedText); fmFmtSize(total,totalText,sizeof totalText);
  snprintf(label,sizeof label,TR("Internal storage   %s / %s"),usedText,totalText);
  auto* internal=lv_list_add_btn(s_fm_list,LV_SYMBOL_DRIVE,label);
  fmStyleRow(internal,colors().COLOR_TEXT);
  lv_obj_add_event_cb(internal,fmInternalClickCb,LV_EVENT_CLICKED,nullptr);
  static StorageRow rows[2];
  const int count=host.storageRows(rows,2);
  for (int i=0;i<count && i<2;++i) {
    auto* row=lv_list_add_btn(s_fm_list,LV_SYMBOL_SD_CARD,rows[i].label);
    fmStyleRow(row,rows[i].available?colors().COLOR_TEXT:colors().COLOR_SUB);
    lv_obj_add_event_cb(row,[](lv_event_t* event) {
      if (!accepts(event)) return;
      auto* row=static_cast<StorageRow*>(lv_event_get_user_data(event));
      const auto code=lv_event_get_code(event);
      if (code==row->event && row->click) row->click(event);
      if (code==LV_EVENT_LONG_PRESSED && row->hold) row->hold(event);
    },LV_EVENT_ALL,&rows[i]);
  }
}
#if !CAP_SOUND_FILES
void fmSndClose() {}
#endif
} } }
