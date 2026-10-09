// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ChatPanel.h"
namespace ui {
namespace screens {
namespace timeline {
using LvChatPanel = ChatPanel;
struct Host {
  bool (*ready)();
  int (*messageCapacity)();
  bool (*messageAt)(int, MessageTypes::UIMessage &);
  int (*activeMessages)(int *, int);
  bool (*hasActiveThread)();
  bool (*activeThreadIsChannel)();
  void (*activeThreadName)(char *, size_t);
  void (*markActiveThreadRead)();
  uint8_t (*repeats)(uint32_t);
  const char *(*nodeName)();
  ChatPanel *direct;
  ChatPanel *channel;
  int (*statusHeight)();
  bool (*activeThreadIsRoom)();
  void (*popupClose)(lv_obj_t **);
  void (*updateJumpButtons)(ChatPanel *);
  void (*focusComposer)(ChatPanel *);
  void (*sanitize)(const lv_font_t *, char *, size_t, const char *);
  bool (*mentionsMe)(const char *);
  void (*retryMessage)(int ringIndex);
  void (*longPressMessage)(int ringIndex);
  void (*trackScroll)(lv_obj_t *);
  lv_group_t *(*navigationGroup)();
  bool (*detachNavigation)();
  void (*navigationDirty)();
  void (*rebuildNavigation)();
  void (*focusHint)(lv_obj_t *);
  void (*trace)(const char *);
};
struct Snapshot {
  ChatPanel *panel;
  int count, firstVisible, lastVisible, dividerIndex;
  int32_t dividerY, totalHeight;
  bool hasOffsets;
};
struct FocusRequest {
  int index;
  uint32_t at;
};
struct Metrics { uint32_t rowsCreated, rowsBound, heightMeasurements, metadataUpdates, windowUpdates; };
Metrics metrics();
void resetMetrics();
// One active timeline on the device. Owns all layout/index buffers, render
// scheduling and URL dialogs. Host panels outlive configure/shutdown; their
// message containers are borrowed and watched for external LVGL deletion.
void configure(const Host &);
void shutdown();
Snapshot snapshot();
FocusRequest requestedFocus();
void requestFocus(int logicalIndex);
void invalidateRows();
int32_t messageCenter(int logicalIndex);
void opened(uint16_t unread);
void closed(ChatPanel *);
void jumpOnOpen(int ringIndex);
bool urlMenuOpen();
bool urlQrOpen();
void closeUrlMenu();
void closeUrlQr();
void chatVirtReset(ChatPanel *);
void chatVirtJumpToOldest(ChatPanel *);
void chatVirtJumpToLatest(ChatPanel *);
void chatVirtScheduleRender(ChatPanel *);
void chatVirtApplyPendingScroll(ChatPanel *);
void chatVirtSyncBubblePositions(ChatPanel *);
void chatVirtOnScrollEnd(ChatPanel *);
void chatVirtRemap1To1Scroll(ChatPanel *);
bool chatVirtAwayFromBottom(ChatPanel *);
void chatVirtRefreshScrollArea(ChatPanel *);
lv_coord_t chatVirtMsgsViewH(ChatPanel *);
lv_coord_t chatVirtMaxScrollY(ChatPanel *);
lv_coord_t chatVirtVirtToLv(int32_t);
int32_t chatVirtMaxVirtTop(ChatPanel *);
void chatVirtCancelRenderTimer();
void chatVirtResetInputForMsgs(ChatPanel *);
int32_t chatVirtMsgVirtBottom(int);
void chatVirtQueueScroll(ChatPanel *, lv_coord_t);
void chatVirtLogTopAnchor(const char *, ChatPanel *, lv_coord_t, int = -1, int = -1);
void refreshChatDetail(ChatPanel &);
void refreshChatDetailAsync(ChatPanel &);
} // namespace timeline
} // namespace screens
} // namespace ui
