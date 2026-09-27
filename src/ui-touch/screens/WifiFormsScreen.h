// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../services/WifiScanJob.h"
#include "../widgets/ObjectRef.h"
#include <cstddef>
#include <cstdint>

namespace ui { namespace screens {

// Owns the active Settings > Wi-Fi list and its Join/Hidden/Details sheet.
// Every method and Host callback runs on the LVGL thread. The radio scan job,
// reconnect policy, and persistent network store remain with the host.
class WifiFormsScreen {
public:
  static constexpr unsigned SavedCapacity = 8;
  static constexpr unsigned ScanCapacity = WifiScanJob::Capacity;

  struct SavedNetwork {
    char ssid[33] = {};
    uint32_t rank = 0;
    bool autoJoin = false;
  };
  struct PageSnapshot {
    SavedNetwork saved[SavedCapacity] = {};
    char scanned[ScanCapacity][WifiScanJob::SsidCapacity] = {};
    char connectedSsid[33] = {};
    char status[160] = {}; // already formatted for the one-line page header
    unsigned savedCount = 0, scannedCount = 0;
    bool radioEnabled = false, connected = false, scanning = false;
  };
  struct Host {
    void *context = nullptr;
    // Read one bounded, immutable UI-thread snapshot per build/refresh.
    bool (*readSnapshot)(void *, PageSnapshot &) = nullptr;
    bool (*loadPassword)(void *, const char *ssid, char *, size_t) = nullptr;
    bool (*saveAndConnect)(void *, const char *ssid, const char *password, bool autoJoin) = nullptr;
    bool (*connectSaved)(void *, const char *ssid) = nullptr;
    bool (*forgetSaved)(void *, const char *ssid) = nullptr;
    bool (*setAutoJoin)(void *, const char *ssid, bool) = nullptr;
    bool (*setRadioEnabled)(void *, bool) = nullptr;
    bool (*requestScan)(void *) = nullptr;
    void (*syncKeyboard)(void *) = nullptr;
    void (*hideKeyboard)(void *) = nullptr;
    void (*attachTextArea)(void *, lv_obj_t *) = nullptr;
    void (*attachSymbolButton)(void *, lv_obj_t *) = nullptr;
    void (*setPasswordMirror)(void *, bool hidden) = nullptr;
    void (*alert)(void *, const char *, unsigned) = nullptr;
    int (*statusHeight)(void *) = nullptr;
    void (*beginPage)(void *, const char *title) = nullptr;
    // Adapter must check that its Wi-Fi page-close hook is still current.
    void (*endPageIfOwned)(void *) = nullptr;
    void (*updateStatusBar)(void *) = nullptr;
    void (*navDirty)(void *) = nullptr;
    void (*closeRoot)(void *, lv_obj_t **) = nullptr;
  };

  explicit WifiFormsScreen(Host host);
  ~WifiFormsScreen();
  WifiFormsScreen(const WifiFormsScreen &) = delete;
  WifiFormsScreen &operator=(const WifiFormsScreen &) = delete;

  void build(lv_obj_t *body, lv_coord_t width);
  void detach();
  void refreshStatus();
  void rebuildList(); // call after the scan job publishes a new UI snapshot
  void closeSheet();
  bool sheetOpen() const { return _sheetRoot.get() != nullptr; }
  lv_obj_t *focusRoot() const { return _sheetRoot.get(); }
  const char *pageTitle() const { return _sheetTitle; }

private:
  enum class SheetKind : uint8_t { None, Join, Hidden, Details };
  enum class RowKind : uint8_t { Saved, Scanned, Rescan, Hidden };
  struct RowContext {
    WifiFormsScreen *owner;
    uint32_t generation;
    RowKind kind;
    char ssid[33];
  };
  static constexpr unsigned RowCapacity = SavedCapacity + ScanCapacity + 2;

  static void bodyDeleted(lv_event_t *);
  static void sheetDeleted(lv_event_t *);
  static void rowClicked(lv_event_t *);
  static void radioChanged(lv_event_t *);
  static void connectClicked(lv_event_t *);
  static void forgetClicked(lv_event_t *);
  static void joinClicked(lv_event_t *);
  static void autoJoinChanged(lv_event_t *);
  static void revealClicked(lv_event_t *);
  static void closeClicked(lv_event_t *);

  bool snapshot(PageSnapshot &);
  bool still(uint32_t generation) const;
  void clearPage(bool deleting);
  void clearSheet(bool deleting);
  void detachTree(lv_obj_t *, bool root = false);
  void alert(const char *, unsigned = 1200);
  void navDirty();
  void openJoin(const char *ssid, bool hidden);
  void openDetails(const char *ssid);
  lv_obj_t *openSheet(const char *title, SheetKind);
  void join();
  void connectSaved();
  void forgetSaved();
  void setAutoJoin(bool);
  void setRadio(bool);
  void appendHeader(const char *);
  void appendRow(const char *, const char *, RowKind, const char *, uint32_t);
  void releaseRows();

  Host _host;
  widgets::ObjectRef _body, _list, _radio, _status;
  widgets::ObjectRef _sheetRoot, _ssidField, _passwordField, _autoJoin, _eyeGlyph;
  lv_coord_t _width = 0;
  char _sheetTitle[40] = {};
  char _sheetSsid[33] = {};
  RowContext *_rows = nullptr; // allocated only while the list is present
  unsigned _rowCount = 0;
  int _listY = 0;
  uint32_t _generation = 0, _listGeneration = 0;
  SheetKind _sheetKind = SheetKind::None;
  bool _destroying = false;
  bool _radioEnabled = false;
  bool _autoJoinEnabled = false;
};

} } // namespace ui::screens
