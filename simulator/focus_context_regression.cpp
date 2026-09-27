// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/FocusContextSelector.h"

#include <cstdint>
#include <stdexcept>

namespace {
namespace focus = ui::focus;

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void expect(const focus::FocusSelection &selection, lv_obj_t *root,
            focus::ContextMode mode, bool onPage, bool topLayer,
            const char *message) {
  check(selection.root == root && selection.mode == mode &&
            selection.onPage == onPage && selection.topLayer == topLayer,
        message);
}
} // namespace

void runFocusContextRegression(void (*pump)(unsigned)) {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  auto *fixture = lv_obj_create(lv_layer_top());
  auto *screen = lv_obj_create(fixture);
  auto *top = lv_obj_create(fixture);
  auto *first = lv_obj_create(top);
  auto *middle = lv_obj_create(top);
  auto *last = lv_obj_create(top);
  focus::ContextSnapshot snapshot;
  snapshot.screen = screen;
  snapshot.topLayer = top;
  snapshot.skipFlag = LV_OBJ_FLAG_USER_1;

  check(focus::frontmostVisibleTopChild(top, snapshot.skipFlag) == last,
        "Frontmost visible top child was not selected");
  auto selection = focus::resolveFocusContext(snapshot);
  expect(selection, last, focus::ContextMode::Top, false, true,
         "Top popup did not own the focus context");
  check(selection.signature == 0xA5A5A5A5u, "Top context signature changed");
  check(!selection.chatIdentity && !selection.composer,
        "Top popup inherited chat-only fields");
  lv_obj_add_flag(last, LV_OBJ_FLAG_USER_1);
  check(focus::frontmostVisibleTopChild(top, snapshot.skipFlag) == middle,
        "Skipped frontmost top child still owned focus");
  lv_obj_add_flag(middle, LV_OBJ_FLAG_HIDDEN);
  check(focus::frontmostVisibleTopChild(top, snapshot.skipFlag) == first,
        "Hidden top child still owned focus");
  selection = focus::resolveFocusContext(snapshot);
  expect(selection, first, focus::ContextMode::Top, false, true,
         "Top selection did not skip marked and hidden children");
  lv_obj_add_flag(first, LV_OBJ_FLAG_USER_1);
  check(!focus::frontmostVisibleTopChild(top, snapshot.skipFlag),
        "Top helper returned a hidden or skipped child");

  auto *wifi = lv_obj_create(fixture);
  auto *settings = lv_obj_create(fixture);
  auto *chat = lv_obj_create(fixture);
  auto *composer = lv_textarea_create(chat);
  const int chatMarker = 17;
  snapshot.wifiSheet = wifi;
  snapshot.settingsSheet = settings;
  snapshot.settingsCategory = 3;
  snapshot.chatOverlay = chat;
  snapshot.chatIdentity = &chatMarker;
  snapshot.composer = composer;
  selection = focus::resolveFocusContext(snapshot);
  expect(selection, wifi, focus::ContextMode::Wifi, false, false,
         "Wi-Fi sheet did not precede Settings and Chat");
  check(selection.signature == 0x9F1CB0EDu, "Wi-Fi context signature changed");
  check(!selection.chatIdentity && !selection.composer,
        "Wi-Fi context inherited chat identity or composer");
  lv_obj_add_flag(wifi, LV_OBJ_FLAG_HIDDEN);
  selection = focus::resolveFocusContext(snapshot);
  expect(selection, wifi, focus::ContextMode::Wifi, false, false,
         "Hidden base-layer Wi-Fi sheet lost its established precedence");

  snapshot.wifiSheet = nullptr;
  selection = focus::resolveFocusContext(snapshot);
  expect(selection, settings, focus::ContextMode::Settings, false, false,
         "Settings sheet did not precede Chat");
  check(selection.signature == (0x5E771465u ^ (4u * 2654435761u)),
        "Settings category signature changed");
  check(!selection.chatIdentity && !selection.composer,
        "Settings context inherited chat-only fields");
  const auto categoryThree = selection.signature;
  check(focus::resolveFocusContext(snapshot).signature == categoryThree,
        "Settings category signature changed without an input change");
  snapshot.settingsCategory = 4;
  check(focus::resolveFocusContext(snapshot).signature != categoryThree,
        "Distinct Settings categories shared a signature");
  lv_obj_add_flag(settings, LV_OBJ_FLAG_HIDDEN);
  selection = focus::resolveFocusContext(snapshot);
  expect(selection, settings, focus::ContextMode::Settings, false, false,
         "Hidden base-layer Settings sheet lost its established precedence");

  snapshot.settingsSheet = nullptr;
  selection = focus::resolveFocusContext(snapshot);
  expect(selection, chat, focus::ContextMode::Chat, false, false,
         "Chat overlay did not precede main tabs");
  check(selection.signature == 0xC4A7C4A7u, "Chat context signature changed");
  check(selection.chatIdentity == &chatMarker && selection.composer == composer,
        "Chat context lost its identity or composer");

  auto *tabview = lv_tabview_create(fixture, LV_DIR_TOP, 28);
  auto *tabA = lv_tabview_add_tab(tabview, "A");
  auto *tabB = lv_tabview_add_tab(tabview, "B");
  auto *tabC = lv_tabview_add_tab(tabview, "C");
  snapshot.chatOverlay = nullptr;
  snapshot.tabview = tabview;
  snapshot.activeTab = 1;
  selection = focus::resolveFocusContext(snapshot);
  expect(selection, tabB, focus::ContextMode::Main, true, false,
         "Inactive tab or tabview frame was selected instead of active tab");
  check(selection.signature == 2u * 2654435761u,
        "Active tab signature changed");
  check(selection.root != tabA && selection.root != tabC &&
            !selection.chatIdentity && !selection.composer,
        "Main tab retained inactive root or Chat fields");
  const auto tabOne = selection.signature;
  check(focus::resolveFocusContext(snapshot).signature == tabOne,
        "Main tab signature changed without an input change");
  snapshot.activeTab = 2;
  selection = focus::resolveFocusContext(snapshot);
  expect(selection, tabC, focus::ContextMode::Main, true, false,
         "Changing active tab did not change focus root");
  check(selection.signature != tabOne,
        "Different active tabs shared a signature");
  snapshot.activeTab = -1;
  selection = focus::resolveFocusContext(snapshot);
  expect(selection, screen, focus::ContextMode::Main,
         true, false, "Negative active tab did not fall back to screen");
  check(selection.signature == 0, "Negative index baseline signature changed");
  snapshot.activeTab = 99;
  expect(focus::resolveFocusContext(snapshot), screen, focus::ContextMode::Main,
         true, false, "Out-of-range active tab did not fall back to screen");

  // Deleted pointers are borrowed inputs; resolving must fall back safely and
  // must not retain identity or widget state from a previous snapshot.
  auto *deleted = lv_obj_create(fixture);
  lv_obj_del(deleted);
  snapshot.wifiSheet = deleted;
  snapshot.settingsSheet = deleted;
  snapshot.chatOverlay = deleted;
  snapshot.tabview = deleted;
  snapshot.topLayer = deleted;
  snapshot.chatIdentity = &chatMarker;
  snapshot.composer = composer;
  snapshot.activeTab = 0;
  selection = focus::resolveFocusContext(snapshot);
  expect(selection, screen, focus::ContextMode::Main, true, false,
         "Deleted overlay/tabview pointer prevented safe screen fallback");
  check(!selection.chatIdentity && !selection.composer,
        "Deleted Chat pointer retained chat-only state");
  snapshot.screen = deleted;
  selection = focus::resolveFocusContext(snapshot);
  check(!selection.root && selection.mode == focus::ContextMode::Main &&
            selection.onPage && !selection.topLayer,
        "Deleted screen pointer was retained as a focus root");

  focus::ContextSnapshot next;
  next.screen = screen;
  selection = focus::resolveFocusContext(next);
  expect(selection, screen, focus::ContextMode::Main, true, false,
         "Fresh snapshot inherited prior modal context");
  check(!selection.chatIdentity && !selection.composer,
        "Fresh snapshot inherited prior Chat identity or composer");
  lv_obj_del(fixture);
  pump(40);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots,
        "Focus context regression leaked a top-layer root");
}
