// Exercise the pure v3 models against real Guardian packets and malformed snapshots.
#include "ui-touch/models/GuardianV3.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); std::abort(); } } while (0)
using namespace guardian::v3;

static void parse(JsonDocument& doc, const std::string& raw) {
  if (!guardian::parseObject(doc, raw)) {
    std::fprintf(stderr, "JSON fixture parse failed (%zu bytes): %.100s\n", raw.size(), raw.c_str());
    CHECK(false);
  }
}
static const char* stationJson = R"({
  "version":"1.1.25","callsign":"OK7PS","grid":"JO70",
  "updated_at":1791712800.5,"revision":4294967296,"ready":null,"readiness_verified":false,
  "mailbox":{"inbox":4294967295,"unread":null,"outbox":0,"outbox_failed":2,"transit":1},
  "server":{"enabled":true,"state":"connecting","last_verified":null},
  "alerts":{"total":9,"unseen":3},
  "radios":[{
    "radio":1,"revision":7,"backend":"guardian_k5","name":"K5",
    "connected":true,"frequency_hz":10000000000,"mode":"FM","ptt":false,"signal":null,
    "error":null,"ready":null,"readiness_verified":false,"busy":false,"busy_reason":null,
    "pending_operations":["radio-control"],"total_sessions":25,
    "modem":{"selected":"ofdm_vhf","name":"SC-FTN","configured":true,"started":false,
      "state":"control_ready","ready":null,"readiness_verified":false,"lifecycle":"per_transfer",
      "active_transport":null,"control_modem":"afsk","vara_mode":"FM",
      "vara":{"command_connected":false,"data_connected":false,"transport_lost":false,"link_state":"idle","error":null}},
    "control":{"active":true,"suspended_for_payload":false,"active_sessions":25,"handoff_pending":false,"handoff_error":null},
    "calibration":{"state":"measuring","peer":"OK1ABC","session_id":"s1","mode":"quick","progress":3.5,
      "total":12,"current":"700 Hz","message":"Měření","pending_offer":null,"error":""},
    "transfers":[{"msg_id":4294967295,"state":"transferring","direction":"send","transport":"ofdm_vhf",
      "source":"OK7PS","destination":"OK1ABC","via":"OK2ABC","progress_bytes":null,"total_bytes":null,
      "total_bytes_exact":false,"percent":null,"bitrate_bps":null}]
  },{"radio":2,"ready":false,"readiness_verified":true,"frequency_hz":null,"signal":-73.5,"transfers":[]}],
  "future_field":{"anything":true}
})";
static const char* capabilitiesJson = R"({
  "api_version":3,"control_allowed":false,"mesh_forwarding":false,
  "operations":["station.get","capabilities.get","control.execute","alert.types"],
  "actions":["radio.connect","radio.tune","scanner.start"],
  "limits":{"json_bytes":32768,"display_note_chars":512,"event_history":256,"alert_history":128,"command_history":128},
  "radios":[{"radio":1,"profiles":["VHF","HF"],"total_profiles":21,"tune_mode":"frequency_only","tune_modes":["FM"],
    "actions":{"radio.connect":{"supported":true,"available":false,"reason":"busy"},
      "radio.tune":{"supported":true,"available":true,"reason":null},
      "scanner.start":{"supported":false,"available":false,"reason":"requires CAT"}}}]
})";
static const char* commandJson = R"({"command_id":"22222222-2222-4222-8222-222222222222",
  "action":"radio.connect","radio":1,"accepted":true,"state":"pending",
  "worker":"radio-control","duplicate":false,"created":1791712800.0})";
static const char* alertJson = R"({"alert_id":"44444444-4444-4444-8444-444444444444",
  "code":2,"note":"Cvičení 😀","source":"OK7PS","created":1791712800.0,
  "origin":{"network":"guardian_rf","local":false},"radio":1,"rf_id":123,"forwarded":false,"seen":false})";
static const char* streamId = "55555555-5555-4555-8555-555555555555";
static const char* eventId = "66666666-6666-4666-8666-666666666666";

static void stationTests() {
  JsonDocument doc; parse(doc, stationJson);
  Station station; CHECK(parseStation(doc.as<JsonVariantConst>(), station));
  CHECK(station.ready == Readiness::Unknown && !station.readiness_verified);
  CHECK(station.revision.known && station.revision.value == 4294967296ULL);
  CHECK(station.mailbox.inbox.known && station.mailbox.inbox.value == 4294967295ULL);
  CHECK(!station.mailbox.unread.known && !station.server.last_verified.known);
  CHECK(station.radios.size() == 2 && station.radios[0].radio == 1 && station.radios[1].radio == 2);
  CHECK(station.radios[0].frequency_hz.known && station.radios[0].frequency_hz.value == MaxFrequency);
  CHECK(!station.radios[1].frequency_hz.known && station.radios[1].signal.value == -73.5);
  CHECK(station.radios[0].modem.configured && !station.radios[0].modem.started);
  CHECK(station.radios[0].modem.ready == Readiness::Unknown);
  CHECK(station.radios[0].control.active_sessions.known && station.radios[0].control.active_sessions.value == 25);
  CHECK(station.radios[0].total_sessions.value == 25 && station.radios[0].calibration.current == "700 Hz");
  CHECK(station.radios[0].transfers[0].msg_id == "4294967295");
  CHECK(!station.radios[0].transfers[0].percent.known && !station.radios[0].transfers[0].total_bytes.known);
  doc["ready"] = true; doc["readiness_verified"] = true;
  CHECK(parseStation(doc.as<JsonVariantConst>(), station) && station.ready == Readiness::Ready);
  doc["ready"] = false;
  CHECK(parseStation(doc.as<JsonVariantConst>(), station) && station.ready == Readiness::NotReady);
  doc["readiness_verified"] = false; CHECK(!parseStation(doc.as<JsonVariantConst>(), station));
  CHECK(station.ready == Readiness::NotReady); // Failed parse preserves the previous complete snapshot.
  doc["ready"] = nullptr; doc["readiness_verified"] = true;
  CHECK(!parseStation(doc.as<JsonVariantConst>(), station));
  parse(doc, stationJson); doc["radios"][0]["frequency_hz"] = MaxFrequency + 1;
  CHECK(!parseStation(doc.as<JsonVariantConst>(), station));
  doc["radios"][0]["frequency_hz"] = -1; CHECK(!parseStation(doc.as<JsonVariantConst>(), station));
  doc["radios"][0]["frequency_hz"] = 123.5; CHECK(!parseStation(doc.as<JsonVariantConst>(), station));
  parse(doc, stationJson); doc["radios"][1]["radio"] = 1; CHECK(!parseStation(doc.as<JsonVariantConst>(), station));
  parse(doc, stationJson); auto transfers = doc["radios"][0]["transfers"].as<JsonArray>();
  JsonDocument transfer; transfer.set(transfers[0]);
  for (size_t i = 1; i < 21; ++i) transfers.add(transfer.as<JsonVariantConst>());
  CHECK(!parseStation(doc.as<JsonVariantConst>(), station));
  parse(doc, stationJson); doc["radios"][0]["transfers"][0]["percent"] = 101;
  CHECK(!parseStation(doc.as<JsonVariantConst>(), station));
  parse(doc, stationJson); doc["radios"][0]["transfers"][0]["percent"] = 100;
  CHECK(parseStation(doc.as<JsonVariantConst>(), station));
  CHECK(station.radios[0].transfers[0].state == "transferring"); // 100% is not delivery state.
}
static void capabilityTests() {
  JsonDocument doc; parse(doc, capabilitiesJson);
  Capabilities caps; CHECK(parseCapabilities(doc.as<JsonVariantConst>(), caps));
  CHECK(caps.api_version == 3 && !caps.control_allowed && caps.radios.size() == 1);
  CHECK(caps.radios[0].total_profiles.value == 21 && caps.radios[0].profiles.size() == 2);
  CHECK(caps.radios[0].action("radio.tune")->available);
  CHECK(!caps.radios[0].action("radio.connect")->available);
  CHECK(!caps.radios[0].action("advert.send"));
  doc["radios"][0]["actions"]["radio.tune"]["supported"] = false;
  CHECK(!parseCapabilities(doc.as<JsonVariantConst>(), caps));
  parse(doc, capabilitiesJson); doc["api_version"] = 2; CHECK(!parseCapabilities(doc.as<JsonVariantConst>(), caps));
  parse(doc, capabilitiesJson); auto profiles = doc["radios"][0]["profiles"].as<JsonArray>();
  for (size_t i = 2; i < 21; ++i) profiles.add("extra");
  CHECK(!parseCapabilities(doc.as<JsonVariantConst>(), caps));
  parse(doc, R"({"items":[{"code":1,"key":"alert.mayday","label":"MAYDAY","priority":3},{"code":2,"key":"alert.medical","label":"Zdravotní nouze","priority":2}],"mesh_forwarding":false})");
  std::vector<AlertType> types; CHECK(parseAlertTypes(doc.as<JsonVariantConst>(), types));
  CHECK(types.size() == 2 && types[1].label == "Zdravotní nouze" && types[1].name == types[1].label);
  doc["items"][1]["code"] = 1; CHECK(!parseAlertTypes(doc.as<JsonVariantConst>(), types));
}
static void commandTests() {
  JsonDocument doc; parse(doc, commandJson); Command command;
  CHECK(parseCommand(doc.as<JsonVariantConst>(), command));
  CHECK(!command.terminal() && !command.succeeded() && !command.requiresDecision());
  doc["state"] = "executing"; doc["accepted"] = false; CHECK(parseCommand(doc.as<JsonVariantConst>(), command));
  CHECK(!command.terminal() && !command.succeeded());
  doc["state"] = "completed"; doc["accepted"] = true; CHECK(parseCommand(doc.as<JsonVariantConst>(), command));
  CHECK(command.terminal() && command.succeeded());
  doc["state"] = "queued"; doc["action"] = "beacon.send"; CHECK(parseCommand(doc.as<JsonVariantConst>(), command));
  CHECK(command.terminal() && command.succeeded());
  doc["state"] = "failed"; doc["error"]["code"] = "busy"; doc["error"]["message"] = "Radio is busy";
  CHECK(parseCommand(doc.as<JsonVariantConst>(), command)); CHECK(command.terminal() && !command.succeeded() && command.accepted);
  doc["accepted"] = false; CHECK(parseCommand(doc.as<JsonVariantConst>(), command) && !command.accepted);
  doc["state"] = "unknown"; doc["error"]["code"] = "interrupted";
  CHECK(parseCommand(doc.as<JsonVariantConst>(), command)); CHECK(command.requiresDecision() && !command.succeeded());
  doc["state"] = "future_state"; CHECK(!parseCommand(doc.as<JsonVariantConst>(), command));
  parse(doc, commandJson); doc["radio"] = 0; CHECK(!parseCommand(doc.as<JsonVariantConst>(), command));
  parse(doc, commandJson); doc["command_id"] = "bad"; CHECK(!parseCommand(doc.as<JsonVariantConst>(), command));
  parse(doc, R"({"id":"c1","ok":true,"result":{"accepted":false,"state":"failed"}})");
  Response response; CHECK(parseResponse(doc.as<JsonVariantConst>(), response) && response.ok);
  CHECK(!parseCommand(doc["result"], command)); // Successful API envelope cannot manufacture command success.
  parse(doc, R"({"id":"c.1_-","ok":false,"error":{"code":"control_denied","message":"Enable on PC"}})");
  CHECK(parseResponse(doc.as<JsonVariantConst>(), response) && !response.ok && response.error.code == "control_denied");
  doc["id"] = "bad id"; CHECK(!parseResponse(doc.as<JsonVariantConst>(), response));
  doc["id"] = std::string(65, 'a'); CHECK(!parseResponse(doc.as<JsonVariantConst>(), response));
  doc["id"] = "c1"; doc["ok"] = "true"; CHECK(!parseResponse(doc.as<JsonVariantConst>(), response));
}
static void alertAndEventTests() {
  JsonDocument doc; parse(doc, alertJson); Alert alert;
  CHECK(parseAlert(doc.as<JsonVariantConst>(), alert) && alert.note == "Cvičení 😀");
  CHECK(alert.radio.known && alert.radio.value == 1 && alert.origin.local.known && !alert.origin.local.value);
  doc["origin"]["network"] = "guard_mesh_ble"; doc["origin"].remove("local");
  doc["origin"]["device"] = "AA:BB:CC:DD:EE:FF"; doc["radio"] = nullptr; doc["rf_id"] = nullptr;
  CHECK(parseAlert(doc.as<JsonVariantConst>(), alert)); CHECK(!alert.radio.known && !alert.rf_id.known && !alert.origin.local.known);
  doc["note"] = std::string(512, 'a'); CHECK(parseAlert(doc.as<JsonVariantConst>(), alert));
  doc["note"] = std::string(513, 'a'); CHECK(!parseAlert(doc.as<JsonVariantConst>(), alert));
  std::string unicode; for (size_t i = 0; i < 512; ++i) unicode += "😀";
  doc["note"] = unicode; CHECK(parseAlert(doc.as<JsonVariantConst>(), alert));
  unicode += "😀"; doc["note"] = unicode; CHECK(!parseAlert(doc.as<JsonVariantConst>(), alert));
  doc["note"] = std::string("bad\0tail", 8); CHECK(!parseAlert(doc.as<JsonVariantConst>(), alert));
  doc["note"] = std::string(1, char(0xc0)); CHECK(!parseAlert(doc.as<JsonVariantConst>(), alert));
  parse(doc, std::string("{\"stream\":\"") + streamId + "\",\"seq\":9007199254740991,\"event_id\":\"" + eventId +
    "\",\"type\":\"alert.created\",\"created\":1791712800,\"history_truncated\":false,\"data\":" + alertJson + "}");
  Event event; CHECK(parseEvent(doc.as<JsonVariantConst>(), event));
  CHECK(event.seq == MaxCursor && event.kind == EventType::AlertCreated && event.alert_id == event.alert.alert_id);
  doc["seq"] = MaxCursor + 1; CHECK(!parseEvent(doc.as<JsonVariantConst>(), event));
  doc["seq"] = 0; CHECK(!parseEvent(doc.as<JsonVariantConst>(), event));
  doc["seq"] = 1; doc["stream"] = "bad"; CHECK(!parseEvent(doc.as<JsonVariantConst>(), event));
  doc["stream"] = streamId; doc["type"] = "alert.seen"; doc["data"].clear(); doc["data"]["alert_id"] = "44444444-4444-4444-8444-444444444444";
  CHECK(parseEvent(doc.as<JsonVariantConst>(), event) && event.kind == EventType::AlertSeen);
  doc["type"] = "future.event"; CHECK(parseEvent(doc.as<JsonVariantConst>(), event) && event.kind == EventType::Unknown);
  doc.remove("stream"); CHECK(!parseEvent(doc.as<JsonVariantConst>(), event));
  CHECK(parseEvent(doc.as<JsonVariantConst>(), event, streamId));
  JsonDocument page; page["stream"] = streamId; page["cursor"] = MaxCursor; page["latest"] = MaxCursor;
  page["truncated"] = true; page["has_more"] = false; doc["seq"] = MaxCursor;
  page["items"].to<JsonArray>().add(doc.as<JsonVariantConst>());
  EventsPage events; CHECK(parseEventsPage(page.as<JsonVariantConst>(), events));
  CHECK(events.items.size() == 1 && events.items[0].stream == streamId && events.truncated);
  page["items"].as<JsonArray>().add(doc.as<JsonVariantConst>()); CHECK(!parseEventsPage(page.as<JsonVariantConst>(), events));
  page["items"].as<JsonArray>().remove(1); page["cursor"] = MaxCursor - 1;
  CHECK(!parseEventsPage(page.as<JsonVariantConst>(), events));
  parse(doc, alertJson); page.clear(); page["total"] = 1; page["revision"] = "abcdef0123456789"; page["next_offset"] = nullptr;
  page["items"].to<JsonArray>().add(doc.as<JsonVariantConst>());
  AlertsPage alerts; CHECK(parseAlertsPage(page.as<JsonVariantConst>(), alerts));
  CHECK(alerts.items.size() == 1 && !alerts.next_offset.known && alerts.revision == "abcdef0123456789");
  for (size_t i = 1; i < 21; ++i) page["items"].as<JsonArray>().add(doc.as<JsonVariantConst>());
  CHECK(!parseAlertsPage(page.as<JsonVariantConst>(), alerts));
}
static void vectorTests(const char* path) {
  std::ifstream file(path); CHECK(file.good());
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
  JsonDocument vectors; parse(vectors, text);
  unsigned replies = 0, events = 0;
  for (auto vector : vectors["transfers"].as<JsonArrayConst>()) {
    const std::string characteristic = vector["characteristic"].as<const char*>();
    if (characteristic.find("f3641405-") != 0 && characteristic.find("f3641406-") != 0) continue;
    std::string raw;
    unsigned index = 0, transferId = 0;
    for (auto fragment : vector["fragments_hex"].as<JsonArrayConst>()) {
      std::istringstream hex(fragment.as<const char*>()); std::vector<unsigned> bytes; unsigned byte;
      while (hex >> std::hex >> byte) { CHECK(byte <= 255); bytes.push_back(byte); }
      CHECK(bytes.size() >= 5 && bytes.size() <= 20 && (bytes[0] & ~3U) == 0);
      CHECK((bytes[2] | (bytes[3] << 8)) == index);
      if (index == 0) { CHECK(bytes[0] & 1); transferId = bytes[1]; }
      else CHECK(!(bytes[0] & 1));
      CHECK(bytes[1] == transferId);
      CHECK(bool(bytes[0] & 2) == (index + 1 == vector["fragments_hex"].size()));
      for (size_t i = 4; i < bytes.size(); ++i) raw += char(bytes[i]);
      ++index;
    }
    JsonDocument packet; parse(packet, raw);
    // Compare parsed trees: vector JSON is pretty-printed, whereas the wire is compact UTF-8.
    CHECK(packet.as<JsonVariantConst>() == vector["json"]);
    if (characteristic.find("f3641405-") == 0) {
      Response response; CHECK(parseResponse(packet.as<JsonVariantConst>(), response)); ++replies;
      if (std::string(vector["name"].as<const char*>()) == "response-radio-pending") {
        Command command; CHECK(parseCommand(packet["result"], command)); CHECK(command.state == CommandState::Pending && !command.succeeded());
      }
    } else {
      Event event; CHECK(parseEvent(packet.as<JsonVariantConst>(), event)); ++events;
      CHECK(event.kind == EventType::AlertCreated && event.seq == 1 && event.alert.note == "Cvičení: bod A.");
    }
  }
  CHECK(replies == 3 && events == 1);
}
int main(int argc, char** argv) {
  stationTests(); capabilityTests(); commandTests(); alertAndEventTests();
  vectorTests(argc > 1 ? argv[1] : "docs/guard_mesh_ble_v3_vectors.json");
  puts("Guardian v3 models: real PC schema/vectors, nullable state, 64-bit values, bounds, UUIDs, command semantics PASS");
}
