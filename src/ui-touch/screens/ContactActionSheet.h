// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include <lvgl.h>

#include "../widgets/ObjectRef.h"

namespace ui {
namespace screens {

class ContactActionSheet {
public:
  // The action order here follows the existing contact action sheet. Keep this
  // list stable: UITask is the policy/mesh adapter and receives only one of
  // these values together with the copied public key.
  enum class Action : uint8_t {
    Message = 0,
    Join,
    Ping,
    Telemetry,
    ShowOnMap,
    Trace,
    Admin,
    RangeTest,
    Sightline,
    Favorite,
    ShareLocation,
    ResetPath,
    ShareContact,
    Block,
    Delete,
  };

  struct Snapshot {
    uint8_t publicKey[32];
    char displayName[40];
    bool isRepeater;
    bool isRoom;
    bool fromMap;
    bool hasMap;
    bool hasSightline;
    bool canShareLocation;
    bool favorite;
    bool locationShared;
    bool blocked;

    Snapshot();
  };

  struct Host {
    void *context;
    lv_coord_t (*contentTop)();
    void (*closeRoot)(void *context, lv_obj_t **root);
    void (*dispatch)(void *context, Action action, const uint8_t publicKey[32]);

    Host();
  };

  explicit ContactActionSheet(const Host &host);
  ~ContactActionSheet();
  ContactActionSheet(const ContactActionSheet &) = delete;
  ContactActionSheet &operator=(const ContactActionSheet &) = delete;

  void open(const Snapshot &snapshot);
  void close();
  bool isOpen() const;

private:
  struct RowContext {
    ContactActionSheet *owner;
    Action action;
  };

  static void deleted(lv_event_t *event);
  static void closeEvent(lv_event_t *event);
  static void event(lv_event_t *event);
  bool owns(lv_event_t *event) const;
  void unbind(lv_obj_t *object);
  void dispatch(lv_event_t *event, Action action);
  void build(const Snapshot &snapshot);

  widgets::ObjectRef _root;
  widgets::ObjectRef _close;
  widgets::ObjectRef _rows[15];
  RowContext _rowContexts[15];
  Host _host;
  Snapshot _snapshot;
  uint32_t _generation;
  bool _destroying;
  uint8_t _rowCount;
};

} // namespace screens
} // namespace ui
