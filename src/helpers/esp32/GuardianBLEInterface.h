// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(LILYGO_TDECK) && defined(BLE_PIN_CODE)
#include <helpers/esp32/SerialBLEInterface.h>
#include <atomic>

// Reuses the existing stack, but keeps the PC link out of MeshCore's UART.
// Guardian and phone advertising/security modes are mutually exclusive.
class GuardianBLEInterface : public SerialBLEInterface {
public:
  void begin(const char* prefix, char* name, uint32_t pin);
  void enable() override;
  void disable() override;
  void tickGuardian();
  bool isConnected() const override { return !_guardian && SerialBLEInterface::isConnected(); }
  bool isWriteBusy() const override { return !_guardian && SerialBLEInterface::isWriteBusy(); }
  size_t checkRecvFrame(uint8_t* dest) override;
  size_t writeFrame(const uint8_t* data, size_t size) override {
    return _guardian ? 0 : SerialBLEInterface::writeFrame(data, size);
  }
  void drainSendQueue() { if (!_guardian) SerialBLEInterface::drainSendQueue(); }
  int access(uint16_t handle, struct ble_gatt_access_ctxt* context);
  bool allowRepeatPairing(uint16_t handle);
  void subscribed(uint16_t connection, uint16_t attribute, bool enabled);
protected:
  void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) override;
  void onDisconnect(NimBLEServer* server) override;
  void onDisconnect(NimBLEServer* server, ble_gap_conn_desc* desc) override;
  void onAuthenticationComplete(ble_gap_conn_desc* desc) override;
  void onWrite(NimBLECharacteristic* characteristic) override;
private:
  std::atomic<bool> _guardian{false}, _radio{false};
  std::atomic<uint16_t> _connection{BLE_HS_CONN_HANDLE_NONE};
  std::atomic<uint32_t> _pairUntil{0};
  std::atomic<bool> _authenticated{false}, _known{false}, _switching{false};
  bool _wanted = false, _ready = false, _loaded = false;
  uint32_t _pin = 0;
  uint32_t _advertiseRetryAt = 0;
  char _phoneName[72]{};
  void advertise();
  void endPairing();
};
#endif
