// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/GuardianJson.h"
#include <cmath>
#include <stdint.h>
#include <utility>
#include <vector>

namespace guardian { namespace v3 {
static constexpr uint64_t MaxCursor = 9007199254740991ULL;
static constexpr uint64_t MaxFrequency = 10000000000ULL;
static constexpr size_t MaxItems = 20;
template <typename T> struct Nullable { bool known = false; T value{}; };
enum class Readiness { Unknown, NotReady, Ready };
struct Error {
  std::string code, message;
  bool empty() const { return code.empty() && message.empty(); }
};
struct Response { std::string id; bool ok = false; Error error; };
struct Transfer {
  std::string msg_id, state, direction, transport, source, destination, via;
  Nullable<uint64_t> progress_bytes, total_bytes;
  Nullable<double> percent, bitrate_bps;
  bool total_bytes_exact = false;
};
struct Modem {
  std::string selected, name, state, lifecycle, active_transport, control_modem, vara_mode;
  bool configured = false, started = false, readiness_verified = false;
  Readiness ready = Readiness::Unknown;
  struct Vara {
    bool command_connected = false, data_connected = false, transport_lost = false;
    std::string link_state, error;
  } vara;
};
struct Control {
  bool active = false, suspended_for_payload = false, handoff_pending = false;
  std::string handoff_error;
  Nullable<uint64_t> active_sessions;
  size_t active_sessions_count = 0;
};
struct Calibration {
  std::string state, peer, session_id, mode, current, message, error, pending_offer_json;
  Nullable<double> progress;
  Nullable<uint64_t> total;
};
struct Radio {
  uint8_t radio = 0;
  Nullable<uint64_t> revision, frequency_hz, pending_operations, total_sessions;
  Nullable<double> signal;
  std::string backend, name, mode, error, busy_reason, pending_operations_json;
  bool connected = false, ptt = false, readiness_verified = false, busy = false;
  Readiness ready = Readiness::Unknown;
  Modem modem;
  Control control;
  Calibration calibration;
  std::vector<Transfer> transfers;
};
struct Mailbox { Nullable<uint64_t> inbox, unread, outbox, outbox_failed, transit; };
struct Server { bool enabled = false; std::string state; Nullable<double> last_verified; };
struct AlertCounts { Nullable<uint64_t> total, unseen; };
struct Station {
  std::string version, callsign, grid;
  Nullable<double> updated_at;
  Nullable<uint64_t> revision;
  Readiness ready = Readiness::Unknown;
  bool readiness_verified = false;
  Mailbox mailbox;
  Server server;
  AlertCounts alerts;
  std::vector<Radio> radios;
};
struct ActionAvailability { std::string action, reason; bool supported = false, available = false; };
struct RadioCapabilities {
  uint8_t radio = 0;
  std::string tune_mode;
  Nullable<uint64_t> total_profiles;
  std::vector<std::string> profiles, tune_modes;
  std::vector<ActionAvailability> actions;
  const ActionAvailability* action(const std::string& name) const {
    for (const auto& item : actions) if (item.action == name) return &item;
    return nullptr;
  }
};
struct Capabilities {
  unsigned api_version = 0;
  bool control_allowed = false;
  std::vector<std::string> operations, actions;
  std::string limits_json;
  std::vector<RadioCapabilities> radios;
};
struct AlertType { unsigned code = 0; std::string key, label, name; Nullable<uint64_t> priority; };
struct Origin { std::string network, device; Nullable<bool> local; };
struct Alert {
  std::string alert_id, note, source;
  unsigned code = 0;
  Nullable<double> created;
  Origin origin;
  Nullable<uint64_t> radio, rf_id;
  bool forwarded = false, seen = false;
};
enum class CommandState { Executing, Pending, Completed, Queued, Failed, Unknown };
struct Command {
  std::string command_id, action, worker;
  uint8_t radio = 0;
  bool accepted = false, duplicate = false;
  CommandState state = CommandState::Unknown;
  Nullable<double> created;
  Error error;
  bool terminal() const { return state != CommandState::Executing && state != CommandState::Pending; }
  bool succeeded() const {
    return accepted && error.empty() && (state == CommandState::Completed || state == CommandState::Queued);
  }
  bool requiresDecision() const { return state == CommandState::Unknown; }
};
enum class EventType { AlertCreated, AlertSeen, CommandChanged, Unknown };
struct Event {
  std::string stream, event_id, type;
  uint64_t seq = 0;
  bool history_truncated = false;
  Nullable<double> created;
  EventType kind = EventType::Unknown;
  Alert alert;
  Command command;
  std::string alert_id;
};
struct AlertsPage {
  std::vector<Alert> items;
  uint64_t total = 0;
  std::string revision;
  Nullable<uint64_t> next_offset;
};
struct EventsPage {
  std::string stream;
  std::vector<Event> items;
  uint64_t cursor = 0, latest = 0;
  bool truncated = false, has_more = false;
};

inline bool uuid(const std::string& value) {
  if (value.size() != 36) return false;
  for (size_t i = 0; i < value.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (value[i] != '-') return false; }
    else if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f') ||
               (value[i] >= 'A' && value[i] <= 'F'))) return false;
  }
  return true;
}
namespace detail {
inline bool text(JsonVariantConst v, std::string& out, size_t max = 2048, bool required = false) {
  if (v.isNull()) return !required;
  if (!v.is<const char*>()) return false;
  const JsonString value = v.as<JsonString>();
  std::string result(value.c_str(), value.size());
  size_t count = 0;
  if (!guardian::utf8(result, &count) || count > max || (required && result.empty())) return false;
  out.swap(result); return true;
}
inline bool boolean(JsonVariantConst v, bool& out, bool required = false) {
  if (v.isNull()) return !required;
  if (!v.is<bool>()) return false;
  out = v.as<bool>(); return true;
}
inline bool integer(JsonVariantConst v, uint64_t& out, uint64_t max = MaxCursor, uint64_t min = 0) {
  if (!v.is<uint64_t>()) return false;
  const uint64_t n = v.as<uint64_t>();
  if (n < min || n > max) return false;
  out = n; return true;
}
inline bool number(JsonVariantConst v, Nullable<uint64_t>& out, uint64_t max = MaxCursor) {
  if (v.isNull()) return true;
  if (!integer(v, out.value, max)) return false;
  out.known = true; return true;
}
inline bool number(JsonVariantConst v, Nullable<double>& out) {
  if (v.isNull()) return true;
  if (!v.is<double>()) return false;
  const double n = v.as<double>();
  if (!std::isfinite(n)) return false;
  out.known = true; out.value = n; return true;
}
inline bool readiness(JsonVariantConst obj, Readiness& ready, bool& verified) {
  if (!obj.is<JsonObjectConst>() || obj["ready"].isUnbound() ||
      !boolean(obj["readiness_verified"], verified, true)) return false;
  if (obj["ready"].isNull()) { ready = Readiness::Unknown; return !verified; }
  if (!obj["ready"].is<bool>() || !verified) return false;
  ready = obj["ready"].as<bool>() ? Readiness::Ready : Readiness::NotReady; return true;
}
inline bool rawObject(JsonVariantConst v, std::string& out, size_t max = 8192) {
  if (v.isNull()) return true;
  if (!v.is<JsonObjectConst>()) return false;
  serializeJson(v, out); return out.size() <= max;
}
inline bool diagnostic(JsonVariantConst v, std::string& out) {
  if (v.is<JsonObjectConst>()) return rawObject(v, out, 2048);
  return text(v, out);
}
inline bool strings(JsonVariantConst v, std::vector<std::string>& out, size_t maxChars = 64) {
  if (v.isNull()) return true;
  if (!v.is<JsonArrayConst>() || v.size() > MaxItems) return false;
  for (auto item : v.as<JsonArrayConst>()) {
    std::string value;
    if (!text(item, value, maxChars, true)) return false;
    out.push_back(std::move(value));
  }
  return true;
}
inline bool identity(JsonVariantConst v, std::string& out) { return text(v, out, 36, true) && uuid(out); }
inline bool radioNumber(JsonVariantConst v, uint8_t& out) {
  uint64_t n = 0; if (!integer(v, n, 2, 1)) return false;
  out = uint8_t(n); return true;
}
inline bool revision(JsonVariantConst v, std::string& out) {
  if (v.is<const char*>()) return text(v, out, 128, true);
  uint64_t n = 0;
  if (!integer(v, n)) return false;
  out = std::to_string(n); return true;
}
}

inline bool parseError(JsonVariantConst v, Error& out) {
  Error result;
  if (v.isNull()) { out = result; return true; }
  if (!v.is<JsonObjectConst>() || !detail::text(v["code"], result.code, 64, true) ||
      !detail::text(v["message"], result.message)) return false;
  out = std::move(result); return true;
}
inline bool parseResponse(JsonVariantConst v, Response& out) {
  Response result;
  if (!v.is<JsonObjectConst>() || !detail::text(v["id"], result.id, 64, true) ||
      !detail::boolean(v["ok"], result.ok, true)) return false;
  for (char c : result.id) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
      (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
  if (result.ok) { if (!v["result"].is<JsonObjectConst>()) return false; }
  else if (!parseError(v["error"], result.error) || result.error.code.empty()) return false;
  out = std::move(result); return true;
}
inline bool parseTransfer(JsonVariantConst v, Transfer& out) {
  Transfer r;
  if (!v.is<JsonObjectConst>() || !detail::revision(v["msg_id"], r.msg_id) ||
      !detail::text(v["state"], r.state, 64, true) || !detail::text(v["direction"], r.direction, 16, true) ||
      (r.direction != "send" && r.direction != "receive") || !detail::text(v["transport"], r.transport, 64) ||
      !detail::text(v["source"], r.source, 64) || !detail::text(v["destination"], r.destination, 64) ||
      !detail::text(v["via"], r.via, 64) || !detail::number(v["progress_bytes"], r.progress_bytes) ||
      !detail::number(v["total_bytes"], r.total_bytes) || !detail::boolean(v["total_bytes_exact"], r.total_bytes_exact) ||
      !detail::number(v["percent"], r.percent) || !detail::number(v["bitrate_bps"], r.bitrate_bps) ||
      (r.percent.known && (r.percent.value < 0 || r.percent.value > 100)) ||
      (r.bitrate_bps.known && r.bitrate_bps.value < 0)) return false;
  out = std::move(r); return true;
}
inline bool parseRadio(JsonVariantConst v, Radio& out) {
  Radio r;
  if (!v.is<JsonObjectConst>() || !detail::radioNumber(v["radio"], r.radio) ||
      !detail::readiness(v, r.ready, r.readiness_verified) || !detail::number(v["revision"], r.revision) ||
      !detail::number(v["frequency_hz"], r.frequency_hz, MaxFrequency) || !detail::number(v["signal"], r.signal) ||
      !detail::number(v["total_sessions"], r.total_sessions) ||
      !detail::text(v["backend"], r.backend, 64) || !detail::text(v["name"], r.name, 128) ||
      !detail::text(v["mode"], r.mode, 64) || !detail::diagnostic(v["error"], r.error) ||
      !detail::text(v["busy_reason"], r.busy_reason) || !detail::boolean(v["connected"], r.connected) ||
      !detail::boolean(v["ptt"], r.ptt) || !detail::boolean(v["busy"], r.busy)) return false;
  if (v["pending_operations"].is<JsonArrayConst>()) {
    if (v["pending_operations"].size() > MaxItems) return false;
    serializeJson(v["pending_operations"], r.pending_operations_json);
    if (r.pending_operations_json.size() > 8192) return false;
    r.pending_operations.known = true; r.pending_operations.value = v["pending_operations"].size();
  } else if (!detail::number(v["pending_operations"], r.pending_operations)) return false;
  const auto modem = v["modem"];
  if (!modem.isNull()) {
    auto& m = r.modem;
    if (!modem.is<JsonObjectConst>() || !detail::readiness(modem, m.ready, m.readiness_verified) ||
        !detail::text(modem["selected"], m.selected, 64) || !detail::text(modem["name"], m.name, 128) ||
        !detail::text(modem["state"], m.state, 64) || !detail::text(modem["lifecycle"], m.lifecycle, 64) ||
        !detail::text(modem["active_transport"], m.active_transport, 64) ||
        !detail::text(modem["control_modem"], m.control_modem, 64) || !detail::text(modem["vara_mode"], m.vara_mode, 64) ||
        !detail::boolean(modem["configured"], m.configured) || !detail::boolean(modem["started"], m.started)) return false;
    const auto vara = modem["vara"];
    if (!vara.isNull() && (!vara.is<JsonObjectConst>() ||
        !detail::boolean(vara["command_connected"], m.vara.command_connected) ||
        !detail::boolean(vara["data_connected"], m.vara.data_connected) ||
        !detail::boolean(vara["transport_lost"], m.vara.transport_lost) ||
        !detail::text(vara["link_state"], m.vara.link_state, 128) ||
        !detail::diagnostic(vara["error"], m.vara.error))) return false;
  }
  const auto control = v["control"];
  if (!control.isNull()) {
    auto& c = r.control;
    if (!control.is<JsonObjectConst>() || !detail::boolean(control["active"], c.active) ||
        !detail::boolean(control["suspended_for_payload"], c.suspended_for_payload) ||
        !detail::boolean(control["handoff_pending"], c.handoff_pending) ||
        !detail::diagnostic(control["handoff_error"], c.handoff_error) ||
        !detail::number(control["active_sessions"], c.active_sessions)) return false;
    c.active_sessions_count = size_t(c.active_sessions.value);
  }
  const auto cal = v["calibration"];
  if (!cal.isNull()) {
    auto& c = r.calibration;
    if (!cal.is<JsonObjectConst>() || !detail::text(cal["state"], c.state, 64) ||
        !detail::text(cal["peer"], c.peer, 16) || !detail::text(cal["session_id"], c.session_id, 128) ||
        !detail::text(cal["mode"], c.mode, 64) || !detail::text(cal["message"], c.message) ||
        !detail::diagnostic(cal["error"], c.error) || !detail::number(cal["progress"], c.progress) ||
        !detail::number(cal["total"], c.total) || !detail::text(cal["current"], c.current)) return false;
    if (!cal["pending_offer"].isNull()) {
      serializeJson(cal["pending_offer"], c.pending_offer_json);
      if (c.pending_offer_json.size() > 8192) return false;
    }
  }
  const auto transfers = v["transfers"];
  if (!transfers.isNull()) {
    if (!transfers.is<JsonArrayConst>() || transfers.size() > MaxItems) return false;
    for (auto item : transfers.as<JsonArrayConst>()) {
      Transfer transfer; if (!parseTransfer(item, transfer)) return false;
      r.transfers.push_back(std::move(transfer));
    }
  }
  out = std::move(r); return true;
}
inline bool parseStation(JsonVariantConst v, Station& out) {
  Station r;
  if (!v.is<JsonObjectConst>() || !detail::text(v["version"], r.version, 64, true) ||
      !detail::text(v["callsign"], r.callsign, 16) || !detail::text(v["grid"], r.grid, 16) ||
      !detail::readiness(v, r.ready, r.readiness_verified) || !detail::number(v["revision"], r.revision) ||
      !detail::number(v["updated_at"], r.updated_at) || !v["radios"].is<JsonArrayConst>() ||
      v["radios"].size() > 2) return false;
  for (auto item : v["radios"].as<JsonArrayConst>()) {
    Radio radio; if (!parseRadio(item, radio)) return false;
    for (const auto& existing : r.radios) if (existing.radio == radio.radio) return false;
    r.radios.push_back(std::move(radio));
  }
  const auto box = v["mailbox"];
  if (!box.isNull() && (!box.is<JsonObjectConst>() || !detail::number(box["inbox"], r.mailbox.inbox) ||
      !detail::number(box["unread"], r.mailbox.unread) || !detail::number(box["outbox"], r.mailbox.outbox) ||
      !detail::number(box["outbox_failed"], r.mailbox.outbox_failed) || !detail::number(box["transit"], r.mailbox.transit))) return false;
  const auto server = v["server"];
  if (!server.isNull() && (!server.is<JsonObjectConst>() || !detail::boolean(server["enabled"], r.server.enabled) ||
      !detail::text(server["state"], r.server.state, 64) || !detail::number(server["last_verified"], r.server.last_verified))) return false;
  const auto alerts = v["alerts"];
  if (!alerts.isNull() && (!alerts.is<JsonObjectConst>() || !detail::number(alerts["total"], r.alerts.total) ||
      !detail::number(alerts["unseen"], r.alerts.unseen))) return false;
  out = std::move(r); return true;
}
inline bool parseCapabilities(JsonVariantConst v, Capabilities& out) {
  Capabilities r; uint64_t api = 0;
  if (!v.is<JsonObjectConst>() || !detail::integer(v["api_version"], api, 3, 3) ||
      !detail::boolean(v["control_allowed"], r.control_allowed, true) ||
      !detail::strings(v["operations"], r.operations) || !detail::strings(v["actions"], r.actions) ||
      !detail::rawObject(v["limits"], r.limits_json) || !v["radios"].is<JsonArrayConst>() || v["radios"].size() > 2) return false;
  r.api_version = unsigned(api);
  for (auto item : v["radios"].as<JsonArrayConst>()) {
    RadioCapabilities radio;
    if (!item.is<JsonObjectConst>() || !detail::radioNumber(item["radio"], radio.radio) ||
        !detail::text(item["tune_mode"], radio.tune_mode, 64) ||
        !detail::strings(item["profiles"], radio.profiles, 24) || !detail::strings(item["tune_modes"], radio.tune_modes) ||
        !detail::number(item["total_profiles"], radio.total_profiles) || !item["actions"].is<JsonObjectConst>() ||
        item["actions"].size() > MaxItems) return false;
    for (const auto& existing : r.radios) if (existing.radio == radio.radio) return false;
    for (JsonPairConst pair : item["actions"].as<JsonObjectConst>()) {
      ActionAvailability action; action.action = pair.key().c_str();
      if (action.action.empty() || action.action.size() > 64 || !pair.value().is<JsonObjectConst>() ||
          !detail::boolean(pair.value()["supported"], action.supported, true) ||
          !detail::boolean(pair.value()["available"], action.available, true) ||
          !detail::text(pair.value()["reason"], action.reason) || (action.available && !action.supported)) return false;
      radio.actions.push_back(std::move(action));
    }
    r.radios.push_back(std::move(radio));
  }
  out = std::move(r); return true;
}
inline bool parseAlertTypes(JsonVariantConst v, std::vector<AlertType>& out) {
  // The operation wraps its stable code catalogue in result.items or result.types.
  const auto items = v.is<JsonArrayConst>() ? v : (v["items"].is<JsonArrayConst>() ? v["items"] : v["types"]);
  if (!items.is<JsonArrayConst>() || items.size() > MaxItems) return false;
  std::vector<AlertType> result;
  for (auto item : items.as<JsonArrayConst>()) {
    AlertType type; uint64_t code = 0;
    if (!item.is<JsonObjectConst>() || !detail::integer(item["code"], code, 65535, 1) ||
        !detail::text(item["key"], type.key, 128, true) ||
        !detail::text(item["label"], type.label, 128, true) || !detail::number(item["priority"], type.priority)) return false;
    type.name = type.label;
    type.code = unsigned(code);
    for (const auto& existing : result) if (existing.code == type.code) return false;
    result.push_back(std::move(type));
  }
  out = std::move(result); return true;
}
inline bool parseAlert(JsonVariantConst v, Alert& out) {
  Alert r; uint64_t code = 0;
  if (!v.is<JsonObjectConst>() || !detail::identity(v["alert_id"], r.alert_id) ||
      !detail::integer(v["code"], code, 65535, 1) || !detail::text(v["note"], r.note, 512) ||
      !detail::text(v["source"], r.source, 64) || !detail::number(v["created"], r.created) ||
      !detail::number(v["radio"], r.radio, 2) || (r.radio.known && r.radio.value == 0) ||
      !detail::number(v["rf_id"], r.rf_id) || !detail::boolean(v["forwarded"], r.forwarded, true) ||
      !detail::boolean(v["seen"], r.seen, true) || !v["origin"].is<JsonObjectConst>() ||
      !detail::text(v["origin"]["network"], r.origin.network, 64, true) ||
      !detail::text(v["origin"]["device"], r.origin.device, 128)) return false;
  const auto local = v["origin"]["local"];
  if (!local.isNull()) {
    if (!local.is<bool>()) return false;
    r.origin.local.known = true; r.origin.local.value = local.as<bool>();
  }
  r.code = unsigned(code); out = std::move(r); return true;
}
inline bool parseCommand(JsonVariantConst v, Command& out) {
  Command r; std::string state;
  if (!v.is<JsonObjectConst>() || !detail::identity(v["command_id"], r.command_id) ||
      !detail::text(v["action"], r.action, 64, true) || !detail::radioNumber(v["radio"], r.radio) ||
      !detail::boolean(v["accepted"], r.accepted, true) || !detail::text(v["state"], state, 32, true) ||
      !detail::text(v["worker"], r.worker, 64) || !detail::boolean(v["duplicate"], r.duplicate) ||
      !detail::number(v["created"], r.created) || !parseError(v["error"], r.error)) return false;
  if (state == "executing") r.state = CommandState::Executing;
  else if (state == "pending") r.state = CommandState::Pending;
  else if (state == "completed") r.state = CommandState::Completed;
  else if (state == "queued") r.state = CommandState::Queued;
  else if (state == "failed") r.state = CommandState::Failed;
  else if (state == "unknown") r.state = CommandState::Unknown;
  else return false;
  out = std::move(r); return true;
}
inline bool parseEvent(JsonVariantConst v, Event& out, const std::string& pageStream = std::string()) {
  Event r;
  r.stream = pageStream;
  if (!v.is<JsonObjectConst>() ||
      (v["stream"].isUnbound() ? !uuid(r.stream) : !detail::identity(v["stream"], r.stream)) ||
      !detail::identity(v["event_id"], r.event_id) || !detail::integer(v["seq"], r.seq, MaxCursor, 1) ||
      !detail::text(v["type"], r.type, 64, true) || !detail::number(v["created"], r.created) ||
      !detail::boolean(v["history_truncated"], r.history_truncated) ||
      !v["data"].is<JsonObjectConst>() || measureJson(v["data"])>16384) return false;
  if (r.type == "alert.created") { r.kind = EventType::AlertCreated; if (!parseAlert(v["data"], r.alert)) return false; r.alert_id = r.alert.alert_id; }
  else if (r.type == "alert.seen") {
    r.kind = EventType::AlertSeen;
    if (!detail::identity(v["data"]["alert_id"], r.alert_id)) return false;
  } else if (r.type == "command.changed") { r.kind = EventType::CommandChanged; if (!parseCommand(v["data"], r.command)) return false; }
  out = std::move(r); return true;
}
inline bool parseAlertsPage(JsonVariantConst v, AlertsPage& out) {
  AlertsPage r;
  if (!v.is<JsonObjectConst>() || !v["items"].is<JsonArrayConst>() || v["items"].size() > MaxItems ||
      !detail::integer(v["total"], r.total) || !detail::revision(v["revision"], r.revision) ||
      !detail::number(v["next_offset"], r.next_offset)) return false;
  for (auto item : v["items"].as<JsonArrayConst>()) {
    Alert alert; if (!parseAlert(item, alert)) return false;
    r.items.push_back(std::move(alert));
  }
  out = std::move(r); return true;
}
inline bool parseEventsPage(JsonVariantConst v, EventsPage& out) {
  EventsPage r;
  if (!v.is<JsonObjectConst>() || !detail::identity(v["stream"], r.stream) ||
      !v["items"].is<JsonArrayConst>() || v["items"].size() > MaxItems ||
      !detail::integer(v["cursor"], r.cursor) || !detail::integer(v["latest"], r.latest) || r.cursor > r.latest ||
      !detail::boolean(v["truncated"], r.truncated, true) || !detail::boolean(v["has_more"], r.has_more, true)) return false;
  uint64_t previous = 0;
  for (auto item : v["items"].as<JsonArrayConst>()) {
    Event event;
    if (!parseEvent(item, event, r.stream) || event.stream != r.stream || event.seq <= previous || event.seq > r.cursor) return false;
    previous = event.seq; r.items.push_back(std::move(event));
  }
  out = std::move(r); return true;
}
}} // namespace guardian::v3
