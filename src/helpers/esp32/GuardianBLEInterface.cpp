// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianBLEInterface.h"
#if defined(LILYGO_TDECK) && defined(BLE_PIN_CODE)
#include "../../ui-touch/services/GuardianLink.h"
#include <Preferences.h>
#include <nimble/nimble/host/include/host/ble_gatt.h>
#include <nimble/nimble/host/include/host/ble_hs_mbuf.h>

namespace {
const char* const Service = "f3641400-b000-4042-ba50-05ca45bf8abc";
const char* const Uart = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
const ble_uuid128_t serviceUuid = BLE_UUID128_INIT(0xbc,0x8a,0xbf,0x45,0xca,0x05,0x50,0xba,0x42,0x40,0x00,0xb0,0x00,0x14,0x64,0xf3);
const ble_uuid128_t statusUuid = BLE_UUID128_INIT(0xbc,0x8a,0xbf,0x45,0xca,0x05,0x50,0xba,0x42,0x40,0x00,0xb0,0x01,0x14,0x64,0xf3);
const ble_uuid128_t protocolUuid = BLE_UUID128_INIT(0xbc,0x8a,0xbf,0x45,0xca,0x05,0x50,0xba,0x42,0x40,0x00,0xb0,0x02,0x14,0x64,0xf3);
// NimBLE-Arduino 1.4 callbacks return void, so they cannot reject ATT writes.
// Register this small service with the host API to return real ATT errors.
ble_gatt_chr_def characteristics[3]{};
ble_gatt_svc_def services[2]{};
GuardianBLEInterface* instance = nullptr;
int accessCallback(uint16_t connection, uint16_t, ble_gatt_access_ctxt* context, void* arg) {
  return static_cast<GuardianBLEInterface*>(arg)->access(connection, context);
}
}

// Called by the checked build patch before NimBLE discards an existing bond.
bool guardMeshAllowRepeatPairing(uint16_t connection) {
  return instance && instance->allowRepeatPairing(connection);
}
bool GuardianBLEInterface::allowRepeatPairing(uint16_t handle) {
  if (!_guardian) return true;
  if (!_radio || _switching || handle != _connection ||
      !_pairUntil || int32_t(_pairUntil.load() - millis()) <= 0) return false;
  // The old bond is about to be deleted. From here a replacement must finish
  // inside the same physical window, just like a first pairing.
  _known = false; _authenticated = false; guardian::disconnected();
  return true;
}

void GuardianBLEInterface::begin(const char* prefix, char* name, uint32_t pin) {
  instance = this; _radio = false; _authenticated = false;
  if (!_loaded) {
    Preferences prefs;
    if (prefs.begin("guardian", true)) { _wanted = prefs.getBool("enabled", false); prefs.end(); }
    _loaded = true;
  }
  _pin = pin; _guardian = _wanted; _switching = false;
  _connection = BLE_HS_CONN_HANDLE_NONE; endPairing(); guardian::disconnected();
  SerialBLEInterface::begin(prefix, name, pin);
  snprintf(_phoneName, sizeof _phoneName, "%s%s", prefix ? prefix : "", name ? name : "");
  memset(characteristics, 0, sizeof characteristics);
  memset(services, 0, sizeof services);
  characteristics[0].uuid = &statusUuid.u;
  characteristics[0].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC;
  characteristics[1].uuid = &protocolUuid.u;
  characteristics[1].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC;
  for (int i = 0; i < 2; ++i) {
    characteristics[i].access_cb = accessCallback;
    characteristics[i].arg = this;
    characteristics[i].min_key_size = 16;
  }
  services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
  services[0].uuid = &serviceUuid.u;
  services[0].characteristics = characteristics;
  _ready = ble_gatts_count_cfg(services) == 0 && ble_gatts_add_svcs(services) == 0;
  guardian::configure(_wanted, false, _ready);
}

void GuardianBLEInterface::advertise() {
  _advertiseRetryAt = millis() + 1000;
  auto* adv = NimBLEDevice::getAdvertising();
  adv->stop();
  adv->reset();
  adv->removeServices();
  adv->setScanResponse(true);
  if (_guardian) {
    if (!_ready) return; // fail closed if the service could not be registered
    uint8_t mac[6]; esp_efuse_mac_get_default(mac);
    char name[24]; snprintf(name, sizeof name, "GuardMesh-%02X%02X", mac[4], mac[5]);
    guardian::deviceName(name);
    NimBLEDevice::setDeviceName(name);
    adv->setName(name); adv->addServiceUUID(Service);
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    adv->start();
  } else {
    NimBLEDevice::setDeviceName(_phoneName);
    adv->setName(_phoneName); adv->addServiceUUID(Uart);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityPasskey(_pin);
    SerialBLEInterface::enable();
  }
}

void GuardianBLEInterface::enable() {
  if (_radio) return;
  _radio = true;
  advertise();
  guardian::configure(_wanted, true, _ready);
}
void GuardianBLEInterface::endPairing() { _pairUntil = 0; guardian::pairingUntil(0); }
void GuardianBLEInterface::disable() {
  _radio = false; endPairing(); guardian::disconnected();
  guardian::configure(_wanted, false, _ready);
  SerialBLEInterface::disable();
  const uint16_t connection = _connection;
  if (connection != BLE_HS_CONN_HANDLE_NONE) NimBLEDevice::getServer()->disconnect(connection);
}

void GuardianBLEInterface::tickGuardian() {
  const auto command = guardian::takeCommand();
  if (command != guardian::Command::None) {
    const bool wanted = command == guardian::Command::Disable ? false :
                        command == guardian::Command::CancelPair ? _wanted : true;
    if (_wanted != wanted) {
      _wanted = wanted;
      Preferences prefs;
      if (prefs.begin("guardian", false)) { prefs.putBool("enabled", _wanted); prefs.end(); }
    }
    if (command == guardian::Command::Pair && _radio) {
      _pairUntil = (millis() + 120000u) | 1u;
      guardian::pairingUntil(_pairUntil);
    } else endPairing();
  }
  if (_pairUntil && int32_t(_pairUntil.load() - millis()) <= 0) endPairing();
  if (_guardian && !_authenticated && !_known && !_pairUntil && _connection != BLE_HS_CONN_HANDLE_NONE)
    NimBLEDevice::getServer()->disconnect(_connection);
  if (_guardian != _wanted && !_switching) {
    _switching = true;
    SerialBLEInterface::disable();
    const uint16_t connection = _connection;
    if (connection != BLE_HS_CONN_HANDLE_NONE) NimBLEDevice::getServer()->disconnect(connection);
    guardian::disconnected();
  }
  auto* server = NimBLEDevice::getServer();
  if (_switching && server->getConnectedCount() == 0) {
    _guardian = _wanted; _switching = false;
    if (_radio) advertise();
  }
  // Restart only from the main loop, never the BLE callback. The PC chooses
  // whether to reconnect; a link alone does not make the data live.
  if (_guardian && _radio && _ready && !_switching && server->getConnectedCount() == 0 &&
      int32_t(millis() - _advertiseRetryAt) >= 0 && !NimBLEDevice::getAdvertising()->isAdvertising()) {
    _advertiseRetryAt = millis() + 1000;
    NimBLEDevice::getAdvertising()->start();
  }
  guardian::configure(_wanted, _radio, _ready);
}
size_t GuardianBLEInterface::checkRecvFrame(uint8_t* dest) {
  return _guardian || _switching ? 0 : SerialBLEInterface::checkRecvFrame(dest);
}

void GuardianBLEInterface::onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) {
  if (!_guardian) { SerialBLEInterface::onConnect(server, desc); return; }
  if (!desc) return;
  const bool pair = _pairUntil && int32_t(_pairUntil.load() - millis()) > 0;
  uint16_t empty = BLE_HS_CONN_HANDLE_NONE;
  const bool known = NimBLEDevice::isBonded(NimBLEAddress(desc->peer_id_addr));
  if (!_radio || _switching || (!pair && !known) ||
      !_connection.compare_exchange_strong(empty, desc->conn_handle)) {
    server->disconnect(desc->conn_handle); return;
  }
  _known = known; _authenticated = false;
  guardian::disconnected(); // reset sequence and age on every physical connection
}
void GuardianBLEInterface::onDisconnect(NimBLEServer* server) {
  if (!_guardian) SerialBLEInterface::onDisconnect(server);
}
void GuardianBLEInterface::onDisconnect(NimBLEServer*, ble_gap_conn_desc* desc) {
  if (!desc || desc->conn_handle != _connection) return;
  _connection = BLE_HS_CONN_HANDLE_NONE; _authenticated = false;
  guardian::disconnected();
}
void GuardianBLEInterface::onAuthenticationComplete(ble_gap_conn_desc* desc) {
  if (!_guardian) { SerialBLEInterface::onAuthenticationComplete(desc); return; }
  if (!desc) return;
  if (desc->conn_handle != _connection) {
    NimBLEDevice::getServer()->disconnect(desc->conn_handle); return;
  }
  const bool pair = _pairUntil && int32_t(_pairUntil.load() - millis()) > 0;
  if (!_radio || _switching || (!_known && !pair && !_authenticated) ||
      !desc->sec_state.encrypted || !desc->sec_state.bonded) {
    _authenticated = false;
    NimBLEDevice::getServer()->disconnect(desc->conn_handle);
    guardian::disconnected(); return;
  }
  endPairing();
  if (!_authenticated.exchange(true)) guardian::connected();
}
void GuardianBLEInterface::onWrite(NimBLECharacteristic* characteristic) {
  if (!_guardian && !_switching) SerialBLEInterface::onWrite(characteristic);
}
int GuardianBLEInterface::access(uint16_t connection, ble_gatt_access_ctxt* context) {
  ble_gap_conn_desc desc{};
  if (!_guardian || !_radio || _switching || !_authenticated || connection != _connection ||
      ble_gap_conn_find(connection, &desc) != 0 || !desc.sec_state.encrypted || !desc.sec_state.bonded)
    return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
  if (context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    static const uint8_t protocol[] = {'G','M',1,0};
    return os_mbuf_append(context->om, protocol, sizeof protocol) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
  if (OS_MBUF_PKTLEN(context->om) != 20) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  uint8_t bytes[20];
  if (ble_hs_mbuf_to_flat(context->om, bytes, sizeof bytes, nullptr) != 0) return BLE_ATT_ERR_UNLIKELY;
  return guardian::receive(bytes, sizeof bytes, millis()) == guardian::Result::Ok ? 0 : BLE_ATT_ERR_UNLIKELY;
}
#endif
