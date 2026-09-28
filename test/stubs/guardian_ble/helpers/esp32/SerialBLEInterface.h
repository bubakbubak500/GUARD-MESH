// Minimal host fake for testing the real Guardian transport without a controller.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#define BLE_HS_CONN_HANDLE_NONE 0xffff
#define BLE_HS_IO_NO_INPUT_OUTPUT 3
#define BLE_HS_IO_DISPLAY_ONLY 0
#define BLE_UUID128_INIT(...) {{128}, {__VA_ARGS__}}
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GATT_CHR_F_WRITE 8
#define BLE_GATT_CHR_F_WRITE_ENC 0x1000
#define BLE_GATT_CHR_F_READ 2
#define BLE_GATT_CHR_F_READ_ENC 0x200
#define BLE_GATT_ACCESS_OP_READ_CHR 0
#define BLE_GATT_ACCESS_OP_WRITE_CHR 2
#define BLE_ATT_ERR_INSUFFICIENT_AUTHEN 5
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_ATT_ERR_UNLIKELY 14
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
struct ble_uuid_t { uint8_t type; };
struct ble_uuid128_t { ble_uuid_t u; uint8_t value[16]; };
struct ble_addr_t { uint8_t type, val[6]; };
struct ble_gap_conn_desc { uint16_t conn_handle; ble_addr_t peer_id_addr; struct { bool encrypted, bonded; } sec_state; };
struct os_mbuf { std::vector<uint8_t> data; };
struct ble_gatt_access_ctxt { int op; os_mbuf* om; };
struct ble_gatt_chr_def {
  const ble_uuid_t* uuid;
  int (*access_cb)(uint16_t,uint16_t,ble_gatt_access_ctxt*,void*);
  void* arg; uint16_t flags; uint8_t min_key_size;
};
struct ble_gatt_svc_def { int type; const ble_uuid_t* uuid; ble_gatt_chr_def* characteristics; };
namespace fake {
extern uint32_t now;
extern bool bonded, saved, registerFails;
extern int phoneWrites;
extern ble_gap_conn_desc desc;
extern const ble_gatt_svc_def* service;
}
inline uint32_t millis() { return fake::now; }
inline void esp_efuse_mac_get_default(uint8_t* mac) { memset(mac,0,6); mac[5]=0xab; }
inline int ble_gatts_count_cfg(const ble_gatt_svc_def*) { return fake::registerFails ? 1 : 0; }
inline int ble_gatts_add_svcs(const ble_gatt_svc_def* service) { fake::service=service; return 0; }
inline int ble_gap_conn_find(uint16_t connection, ble_gap_conn_desc* desc) {
  if (connection != fake::desc.conn_handle) return 1;
  *desc=fake::desc; return 0;
}
#define OS_MBUF_PKTLEN(m) ((m)->data.size())
inline int ble_hs_mbuf_to_flat(os_mbuf* m, void* dest, size_t n, uint16_t*) {
  if (m->data.size() > n) return 1;
  memcpy(dest,m->data.data(),m->data.size()); return 0;
}
inline int os_mbuf_append(os_mbuf* m, const void* data, size_t n) {
  auto p=static_cast<const uint8_t*>(data); m->data.insert(m->data.end(),p,p+n); return 0;
}
class NimBLEAddress { public: explicit NimBLEAddress(ble_addr_t) {} };
class NimBLECharacteristic {};
class NimBLEAdvertising {
public:
  bool running=false;
  std::string name, service;
  void stop() { running=false; }
  void reset() { running=false; }
  void removeServices() { service.clear(); }
  void setScanResponse(bool) {}
  void setName(const std::string& n) { name=n; }
  void addServiceUUID(const char* s) { service=s; }
  void start() { running=true; }
  bool isAdvertising() { return running; }
};
class NimBLEServer {
public:
  unsigned count=0;
  std::vector<uint16_t> disconnected;
  int disconnect(uint16_t handle) { disconnected.push_back(handle); return 0; }
  size_t getConnectedCount() { return count; }
};
class NimBLEDevice {
public:
  static NimBLEAdvertising adv;
  static NimBLEServer server;
  static bool mitm;
  static int io;
  static NimBLEAdvertising* getAdvertising() { return &adv; }
  static NimBLEServer* getServer() { return &server; }
  static void setDeviceName(const char*) {}
  static void setSecurityAuth(bool, bool m, bool) { mitm=m; }
  static void setSecurityIOCap(int i) { io=i; }
  static void setSecurityPasskey(uint32_t) {}
  static bool isBonded(NimBLEAddress) { return fake::bonded; }
};
class SerialBLEInterface {
public:
  bool enabled=false;
  void begin(const char*,char*,uint32_t) { enabled=false; }
  virtual ~SerialBLEInterface() = default;
  virtual void enable() { enabled=true; NimBLEDevice::adv.start(); }
  virtual void disable() { enabled=false; NimBLEDevice::adv.stop(); }
  virtual bool isConnected() const { return enabled && fake::desc.sec_state.encrypted; }
  virtual bool isWriteBusy() const { return false; }
  virtual size_t checkRecvFrame(uint8_t*) { return 0; }
  virtual size_t writeFrame(const uint8_t*,size_t n) { ++fake::phoneWrites; return n; }
  void drainSendQueue() {}
protected:
  virtual void onConnect(NimBLEServer*,ble_gap_conn_desc*) {}
  virtual void onDisconnect(NimBLEServer*) {}
  virtual void onDisconnect(NimBLEServer*,ble_gap_conn_desc*) {}
  virtual void onAuthenticationComplete(ble_gap_conn_desc*) {}
  virtual void onWrite(NimBLECharacteristic*) { ++fake::phoneWrites; }
};
