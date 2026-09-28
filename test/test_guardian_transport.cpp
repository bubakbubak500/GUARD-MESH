// Exercise production callbacks and mailbox; fake only NimBLE and NVS.
#include "helpers/esp32/GuardianBLEInterface.h"
#include "ui-touch/services/GuardianLink.h"
#include <cassert>
namespace fake {
uint32_t now=100;
bool bonded=false, saved=false, registerFails=false;
int phoneWrites=0;
ble_gap_conn_desc desc{};
const ble_gatt_svc_def* service=nullptr;
}
NimBLEAdvertising NimBLEDevice::adv;
NimBLEServer NimBLEDevice::server;
bool NimBLEDevice::mitm=true;
int NimBLEDevice::io=0;
struct Transport : GuardianBLEInterface {
  using GuardianBLEInterface::onConnect;
  using GuardianBLEInterface::onDisconnect;
  using GuardianBLEInterface::onAuthenticationComplete;
  using GuardianBLEInterface::onWrite;
};
int main() {
  Transport ble; char name[]="Test";
  ble.begin("Mesh-",name,123456); ble.enable();
  assert(NimBLEDevice::mitm && NimBLEDevice::adv.name=="Mesh-Test");
  assert(fake::service && fake::service->characteristics[0].flags==(BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC));
  assert(fake::service->characteristics[1].flags==(BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC));
  assert(fake::service->characteristics[1].min_key_size==16);
  guardian::request(guardian::Command::Enable); ble.tickGuardian();
  assert(fake::saved && !NimBLEDevice::mitm && NimBLEDevice::adv.name=="GuardMesh-00AB");
  assert(NimBLEDevice::adv.service=="f3641400-b000-4042-ba50-05ca45bf8abc");
  fake::desc.conn_handle=3;
  ble.onConnect(&NimBLEDevice::server,&fake::desc);
  assert(NimBLEDevice::server.disconnected.back()==3); // unknown, no physical permission
  guardian::request(guardian::Command::Pair); ble.tickGuardian();
  ble.onConnect(&NimBLEDevice::server,&fake::desc);
  assert(ble.allowRepeatPairing(3) && !ble.allowRepeatPairing(4));
  os_mbuf buffer; ble_gatt_access_ctxt context{BLE_GATT_ACCESS_OP_READ_CHR,&buffer};
  assert(ble.access(3,&context)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
  fake::desc.sec_state={true,true}; ble.onAuthenticationComplete(&fake::desc);
  assert(!ble.allowRepeatPairing(3));
  assert(guardian::snapshot(fake::now).session.connected && !guardian::snapshot(fake::now).session.fresh(fake::now));
  assert(ble.access(3,&context)==0 && buffer.data==std::vector<uint8_t>({'G','M',1,0}));
  context.op=BLE_GATT_ACCESS_OP_WRITE_CHR;
  buffer.data={'G','M',1,7,5,0,0,0,2,0,0,0,1,0,0,0,7,0,0,0};
  assert(ble.access(3,&context)==0);
  assert(guardian::snapshot(fake::now).session.status.inbox==5);
  assert(ble.access(3,&context)!=0); // duplicate does not extend validity
  fake::now+=15000;
  assert(!guardian::snapshot(fake::now).session.fresh(fake::now));
  assert(guardian::snapshot(fake::now).session.status.inbox==0); // no stale counts in UI mailbox
  buffer.data[16]=8; buffer.data[3]=0x87;
  assert(ble.access(3,&context)!=0);
  buffer.data[3]=7; assert(ble.access(3,&context)==0);
  buffer.data.pop_back(); assert(ble.access(3,&context)==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
  NimBLECharacteristic uart; ble.onWrite(&uart);
  assert(ble.writeFrame(buffer.data.data(),buffer.data.size())==0 && !ble.isConnected() && fake::phoneWrites==0);
  ble_gap_conn_desc intruder=fake::desc; intruder.conn_handle=4;
  ble.onConnect(&NimBLEDevice::server,&intruder);
  ble.onAuthenticationComplete(&intruder);
  ble.onDisconnect(&NimBLEDevice::server,&intruder);
  assert(guardian::snapshot(fake::now).session.connected); // rejected second PC cannot clear first
  ble.onDisconnect(&NimBLEDevice::server,&fake::desc);
  assert(!guardian::snapshot(fake::now).session.connected);
  fake::bonded=true; ble.onConnect(&NimBLEDevice::server,&fake::desc);
  ble.onAuthenticationComplete(&fake::desc);
  buffer.data={'G','M',1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  assert(ble.access(3,&context)==0); // bonded reconnect resets sequence
  ble.disable(); assert(!guardian::snapshot(fake::now).session.connected && !NimBLEDevice::adv.running);
  assert(ble.access(3,&context)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
  ble.onDisconnect(&NimBLEDevice::server,&fake::desc); ble.enable();
  fake::bonded=false; fake::desc.sec_state={false,false};
  guardian::request(guardian::Command::Pair); ble.tickGuardian();
  ble.onConnect(&NimBLEDevice::server,&fake::desc);
  fake::now+=120001; ble.tickGuardian();
  fake::desc.sec_state={true,true}; ble.onAuthenticationComplete(&fake::desc);
  assert(!guardian::snapshot(fake::now).session.connected); // timed-out pairing must not succeed
  ble.onDisconnect(&NimBLEDevice::server,&fake::desc);
  guardian::request(guardian::Command::Disable); ble.tickGuardian();
  assert(!fake::saved && NimBLEDevice::mitm && NimBLEDevice::adv.name=="Mesh-Test");
  ble.onWrite(&uart); assert(fake::phoneWrites==1);
  puts("Guardian transport: registration, ATT errors, security, pairing timeout, reconnect, isolation PASS");
}
